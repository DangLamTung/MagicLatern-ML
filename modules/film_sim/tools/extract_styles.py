#!/usr/bin/env python3
"""Pull the camera's picture style blobs out of a ROM dump.

Each user slot is stored as a 16,704-byte record:

    +0x00  u16  self-length 0x4140
    +0x02  u8   base style id (0x81 Standard, 0x82 Portrait, 0x84 Neutral ...)
    +0x04  24-byte NUL-padded style name
    ...    sliders, then the 16,632-byte body

The records appear twice in ROM (A/B banks), so the same style is found more
than once; identical blobs are de-duplicated by content.

    python3 extract_styles.py ROM0.BIN -o styles/
"""

import argparse
import hashlib
import os
import re
import sys

BLOB_SIZE = 16704
NAME_RE = re.compile(rb"^[\x20-\x7e]{1,23}$")


def find_blobs(data):
    """Yield (offset, name, base_style, blob) for every plausible record."""
    out = []
    pos = 0
    while True:
        pos = data.find(b"\x40\x41", pos)
        if pos < 0:
            return out
        # 0x4140 self-length, then a base style byte, then 0x00
        if (pos + 0x48 <= len(data)
                and data[pos + 1] == 0x41
                and data[pos + 3] == 0x00
                and 0x80 <= data[pos + 2] <= 0x8F):
            raw = data[pos + 4:pos + 0x1C].split(b"\x00")[0]
            if raw and NAME_RE.match(raw):
                out.append((pos, raw.decode("latin1"), data[pos + 2],
                            data[pos:pos + BLOB_SIZE]))
        pos += 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("-o", "--out", default="styles")
    args = ap.parse_args()

    try:
        data = open(args.rom, "rb").read()
    except OSError as e:
        sys.exit(f"cannot read {args.rom}: {e}")

    blobs = find_blobs(data)
    if not blobs:
        sys.exit("no picture style records found - is this really a ROM dump?")

    os.makedirs(args.out, exist_ok=True)
    seen = {}
    n = 0
    for off, name, base, blob in blobs:
        h = hashlib.sha256(blob).hexdigest()
        if h in seen:
            print(f"  dup  '{name}' at 0x{off:06x} == {seen[h]}")
            continue
        n += 1
        path = os.path.join(args.out, f"STYLE{n:02d}.BIN")
        with open(path, "wb") as f:
            f.write(blob)
        seen[h] = os.path.basename(path)
        body = blob[0x48:]
        uniq = len(set(body))
        print(f"  {os.path.basename(path)}  '{name}'  base=0x{base:02x} "
              f"at 0x{off:06x}  body {len(body)} B, {uniq} distinct byte values")

    print(f"\n{n} unique style(s) -> {args.out}")


if __name__ == "__main__":
    main()
