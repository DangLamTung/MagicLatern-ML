#!/usr/bin/env python3
"""Composite the fsg preview frames: PPM fills + JSON text -> PNG.

Text is drawn here rather than in C so the shapes come from the camera code and
the glyphs come from a real font. Sizes mirror the ML fonts closely enough to
judge layout: small 12px, medium 18px, large 26px.
"""
import json
import subprocess
import sys
from PIL import Image, ImageDraw, ImageFont

FONT_PATH = "/System/Library/Fonts/Supplemental/Arial.ttf"
FONT_BOLD = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
SIZES = {0: 13, 1: 19, 2: 27}

def font(size, bold=False):
    path = FONT_BOLD if bold else FONT_PATH
    try:
        return ImageFont.truetype(path, SIZES[size])
    except Exception:
        return ImageFont.load_default()

def render(name):
    img = Image.open(f"/tmp/fsg_{name}.ppm").convert("RGB")
    draw = ImageDraw.Draw(img)
    with open(f"/tmp/fsg_{name}.json") as f:
        items = json.load(f)
    for t in items:
        fnt = font(t["size"], bold=(t["size"] == 2))
        # ML prints with the glyph box top at y; nudge so the preview matches
        draw.text((t["x"], t["y"] - 2), t["s"], font=fnt, fill=tuple(
            (t["fg"] >> s) & 255 for s in (16, 8, 0)))
    return img

def available():
    """Frames the preview actually wrote.

    This used to be a hardcoded list and it silently rotted: preview.c began
    writing stock0..stockN when the shelf drawer was renamed, render.py kept
    asking for shelf0, and the documented command died on FileNotFoundError.
    """
    import glob
    import os
    names = []
    for ppm in sorted(glob.glob("/tmp/fsg_*.ppm")):
        name = os.path.basename(ppm)[4:-4]
        if os.path.exists(f"/tmp/fsg_{name}.json"):
            names.append(name)
    return names


def main():
    names = sys.argv[1:] or available()
    if not names:
        sys.exit("no /tmp/fsg_*.ppm frames: run the preview first")
    frames = [(n, render(n)) for n in names]
    cols, pad = 2, 12
    w, h = 720, 480
    rows = (len(frames) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * w + (cols + 1) * pad, rows * h + (rows + 1) * pad),
                      (24, 24, 28))
    for i, (n, im) in enumerate(frames):
        r, c = divmod(i, cols)
        sheet.paste(im, (pad + c * (w + pad), pad + r * (h + pad)))
        out = f"/tmp/fsg_{n}.png"
        im.save(out)
        print(out)
    sheet.save("/tmp/fsg_sheet.png")
    print("/tmp/fsg_sheet.png")

if __name__ == "__main__":
    main()
