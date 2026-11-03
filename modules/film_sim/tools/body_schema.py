#!/usr/bin/env python3
"""Structural analysis of the 16,632-byte picture-style payload.

Why
---
A User Def slot is `[0x48-byte header][16,632-byte payload]`.  The payload is
the rendering data, and it is the part a module cannot produce: Canon's parser
builds it from a 1-4 KB .pf2 (three colour curves of <=12 points plus six HSL
axes), and what it builds does not look like a table anybody can read directly.

This tool measures structure instead of guessing at one:

  strides   every stride that divides the payload, scored by how often
            payload[i] == payload[i+stride].  A real record array repeats.
  runs      longest monotone increasing/decreasing sequence, which is what a
            transfer curve looks like in any encoding.
  classes   per byte-position-within-a-record value spread.  In a table of
            n-byte records, one position usually varies (the value) while the
            rest stay near-constant (the index).
  diff      two payloads interleaved: which byte ranges carry the style and
            which are constant scaffolding.  That is the fastest way to find
            where the LUT lives.

Usage
-----
    ./body_schema.py SLOT1.BIN SLOT2_AMABAO.BIN
    ./body_schema.py --diff a.BIN b.BIN
"""

import argparse
import collections
import math
import sys

HDR = 0x48
PAYLOAD = 16632


def load(path):
    d = open(path, "rb").read()
    if len(d) < HDR + PAYLOAD:
        sys.exit("%s: %d bytes, need %d" % (path, len(d), HDR + PAYLOAD))
    return d[HDR:HDR + PAYLOAD]


def entropy(b):
    n = len(b)
    return -sum(v / n * math.log2(v / n) for v in collections.Counter(b).values())


def strides(p):
    out = []
    for s in range(2, 2049):
        if len(p) % s:
            continue
        eq = sum(1 for i in range(len(p) - s) if p[i] == p[i + s])
        out.append((eq / (len(p) - s), s))
    return sorted(out, reverse=True)[:6]


def longest_run(p):
    best = (0, 0)
    run = 1
    up = True
    for i in range(1, len(p)):
        d = p[i] - p[i - 1]
        if d == 1:
            if run == 1:
                up = True
            run += 1
        elif d == -1:
            if run == 1:
                up = False
            run += 1
        else:
            run = 1
        if run > best[0]:
            best = (run, i - run + 1)
    return best


def classes(p, rec):
    """value spread per position inside a record of `rec` bytes"""
    cols = []
    for c in range(rec):
        v = p[c::rec]
        cols.append((max(v) - min(v), sum(v) / len(v), c))
    cols.sort(reverse=True)
    return cols


def describe(path):
    p = load(path)
    print("=== %s" % path)
    print("  entropy            %.2f bits/byte" % entropy(p))
    print("  best strides       " + ", ".join("0x%x %.0f%%" % (s, 100 * r) for r, s in strides(p)))
    n, at = longest_run(p)
    print("  longest +-1 run    %d bytes at 0x%x" % (n, at))
    for rec in (4, 8, 12, 33, 216, 248, 264):
        if len(p) % rec:
            continue
        cols = classes(p, rec)
        print("  record %-4d        noisiest cols: %s" % (
            rec, ", ".join("#%d spread=%d" % (c, s) for s, _, c in cols[:3])))


def diff(pa, pb):
    a, b = load(pa), load(pb)
    same = [i for i in range(PAYLOAD) if a[i] == b[i]]
    print("=== %s  vs  %s" % (pa, pb))
    print("  identical bytes    %d / %d (%.1f%%)" % (len(same), PAYLOAD, 100 * len(same) / PAYLOAD))
    # contiguous regions that differ
    regions, start = [], None
    for i in range(PAYLOAD):
        d = a[i] != b[i]
        if d and start is None:
            start = i
        elif not d and start is not None:
            regions.append((start, i)); start = None
    if start is not None:
        regions.append((start, PAYLOAD))
    regions.sort(key=lambda r: r[1] - r[0], reverse=True)
    print("  differing regions  %d; largest:" % len(regions))
    for s, e in regions[:6]:
        print("     0x%04x..0x%04x  %5d bytes" % (s, e, e - s))
    # where do differences sit modulo small numbers?
    for m in (2, 4, 8):
        c = collections.Counter(i % m for i in range(PAYLOAD) if a[i] != b[i])
        print("  differs mod %-2d      %s" % (m, [c.get(k, 0) for k in range(m)]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="*")
    ap.add_argument("--diff", nargs=2, metavar=("A", "B"))
    args = ap.parse_args()
    if args.diff:
        diff(*args.diff)
        return 0
    if not args.files:
        ap.error("give body files, or --diff A B")
    for f in args.files:
        describe(f)
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
