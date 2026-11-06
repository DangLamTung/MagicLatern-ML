#!/usr/bin/env python3
"""
Unit tests for romfind. No ROM needed: each case builds a synthetic image with
exactly the shape the tool is supposed to recognise, so a regression shows up
here instead of as a wrong address in 6D_116.sym.

    ./selftest.py        exit 0 if every case passes
"""
import os, struct, subprocess, sys, tempfile

TOOL = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'romfind.py')
BASE = 0xFF000000
fails = []

def run(*args):
    r = subprocess.run([sys.executable, TOOL] + list(args),
                       capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr

def case(name, ok, detail=''):
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else '  ' + detail[:200]))
    if not ok:
        fails.append(name)

def write(img):
    f = tempfile.NamedTemporaryFile(suffix='.bin', delete=False)
    f.write(bytes(img)); f.close()
    return f.name

def main():
    # 1. a literal pool pointing at a string
    img = bytearray(0x2000)
    img[0x100:0x100+17] = b'halt all sockets\x00'
    img[0x140:0x140+12] = b'Listen queue\x00'
    img[0x180:0x180+8] = b'preserve\x00'
    # base detection scores the FRACTION of in-range pointers that land on a
    # readable string, so a single pointer cannot be scored: give it three
    struct.pack_into('<I', img, 0x1800, BASE + 0x100)
    struct.pack_into('<I', img, 0x1804, BASE + 0x140)
    struct.pack_into('<I', img, 0x1808, BASE + 0x180)
    p = write(img)
    rc, out = run('xref', p, hex(BASE), 'halt all sockets')
    case('literal-pool xref', rc == 0 and 'literal at' in out, out)
    rc, out = run('base', p)
    case('base detection', rc == 0 and '0xff000000' in out.lower(), out)
    os.unlink(p)

    # 2. a reference ONLY by ARM ADR, no literal pool at all -- the case that
    #    made every string search in the field look like "not referenced"
    img = bytearray(0x2000)
    img[0x1080:0x1080+17] = b'halt all sockets\x00'
    struct.pack_into('<I', img, 0x1000, 0xE92D4010)   # push {r4, lr}
    struct.pack_into('<I', img, 0x1004, 0xE28F0074)   # add r0, pc, #0x74
    p = write(img)
    rc, out = run('xref', p, hex(BASE), 'halt all sockets')
    case('ADR xref', rc == 0 and 'adr' in out.lower(), out)
    os.unlink(p)

    # 3. note: func needs objdump, so it is skipped rather than failing CI on a
    #    machine without the ARM toolchain
    print('skipped  func (needs arm-none-eabi-objdump)')

    # 4. the tool must refuse to invent an answer
    img = bytearray(0x400)
    img[0x100:0x110] = b'unreferenced\x00'
    p = write(img)
    rc, out = run('xref', p, hex(BASE), 'unreferenced')
    case('no reference reported honestly', rc == 2, out)
    os.unlink(p)

    print('%d failure(s)' % len(fails))
    return 1 if fails else 0

if __name__ == '__main__':
    sys.exit(main())
