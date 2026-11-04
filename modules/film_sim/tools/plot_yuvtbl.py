#!/usr/bin/env python3
"""Plot the shot-time tables a .fs recipe bakes into.

A still frame is graded, before Canon's encoder reads it, by a set of tables --
not by a 3D cube. In total 3,080 bytes:

    y[256]        luma curve
    gain[256]     chroma gain per luma
    uoff[256]     split tone / cast, per luma
    voff[256]
    cg[1024]      chroma gain over the 32x32 (U,V) grid
    dy[1024]      luma delta over the same grid
    matrix[4]     hue rotation, 8.8 fixed point

This draws them so the shape of a stock is visible, which the recipe text does
not show. Each panel is auto-scaled to its own data and prints that range, so
two stocks can be compared by shape even when one varies by 2 levels and the
other by 60.

    ./se/hosttest/tabledump films/velvia.fs > /tmp/tbl_velvia.txt
    python3 tools/plot_yuvtbl.py out.png /tmp/tbl_velvia.txt [...]
"""
import sys
from PIL import Image, ImageDraw, ImageFont

INK = (25, 25, 25)
GREY = (120, 120, 120)
RED = (190, 30, 30)
BLUE = (20, 90, 190)
CYAN = (0, 120, 150)


def parse(path):
    name, strength = "?", 100
    one, mat, two, sec = [], None, [], None
    for line in open(path):
        line = line.rstrip("\n")
        if line.startswith("# name"):
            name = line.split(None, 2)[2]
        elif line.startswith("# strength"):
            strength = int(line.split()[2])
        elif line.startswith("["):
            sec = line.strip("[]")
        elif sec == "1D" and line[:1].isdigit():
            one.append([int(v) for v in line.split()])
        elif sec == "MATRIX":
            mat = [int(v) for v in line.split()]
        elif sec == "2D" and line[:1].isdigit():
            two.append([int(v) for v in line.split()])
    return dict(name=name, strength=strength, one=one, mat=mat, two=two)


def font(size, bold=False):
    p = "/System/Library/Fonts/Supplemental/Arial%s.ttf" % (" Bold" if bold else "")
    try:
        return ImageFont.truetype(p, size)
    except Exception:
        return ImageFont.load_default()


def series_panel(d, box, series, names, note):
    x0, y0, w, h = box
    lo = min(min(s) for s in series)
    hi = max(max(s) for s in series)
    if hi - lo < 8:            # keep a tiny wobble from looking like a crash
        mid = (hi + lo) / 2.0
        lo, hi = mid - 4, mid + 4
    d.rectangle([x0, y0, x0 + w, y0 + h], outline=(215, 215, 215))
    cols = [INK, RED, BLUE]
    for i, s in enumerate(series):
        pts = [(x0 + k * w / (len(s) - 1.0), y0 + h - (v - lo) * h / float(hi - lo))
               for k, v in enumerate(s)]
        d.line(pts, fill=cols[i % 3], width=2)
        d.text((x0 + 4, y0 - 16 - i * 14), names[i], fill=cols[i % 3], font=font(11))
    d.text((x0, y0 + h + 4), "%s   range %d .. %d" % (note, lo, hi), fill=GREY, font=font(12))


def grid_panel(d, x0, y0, cell, vals, label):
    n = 32
    lo, hi = min(vals), max(vals)
    for cv in range(n):
        for cu in range(n):
            v = vals[cv * n + cu]
            t = (v - lo) / float(hi - lo) if hi > lo else 0.5
            if t < 0.5:                      # low = blue, mid = white, high = red
                k = t / 0.5
                c = (int(40 + 215 * k), int(100 + 155 * k), int(215 + 35 * k))
            else:
                k = (t - 0.5) / 0.5
                c = (255, int(255 - 190 * k), int(250 - 220 * k))
            d.rectangle([x0 + cu * cell, y0 + cv * cell,
                         x0 + (cu + 1) * cell - 1, y0 + (cv + 1) * cell - 1], fill=c)
    d.rectangle([x0, y0, x0 + n * cell, y0 + n * cell], outline=(150, 150, 150))
    d.text((x0, y0 - 18), label, fill=INK, font=font(13, True))
    d.text((x0, y0 + n * cell + 4), "U -128 .. +127  (left to right)", fill=GREY, font=font(11))
    d.text((x0, y0 + n * cell + 18), "V -128 .. +127  (top to bottom)", fill=GREY, font=font(11))
    d.text((x0, y0 + n * cell + 32), "range %d .. %d" % (lo, hi), fill=GREY, font=font(11))


def main():
    out, paths = sys.argv[1], sys.argv[2:]
    stocks = [parse(p) for p in paths]
    ROW = 290
    W = 1240
    H = 96 + ROW * len(stocks) + 40
    im = Image.new("RGB", (W, H), "white")
    d = ImageDraw.Draw(im)
    d.text((24, 16), "What the camera stores for a film look: 3,080 bytes of tables, not a 3D cube",
           fill=INK, font=font(21, True))
    d.text((24, 44), "y[256] luma curve  ·  gain[256] chroma gain  ·  uoff/voff[256] split tone  ·  "
                     "cg[1024] + dy[1024] over the 32x32 (U,V) grid  ·  matrix[4] hue rotation",
           fill=GREY, font=font(13))
    d.text((24, 64), "Panels are auto-scaled and print their own range, so two stocks are compared by shape.",
           fill=GREY, font=font(13))

    y = 96
    for s in stocks:
        one = s["one"]
        d.text((24, y), "%s" % s["name"], fill=INK, font=font(17, True))
        d.text((24 + 8 + d.textlength(s["name"], font=font(17, True)), y + 2),
               "   strength %d   matrix %s" % (s["strength"], s["mat"]), fill=GREY, font=font(13))
        series_panel(d, (24, y + 48, 250, 112), [[r[0] for r in one]], ["luma curve  y[]"], "y[]")
        series_panel(d, (24, y + 214, 250, 26), [[r[1] for r in one]], ["chroma gain  gain[]"], "gain[]")
        series_panel(d, (292, y + 70, 250, 92), [[r[2] for r in one], [r[3] for r in one]],
                     ["uoff  (blue/amber)", "voff  (green/magenta)"], "uoff / voff")
        grid_panel(d, 596, y + 34, 5, [r[0] for r in s["two"]], "cg[]  chroma gain per hue cell")
        grid_panel(d, 806, y + 34, 5, [r[1] for r in s["two"]], "dy[]  luma delta per hue cell")
        d.text((1012, y + 40), "reading a cell:\nblue = less chroma\nwhite = unchanged\nred = more chroma",
               fill=GREY, font=font(11))
        d.text((1012, y + 120), "The 32x32 grid is what makes\nthis 2D and not a plain curve:\nthe same luma is given a\ndifferent chroma gain depending\non where its colour sits.",
               fill=GREY, font=font(11))
        y += ROW
    im.save(out)
    print("wrote %s %s" % (out, im.size))


if __name__ == "__main__":
    main()
