#!/usr/bin/env python3
"""Check that a picture style survived the round trip to the camera and back.

The module writes ML/LOGS/RELOAD.BIN with exactly the bytes the camera pushed
back after a style was loaded.  Compare that against the STYLE file that was
sent and you know, without trusting any of the module's own checking, whether
the camera stored what it was given.

    python3 check_reload.py RELOAD.BIN                       # find the match
    python3 check_reload.py RELOAD.BIN --style styles/STYLE01.BIN
"""

import argparse
import hashlib
import os
import struct
import sys
import zlib

BLOB_SIZE = 16704
BODY_OFF = 0x48


def describe(data, label):
    if len(data) < BODY_OFF:
        print(f"{label}: only {len(data)} bytes, too short to be a style")
        return
    self_len = struct.unpack_from("<H", data, 0)[0]
    name = data[4:0x1C].split(b"\x00")[0].decode("latin1", "replace")
    sliders = struct.unpack_from("<4i", data, 0x24)
    print(f"{label}:")
    print(f"  size        {len(data)}")
    print(f"  self-length 0x{self_len:04x} ({self_len})"
          f"{'' if self_len == len(data) else '   <-- does not match size'}")
    print(f"  base style  0x{data[2]:02x}")
    print(f"  name        '{name}'")
    print(f"  sliders     contrast {sliders[0]:+d}  sharpness {sliders[1]}  "
          f"saturation {sliders[2]:+d}  tone {sliders[3]:+d}")
    print(f"  crc32       0x{zlib.crc32(data) & 0xFFFFFFFF:08x}")
    print(f"  sha256      {hashlib.sha256(data).hexdigest()[:16]}")


def compare(sent, got):
    if len(sent) != len(got):
        print(f"\nSIZE DIFFERS: sent {len(sent)}, got {len(got)}")
        n = min(len(sent), len(got))
    else:
        n = len(sent)

    diffs = [i for i in range(n) if sent[i] != got[i]]
    print(f"\ndiffering bytes: {len(diffs)} of {n}")
    if not diffs:
        print("RESULT: IDENTICAL - the camera stored the style byte for byte")
        return 0

    print("RESULT: NOT identical")
    for i in diffs[:8]:
        if i < BODY_OFF:
            where = "header"
        else:
            where = f"body +0x{i - BODY_OFF:x}"
        print(f"  file +0x{i:04x} ({where}): sent 0x{sent[i]:02x} got 0x{got[i]:02x}")

    body_diff = [i for i in diffs if i >= BODY_OFF]
    header_diff = [i for i in diffs if i < BODY_OFF]
    if header_diff:
        print(f"  header differs at {len(header_diff)} offset(s): the camera rewrote "
              f"the name/sliders, which it is expected to do")
    if body_diff:
        cols = {(i - BODY_OFF) % 4 for i in body_diff}
        print(f"  body differs at {len(body_diff)} offset(s), first at file "
              f"+0x{body_diff[0]:04x} (body +0x{body_diff[0] - BODY_OFF:x}), "
              f"in {len(cols)} of 4 byte columns")
    return 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("reload")
    ap.add_argument("--style")
    ap.add_argument("--dir", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "styles"))
    args = ap.parse_args()

    try:
        got = open(args.reload, "rb").read()
    except OSError as e:
        sys.exit(f"cannot read {args.reload}: {e}")

    cands = []
    if args.style:
        cands = [args.style]
    elif os.path.isdir(args.dir):
        cands = sorted(os.path.join(args.dir, f) for f in os.listdir(args.dir)
                       if f.lower().endswith(".bin"))

    describe(got, os.path.basename(args.reload))

    if not cands:
        sys.exit("\nno STYLE files to compare against; pass --style")

    got_crc = zlib.crc32(got) & 0xFFFFFFFF
    hit = None
    for c in cands:
        try:
            d = open(c, "rb").read()
        except OSError:
            continue
        if (zlib.crc32(d) & 0xFFFFFFFF) == got_crc:
            hit = c
            break

    if hit is None and not args.style:
        print("\nno STYLE file on the computer has this CRC32")
        print("candidates:")
        for c in cands:
            d = open(c, "rb").read()
            print(f"  {os.path.basename(c)}  0x{zlib.crc32(d) & 0xFFFFFFFF:08x}")
        sys.exit(1)

    target = hit or args.style
    sent = open(target, "rb").read()
    print()
    describe(sent, f"sent: {os.path.basename(target)}")
    sys.exit(compare(sent, got))


if __name__ == "__main__":
    main()
