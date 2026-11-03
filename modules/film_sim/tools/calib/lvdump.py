#!/usr/bin/env python3
"""Decode a LiveView UYVY dump taken from the camera.

The film_sim module writes ML/FILMSIM/LV_RAW.BIN with Canon's own LiveView
buffer (see the LVDUMP.TST flag), so this reads the bytes the EDMAC actually
produced -- not a reconstruction.

It decodes the frame with both possible chroma conventions and reports which
one is plausible for real imagery:

  * a real scene is close to grey on average, so the correct reading has
    mean |U|, |V| near 0;
  * the wrong reading is a constant +128 offset, which shows up as a large
    mean and as pixels pinned at the clamp limits.

Random noise (what QEMU produces, since no sensor is emulated) is symmetric
under both readings, so it cannot decide the question -- the dump has to come
from a real camera.

    ./lvdump.py LV_RAW.BIN [--pitch 1440] [--out DIR]
"""

import argparse
import os
import sys

from PIL import Image


def uyvy_to_rgb(buf, pitch, height, signed):
    w = pitch // 2
    out = Image.new("RGB", (w, height))
    px = out.load()
    stats = {"clamped": 0, "pixels": 0, "sum_u": 0, "sum_v": 0, "sum_abs": 0}

    for y in range(height):
        base = y * pitch
        for x in range(0, w, 2):
            i = base + x * 2
            u8, v8 = buf[i], buf[i + 2]
            if signed:
                u = u8 - 256 if u8 > 127 else u8
                v = v8 - 256 if v8 > 127 else v8
            else:
                u = u8 - 128
                v = v8 - 128
            stats["sum_u"] += u
            stats["sum_v"] += v
            stats["sum_abs"] += abs(u) + abs(v)

            for k in (0, 1):
                Y = buf[i + 1 + 2 * k]
                r = Y + (1402 * v) // 1000
                g = Y - (344 * u) // 1000 - (714 * v) // 1000
                b = Y + (1772 * u) // 1000
                rc = 0 if r < 0 else (255 if r > 255 else r)
                gc = 0 if g < 0 else (255 if g > 255 else g)
                bc = 0 if b < 0 else (255 if b > 255 else b)
                if (rc != r) or (gc != g) or (bc != b):
                    stats["clamped"] += 1
                stats["pixels"] += 1
                px[x + k, y] = (rc, gc, bc)
    return out, stats


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--pitch", type=int, default=1440)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    if not os.path.exists(args.dump):
        sys.exit(f"not found: {args.dump}")

    data = open(args.dump, "rb").read()
    height = len(data) // args.pitch
    if height <= 0:
        sys.exit(f"dump is {len(data)} bytes, smaller than one {args.pitch} byte row")

    out = args.out or os.path.dirname(os.path.abspath(args.dump))
    width = args.pitch // 2
    print(f"{args.dump}: {len(data)} bytes -> {width}x{height} UYVY, pitch {args.pitch}")

    results = {}
    for name, signed in (("signed", True), ("offset", False)):
        img, st = uyvy_to_rgb(data, args.pitch, height, signed)
        path = os.path.join(out, f"LV_{name}.png")
        img.save(path)
        n = st["pixels"] or 1
        results[name] = {
            "mean_u": st["sum_u"] / n,
            "mean_v": st["sum_v"] / n,
            "mean_abs": st["sum_abs"] / n,
            "clamped": st["clamped"] / n,
            "path": path,
        }

    print()
    print(f"  {'reading':8s} {'mean U':>8s} {'mean V':>8s} {'mean|UV|':>9s} {'clamped':>8s}")
    for name, r in results.items():
        print(f"  {name:8s} {r['mean_u']:8.1f} {r['mean_v']:8.1f} "
              f"{r['mean_abs']:9.1f} {r['clamped']*100:7.1f}%")

    s, o = results["signed"], results["offset"]
    print()
    noise = abs(s["mean_abs"] - o["mean_abs"]) < 5 and abs(s["clamped"] - o["clamped"]) < 0.05
    if noise:
        print("  inconclusive: this looks like noise (no sensor frames), which is")
        print("  symmetric under both readings.  Dump from a real 6D instead.")
    else:
        winner = "signed" if (abs(s["mean_u"]) + abs(s["mean_v"])) < (abs(o["mean_u"]) + abs(o["mean_v"])) else "offset"
        print(f"  => {winner} U/V is the plausible reading for this frame:")
        print(f"     smaller chroma bias and fewer clamped pixels.")
        print(f"     film_sim uses signed; {'' if winner == 'signed' else 'THAT WOULD BE A MISMATCH. '}")
    for name, r in results.items():
        print(f"  {name}: {r['path']}")


if __name__ == "__main__":
    main()
