#!/usr/bin/env python3
"""Structural analysis of Canon Picture Style slot bodies (the 16 632-byte payload).

The slot blob is 16 704 bytes: a 0x48-byte header (length, base style, name,
sliders, constants) followed by a 16 632-byte body.  This tool answers the
question "can we author that body ourselves, or must we clone it?" by running
a battery of tests against bodies read back from flash or from the camera.

Usage:
    decode_body.py                      # analyse the 3 real bodies in ROM0.BIN
    decode_body.py --rom path/ROM0.BIN
    decode_body.py --body SLOT1.BIN ...  # analyse dumped slot blobs instead
    decode_body.py --cmp SLOT1.BIN      # compare a dump against the ROM slots

Findings on 6D 1.1.6 (CineStyle / Film1ByAmaBao / chan dung1):
  * no standard codec matches (raw deflate, lzma, bz2 all fail)
  * byte histogram is flat (empirical entropy ~7.1/8 per 256-byte block)
  * body repeats every 0x2000 with 64 % of bytes identical, the rest differing
    by small nibble-level amounts
  * body divides evenly into 4096 four-byte records (16384) + a 248-byte trailer
  * differences between the two 8192-byte halves concentrate at byte offsets
    == 2 (mod 4)
  * nothing resembles a LUT: no gradient, no ramp, no sawtooth of period 16

Interpretation: the body is whitened (XOR/permutation with an 8192-byte pad)
over a table that is largely identical in both halves.  Recovering the pad
needs a known-plaintext pair, i.e. a .pf2/.pf3 whose body was registered on
this same camera.  Until then the only sound way to get a real look into
User 1/2/3 is to clone one of the bodies the camera already holds.
"""

import argparse
import bz2
import lzma
import math
import struct
import sys
import zlib
from collections import Counter

BLOB_SIZE = 16704
HEAD_SIZE = 0x48
BODY_SIZE = BLOB_SIZE - HEAD_SIZE
PERIOD = 0x2000
ROM_SLOTS = (0x61B40, 0x65C94, 0x69DE8)


def entropy(data):
    if not data:
        return 0.0
    n = len(data)
    return -sum(v / n * math.log2(v / n) for v in Counter(data).values())


def xor_bytes(a, b):
    return bytes(x ^ y for x, y in zip(a, b))


def load_bodies_from_rom(path):
    rom = open(path, "rb").read()
    bodies = []
    for off in ROM_SLOTS:
        blob = rom[off:off + BLOB_SIZE]
        if len(blob) < BLOB_SIZE or struct.unpack("<H", blob[:2])[0] != 0x4140:
            print(f"  ! slot @{off:#x} not found (skipped)")
            continue
        name = blob[4:0x1C].split(b"\0")[0].decode("latin-1")
        bodies.append((f"{name} @{off:#x}", blob[0x48:]))
    return bodies


def load_bodies_from_files(paths):
    bodies = []
    for p in paths:
        data = open(p, "rb").read()
        if len(data) >= BLOB_SIZE:
            data = data[HEAD_SIZE:BLOB_SIZE]
        name = p.rsplit("/", 1)[-1]
        bodies.append((name, data))
    return bodies


def find_period(body):
    """Best repeating period: fraction of equal bytes at distance p."""
    n = len(body)
    best = []
    for p in range(256, n // 2 + 1, 4):
        hits = sum(1 for i in range(0, n - p, 7) if body[i] == body[i + p])
        best.append((hits / ((n - p) // 7), p))
    best.sort(reverse=True)
    return best


def probe_codecs(body):
    results = {}
    for name, fn in (("raw deflate", lambda x: zlib.decompress(x, -15)),
                     ("lzma", lzma.decompress),
                     ("bz2", bz2.decompress)):
        ok = False
        for label, seg in (("whole", body),
                           (f"head {PERIOD}", body[:PERIOD]),
                           (f"tail {PERIOD}", body[PERIOD:2 * PERIOD]),
                           ("trailer", body[2 * PERIOD:])):
            try:
                fn(seg)
                ok = True
                results[name] = label
                break
            except Exception:
                pass
        if not ok:
            results[name] = None
    return results


def diff_residues(body):
    """Where do the two 8192-byte halves differ, modulo small strides?"""
    diffs = [i for i in range(len(body) - PERIOD) if body[i] != body[i + PERIOD]]
    out = {}
    for m in (2, 3, 4, 8):
        counts = Counter(i % m for i in diffs)
        expected = len(diffs) / m
        out[m] = (counts, max(abs(v - expected) / expected for v in counts.values()))
    return diffs, out


def lut_checks(body):
    """Look for any LUT-like ordering in 4-byte records."""
    recs = [body[i:i + 4] for i in range(0, 4 * (len(body) // 4), 4)]
    report = []
    for ch in range(4):
        vals = [r[ch] for r in recs]
        mono = sum(1 for a, b in zip(vals, vals[1:]) if b >= a) / (len(vals) - 1)
        report.append((ch, sum(vals) / len(vals), mono))
    return recs, report


def analyse(label, body):
    n = len(body)
    print(f"\n=== {label} ===")
    print(f"  body length        : {n}")
    print(f"  body entropy       : {entropy(body):.2f} bits/byte")
    periods = find_period(body)
    print("  best periods       : " +
          ", ".join(f"{p:#x} ({f:.2f} equal)" for f, p in periods[:3]))
    period, frac = periods[0][1], periods[0][0]
    if period == PERIOD:
        d = xor_bytes(body[:n - PERIOD], body[PERIOD:])
        print(f"  halves differ      : {sum(1 for b in d if b):5d}/{len(d)} bytes, "
              f"xor entropy {entropy(d):.2f}")
        print(f"  xor top bytes      : " +
              ", ".join(f"{b:#04x}x{c}" for b, c in Counter(d).most_common(5)))
        diffs, res = diff_residues(body)
        print(f"  diff count         : {len(diffs)}")
        for m, (counts, skew) in res.items():
            print(f"    mod {m}: skew {skew:.2f}  " +
                  "[" + " ".join(str(counts.get(k, 0)) for k in range(m)) + "]")
        print(f"  records of 4 B     : {n // 4} ({4 * (n // 4)} B) + {n % 4} B")
        print(f"  2*period split     : {2 * PERIOD} + {n - 2 * PERIOD} trailer "
              f"(trailer starts {body[2 * PERIOD:2 * PERIOD + 8].hex()})")
    codecs = probe_codecs(body)
    print("  codec probe        : " +
          ", ".join(f"{k}={'HIT' if v else 'no'}" for k, v in codecs.items()))
    recs, report = lut_checks(body)
    print("  4-byte record stats: " +
          ", ".join(f"ch{ch}: mean {m:.0f} mono {mono:.2f}" for ch, m, mono in report))
    verdict = "whitened/opaque -> clone only" if not any(codecs.values()) else "DECODED"
    print(f"  verdict            : {verdict}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", default="qemu/6D/ROM0.BIN",
                    help="ROM0.BIN holding the stored style slots")
    ap.add_argument("--body", nargs="+", metavar="BIN",
                    help="slot dump(s) to analyse (SLOT1.BIN etc.)")
    ap.add_argument("--cmp", metavar="BIN",
                    help="compare a slot dump against the slots in ROM")
    args = ap.parse_args()

    if args.body:
        bodies = load_bodies_from_files(args.body)
    else:
        try:
            bodies = load_bodies_from_rom(args.rom)
        except FileNotFoundError:
            sys.exit(f"ROM not found: {args.rom} (use --body SLOT1.BIN)")
        if not bodies:
            sys.exit("no style slots found in ROM")

    for label, body in bodies:
        analyse(label, body)

    if args.cmp and not args.body:
        rom_body = bodies[0][1]
        dump = open(args.cmp, "rb").read()
        dump = dump[HEAD_SIZE:BLOB_SIZE] if len(dump) >= BLOB_SIZE else dump
        same = sum(1 for a, b in zip(dump, rom_body) if a == b)
        print(f"\n=== {args.cmp} vs {bodies[0][0]} ===")
        print(f"  lengths {len(dump)} / {len(rom_body)}, "
              f"{same} identical bytes ({100 * same / max(1, len(rom_body)):.1f} %)")


if __name__ == "__main__":
    main()
