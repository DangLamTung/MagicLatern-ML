#!/usr/bin/env python3
"""Build deliberately extreme picture styles, so a change is obvious on the LCD.

The camera renders a User Def slot from base style + four sliders only; the
16,632-byte payload is ignored.  Converted .pf2 files therefore all land close
together, which is useless for judging whether a switch worked.  These styles
sit at the ends of the slider range on purpose.

    ./make_extreme_styles.py <template.bin> <outdir>

The template supplies the payload and the header shape (any valid 16,704-byte
blob works); base, name and sliders are overwritten.
"""

import os
import struct
import sys

MASK = 0xDEADBEEF

# name            base  contrast sharp  sat  tone
STYLES = [
    ("CAL mono",       0x86,  0, 3, MASK, MASK),
    ("CAL con +max", 0x81,  4, 3,  2, 0),
    ("CAL con -max", 0x84, -4, 3,  0, 0),
    ("CAL sat +max", 0x83, 2, 3,  4, 0),
    ("CAL sat -max", 0x84, 0, 3, -4, 0),
    ("CAL tone warm",     0x81,  2, 3,  3,  4),
    ("CAL tone cool",     0x81,  2, 3,  3, -4),
    ("CAL flat",      0x84, -4, 3, -4, 0),
    ("CAL punch",         0x83,  4, 7,  4, 0),
    ("CAL hard",  0x81,  4, 3,  0,  0),
]


def build(template, base, name, con, sharp, sat, tone):
    blob = bytearray(template)
    blob[0:2] = struct.pack("<H", 16704)
    blob[2] = base
    blob[3] = 0
    nm = name.encode("ascii")[:23]
    blob[4:0x1C] = nm + b"\x00" * (0x18 - len(nm))
    for i, v in enumerate((con, sharp, sat, tone)):
        blob[0x1C + 4 * i:0x20 + 4 * i] = struct.pack("<I", v & 0xFFFFFFFF)
    blob[0x34:0x3C] = struct.pack("<II", MASK, MASK)     # 5th/6th slider sentinels
    return bytes(blob)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    tpl = open(sys.argv[1], "rb").read()
    if len(tpl) != 16704:
        print(f"template must be 16704 bytes, got {len(tpl)}")
        return 2
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        if f.startswith("STYLE") and f.endswith(".BIN"):
            os.remove(os.path.join(out, f))

    for i, (name, base, con, sharp, sat, tone) in enumerate(STYLES, 1):
        blob = build(tpl, base, name, con, sharp, sat, tone)
        path = os.path.join(out, f"STYLE{i:02d}.BIN")
        open(path, "wb").write(blob)
        sat_s = "n/a" if sat == MASK else f"{sat:+d}"
        print(f"STYLE{i:02d}  base 0x{base:02x}  contrast {con:+d}  sat {sat_s:>4s}  "
              f"tone {tone if tone == MASK else format(tone, '+d'):>4}  {name}")
    print(f"\n{len(STYLES)} styles written to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
