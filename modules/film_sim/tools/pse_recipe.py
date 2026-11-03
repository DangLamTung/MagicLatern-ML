#!/usr/bin/env python3
"""Turn each film stock into Picture Style Editor settings.

Canon's Picture Style Editor (PSE) edits a tone curve plus per-colour-tone
hue/saturation/brightness for six axes (R, Y, G, C, B, M).  That is the same
model the film simulation uses, so the numbers can be transferred directly
instead of guessed:

    python3 pse_recipe.py                  # all stocks -> pse_recipes.txt
    python3 pse_recipe.py -p 3 -o r.txt    # one stock

How the numbers are obtained: a chart of the six primaries plus a 9-step grey
ramp is pushed through the real filter core (tools/sim, same fs_core.h the
camera runs), then the input and output colours are compared in HSL.  The
result is exactly what PSE would have to be told.

Workflow this enables (no reverse engineering, Canon does the conversion):
   1. run this, type the numbers into Picture Style Editor, save the .pf3
   2. register it with EOS Utility into User 2 (or 3) once
   3. boot the camera with the film_sim module: it stores ML/LOGS/SLOT2.BIN
   4. Film -> Source Slot = User 2, Clone Slot -> User 1 -> JPEGs use it

Limits of the transfer: PSE's model is a tone curve plus six hue axes.  The
filter additionally does shadow/highlight split toning and highlight
desaturation, which PSE cannot express, so the .pf3 is a close approximation
rather than a bit-exact copy.  Once the style exists in a user slot, step 4
copies it byte for byte, so the camera is then exact for that style.
Type the numbers with the PSE base style and sliders left at their defaults;
the curve below already contains the contrast/black/shoulder of the stock.
"""

import argparse
import colorsys
import os
import re
import subprocess
import sys

from PIL import Image

W, H = 720, 480
PW = W // 6                        # primary patch width (top half)
RW = W // 9                        # grey ramp patch width (bottom half)
STEPS = 9
PRIMARIES = [
    ("R", (255, 0, 0)),
    ("Y", (255, 255, 0)),
    ("G", (0, 255, 0)),
    ("C", (0, 255, 255)),
    ("B", (0, 0, 255)),
    ("M", (255, 0, 255)),
]


def make_chart(path):
    img = Image.new("RGB", (W, H))
    px = img.load()
    for i, (_, rgb) in enumerate(PRIMARIES):
        for y in range(0, H // 2):
            for x in range(i * PW, min(W, (i + 1) * PW)):
                px[x, y] = rgb
    grey_vals = [round(255 * s / (STEPS - 1)) for s in range(STEPS)]
    for s, v in enumerate(grey_vals):
        for y in range(H // 2, H):
            for x in range(s * RW, min(W, (s + 1) * RW)):
                px[x, y] = (v, v, v)
    img.save(path)
    return grey_vals


def sample(img, cx, cy, r=20):
    px = img.load()
    tot = [0, 0, 0]
    n = 0
    for y in range(max(0, cy - r), min(img.height, cy + r)):
        for x in range(max(0, cx - r), min(img.width, cx + r)):
            p = px[x, y]
            for c in range(3):
                tot[c] += p[c]
            n += 1
    return tuple(t / n for t in tot)


def hue_deg(rgb):
    r, g, b = (v / 255 for v in rgb)
    h, s, v = colorsys.rgb_to_hsv(r, g, b)
    return h * 360, s, v


def axis_delta(before, after):
    h0, s0, v0 = hue_deg(before)
    h1, s1, v1 = hue_deg(after)
    dh = (h1 - h0 + 180) % 360 - 180
    return dh, (s1 - s0) * 100, (v1 - v0) * 100


def parse_presets(header_path):
    """Read fs_presets[] / fs_preset_names[] out of fs_core.h so the report
    cannot drift.  The name table is authoritative: the comment above each
    parameter row carries the older stock names."""
    txt = open(header_path).read()
    body = txt[txt.index("static const fs_params_t fs_presets"):txt.index("static const char * const fs_preset_names")]
    rows = re.findall(r"/\*[^*]*\*/\s*\{\s*([-\d,\s]+?)\s*\}", body)
    tbl = txt[txt.index("static const char * const fs_preset_names"):]
    tbl = tbl[:tbl.index("};")]
    names = re.findall(r'"([^"]+)"', tbl)
    return [(n.strip(), [int(v) for v in vals.split(",")]) for n, vals in zip(names, rows)]


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("-p", "--preset", type=int, help="only this preset index")
    ap.add_argument("-o", "--out", default=os.path.join(here, "pse_recipes.txt"))
    ap.add_argument("--sim", default=os.path.join(here, "sim", "filmsim_sim"))
    ap.add_argument("--header", default=os.path.join(here, "..", "fs_core.h"))
    args = ap.parse_args()

    sim = os.path.abspath(args.sim)
    if not os.path.exists(sim):
        sys.exit(f"simulator missing: {sim}  (make -C ../sim)")

    work = os.path.join(here, "sim", "pse_work")
    os.makedirs(work, exist_ok=True)
    chart = os.path.join(work, "chart.ppm")
    grey_vals = make_chart(chart)
    subprocess.run([sim, work, chart, "100", "0"], check=True, capture_output=True)

    presets = parse_presets(os.path.abspath(args.header))
    before = Image.open(os.path.join(work, "before.ppm")).convert("RGB")
    files = sorted(f for f in os.listdir(work) if re.match(r"^\d\d-.+\.ppm$", f))

    lines = []
    add = lines.append
    add("Picture Style Editor recipes (generated from the film_sim filter core)")
    add("=" * 70)
    add("")
    add("Each block is what Picture Style Editor needs for one stock: a tone curve")
    add("and, for the six colour axes, the hue / saturation / brightness change that")
    add("the filter applies.")
    add("")
    add("Route: enter the numbers in PSE, save a .pf3 (NOT .pf2 - see below), then")
    add("register it with EOS Utility into User Def 1.  The camera's JPEGs use it")
    add("from then on, with no module involved.  To keep it in the card library, use")
    add("Film -> Save User 1 in the module afterwards.")
    add("")
    add(f"tone curve inputs (grey ramp): {grey_vals}")
    add("")
    add("SAVE AS PF3, NEVER PF2.  PSE warns when saving PF2 that these are lost:")
    add("  - tone curve (RGB)")
    add("  - six colour-axis adjustment")
    add("  - sharpness fineness and threshold")
    add("Both of the first two are the whole point of the numbers below, so a PF2")
    add("file would silently discard this recipe.  PF3 keeps them; the 6D reads it.")
    add("")
    add("LIMITS: PSE has a tone curve plus six hue axes.  The filter also does")
    add("shadow/highlight split toning and highlight desaturation, which PSE")
    add("cannot express, so a .pf3 built from these numbers is a close")
    add("approximation, not a bit-exact copy.  Leave the PSE base style and its")
    add("sliders at default - the curve below already contains contrast, black")
    add("point and shoulder.")
    add("")
    add("EASIEST FIRST TEST: the monochrome stock.  Start PSE from its built-in")
    add("Monochrome style, apply the curve below, leave every hue axis at -100")
    add("saturation, save, register.  A black-and-white JPEG is impossible to")
    add("misread, so one shot proves the whole path end to end.")
    add("")

    for idx, f in enumerate(files):
        if args.preset is not None and idx != args.preset:
            continue
        name = os.path.splitext(f)[0].split("-", 1)[1].replace("_", " ")
        params = None
        for pname, vals in presets:
            if re.sub(r"[^a-z0-9]", "", pname.lower()) == re.sub(r"[^a-z0-9]", "", name.lower()):
                params = vals
                break
        after = Image.open(os.path.join(work, f)).convert("RGB")

        add(f"--- {name} " + "-" * max(0, 50 - len(name)))
        if params:
            add(f"  preset params: contrast {params[0]}  black {params[1]}  shoulder {params[2]}"
                f"  saturation {params[3]}  hue {params[4]}")
        add("")
        add("  tone curve (input -> output, 8-bit):")
        pts = []
        for s, v in enumerate(grey_vals):
            cx, cy = min(W - 2, s * RW + RW // 2), H // 2 + H // 4
            out = sample(after, cx, cy)
            o = round(sum(out) / 3)
            pts.append((v, o))
        add("    " + "  ".join(f"{i:3d}->{o:3d}" for i, o in pts))
        add("")
        add("  colour axes (hue deg / sat % / brightness %):")
        achromatic = False
        for i, (axis, rgb) in enumerate(PRIMARIES):
            cx, cy = i * PW + PW // 2, H // 4
            dh, ds, dv = axis_delta(rgb, sample(after, cx, cy))
            if ds <= -99.0:
                achromatic = True       # hue of a neutral patch is undefined
                add(f"    {axis}:  sat -100.0 (fully desaturated)")
            else:
                add(f"    {axis}:  hue {dh:+6.1f}   sat {ds:+6.1f}   bright {dv:+6.1f}")
        if achromatic:
            add("    -> a monochrome style: set saturation -100 on all six axes")
        add("")

    out = os.path.abspath(args.out)
    with open(out, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    print("\n".join(lines[:40]))
    print(f"...\nwritten: {out}")


if __name__ == "__main__":
    main()
