#!/usr/bin/env python3
"""
romfind - locate a firmware function by the strings it references.

Magic Lantern can only call what 6D_116.sym exports, and the 6D's WiFi
stack (NWCommu: sockets, listen queues, PTP-over-network) is not in it.
This tool is the first step of adding it: find the code, then the function
entry, then the address to put in the .sym.

Three steps, each of which must be verified rather than assumed:

  base     find the image's load address by mapping 32-bit words back into
           the file; a wrong base produces no hits at all, so a base is
           only accepted when the words it produces land on real strings
  xref     find absolute pointers to a string, and print what each one
           points at, so a coincidental match is visible
  func     disassemble backwards from a reference with objdump and find the
           nearest function prologue (push {..., lr}), which is the entry

  ./romfind.py base  ROM1.BIN
  ./romfind.py xref  ROM1.BIN 0xFF000000 "halt all sockets"
  ./romfind.py func  ROM1.BIN 0xFF000000 0x00ba3bbb

Nothing here writes to the image.
"""
import array, bisect, collections, os, struct, subprocess, sys

def load(path):
    d = open(path, 'rb').read()
    a = array.array('I')
    a.frombytes(d[:len(d) // 4 * 4])
    if struct.pack('<I', 1) != struct.pack('=I', 1):
        a.byteswap()
    return d, a

def word_at(d, off):
    return struct.unpack_from('<I', d, off)[0]

def cstr(d, off, n=60):
    if not (0 <= off < len(d)):
        return None
    s = d[off:off + n].split(b'\0')[0]
    try:
        t = s.decode('ascii')
    except Exception:
        return None
    return t if len(t) >= 4 and t.isprintable() else None


def _ror(v, r):
    r &= 31
    return ((v >> r) | (v << (32 - r))) & 0xFFFFFFFF if r else v

def find_adr(d, base, target, tol=8):
    """
    PC-relative references, which leave no literal in a pool.

    ARM  ADR  = ADD/SUB Rd, PC, #imm  -> 0xE28F0xxx / 0xE24F0xxx (cond varies)
    Thumb ADR = ADD Rd, PC, #imm8*4   -> 0xA000 | Rd<<8 | imm8

    This is why "halt all sockets" showed no xref: a string used through ADR
    has no 32-bit word pointing at it, and a literal-pool scan can never find
    it. Both forms are checked here; the ARM PC bias is +8, the Thumb ADR bias
    is +4 from the instruction's own address rounded to 4.
    """
    out = []
    a = array.array('I')
    a.frombytes(d[:len(d) // 4 * 4])
    if struct.pack('<I', 1) != struct.pack('=I', 1):
        a.byteswap()
    for i, w in enumerate(a):
        pc = base + i * 4
        op = w & 0x0FFF0000
        if op in (0x028F0000, 0x024F0000):          # ARM add/sub rd, pc, #imm
            imm = _ror(w & 0xFF, ((w >> 8) & 0xF) * 2)
            v = pc + 8 + imm if op == 0x028F0000 else pc + 8 - imm
            if any(v == target + k for k in range(tol)):
                out.append((pc, 'arm adr/add', v))
        # Thumb: two halfwords, so step on 2-byte grid
    for i in range(0, len(d) - 1, 2):
        hw = struct.unpack_from('<H', d, i)[0]
        if (hw & 0xF800) == 0xA000:                  # thumb ADR
            pc = base + ((i + 4) & ~3)
            v = pc + (hw & 0xFF) * 4
            if any(v == target + k for k in range(tol)):
                out.append((base + i, 'thumb adr', v))
    return out

def cmd_base(path):
    d, a = load(path)
    # The score is the FRACTION of in-range pointers that land on a readable
    # string, not how many there are: a count just rewards a big image, while
    # the wrong base scatters its pointers into noise.
    best = []
    for base in range(0xE0000000, 0x100000000, 0x100000):
        inr = [w for w in a if base <= w < base + len(d)]
        if len(inr) < 2:
            continue
        step = max(1, len(inr) // 400)
        sample = inr[::step][:400]
        good = sum(1 for w in sample if cstr(d, w - base))
        if good:
            best.append((good * 1000 // len(sample), len(inr), base, sample))
    best.sort(reverse=True)
    if not best:
        print('no candidate base; the image may not contain absolute pointers')
        return 1
    for pct, count, base, sample in best[:3]:
        names = [n for n in (cstr(d, w - base) for w in sample) if n]
        print('base 0x%08x  %d pointers, %d.%d%% land on a string'
              % (base, count, pct // 10, pct % 10))
        for n in names[:5]:
            print('    -> %r' % n)
    return 0

def cmd_xref(path, base, text):
    d, a = load(path)
    off = d.find(text.encode())
    if off < 0:
        print('string not found in image')
        return 1
    print('string %r at file offset 0x%08x = address 0x%08x'
          % (text, off, base + off))
    refs = []
    for i in range(0, len(a)):
        w = a[i]
        if w == base + off or w == base + off + 1:
            refs.append(i * 4)
    if not refs:
        print('no absolute pointer. Trying PC-relative ADR, which leaves no')
        print('literal in a pool -- the likely reason the pool scan found nothing.')
        found = find_adr(d, base, base + off)
        if not found:
            print('no ADR either. The reference is then in the other ROM image,')
            print('or the string is only reached through a computed offset.')
            return 2
        for pc, kind, v in found[:10]:
            print('  %s at 0x%08x -> 0x%08x  (file offset 0x%08x)'
                  % (kind, pc, v, pc - base))
        print('  those are instruction addresses, not function entries: use')
        print("  ./romfind.py func <img> <base> 0x<addr> on one of them.")
        return 0
    for r in refs[:10]:
        print('  referenced by a literal at file offset 0x%08x' % r)
    return 0

def cmd_func(path, base, off, span=0x600):
    # Accept an ADDRESS as well as a file offset. xref prints addresses, and
    # asking a human to subtract the base from a 16 MB image by hand is how a
    # wrong function gets disassembled and a wrong address ends up in the .sym.
    if off >= base:
        off -= base
    off &= ~1                       # a Thumb cross-reference has bit 0 set
    start = off - span
    if start < 0:
        start = 0
    if start >= off:                # cannot ask objdump for an empty range
        start = off - 0x40 if off >= 0x40 else 0
    args = ['arm-none-eabi-objdump', '-D', '-b', 'binary',
            '-m', 'arm', '--adjust-vma=0x%x' % base,
            # for -b binary the sections are shifted by --adjust-vma, so the
            # range has to be given in the adjusted space
            '--start-address=0x%x' % (base + start),
            '--stop-address=0x%x' % (base + off),
            path]
    try:
        out = subprocess.run(args, capture_output=True, text=True, timeout=120)
    except FileNotFoundError:
        print('arm-none-eabi-objdump not on PATH; it is in toolchain/*/bin')
        return 1
    if out.returncode != 0:
        print(out.stderr[:400])
        return 1
    lines = out.stdout.splitlines()
    # walk backwards for the last `push {..., lr}` before the reference: on
    # ARM the return address is saved on entry, so that is the function start
    cand = None
    for ln in lines:
        if 'push' in ln and 'lr' in ln:
            cand = ln
    print(lines[0] if lines else '')
    if cand:
        print('nearest prologue going back from the reference:')
        print('   ', cand.strip())
        print('That address, if it resolves in the .sym, is the entry point.')
    else:
        print('no push ... lr in the window; widen span or the reference is not')
        print('in function code (it may be in a data literal pool).')
    return 0

def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    c = argv[1]
    if c == 'base':
        return cmd_base(argv[2])
    if c == 'xref':
        return cmd_xref(argv[2], int(argv[3], 0), argv[4])
    if c == 'func':
        return cmd_func(argv[2], int(argv[3], 0), int(argv[4], 0))
    print(__doc__)
    return 2

if __name__ == '__main__':
    sys.exit(main(sys.argv))
