#!/usr/bin/env python3
"""Convert binary PPM (P6) files to PNG, without external dependencies."""

import os
import struct
import sys
import zlib


def read_ppm(path):
    data = open(path, "rb").read()
    if not data.startswith(b"P6"):
        raise ValueError("%s: not a P6 PPM" % path)

    fields = []
    i = 2
    while len(fields) < 3:
        while i < len(data) and data[i:i + 1].isspace():
            i += 1
        if data[i:i + 1] == b"#":
            while data[i:i + 1] not in (b"\n", b""):
                i += 1
            continue
        start = i
        while i < len(data) and not data[i:i + 1].isspace():
            i += 1
        fields.append(int(data[start:i]))
    i += 1  # single whitespace after maxval

    w, h, maxval = fields
    if maxval != 255:
        raise ValueError("%s: only 8-bit PPM supported" % path)
    return w, h, data[i:i + w * h * 3]


def write_png(path, w, h, rgb):
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(tag, payload):
        c = struct.pack(">I", len(payload)) + tag + payload
        return c + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


def main():
    targets = sys.argv[1:] or ["."]
    for target in targets:
        if os.path.isdir(target):
            files = sorted(f for f in os.listdir(target) if f.endswith(".ppm"))
            files = [os.path.join(target, f) for f in files]
        else:
            files = [target]

        for f in files:
            out = f[:-4] + ".png"
            w, h, rgb = read_ppm(f)
            write_png(out, w, h, rgb)
            print("wrote", out)


if __name__ == "__main__":
    main()
