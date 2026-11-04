#!/usr/bin/env python3
"""Fit a .cube into a .fs, so the shutter path can approximate the LUT.

Why this exists: the develop reads <lut> and applies the cube, but the frame
Canon encodes is graded by fs_yuvlook_apply(), which knows only the .fs
tables -- curves, six hue axes, split tone, saturation. A stock whose colour
is entirely in its cube therefore came out UNGRADED at capture while the
develop showed the film. This projects the cube onto the .fs vocabulary so the
two agree as closely as a separable model can.

What is fitted, in the order the engine applies it
(look.c grade_row):

  1. CURVE_R/G/B     the neutral axis: what lut(v,v,v) does to each channel,
                     as 17 monotone-cubic control points (the engine's own
                     evaluator, mirrored here and cross-checked below).
  2. HUE_SAT/ROT/LUM six sectors, from coloured probes: how far each hue's
                     chroma is scaled, rotated and lifted AFTER the curves.
  3. SAT             the mean of those six chroma scales, so the per-sector
                     numbers only carry the differences between hues.

Split tone and halation are left out on purpose: the per-channel curves
already carry the cast along the neutral axis, and splitting it into sh/hi
would count it twice.

STATUS OF THE HUE/SAT STAGE: fitted, measured, and OFF by default.
On IMG_9937 and IMG_9920 through the real engine, against the same frame
developed with the cube:

    stock Vista 200     IMG_9937      IMG_9920
    flat recipe now       7.73          8.23   (the frame is ungraded)
    curves only           3.35          4.32   <- what this writes
    curves + hue + sat   11.58         13.65   (worse than doing nothing)

The separable model can carry the tone and the cast. It cannot carry the
colour: a film LUT moves saturated colours by 20-40 levels in a way six hue
sectors and one saturation multiplier do not describe, and a fit that chases
it makes a real photograph worse. `--hue` enables it for experiments.

    python3 lut2fs.py --fit in.cube out.fs     # write a recipe
    python3 lut2fs.py --report in.cube         # params + probe residual
    python3 lut2fs.py --hue --fit ...          # include the hue stage
"""
import argparse
import math
import sys

import cube2bin

SECTORS = ("R", "Y", "G", "C", "B", "M")
CTRL = list(range(0, 256, 16)) + [255]          # 17 control points, max 24
PTS = 24


# ---------------------------------------------------------------- the cube
class Lut:
    def __init__(self, path):
        self.size, self.dmin, self.dmax, self.vals = cube2bin.parse_cube(path)

    def __call__(self, r, g, b):
        """0..255 in, 0..255 floats out."""
        v = cube2bin.sample(self.size, self.dmin, self.dmax, self.vals,
                            min(1.0, max(0.0, r / 255.0)),
                            min(1.0, max(0.0, g / 255.0)),
                            min(1.0, max(0.0, b / 255.0)))
        return [min(255.0, max(0.0, c * 255.0)) for c in v]


# ------------------------------------------------- the engine's interpolator
def hermite(xs, ys, x):
    """grade.c se_curve_eval, mirrored: monotone cubic Hermite with the
    Fritsch-Carlson tangent clamp. Cross-checked against tabledump below."""
    n = len(xs)
    if n < 2:
        return x
    if x <= xs[0]:
        return ys[0]
    if x >= xs[-1]:
        return ys[-1]
    d = []
    for i in range(n - 1):
        dx = xs[i + 1] - xs[i]
        d.append((ys[i + 1] - ys[i]) / dx if dx > 1e-6 else 0.0)
    m = [0.0] * n
    m[0] = d[0]
    m[n - 1] = d[n - 2]
    for i in range(1, n - 1):
        m[i] = 0.0 if d[i - 1] * d[i] <= 0 else (d[i - 1] + d[i]) * 0.5
    for i in range(n - 1):
        if d[i] == 0.0:
            m[i] = m[i + 1] = 0.0
            continue
        a = m[i] / d[i]
        b = m[i + 1] / d[i]
        s = a * a + b * b
        if s > 9.0:
            f = 3.0 / math.sqrt(s)
            m[i] = f * a * d[i]
            m[i + 1] = f * b * d[i]
    k = 0
    for i in range(n - 1):
        if xs[i] <= x <= xs[i + 1]:
            k = i
            break
    h = xs[k + 1] - xs[k]
    t = (x - xs[k]) / h
    t2 = t * t
    t3 = t2 * t
    h00 = 2 * t3 - 3 * t2 + 1
    h10 = t3 - 2 * t2 + t
    h01 = -2 * t3 + 3 * t2
    h11 = t3 - t2
    return h00 * ys[k] + h10 * h * m[k] + h01 * ys[k + 1] + h11 * h * m[k + 1]


# --------------------------------------------------------------- colour
def luma8(r, g, b):
    return (77 * r + 150 * g + 29 * b) >> 8


def hue1536(r, g, b):
    mx = max(r, g, b)
    mn = min(r, g, b)
    d = mx - mn
    if d <= 0:
        return -1, 0
    if mx == r:
        h = (g - b) * 256 // d
    elif mx == g:
        h = (b - r) * 256 // d + 512
    else:
        h = (r - g) * 256 // d + 1024
    return (h % 1536), d


def ycc(r, g, b):
    y = luma8(r, g, b)
    return y, b - y, r - y          # the (u,v) the hue stage rotates


def from_yuv601(y, u, v):
    """(Y, Cb, Cr) -> RGB, the same integer path look.c uses."""
    u = int(round(u))
    v = int(round(v))
    r = y + ((359 * v) >> 8)
    b = y + ((454 * u) >> 8)
    g = y - ((88 * u + 183 * v) >> 8)
    return r, g, b


# ---------------------------------------------------------------- the fit
def fit(cube_path, verbose=False):
    lut = Lut(cube_path)

    curves = []
    for c in range(3):
        curves.append([(x, lut(x, x, x)[c]) for x in CTRL])

    xs = [p[0] / 255.0 for p in curves[0]]
    ys = [[p[1] / 255.0 for p in curves[c]] for c in range(3)]

    def mapc(c, v):
        return min(255.0, max(0.0, hermite(xs, ys[c], v / 255.0) * 255.0))

    def post(p):
        return [mapc(c, p[c]) for c in range(3)]

    gains = [[] for _ in range(6)]
    rots = [[] for _ in range(6)]
    lums = [[] for _ in range(6)]

    for level in (72, 112, 152, 192):
        for ang in range(0, 360, 30):
            rad = 0.34 * level
            u = rad * math.cos(math.radians(ang))
            v = rad * math.sin(math.radians(ang))
            p = from_yuv601(level, u, v)
            if min(p) < 0 or max(p) > 255:
                continue
            tgt = lut(*p)
            src = post(p)

            yt, ut, vt = ycc(*[int(round(x)) for x in tgt])
            yp, up, vp = ycc(*[int(round(x)) for x in src])
            if yp <= 4:
                continue

            # the sector the ENGINE will assign, from the post-curve colour
            h, _ = hue1536(*[int(round(x)) for x in src])
            if h < 0:
                continue
            sec = h >> 8

            mp = math.hypot(up, vp)
            mt = math.hypot(ut, vt)
            if mp < 8 or mt < 4:
                continue

            gains[sec].append(mt / mp)
            rots[sec].append(
                math.degrees(math.atan2(vt, ut) - math.atan2(vp, up)))
            # against the pixel's own post-curve luma, not the grey curve at
            # that level: a saturated colour is not where the neutral axis is
            if yp > 4:
                lums[sec].append(yt / yp)

    per_sec = []
    for s in range(6):
        g = sum(gains[s]) / len(gains[s]) if gains[s] else 1.0
        r = sum(rots[s]) / len(rots[s]) if rots[s] else 0.0
        if r > 180:
            r -= 360
        if r < -180:
            r += 360
        l = sum(lums[s]) / len(lums[s]) if lums[s] else 1.0
        per_sec.append((g, r, l))

    sat = sum(g for g, _, _ in per_sec) / 6.0
    if sat <= 0.01:
        sat = 1.0
    hue_sat = [max(20, min(400, round(100.0 * g / sat))) for g, _, _ in per_sec]
    hue_rot = [max(-180, min(180, round(r))) for _, r, _ in per_sec]
    hue_lum = [max(20, min(400, round(100.0 * l))) for _, _, l in per_sec]
    sat100 = max(0, min(400, round(100.0 * sat)))

    # probe residual: the model with these numbers, against the cube
    def model(p):
        src = post(p)
        h, chroma = hue1536(*[int(round(x)) for x in src])
        y, u, v = ycc(*[int(round(x)) for x in src])
        if h is not None and h >= 0:
            sec = h >> 8
            u = int(u * hue_sat[sec] / 100.0)
            v = int(v * hue_sat[sec] / 100.0)
            y = int(y * hue_lum[sec] / 100.0)
            a = math.radians(hue_rot[sec])
            cs, sn = math.cos(a), math.sin(a)
            u2 = u * cs - v * sn
            v2 = u * sn + v * cs
            u, v = int(u2), int(v2)
        u = int(u * sat100 / 100.0)
        v = int(v * sat100 / 100.0)
        return from_yuv601(y, u, v)

    err = []
    for level in (64, 96, 128, 160, 192):
        for ang in range(0, 360, 20):
            rad = 0.3 * level
            p = from_yuv601(level, rad * math.cos(math.radians(ang)),
                            rad * math.sin(math.radians(ang)))
            if min(p) < 0 or max(p) > 255:
                continue
            t = [int(round(x)) for x in lut(*p)]
            m = model(p)
            err.append(max(abs(t[c] - m[c]) for c in range(3)))
    mean_err = sum(err) / len(err) if err else -1

    return dict(curves=curves, hue_sat=hue_sat, hue_rot=hue_rot,
                hue_lum=hue_lum, sat=sat100, residual=mean_err,
                worst=max(err) if err else -1)


def write_fs(path, name, kind, iso, f, grain, gsize, hal, thr, rad, hue=False):
    with open(path, "w") as fp:
        fp.write("VERSION   3\n")
        fp.write("NAME      %s\n" % name.upper())
        fp.write("DESC      IMPORTED: CUBE FITTED INTO THE SHUTTER-PATH CURVES\n")
        fp.write("WB        AS_SHOT\n")
        for c, tag in enumerate(("CURVE_R", "CURVE_G", "CURVE_B")):
            pts = " ".join("%d,%d" % (x, int(round(min(255, max(0, y)))))
                           for x, y in f["curves"][c])
            fp.write("%-10s%s\n" % (tag, pts))
        if hue:
            fp.write("SAT       %d\n" % f["sat"])
            fp.write("HUE_SAT   %s\n" % ",".join(str(v) for v in f["hue_sat"]))
            fp.write("HUE_ROT   %s\n" % ",".join(str(v) for v in f["hue_rot"]))
            fp.write("HUE_LUM   %s\n" % ",".join(str(v) for v in f["hue_lum"]))
        else:
            # Fitted but not written: see the measurement at the top. The
            # frame keeps the cube's tone and cast, and its colour is left
            # alone rather than moved somewhere the model cannot justify.
            fp.write("SAT       100\n")
            fp.write("HUE_SAT   100,100,100,100,100,100\n")
            fp.write("HUE_ROT   0,0,0,0,0,0\n")
            fp.write("HUE_LUM   100,100,100,100,100,100\n")
        fp.write("HALATION  %d %d %d\n" % (hal, thr, rad))
        fp.write("HAL_TINT  255,86,40\n")
        fp.write("GRAIN     %d\n" % grain)
        fp.write("GRAIN_SIZE %d\n" % gsize)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cube")
    ap.add_argument("--fit", metavar="out.fs")
    ap.add_argument("--name", default="IMPORTED")
    ap.add_argument("--kind", default="neg")
    ap.add_argument("--iso", type=int, default=200)
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--hue", action="store_true",
                    help="write the hue/sat stage too (measured worse; see above)")
    a = ap.parse_args()

    f = fit(a.cube)
    if a.report or not a.fit:
        print("%s: SAT %d  HUE_SAT %s  HUE_ROT %s  HUE_LUM %s"
              % (a.cube, f["sat"], f["hue_sat"], f["hue_rot"], f["hue_lum"]))
        print("  probe residual: mean %.2f, worst %d levels"
              % (f["residual"], f["worst"]))
    if a.fit:
        write_fs(a.fit, a.name, a.kind, a.iso, f, 10, 10, 45, 226, 16, hue=a.hue)
        print("wrote %s" % a.fit)
    return 0


if __name__ == "__main__":
    sys.exit(main())
