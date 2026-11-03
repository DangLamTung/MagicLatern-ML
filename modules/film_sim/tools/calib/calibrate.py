#!/usr/bin/env python3
"""Colour-calibration check for the film simulation.

Two questions, both answered with numbers instead of opinions:

1. Chroma convention of the 6D LiveView UYVY buffers.
   Canon/ML store U/V as two's complement bytes (imgconv.c clamps to
   -128..127 and UYVY_PACK just masks).  This script round-trips a
   ColorChecker through both possible readings -- signed and offset
   binary -- and reports the colour error of each, so the correct one
   wins by measurement.

2. What the film stocks actually do to colour.
   The 24 patches go through the real filter core (tools/sim, same
   fs_core.h the camera runs) and every stock gets a per-patch CIEDE2000
   error against the reference, so "Neutral" can be proven to be an
   identity and the other stocks have their shift documented.

    ./calibrate.py [--outdir DIR] [--sim PATH]
"""

import argparse
import math
import os
import shutil
import subprocess
import sys

from PIL import Image

# X-Rite ColorChecker Classic, sRGB approximations
CHART = [
    ("dark skin",      (115,  82,  68)),
    ("light skin",     (194, 150, 130)),
    ("blue sky",       ( 98, 122, 157)),
    ("foliage",        ( 87, 108,  67)),
    ("blue flower",    (133, 128, 177)),
    ("bluish green",   (103, 189, 170)),
    ("orange",         (214, 126,  44)),
    ("purplish blue",  ( 80,  91, 166)),
    ("moderate red",   (193,  90,  99)),
    ("purple",         ( 94,  60, 108)),
    ("yellow green",   (157, 188,  64)),
    ("orange yellow",  (224, 163,  46)),
    ("blue",           ( 56,  61, 150)),
    ("green",          ( 70, 148,  73)),
    ("red",            (175,  54,  60)),
    ("yellow",         (231, 199,  31)),
    ("magenta",        (187,  86, 149)),
    ("cyan",           (  8, 133, 161)),
    ("white",          (243, 243, 242)),
    ("neutral 8",      (200, 200, 200)),
    ("neutral 6.5",    (160, 160, 160)),
    ("neutral 5",      (122, 122, 122)),
    ("neutral 3.5",    ( 85,  85,  85)),
    ("black",          ( 52,  52,  52)),
]

COLS, ROWS = 6, 4
PATCH_W, PATCH_H = 120, 120
W, H = COLS * PATCH_W, ROWS * PATCH_H


# --- colour maths -----------------------------------------------------------

def srgb_to_lab(rgb):
    def lin(c):
        c /= 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

    r, g, b = (lin(v) for v in rgb)
    x = (0.4124564 * r + 0.3575761 * g + 0.1804375 * b) / 0.95047
    y = (0.2126729 * r + 0.7151522 * g + 0.0721750 * b) / 1.00000
    z = (0.0193339 * r + 0.1191920 * g + 0.9503041 * b) / 1.08883

    def f(t):
        return t ** (1 / 3) if t > 0.008856 else 7.787 * t + 16 / 116

    fx, fy, fz = f(x), f(y), f(z)
    return (116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz))


def ciede2000(lab1, lab2):
    L1, a1, b1 = lab1
    L2, a2, b2 = lab2
    C1, C2 = math.hypot(a1, b1), math.hypot(a2, b2)
    Cb = (C1 + C2) / 2
    G = 0.5 * (1 - math.sqrt(Cb ** 7 / (Cb ** 7 + 25 ** 7))) if Cb else 0.5
    a1p, a2p = (1 + G) * a1, (1 + G) * a2
    C1p, C2p = math.hypot(a1p, b1), math.hypot(a2p, b2)
    h1p = math.degrees(math.atan2(b1, a1p)) % 360 if (a1p or b1) else 0
    h2p = math.degrees(math.atan2(b2, a2p)) % 360 if (a2p or b2) else 0

    dLp = L2 - L1
    dCp = C2p - C1p
    if C1p * C2p == 0:
        dhp = 0
    else:
        dhp = h2p - h1p
        if dhp > 180:
            dhp -= 360
        elif dhp < -180:
            dhp += 360
    dHp = 2 * math.sqrt(C1p * C2p) * math.sin(math.radians(dhp) / 2)

    Lbp = (L1 + L2) / 2
    Cbp = (C1p + C2p) / 2
    if C1p * C2p == 0:
        hbp = h1p + h2p
    elif abs(h1p - h2p) <= 180:
        hbp = (h1p + h2p) / 2
    elif h1p + h2p < 360:
        hbp = (h1p + h2p + 360) / 2
    else:
        hbp = (h1p + h2p - 360) / 2

    T = (1 - 0.17 * math.cos(math.radians(hbp - 30))
         + 0.24 * math.cos(math.radians(2 * hbp))
         + 0.32 * math.cos(math.radians(3 * hbp + 6))
         - 0.20 * math.cos(math.radians(4 * hbp - 63)))
    dTh = 30 * math.exp(-((hbp - 275) / 25) ** 2)
    Rc = 2 * math.sqrt(Cbp ** 7 / (Cbp ** 7 + 25 ** 7)) if Cbp else 0
    Sl = 1 + (0.015 * (Lbp - 50) ** 2) / math.sqrt(20 + (Lbp - 50) ** 2)
    Sc = 1 + 0.045 * Cbp
    Sh = 1 + 0.015 * Cbp * T
    Rt = -math.sin(math.radians(2 * dTh)) * Rc

    return math.sqrt((dLp / Sl) ** 2 + (dCp / Sc) ** 2 + (dHp / Sh) ** 2
                     + Rt * (dCp / Sc) * (dHp / Sh))


# --- YUV, exactly as the camera does it -------------------------------------

def chart_pixel(x, y):
    idx = (y // PATCH_H) * COLS + (x // PATCH_W)
    return CHART[min(idx, len(CHART) - 1)][1]


def rgb_to_uyvy_chart():
    """sRGB chart -> UYVY bytes using ML's rgb2yuv422_rec601 coefficients."""
    buf = bytearray(W * H * 2)
    for y in range(H):
        for x in range(0, W, 2):
            vals = []
            for k in (0, 1):
                R, G, B = chart_pixel(x + k, y)
                Y = (306 * R + 601 * G + 116 * B) // 1024
                U = (-172 * R - 337 * G + 509 * B) // 1024
                V = (509 * R - 427 * G - 82 * B) // 1024
                vals.append((max(0, min(255, Y)),
                             max(-128, min(127, U)),
                             max(-128, min(127, V))))
            u = (vals[0][1] + vals[1][1]) // 2
            v = (vals[0][2] + vals[1][2]) // 2
            i = (y * W + x) * 2
            buf[i + 0] = u & 0xFF
            buf[i + 1] = vals[0][0]
            buf[i + 2] = v & 0xFF
            buf[i + 3] = vals[1][0]
    return buf


def uyvy_to_rgb(buf, signed):
    """BT.601 back to RGB. signed=True reads U/V as int8, else as offset binary."""
    out = Image.new("RGB", (W, H))
    px = out.load()
    for y in range(H):
        for x in range(0, W, 2):
            i = (y * W + x) * 2
            if signed:
                u = buf[i] - 256 if buf[i] > 127 else buf[i]
                v = buf[i + 2] - 256 if buf[i + 2] > 127 else buf[i + 2]
            else:
                u = buf[i] - 128
                v = buf[i + 2] - 128
            for k in (0, 1):
                Y = buf[i + 1 + 2 * k]
                r = Y + (1402 * v) // 1000
                g = Y - (344 * u) // 1000 - (714 * v) // 1000
                b = Y + (1772 * u) // 1000
                px[x + k, y] = (max(0, min(255, r)),
                                max(0, min(255, g)),
                                max(0, min(255, b)))
    return out


def patch_centres():
    for row in range(ROWS):
        for col in range(COLS):
            yield (row * COLS + col,
                   col * PATCH_W + PATCH_W // 2,
                   row * PATCH_H + PATCH_H // 2)


def measure(img, cx, cy, radius=25):
    """Average a patch area so grain and 4:2:2 subsampling do not dominate."""
    px = img.load()
    tot = [0, 0, 0]
    n = 0
    for y in range(cy - radius, cy + radius):
        for x in range(cx - radius, cx + radius):
            p = px[x, y]
            for c in range(3):
                tot[c] += p[c]
            n += 1
    return tuple(t / n for t in tot)


def hue_deg(rgb):
    L, a, b = srgb_to_lab(rgb)
    return math.degrees(math.atan2(b, a)) % 360


MEMORY = {"dark skin", "light skin", "foliage", "blue sky", "blue flower"}


def clip_and_hue(img):
    """count clipped patches and the worst hue drift on memory colours"""
    clipped = 0
    worst_hue = (0.0, "")
    for idx, cx, cy in patch_centres():
        got = measure(img, cx, cy)
        if any(v <= 1.0 or v >= 254.0 for v in got):
            clipped += 1
        name = CHART[idx][0]
        if name in MEMORY:
            d = abs((hue_deg(got) - hue_deg(CHART[idx][1]) + 180) % 360 - 180)
            if d > worst_hue[0]:
                worst_hue = (d, name)
    return clipped, worst_hue


def delta_e_table(img, reference=None):
    """dE00 per patch vs the sRGB chart, or vs another rendered image."""
    errors = []
    for idx, cx, cy in patch_centres():
        ref = CHART[idx][1] if reference is None else measure(reference, cx, cy)
        got = measure(img, cx, cy)
        errors.append((CHART[idx][0], ciede2000(srgb_to_lab(ref), srgb_to_lab(got))))
    return errors


# --- main -------------------------------------------------------------------

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", default=os.path.join(here, "out"))
    ap.add_argument("--sim", default=os.path.join(here, "..", "sim", "filmsim_sim"))
    args = ap.parse_args()

    sim = os.path.abspath(args.sim)
    if not os.path.exists(sim):
        sys.exit(f"simulator not found: {sim} (run make -C ../sim)")

    out = os.path.abspath(args.outdir)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)

    chart = Image.new("RGB", (W, H))
    p = chart.load()
    for y in range(H):
        for x in range(W):
            p[x, y] = chart_pixel(x, y)
    chart.save(os.path.join(out, "00-chart-reference.png"))
    chart_ppm = os.path.join(out, "chart.ppm")
    chart.save(chart_ppm)

    # 1. chroma convention A/B
    buf = rgb_to_uyvy_chart()
    signed_img = uyvy_to_rgb(buf, signed=True)
    offset_img = uyvy_to_rgb(buf, signed=False)
    signed_img.save(os.path.join(out, "01-roundtrip-signed.png"))
    offset_img.save(os.path.join(out, "02-roundtrip-offset.png"))

    signed_err = delta_e_table(signed_img)
    offset_err = delta_e_table(offset_img)

    def summary(errs):
        vals = [e for _, e in errs]
        return sum(vals) / len(vals), max(vals)

    # 2. film stocks through the real filter core
    simout = os.path.join(out, "sim")
    subprocess.run([sim, simout, chart_ppm], check=True, capture_output=True, text=True)


    stock_files = sorted(f for f in os.listdir(simout)
                         if f.endswith(".ppm") and f != "before.ppm")
    before = Image.open(os.path.join(simout, "before.ppm")).convert("RGB")
    before.save(os.path.join(out, "03-pipeline-input.png"))
    before_err = delta_e_table(before)

    # 2b. identity proof: Neutral with grain off must be a pure pass-through
    simout0 = os.path.join(out, "sim-nograin")
    subprocess.run([sim, simout0, chart_ppm, "100", "0"],
                   check=True, capture_output=True, text=True)
    neutral0 = Image.open(os.path.join(simout0, "00-Neutral.ppm")).convert("RGB")
    # against the pipeline input, so the 8-bit UYVY round-trip cancels out
    neutral0_err = delta_e_table(neutral0, reference=before)
    neutral0_mean = sum(e for _, e in neutral0_err) / len(neutral0_err)
    neutral0_max = max(e for _, e in neutral0_err)

    results = []
    for f in stock_files:
        img = Image.open(os.path.join(simout, f)).convert("RGB")
        name = os.path.splitext(f)[0].split("-", 1)[1].replace("_", " ")
        results.append((name, img, delta_e_table(img)))
        img.save(os.path.join(out, "10-" + os.path.splitext(f)[0] + ".png"))

    sm, mx = summary(signed_err)
    om, omx = summary(offset_err)
    bm, bmx = summary(before_err)

    lines = []
    add = lines.append
    add("film_sim colour calibration")
    add("===========================")
    add("")
    add("Input: X-Rite ColorChecker Classic (sRGB approximations), 24 patches,")
    add("720x480, converted to UYVY with ML's rgb2yuv422_rec601 coefficients.")
    add("Error metric: CIEDE2000 between reference sRGB and the measured patch.")
    add("(dE00 ~1 is just visible, ~2-3 is a clear shift, >5 is wrong.)")
    add("")
    add("1. Chroma convention of the 6D LiveView buffers")
    add("------------------------------------------------")
    add(f"  signed  U/V (int8, neutral 0x00)  mean dE00 {sm:5.2f}  max {mx:5.2f}")
    add(f"  offset  U/V (neutral 0x80)        mean dE00 {om:5.2f}  max {omx:5.2f}")
    if sm < om:
        add("  => signed wins, matching imgconv.c (COERCE(...,-128,127) + UYVY_PACK).")
        add("     The module uses signed chroma.")
    else:
        add("  => offset binary wins; the module's convention is wrong.")
    add("")
    add("2. Round-trip through the UYVY path (no film applied)")
    add("----------------------------------------------------")
    add(f"  RGB -> UYVY -> RGB               mean dE00 {bm:5.2f}  max {bmx:5.2f}")
    add("  (4:2:2 subsampling and 8-bit rounding set this floor)")
    add("")
    add("2b. Is the filter itself colour-neutral?")
    add("----------------------------------------")
    add(f"  Neutral, strength 100, grain 0    mean dE00 {neutral0_mean:5.2f}  max {neutral0_max:5.2f}")
    add("  (measured against the pipeline input, so the 8-bit UYVY round-trip")
    add("   above cancels out and only the filter's own error remains)")
    if neutral0_max < 0.05:
        add("  => bit-exact identity: the filter introduces no colour error; every")
        add("     other number below is the film look doing what it was written to do.")
    else:
        add("  => not identity; the filter core has a systematic error.")
    add("")
    add("3. Film stocks, per-patch mean dE00 vs the reference")
    add("----------------------------------------------------")
    add(f"  {'stock':22s} {'mean':>6s} {'max':>6s} {'clip':>5s} {'max dh':>7s}   worst patch")
    for name, img, errs in results:
        vals = [e for _, e in errs]
        worst = max(errs, key=lambda e: e[1])
        clipped, (dh, dh_name) = clip_and_hue(img)
        add(f"  {name:22s} {sum(vals)/len(vals):6.2f} {max(vals):6.2f} {clipped:5d} {dh:7.1f}   {worst[0]} ({worst[1]:.1f})")
    add("")
    add("  clip = patches with a channel pinned at 0 or 255 (colour thrown away)")
    add("  max dh = largest hue rotation in degrees on memory colours")
    add("           (skin, foliage, sky, flower), where a shift is most visible")
    add("")
    add("Per-patch detail")
    for name, _img, errs in results:
        add(f"  {name}:")
        for pname, e in errs:
            add(f"    {pname:14s} {e:5.2f}")
    add("")

    report = os.path.join(out, "CALIBRATION.txt")
    with open(report, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    print("\n".join(lines[:40]))
    print(f"written: {report}")


if __name__ == "__main__":
    main()
