#!/usr/bin/env python3
"""Run Magic Lantern Film Simulation on any real-world photo.

Feeds the photo into the actual C filter engine (fs_core.h) compiled inside
filmsim_sim, producing real-time LiveView before/after renders and comparison sheets.
"""

import os
import subprocess
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("Error: PIL/Pillow is required. Run: pip install Pillow", file=sys.stderr)
    sys.exit(1)

SIM_DIR = Path(__file__).resolve().parent
SIM_BIN = SIM_DIR / "filmsim_sim"


def compile_sim():
    if not SIM_BIN.exists():
        subprocess.check_call(["make", "-C", str(SIM_DIR), "filmsim_sim"])


def process_photo(photo_path, out_dir="out_photo"):
    compile_sim()
    photo_path = Path(photo_path)
    out_dir_path = SIM_DIR / out_dir
    out_dir_path.mkdir(parents=True, exist_ok=True)

    # 1. Load and resize/crop to 720x480 (Canon 6D LiveView geometry)
    im = Image.open(photo_path).convert("RGB")
    target_w, target_h = 720, 480

    # Aspect fill & center crop
    im_ratio = im.width / im.height
    target_ratio = target_w / target_h

    if im_ratio > target_ratio:
        new_w = int(im.height * target_ratio)
        offset = (im.width - new_w) // 2
        im = im.crop((offset, 0, offset + new_w, im.height))
    else:
        new_h = int(im.width / target_ratio)
        offset = (im.height - new_h) // 2
        im = im.crop((0, offset, im.width, offset + new_h))

    im = im.resize((target_w, target_h), Image.Resampling.LANCZOS)

    input_ppm = out_dir_path / "input.ppm"
    with open(input_ppm, "wb") as f:
        f.write(b"P6\n720 480\n255\n" + im.tobytes())

    # 2. Run the actual C filter engine on the image
    cmd = [str(SIM_BIN), str(out_dir_path), str(input_ppm)]
    print("Running C filter engine (fs_core.h) on %s..." % photo_path.name)
    subprocess.check_call(cmd)

    # 3. Convert all PPMs to PNGs
    subprocess.check_call([sys.executable, str(SIM_DIR / "ppm2png.py"), str(out_dir_path)])

    # 4. Generate comparison contact sheet
    generate_comparison_sheet(out_dir_path)
    print("Done! Output images generated in: %s" % out_dir_path)


def generate_comparison_sheet(out_dir):
    pngs = sorted(out_dir.glob("*.png"))
    pngs = [p for p in pngs if p.name != "comparison_sheet.png" and p.name != "contact-sheet.png"]

    if not pngs:
        return

    # 2 columns x 4 rows layout
    cols = 2
    rows = (len(pngs) + cols - 1) // cols
    card_w, card_h = 720, 480
    header_h = 32

    sheet = Image.new("RGB", (cols * card_w, rows * (card_h + header_h)), (20, 20, 20))
    draw = ImageDraw.Draw(sheet)

    for i, p in enumerate(pngs):
        col = i % cols
        row = i // cols
        x = col * card_w
        y = row * (card_h + header_h)

        label = p.stem.replace("_", " ")
        if label == "before":
            label = "ORIGINAL (BEFORE)"
        elif label.startswith("0"):
            label = label[3:].upper()

        draw.rectangle([x, y, x + card_w, y + header_h], fill=(30, 30, 30))
        draw.text((x + 12, y + 8), label, fill=(230, 230, 230))

        tile = Image.open(p)
        sheet.paste(tile, (x, y + header_h))

    out_sheet = out_dir / "comparison_sheet.jpg"
    sheet.save(out_sheet, quality=92)
    print("Created comparison grid: %s" % out_sheet)


if __name__ == "__main__":
    target = sys.argv[1] if len(sys.argv) > 1 else str(SIM_DIR / "../../../landscape.jpg")
    out = sys.argv[2] if len(sys.argv) > 2 else "out_photo"
    process_photo(target, out)
