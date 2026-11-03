#!/usr/bin/env python3
"""Pick a short list of distinct looks out of the converted .pf2 library.

The camera does not render the 16,632-byte payload: it renders base style +
the four sliders.  110 converted files therefore collapse into a few dozen
distinct looks, most of them near-duplicates.  This script keeps one file per
distinct (base, sliders) combination, forces black-and-white styles onto the
real Monochrome base (0x86), and puts B&W first.

    ./curate_styles.py <src dir> <dst dir> [max]

Files written as STYLE01.BIN.. so the module's Load Style cycle walks them.
"""

import glob
import os
import shutil
import struct
import sys

BASE_MONO = 0x86
BW_HINTS = ("bw", "b/w", "mono", "black", "gray", "grey", "黑白", "ilford", "tri-x", "hp5")


def sliders(blob):
    """The four 6D sliders, clamped the way the module clamps them."""
    c, s, sat, tone = (int.from_bytes(blob[0x1C + 4 * i:0x20 + 4 * i], "little", signed=True)
                       for i in range(4))
    return (max(-4, min(4, c)), max(0, min(7, s)), max(-4, min(4, sat)), max(-4, min(4, tone)))


def is_bw(file_name, style_name):
    hay = (file_name + " " + style_name).lower()
    return any(h in hay for h in BW_HINTS)


def fix_blob(blob, force_mono):
    """Write the clamped sliders back and optionally force the Monochrome base."""
    out = bytearray(blob)
    c, s, sat, tone = sliders(blob)
    if force_mono:
        out[2] = BASE_MONO
        sat = 0
    for i, v in enumerate((c, s, sat, tone)):
        out[0x1C + 4 * i:0x20 + 4 * i] = struct.pack("<i", v)
    return bytes(out)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    src, dst = sys.argv[1], sys.argv[2]
    limit = int(sys.argv[3]) if len(sys.argv) > 3 else 12

    seen = {}
    for path in sorted(glob.glob(os.path.join(src, "STYLE*.BIN"))):
        blob = open(path, "rb").read()
        if len(blob) != 16704:
            continue
        name = blob[4:0x1C].split(b"\x00")[0].decode("latin1")
        mono = is_bw(os.path.basename(path), name)
        fixed = fix_blob(blob, mono)
        key = (fixed[2], sliders(fixed))
        # prefer the first file (alphabetical) for each identical look
        seen.setdefault(key, []).append((os.path.basename(path), name, fixed))

    # bucket by base so the short list spans every built-in base style
    buckets = {}
    for (base, sl), group in seen.items():
        fn, name, blob = group[0]
        buckets.setdefault(base, []).append((base == BASE_MONO, base, name, fn, blob, len(group)))

    # within a base, the most "moved" sliders are the most distinct looks
    def bite(p):
        sl = sliders(p[4])
        return (-(abs(sl[0]) + abs(sl[2]) + abs(sl[3])), -sl[1], p[2].lower())

    order = [BASE_MONO, 0x85, 0x84, 0x83, 0x82, 0x81]
    for b in order:
        buckets.get(b, []).sort(key=bite)

    picks = []
    while len(picks) < limit:
        added = False
        for b in order:
            q = buckets.get(b)
            if q:
                picks.append(q.pop(0))
                added = True
                if len(picks) >= limit:
                    break
        if not added:
            break

    os.makedirs(dst, exist_ok=True)
    for f in glob.glob(os.path.join(dst, "STYLE*.BIN")):
        os.remove(f)
    for i, (mono, base, name, fn, blob, dupes) in enumerate(picks, 1):
        with open(os.path.join(dst, f"STYLE{i:02d}.BIN"), "wb") as f:
            f.write(blob)
        tag = "MONO" if mono else "    "
        print(f"STYLE{i:02d} {tag} base 0x{base:02x} sliders {sliders(blob)} "
              f"{name:18s} (-{dupes - 1} duplicate)" if dupes > 1 else
              f"STYLE{i:02d} {tag} base 0x{base:02x} sliders {sliders(blob)} {name}")
    print(f"\n{len(picks)} looks from {sum(len(g) for g in seen.values())} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
