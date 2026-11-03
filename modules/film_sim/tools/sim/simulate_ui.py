#!/usr/bin/env python3
"""Simulate the Canon EOS 6D Magic Lantern UI and LiveView display.

Renders pixel-accurate 720x480 screen simulations:
1. Magic Lantern Menu Screen (showing Film Sim submenu & 35mm Grain setting)
2. LiveView Screen (showing real-time Fuji Classic Neg filter + 35mm grain + camera OSD)
3. Combined showcase comparison card
"""

import os
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

SIM_DIR = Path(__file__).resolve().parent
ROOT_DIR = SIM_DIR / "../../../.."
OUT_DIR = SIM_DIR / "out_ui"
OUT_DIR.mkdir(parents=True, exist_ok=True)

FONT_PATH_MONO = "/System/Library/Fonts/Menlo.ttc"
FONT_PATH_SANS = "/System/Library/Fonts/Helvetica.ttc"


def get_font(path, size, index=0):
    try:
        return ImageFont.truetype(path, size, index=index)
    except Exception:
        return ImageFont.load_default()


def render_ml_menu():
    """Render the exact Magic Lantern in-camera menu on 720x480 screen."""
    w, h = 720, 480
    im = Image.new("RGB", (w, h), (14, 14, 14))
    draw = ImageDraw.Draw(im)

    font_tab = get_font(FONT_PATH_SANS, 13, 0)
    font_title = get_font(FONT_PATH_SANS, 16, 1)  # Bold
    font_item = get_font(FONT_PATH_MONO, 15, 0)
    font_val = get_font(FONT_PATH_MONO, 15, 1)   # Bold
    font_help = get_font(FONT_PATH_SANS, 13, 0)
    font_footer = get_font(FONT_PATH_MONO, 11, 0)

    # 1. Top Tab Bar (Y: 0..32)
    draw.rectangle([0, 0, w, 32], fill=(24, 24, 28))
    tabs = ["Audio", "Expo", "Overlay", "Movie", "Shoot", "Focus", "Display", "Prefs", "Modules", "Help"]
    tab_x = 10
    for t in tabs:
        tw = draw.textlength(t, font=font_tab)
        if t == "Display":
            # Active tab highlight
            draw.rectangle([tab_x - 4, 3, tab_x + tw + 4, 29], fill=(0, 90, 180))
            draw.text((tab_x, 8), t, fill=(255, 255, 255), font=font_tab)
        else:
            draw.text((tab_x, 8), t, fill=(160, 160, 160), font=font_tab)
        tab_x += int(tw + 16)

    # 2. Submenu Title Bar (Y: 32..62)
    draw.rectangle([0, 32, w, 62], fill=(18, 32, 54))
    draw.line([0, 62, w, 62], fill=(0, 120, 220), width=2)
    draw.text((14, 38), "Display  >  Film Sim (LiveView & Picture Style)", fill=(240, 240, 255), font=font_title)
    draw.text((w - 110, 40), "[Q: Close]", fill=(180, 210, 240), font=font_tab)

    # 3. Menu Items List (Y: 70..375)
    menu_items = [
        ("LiveView Filter", "[ON]", (40, 220, 80), False, "On/Off"),
        ("Film Stock", "Fuji Classic Neg", (255, 220, 90), False, "Stock"),
        ("Look Strength", "100%", (255, 255, 255), False, "Strength"),
        ("35mm Grain", "20%", (80, 240, 180), True, "Grain slider (Active selection)"),
        ("In-Camera Style", "User 1 (Fuji Neg)", (90, 200, 255), False, "DIGIC 5+"),
        ("JPEG Contrast", "+1", (255, 210, 80), False, "Hardware slider"),
        ("JPEG Saturation", "-1", (255, 210, 80), False, "Hardware slider"),
        ("JPEG Color Tone", "+1", (255, 210, 80), False, "Hardware slider"),
        ("Pipeline Info", "LiveView Only", (180, 180, 180), False, "Notice"),
    ]

    item_y = 70
    row_h = 33
    for name, val, val_col, is_selected, note in menu_items:
        if is_selected:
            # Selected item row background (Magic Lantern blue bar)
            draw.rectangle([6, item_y - 2, w - 6, item_y + row_h - 4], fill=(0, 75, 150))
            draw.rectangle([6, item_y - 2, 12, item_y + row_h - 4], fill=(0, 220, 255))
            text_fill = (255, 255, 255)
            # Arrow indicator
            draw.text((18, item_y + 4), ">", fill=(0, 240, 255), font=font_item)
        else:
            text_fill = (210, 210, 210)

        # Name
        draw.text((36, item_y + 5), name, fill=text_fill, font=font_item)

        # Value
        vw = draw.textlength(val, font=font_val)
        draw.text((w - vw - 36, item_y + 5), val, fill=val_col, font=font_val)

        item_y += row_h

    # 4. Help Box (Y: 375..450)
    draw.rectangle([6, 375, w - 6, 452], fill=(22, 22, 26), outline=(60, 60, 65))
    draw.text((16, 383), "35mm Grain:", fill=(0, 220, 255), font=font_title)
    draw.text((120, 385), "Organic 35mm monochrome film grain intensity (0..100%).", fill=(230, 230, 230), font=font_help)
    draw.text((16, 408), "Default: 20% (subtle film texture). Set to 0 to disable, or 50%+ for push-process look.", fill=(190, 190, 190), font=font_help)
    draw.text((16, 430), "Keys: [LEFT] / [RIGHT] adjust by 5%  |  [SET] reset/toggle  |  [Q] exit submenu", fill=(255, 215, 80), font=font_help)

    # 5. Bottom Status Footer (Y: 456..480)
    draw.rectangle([0, 456, w, 480], fill=(12, 12, 14))
    draw.text((12, 461), "Magic Lantern v2.3.6D.116", fill=(140, 140, 140), font=font_footer)
    draw.text((280, 461), "Bat: 84% (LP-E6)", fill=(80, 220, 100), font=font_footer)
    draw.text((450, 461), "SD Free: 29.4 GB", fill=(160, 160, 160), font=font_footer)
    draw.text((615, 461), "Canon EOS 6D", fill=(200, 200, 200), font=font_footer)

    out_file = OUT_DIR / "ml_menu_simulation.png"
    im.save(out_file, quality=95)
    print("Saved ML Menu Simulation: %s" % out_file)
    return im


def render_liveview_hud():
    """Render the LiveView screen with Fuji Classic Neg filter and camera HUD."""
    w, h = 720, 480

    # 1. Base image with Fuji Classic Neg & grain
    sample_path = SIM_DIR / "out_mountain" / "01-Fuji_Classic_Neg.png"
    if not sample_path.exists():
        sample_path = ROOT_DIR / "landscape.jpg"
    base = Image.open(sample_path).convert("RGBA").resize((w, h))

    # Overlay layer
    overlay = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)

    font_hud_lg = get_font(FONT_PATH_MONO, 16, 1)
    font_hud_md = get_font(FONT_PATH_MONO, 13, 0)
    font_hud_sm = get_font(FONT_PATH_MONO, 11, 0)

    # Top HUD gradient bar
    for y in range(40):
        alpha = int(180 * (1.0 - y / 40.0))
        draw.line([0, y, w, y], fill=(0, 0, 0, alpha))

    # Bottom HUD gradient bar
    for y in range(h - 45, h):
        alpha = int(180 * ((y - (h - 45)) / 45.0))
        draw.line([0, y, w, y], fill=(0, 0, 0, alpha))

    # Top Canon OSD
    draw.text((16, 10), "M", fill=(255, 255, 255, 240), font=font_hud_lg)
    draw.text((50, 10), "1/125", fill=(255, 255, 255, 240), font=font_hud_lg)
    draw.text((125, 10), "F2.8", fill=(255, 255, 255, 240), font=font_hud_lg)
    draw.text((190, 10), "ISO 400", fill=(255, 255, 255, 240), font=font_hud_lg)

    # ML Status badge on top right
    draw.rectangle([380, 8, w - 16, 30], fill=(0, 50, 90, 200), outline=(0, 150, 255, 220))
    draw.text((390, 11), "ML FilmSim: Fuji Classic Neg (Gr 20%)", fill=(100, 230, 255, 255), font=font_hud_sm)

    # Center AF Focus Bracket
    cx, cy = w // 2, h // 2
    bx, by = 60, 45
    # Thin rule-of-thirds grid lines (subtle white dashed)
    grid_col = (255, 255, 255, 55)
    draw.line([w // 3, 0, w // 3, h], fill=grid_col, width=1)
    draw.line([2 * w // 3, 0, 2 * w // 3, h], fill=grid_col, width=1)
    draw.line([0, h // 3, w, h // 3], fill=grid_col, width=1)
    draw.line([0, 2 * h // 3, w, 2 * h // 3], fill=grid_col, width=1)

    # AF Reticle
    af_col = (255, 255, 255, 180)
    draw.line([cx - bx, cy - by, cx - bx + 15, cy - by], fill=af_col, width=2)
    draw.line([cx - bx, cy - by, cx - bx, cy - by + 15], fill=af_col, width=2)
    draw.line([cx + bx, cy - by, cx + bx - 15, cy - by], fill=af_col, width=2)
    draw.line([cx + bx, cy - by, cx + bx, cy - by + 15], fill=af_col, width=2)
    draw.line([cx - bx, cy + by, cx - bx + 15, cy + by], fill=af_col, width=2)
    draw.line([cx - bx, cy + by, cx - bx, cy + by - 15], fill=af_col, width=2)
    draw.line([cx + bx, cy + by, cx + bx - 15, cy + by], fill=af_col, width=2)
    draw.line([cx + bx, cy + by, cx + bx, cy + by - 15], fill=af_col, width=2)

    # Center AF dot
    draw.ellipse([cx - 2, cy - 2, cx + 2, cy + 2], fill=(255, 50, 50, 200))

    # Bottom Canon OSD
    draw.text((16, h - 32), "-2..1..[0]..1..2+", fill=(255, 255, 255, 230), font=font_hud_md)
    draw.text((220, h - 32), "[RAW+L]", fill=(240, 240, 240, 230), font=font_hud_md)
    draw.text((320, h - 32), "WB 5500K (A:2, G:1)", fill=(255, 210, 80, 230), font=font_hud_sm)
    draw.text((510, h - 32), "[ 842]", fill=(255, 255, 255, 230), font=font_hud_md)
    draw.text((610, h - 32), "🔋 84%", fill=(100, 240, 120, 240), font=font_hud_md)

    # Composite overlay over base
    combined = Image.alpha_composite(base, overlay).convert("RGB")
    out_file = OUT_DIR / "liveview_hud_simulation.png"
    combined.save(out_file, quality=95)
    print("Saved LiveView HUD Simulation: %s" % out_file)
    return combined


def create_showcase(menu_im, lv_im):
    """Combine both into an informative side-by-side showcase card."""
    margin = 20
    header_h = 60
    card_w = menu_im.width * 2 + margin * 3
    card_h = menu_im.height + header_h + margin * 2

    card = Image.new("RGB", (card_w, card_h), (18, 18, 22))
    draw = ImageDraw.Draw(card)

    font_hdr = get_font(FONT_PATH_SANS, 24, 1)
    font_sub = get_font(FONT_PATH_SANS, 14, 0)
    font_lbl = get_font(FONT_PATH_MONO, 14, 1)

    # Header
    draw.text((margin, 12), "Canon EOS 6D - Magic Lantern Film Simulation System", fill=(255, 255, 255), font=font_hdr)
    draw.text((margin, 40), "Real-time 35mm Film Grain (0..100%) + Fuji Classic Negative Color Science + DIGIC 5+ Picture Style Sync", fill=(160, 200, 240), font=font_sub)

    # Paste Menu
    x1 = margin
    y1 = header_h + margin
    card.paste(menu_im, (x1, y1))
    draw.rectangle([x1, y1 - 22, x1 + 320, y1], fill=(0, 60, 120))
    draw.text((x1 + 8, y1 - 18), "1. MAGIC LANTERN IN-CAMERA MENU", fill=(255, 255, 255), font=font_lbl)

    # Paste LiveView
    x2 = x1 + menu_im.width + margin
    card.paste(lv_im, (x2, y1))
    draw.rectangle([x2, y1 - 22, x2 + 340, y1], fill=(0, 120, 70))
    draw.text((x2 + 8, y1 - 18), "2. REAL-TIME LIVEVIEW HUD PREVIEW", fill=(255, 255, 255), font=font_lbl)

    out_file = OUT_DIR / "combined_ui_showcase.jpg"
    card.save(out_file, quality=92)
    print("Saved Combined UI Showcase: %s" % out_file)


if __name__ == "__main__":
    menu_img = render_ml_menu()
    lv_img = render_liveview_hud()
    create_showcase(menu_img, lv_img)
