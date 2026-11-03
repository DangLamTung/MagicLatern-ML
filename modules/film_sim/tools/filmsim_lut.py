#!/usr/bin/env python3
"""Export a 3D LUT that matches the Film Simulation ML module.

The module applies this pipeline to the LiveView image (UYVY, BT.601):

    RGB -> YCbCr -> per-luma curve -> chroma matrix (hue) -> saturation
        -> split toning -> GRAIN -> YCbCr -> RGB

This script reproduces the same math on the CPU and writes a .cube LUT, so
that raw stills (CR2/DNG) and raw video (MLV) can be graded to match what
the camera showed in LiveView.  Grain is not part of the LUT (it is spatial
noise, not a colour transform); add grain in your raw converter.

Tested with Resolve, MLVApp and Lightroom (LUT-based camera profiles).

Example:
    python3 filmsim_lut.py -p warm_negative -s 80 -o warm_negative.cube
"""

import argparse
import math
import sys

PRESETS = {
    # name: contrast, black, shoulder, saturation, hue,
    #       shadow_cb, shadow_cr, high_cb, high_cr, hi_desat
    "neutral":        ( 0,  0,  0, 100,  0,   0,   0,   0,   0,  0),
    "fuji_superia":   (32, 10, 30,  92,  5,  10, -12,  -4,   6, 40),
    "fuji_pro400h":   (18,  8, 25,  96,  3,   6,  -6,   2,  -3, 30),
    "kodak_portra":   (15, 10, 25,  90, -3,  -2,   3,  -4,   8, 35),
    "fuji_velvia":    (45,  0, 10, 140,  0,  -8,   2,   8,  -2,  0),
    "cinema":         (25, 12, 35,  85, -6,  14, -18, -10,  14, 45),
    "bw_classic":     (50, 10, 30,   0,  0,   0,   0,   0,   0,  0),
    "tungsten":       (18, 14, 30,  80,  8,  12, -14,  -8,  16, 50),
    # aliases
    "classic_neg":      (32, 10, 30,  92,  5,  10, -12,  -4,   6, 40),
    "fuji_classic_neg": (32, 10, 30,  92,  5,  10, -12,  -4,   6, 40),
    "warm_negative":    (15, 10, 25,  90, -3,  -2,   3,  -4,   8, 35),
    "cool_negative":    (18,  8, 25,  96,  3,   6,  -6,   2,  -3, 30),
    "vivid_slide":      (45,  0, 10, 140,  0,  -8,   2,   8,  -2,  0),
}


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def fast_sin_deg(deg):
    """Same polynomial approximation as the module (8.8 fixed point)."""
    x = deg * 0.01745329252
    x2 = x * x
    return int((x - x * x2 / 6.0 + x * x2 * x2 / 120.0) * 256.0)


def fast_cos_deg(deg):
    x = deg * 0.01745329252
    x2 = x * x
    return int((1.0 - x2 / 2.0 + x2 * x2 / 24.0) * 256.0)


class FilmSim:
    def __init__(self, preset, strength=100):
        if preset not in PRESETS:
            raise ValueError("unknown preset %r (choose from %s)"
                             % (preset, ", ".join(sorted(PRESETS))))
        con, blk, sho, sat, hue, scb, scr, hcb, hcr, hds = PRESETS[preset]
        s = clamp(strength, 0, 100)

        contrast = con / 100.0 * s / 100.0
        lift = blk / 100.0 * s / 100.0 * 0.20
        shoulder = sho / 100.0 * s / 100.0 * 0.5

        self.curve = []
        for i in range(256):
            x = i / 255.0
            # filmic S-curve, identical to the module (rational, no cubics)
            xr = 1.0 - x
            s_curve = (x * x) / (x * x + xr * xr)
            v = x + contrast * (s_curve - x)
            v = v + lift * (1.0 - v) * (1.0 - v)
            v = v * (1.0 + shoulder) / (1.0 + shoulder * v)
            v = clamp(v, 0.0, 1.0)
            y = int(v * 255.0 + 0.5)
            self.curve.append((y * s + i * (100 - s)) // 100)

        sat_f = (1.0 + (sat / 100.0 - 1.0) * s / 100.0) * 128.0
        hds_f = hds / 100.0 * s / 100.0
        scb_f = scb * s / 100.0
        scr_f = scr * s / 100.0
        hcb_f = hcb * s / 100.0
        hcr_f = hcr * s / 100.0

        self.sat = []
        self.cb_off = []
        self.cr_off = []
        for i in range(256):
            t = i / 255.0
            sh = (1.0 - t) * (1.0 - t)
            hi = t * t
            si = sat_f
            if t > 0.6:
                si *= 1.0 - hds_f * (t - 0.6) / 0.4
            self.sat.append(clamp(int(si + 0.5), 0, 255))
            self.cb_off.append(clamp(int(scb_f * sh + hcb_f * hi), -128, 127))
            self.cr_off.append(clamp(int(scr_f * sh + hcr_f * hi), -128, 127))

        hue_s = hue * s / 100
        co = fast_cos_deg(hue_s)
        sn = fast_sin_deg(hue_s)
        self.m = (co, -sn, sn, co)

    def apply(self, r, g, b):
        """r,g,b in 0..255 (ints) -> (r,g,b) in 0..255 (ints)."""
        # BT.601 full range, same as ML's imgconv
        y = int(0.299 * r + 0.587 * g + 0.114 * b + 0.5)
        cb = int(-0.168736 * r - 0.331264 * g + 0.5 * b + 128 + 0.5)
        cr = int(0.5 * r - 0.418688 * g - 0.081312 * b + 128 + 0.5)
        y = clamp(y, 0, 255)
        cb = clamp(cb, 0, 255)
        cr = clamp(cr, 0, 255)

        oy = self.curve[y]

        du = cb - 128
        dv = cr - 128
        nu = (self.m[0] * du + self.m[1] * dv) >> 8
        nv = (self.m[2] * du + self.m[3] * dv) >> 8

        sat = self.sat[oy]
        nu = ((nu * sat) >> 7) + self.cb_off[oy] + 128
        nv = ((nv * sat) >> 7) + self.cr_off[oy] + 128

        cb = clamp(nu, 0, 255) - 128
        cr = clamp(nv, 0, 255) - 128

        y = oy
        rr = y + 1.402 * cr
        gg = y - 0.344136 * cb - 0.714136 * cr
        bb = y + 1.772 * cb
        return (int(clamp(round(rr), 0, 255)),
                int(clamp(round(gg), 0, 255)),
                int(clamp(round(bb), 0, 255)))


def write_cube(sim, size, path, title):
    with open(path, "w") as f:
        f.write("TITLE \"%s\"\n" % title)
        f.write("LUT_3D_SIZE %d\n" % size)
        f.write("DOMAIN_MIN 0.0 0.0 0.0\n")
        f.write("DOMAIN_MAX 1.0 1.0 1.0\n")
        # .cube ordering: red varies fastest
        for bi in range(size):
            b = round(bi * 255.0 / (size - 1))
            for gi in range(size):
                g = round(gi * 255.0 / (size - 1))
                for ri in range(size):
                    r = round(ri * 255.0 / (size - 1))
                    out = sim.apply(r, g, b)
                    f.write("%.6f %.6f %.6f\n"
                            % (out[0] / 255.0, out[1] / 255.0, out[2] / 255.0))


def self_test():
    ok = True

    neutral = FilmSim("neutral")
    for v in range(0, 256, 17):
        got = neutral.apply(v, v, v)
        if max(abs(c - v) for c in got) > 3:
            print("FAIL: neutral preset is not near-identity at %d: %s" % (v, got))
            ok = False

    # a saturated colour must stay saturated (no chroma collapse)
    r, g, b = neutral.apply(200, 60, 40)
    if not (r > g > b):
        print("FAIL: neutral preset changed colour order: %s" % ((r, g, b),))
        ok = False

    bw = FilmSim("bw_classic")
    r, g, b = bw.apply(200, 60, 40)
    if not (r == g == b):
        print("FAIL: B&W preset is not monochrome: %s" % ((r, g, b),))
        ok = False

    for name in PRESETS:
        sim = FilmSim(name)
        prev = -1
        for v in range(256):
            y = sim.apply(v, v, v)[1]
            if y < prev - 1:
                print("FAIL: %s luma curve is not monotonic at %d" % (name, v))
                ok = False
                break
            prev = y

    print("self-test:", "OK" if ok else "FAILED")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--preset", default="warm_negative",
                    choices=sorted(PRESETS))
    ap.add_argument("-s", "--strength", type=int, default=100,
                    help="0-100, matches the module's Strength setting")
    ap.add_argument("-n", "--size", type=int, default=33,
                    help="LUT cube size (17, 33 or 65)")
    ap.add_argument("-o", "--output", default=None,
                    help="output .cube path (default: <preset>.cube)")
    ap.add_argument("--self-test", action="store_true",
                    help="run internal sanity checks and exit")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    sim = FilmSim(args.preset, args.strength)
    out = args.output or "%s.cube" % args.preset
    title = "Magic Lantern Film Sim %s %d%%" % (args.preset, args.strength)
    write_cube(sim, args.size, out, title)
    print("wrote %s (%d^3)" % (out, args.size))
    return 0


if __name__ == "__main__":
    sys.exit(main())
