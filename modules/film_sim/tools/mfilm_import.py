#!/usr/bin/env python3
"""Import M-Film's film stock catalogue into this module.

M-Film (https://github.com/HNGSeries/M-Film, GPL-2.0) publishes 72 film
stocks. Nine of them are described by a model in ml-dev/films/films.py; the
other 63 take their colour from the Film-Luts collection by Yahia Angelo
(https://github.com/YahiaAngelo/Film-Luts, MIT), which their builder fetches
at build time. None of the .cube files are in their repository.

This tool takes the catalogue and produces, per stock, the three things this
module needs:

    ML/FILMS/NN_<CODE>.xml    the stock definition, <lut> pointing at  ->
    ML/FILMSIM/LUT/<CODE>.BIN a packed 17^3 LUT baked from the MIT .cube ->
    ML/FILM/<CODE>.fs         the look: grain and halation only

The colour lives entirely in the LUT, as it does on their side ("the colour
from the LUT, the rest from the kind and speed"), so the recipe is flat --
SAT 100, no curve. The LUT is applied after the camera->sRGB conversion,
before the look, which is the order those LUTs expect: they take ordinary
sRGB pictures.

Grain and halation are a fit to their kind and box speed, not their numbers:
their model is in scene-linear stops with its own tone curve, and copying a
number across would be a made-up claim. The mapping is printed per stock so
it can be argued with.

    python3 mfilm_import.py --list
    python3 mfilm_import.py --add apx25 delta3200
    python3 mfilm_import.py --all-missing --out /Volumes/EOS_DIGITAL
    python3 mfilm_import.py --verify --photo IMG_9937.CR2 --out ...

Nothing is written unless --out is given and it is a directory; the .cube
files are cached in tools/.cache/filmluts/.
"""
import argparse
import math
import os
import subprocess
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import cube2bin
import lut2fs

LUT_RAW = "https://raw.githubusercontent.com/YahiaAngelo/Film-Luts/main/luts/%s.cube"
CACHE = os.path.join(HERE, ".cache", "filmluts")

# The nine stocks M-Film models itself rather than through a LUT.
NATIVE = [
    ("portra400", "Kodak", "Portra 400", 400, "neg"),
    ("cinestill800t", "CineStill", "CineStill 800T", 800, "cine"),
    ("ektar100", "Kodak", "Ektar 100", 100, "neg"),
    ("gold200", "Kodak", "Gold 200", 200, "neg"),
    ("superia400", "Fujifilm", "Superia 400", 400, "neg"),
    ("velvia50", "Fujifilm", "Velvia 50", 50, "slide"),
    ("hp5", "Ilford", "HP5 Plus 400", 400, "bw"),
    ("trix400", "Kodak", "Tri-X 400", 400, "bw"),
    ("cinestill400t", "CineStill", "CineStill 400T", 400, "cine"),
]

# key, brand, name, iso, kind, lut path (Film-Luts, MIT)
LUT_STOCKS = [
    ("portra160", "Kodak", "Portra 160", 160, "neg", "negative_new/kodak_portra_160"),
    ("portra800", "Kodak", "Portra 800", 800, "neg", "negative_new/kodak_portra_800"),
    ("portra160nc", "Kodak", "Portra 160NC", 160, "neg", "negative_old/kodak_portra_160_nc"),
    ("portra160vc", "Kodak", "Portra 160VC", 160, "neg", "negative_old/kodak_portra_160_vc"),
    ("portra400nc", "Kodak", "Portra 400NC", 400, "neg", "negative_old/kodak_portra_400_nc"),
    ("portra400vc", "Kodak", "Portra 400VC", 400, "neg", "negative_old/kodak_portra_400_vc"),
    ("portra400uc", "Kodak", "Portra 400UC", 400, "neg", "negative_old/kodak_portra_400_uc"),
    ("elite200", "Kodak", "Elite Color 200", 200, "neg", "negative_color/kodak_elite_color_200"),
    ("elite400", "Kodak", "Elite Color 400", 400, "neg", "negative_color/kodak_elite_color_400"),
    ("pro160c", "Fujifilm", "Pro 160C", 160, "neg", "negative_new/fuji_160c"),
    ("pro400h", "Fujifilm", "Pro 400H", 400, "neg", "negative_new/fuji_400h"),
    ("pro800z", "Fujifilm", "Pro 800Z", 800, "neg", "negative_new/fuji_800z"),
    ("reala100", "Fujifilm", "Reala 100", 100, "neg", "negative_color/fuji_superia_reala_100"),
    ("superia100", "Fujifilm", "Superia 100", 100, "neg", "negative_old/fuji_superia_100"),
    ("superia200", "Fujifilm", "Superia 200", 200, "neg", "negative_color/fuji_superia_200"),
    ("superia800", "Fujifilm", "Superia 800", 800, "neg", "negative_old/fuji_superia_800"),
    ("superia1600", "Fujifilm", "Superia 1600", 1600, "neg", "negative_old/fuji_superia_1600"),
    ("xtra800", "Fujifilm", "X-TRA 800", 800, "neg", "negative_color/fuji_superia_x-tra_800"),
    ("vista200", "Agfa", "Vista 200", 200, "neg", "negative_color/agfa_vista_200"),
    ("ultra100", "Agfa", "Ultra 100", 100, "neg", "negative_color/agfa_ultra_color_100"),
    ("redscale100", "Lomography", "Redscale 100", 100, "neg", "negative_color/lomography_redscale_100"),
    ("velvia100", "Fujifilm", "Velvia 100", 100, "slide", "colorslide/fuji_velvia_100_generic"),
    ("provia100f", "Fujifilm", "Provia 100F", 100, "slide", "colorslide/fuji_provia_100f"),
    ("provia400f", "Fujifilm", "Provia 400F", 400, "slide", "colorslide/fuji_provia_400f"),
    ("provia400x", "Fujifilm", "Provia 400X", 400, "slide", "colorslide/fuji_provia_400x"),
    ("astia100f", "Fujifilm", "Astia 100F", 100, "slide", "colorslide/fuji_astia_100f"),
    ("sensia100", "Fujifilm", "Sensia 100", 100, "slide", "colorslide/fuji_sensia_100"),
    ("kodachrome25", "Kodak", "Kodachrome 25", 25, "slide", "colorslide/kodak_kodachrome_25"),
    ("kodachrome64", "Kodak", "Kodachrome 64", 64, "slide", "colorslide/kodak_kodachrome_64"),
    ("kodachrome200", "Kodak", "Kodachrome 200", 200, "slide", "colorslide/kodak_kodachrome_200"),
    ("e100gx", "Kodak", "Ektachrome E100GX", 100, "slide", "colorslide/kodak_e-100_gx_ektachrome_100"),
    ("e100vs", "Kodak", "Ektachrome E100VS", 100, "slide", "colorslide/kodak_ektachrome_100_vs"),
    ("elitechrome200", "Kodak", "Elite Chrome 200", 200, "slide", "colorslide/kodak_elite_chrome_200"),
    ("elitechrome400", "Kodak", "Elite Chrome 400", 400, "slide", "colorslide/kodak_elite_chrome_400"),
    ("extracolor100", "Kodak", "ExtraColor 100", 100, "slide", "colorslide/kodak_elite_extracolor_100"),
    ("precisa100", "Agfa", "Precisa 100", 100, "slide", "colorslide/agfa_precisa_100"),
    ("elite100xpro", "Kodak", "Elite 100 X-Pro", 100, "xpro", "negative_color/kodak_elite_100_xpro"),
    ("superia200xpro", "Fujifilm", "Superia 200 X-Pro", 200, "xpro", "colorslide/fuji_superia_200_xpro"),
    ("xproslide200", "Lomography", "X-Pro Slide 200", 200, "xpro", "colorslide/lomography_x-pro_slide_200"),
    ("tmax100", "Kodak", "T-MAX 100", 100, "bw", "bw/kodak_t-max_100"),
    ("tmax400", "Kodak", "T-MAX 400", 400, "bw", "bw/kodak_t-max_400"),
    ("tmax3200", "Kodak", "T-MAX P3200", 3200, "bw", "bw/kodak_t-max_3200"),
    ("bw400cn", "Kodak", "BW400CN", 400, "bw", "bw/kodak_bw_400_cn"),
    ("hie", "Kodak", "HIE Infrared", 400, "bw", "bw/kodak_hie_hs_infra"),
    ("delta100", "Ilford", "Delta 100", 100, "bw", "bw/ilford_delta_100"),
    ("delta400", "Ilford", "Delta 400", 400, "bw", "bw/ilford_delta_400"),
    ("delta3200", "Ilford", "Delta 3200", 3200, "bw", "bw/ilford_delta_3200"),
    ("fp4", "Ilford", "FP4 Plus", 125, "bw", "bw/ilford_fp_4_plus_125"),
    ("panf50", "Ilford", "Pan F Plus", 50, "bw", "bw/ilford_pan_f_plus_50"),
    ("hps800", "Ilford", "HPS 800", 800, "bw", "bw/ilford_hps_800"),
    ("xp2", "Ilford", "XP2 Super", 400, "bw", "bw/ilford_xp_2"),
    ("acros100", "Fujifilm", "Acros 100", 100, "bw", "bw/fuji_neopan_acros_100"),
    ("neopan1600", "Fujifilm", "Neopan 1600", 1600, "bw", "bw/fuji_neopan_1600"),
    ("apx25", "Agfa", "APX 25", 25, "bw", "bw/agfa_apx_25"),
    ("apx100", "Agfa", "APX 100", 100, "bw", "bw/agfa_apx_100"),
    ("retro80s", "Rollei", "Retro 80S", 80, "bw", "bw/rollei_retro_80s"),
    ("retro100", "Rollei", "Retro 100 Tonal", 100, "bw", "bw/rollei_retro_100_tonal"),
    ("ortho25", "Rollei", "Ortho 25", 25, "bw", "bw/rollei_ortho_25"),
    ("ir400", "Rollei", "Infrared 400", 400, "bw", "bw/rollei_ir_400"),
    ("kodak2383", "Kodak", "Vision 2383", 200, "print", "print/kodak_2383_constlmap"),
    ("kodak2393", "Kodak", "Vision 2393", 200, "print", "print/kodak_2393_constlmap"),
    ("fuji3510", "Fujifilm", "Eterna 3510", 200, "print", "print/fuji_3510_constlmap"),
    ("fuji3513", "Fujifilm", "Eterna 3513", 200, "print", "print/fuji_3513_constlmap"),
]

KIND_NAME = {
    "neg": "Color Negative", "slide": "Slide", "xpro": "Cross Processed",
    "bw": "Black & White", "cine": "Cinema", "print": "Print Film",
}

# Halation in this module's units (0..100) by kind, from the stocks already
# tuned by hand: slides glow least, cinema negatives most.
HALATION = {"neg": 45, "slide": 30, "xpro": 35, "bw": 40, "cine": 55, "print": 20}


def catalogue():
    """Every stock, as (key, brand, name, iso, kind, is_native)."""
    out = [(k, b, n, i, kind, True) for (k, b, n, i, kind) in NATIVE]
    out += [(k, b, n, i, kind, False) for (k, b, n, i, kind, _p) in LUT_STOCKS]
    return out


def lut_path_of(key):
    for (k, _b, _n, _i, _kind, p) in LUT_STOCKS:
        if k == key:
            return p
    return None


# Stocks we already ship under another name: importing them again would put
# the same film on the shelf twice.
SAME_AS = {
    "panf50": "PAN F PLUS 50",
    "hp5": "HP5 PLUS",
    "trix400": "TRI-X 400",
    "acros100": "ACROS 100",
}


def already_have(key, name, have_names):
    if name.lower() in have_names:
        return True
    other = SAME_AS.get(key)
    return bool(other and other.lower() in have_names)


def code_for(name):
    """'APX 25' -> 'APX25'. Codes are the recipe file name and the XML <code>."""
    return "".join(c for c in name.upper() if c.isalnum())[:12]


def grain_of(iso, kind):
    """Grain 0..100 in this module's units, anchored on their native films:
    colour 0.009 at ISO 100 (Ektar), +0.0045 per stop; B&W 0.020 at ISO 100,
    +0.006 per stop (HP5 and Tri-X either side of it). Their value is a
    display amplitude; 800/900 converts to our GRAIN scale."""
    stops = math.log2(max(iso, 25) / 100.0)
    if kind == "bw":
        return max(4, min(60, round((0.020 + 0.006 * stops) * 900)))
    return max(3, min(45, round((0.009 + 0.0045 * stops) * 800)))


def existing(target):
    """Codes already on the card, so an import never shadows a tuned stock."""
    d = os.path.join(target, "ML", "FILMS")
    names = {}
    if os.path.isdir(d):
        for fn in sorted(os.listdir(d)):
            if not fn.lower().endswith(".xml"):
                continue
            try:
                txt = open(os.path.join(d, fn), errors="replace").read()
            except OSError:
                continue
            def tag(t):
                a = txt.find("<%s>" % t)
                b = txt.find("</%s>" % t)
                return txt[a + len(t) + 2:b].strip() if a >= 0 and b > a else ""
            if tag("name"):
                names[tag("name").lower()] = tag("code")
    return names, d


def fetch_cube(key):
    path = lut_path_of(key)
    if not path:
        return None
    os.makedirs(CACHE, exist_ok=True)
    dst = os.path.join(CACHE, key + ".cube")
    if os.path.exists(dst) and os.path.getsize(dst) > 1024:
        return dst
    url = LUT_RAW % path
    try:
        with urllib.request.urlopen(url, timeout=30) as r:
            data = r.read()
    except Exception as e:
        print("  fetch failed: %s (%s)" % (url, e))
        return None
    open(dst, "wb").write(data)
    return dst


def bake(cube_path, bin_path, n):
    """cube2bin's resampler, called in process so the import is one command."""
    size, dmin, dmax, vals = cube2bin.parse_cube(cube_path)
    import struct
    body = bytearray()
    for b in range(n):
        for g in range(n):
            for r in range(n):
                v = cube2bin.sample(size, dmin, dmax, vals,
                                    r / (n - 1.0), g / (n - 1.0), b / (n - 1.0))
                for c in range(3):
                    x = int(round(min(1.0, max(0.0, v[c])) * 65535.0))
                    body += struct.pack("<H", x)
    with open(bin_path, "wb") as f:
        f.write(b"FLUT" + struct.pack("<HH", n, 0) + body)


def write_recipe(path, name, kind, iso, cube=None):
    """The recipe: the fitted tone and cast, plus grain and halation.

    The curves come from lut2fs, which projects the cube onto the engine's
    separable model -- without them the shutter path, which cannot read a
    cube, graded these stocks to nothing. See lut2fs for the measurement of
    what that projection does and does not carry."""
    g = grain_of(iso, kind)
    gs = 8 + (g // 4)
    hal = HALATION.get(kind, 40)
    thr = 226 if kind in ("cine", "neg") else 232
    rad = 16 if kind in ("cine", "neg") else 13
    if cube:
        f = lut2fs.fit(cube)
        lut2fs.write_fs(path, name, kind, iso, f, g, gs, hal, thr, rad)
        return g
    text = """VERSION   3
NAME      %s
DESC      %s
WB        AS_SHOT
SAT       100
HALATION  %d %d %d
HAL_TINT  255,86,40
GRAIN     %d
GRAIN_SIZE %d
""" % (name.upper(), "IMPORTED FROM M-FILM; COLOUR LUT BY FILM-LUTS (MIT)", hal, thr, rad, g, gs)
    open(path, "w").write(text)
    return g


def write_xml(path, name, code, brand, kind, iso, lut):
    txt = """<?xml version="1.0" encoding="UTF-8"?>
<film>
    <name>%s</name>
    <code>%s</code>
    <brand>%s</brand>
    <type>%s</type>
    <base>0x%02x</base>
    <iso>%d</iso>
    <raw_iso>0x%02x</raw_iso>
    <contrast>0</contrast>
    <sharpness>1</sharpness>
    <saturation>0</saturation>
    <tone>0</tone>
    <kelvin>5500</kelvin>
    <wbs_ba>0</wbs_ba>
    <wbs_gm>0</wbs_gm>
    <mono>%d</mono>
    <lut>%s</lut>
    <description>%s</description>
</film>
""" % (name, code, brand, KIND_NAME.get(kind, "Color Negative"),
       0x84, iso, 0x4D, 1 if kind == "bw" else 0, lut,
       "%s %s, ISO %d. Imported from M-Film's catalogue; colour from the "
       "Film-Luts collection (MIT)." % (brand, name, iso))
    open(path, "w").write(txt)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--add", nargs="*", default=[])
    ap.add_argument("--all-missing", action="store_true")
    ap.add_argument("--refit", action="store_true",
                    help="rewrite the .fs of stocks already on the target, from "
                         "their cubes, without touching the XML or the .BIN")
    ap.add_argument("--out")
    ap.add_argument("--verify", metavar="PHOTO")
    args = ap.parse_args()

    cat = catalogue()
    have, filmdir = existing(args.out or "/nonexistent")
    have_names = set(have)

    if args.list:
        for (k, brand, name, iso, kind, native) in cat:
            mark = "have" if name.lower() in have_names else "    "
            print("%s  %-14s %-24s %5s  %-6s %s" %
                  (mark, k, brand + " " + name, iso, kind, "model" if native else "lut"))
        print("\n%d stocks in the catalogue, %d already on the target" % (len(cat), len(have_names)))
        return 0

    if args.refit:
        return refit(args.out, cat, filmdir)

    want = []
    if args.all_missing:
        want = [k for (k, _b, n, _i, _kind, _nat) in cat
                if not already_have(k, n, have_names)]
    want += args.add
    if not want:
        print("nothing to do; --list to see the catalogue")
        return 2

    if not args.out:
        print("--out is required to write (the SD card root, e.g. /Volumes/EOS_DIGITAL)")
        return 2
    out = args.out
    num = 0
    for fn in (os.listdir(filmdir) if os.path.isdir(filmdir) else []):
        if fn.lower().endswith(".xml") and fn[:2].isdigit():
            num = max(num, int(fn[:2]))
    lutdir = os.path.join(out, "ML", "FILMSIM", "LUT")
    recipedir = os.path.join(out, "ML", "FILM")
    os.makedirs(lutdir, exist_ok=True)
    os.makedirs(recipedir, exist_ok=True)

    used = set(have.values())
    made = []
    for key in want:
        row = [c for c in cat if c[0] == key]
        if not row:
            print("%-14s not in the catalogue" % key)
            continue
        (_k, brand, name, iso, kind, _native) = row[0]
        if already_have(key, name, have_names):
            print("%-24s already on the target, skipped" % name)
            continue
        code = code_for(name)
        base, n = code, 2
        while code in used:
            code = "%s%d" % (base, n)
            n += 1
        used.add(code)
        num += 1

        cube = fetch_cube(key)
        if not cube:
            print("%-24s no LUT available, skipped" % name)
            continue
        binpath = os.path.join(lutdir, code + ".BIN")
        bake(cube, binpath, 17)
        lut_rel = "ML/FILMSIM/LUT/%s.BIN" % code
        g = write_recipe(os.path.join(recipedir, code + ".fs"), name, kind, iso, cube)
        write_xml(os.path.join(filmdir, "%02d_%s.xml" % (num, code.lower())),
                  name, code, brand, kind, iso, lut_rel)
        made.append((code, name, os.path.join(recipedir, code + ".fs"), binpath))
        print("%-24s -> %s  grain %d  %s" % (name, code, g, lut_rel))

    print("\n%d stocks imported into %s" % (len(made), out))
    if args.verify:
        verify(made, args.verify)
    return 0


def refit(out, cat, filmdir):
    """Rewrite every imported recipe with the fitted curves.

    The XML and the .BIN are untouched: what changes is the only part the
    shutter path can read. Stocks whose XML has no <lut> under
    ML/FILMSIM/LUT/ are ours and are left alone.
    """
    byname = {n.lower(): k for (k, _b, n, _i, _kind, _nat) in cat}
    n_ok = n_skip = 0
    for fn in sorted(os.listdir(filmdir)):
        if not fn.lower().endswith(".xml"):
            continue
        txt = open(os.path.join(filmdir, fn), errors="replace").read()

        def tag(t):
            a, b = txt.find("<%s>" % t), txt.find("</%s>" % t)
            return txt[a + len(t) + 2:b].strip() if a >= 0 and b > 0 else ""

        lut = tag("lut")
        if "ML/FILMSIM/LUT/" not in lut:
            continue
        code, name = tag("code"), tag("name")
        iso = int(tag("iso") or 0)
        kind = {"Color Negative": "neg", "Slide": "slide",
                "Cross Processed": "xpro", "Black & White": "bw",
                "Cinema": "cine", "Print Film": "print"}.get(tag("type"), "neg")
        key = byname.get(name.lower())
        cube = fetch_cube(key) if key else None
        if not cube:
            print("%-24s no cube cached, recipe left as it is" % name)
            n_skip += 1
            continue
        recipe = os.path.join(out, "ML", "FILM", code + ".fs")
        write_recipe(recipe, name, kind, iso, cube)
        print("%-24s -> %s.fs  (fitted from %s)" % (name, code, os.path.basename(cube)))
        n_ok += 1
    print("\n%d recipes refitted, %d left alone" % (n_ok, n_skip))
    return 0


def verify(made, photo):
    """Develop the photo once per imported stock, with the camera's own code,
    and report what came out. A recipe that bakes to an identity look or an
    all-black frame is a failed import, and this is what catches it."""
    exe = os.path.join(HERE, "..", "se", "hosttest", "se_cam_test")
    if not os.path.exists(exe):
        print("build se/hosttest first: make -C %s" % os.path.dirname(exe))
        return
    print("\n%-8s %-10s %s" % ("code", "mean", "chroma sd"))
    for (code, name, recipe, lut) in made:
        out = "/tmp/mfilm_%s.jpg" % code
        r = subprocess.run([exe, photo, recipe, out, "5", "-", "100", "100", lut],
                           capture_output=True, text=True)
        if r.returncode != 0:
            print("%-8s FAILED  %s" % (code, r.stdout.strip().splitlines()[-1:]))
            continue
        try:
            from PIL import Image, ImageStat
            im = Image.open(out).convert("YCbCr")
            y, cb, cr = im.split()
            print("%-8s %-10.1f %.2f" % (code, ImageStat.Stat(y).mean[0],
                                         (ImageStat.Stat(cb).stddev[0] ** 2 +
                                          ImageStat.Stat(cr).stddev[0] ** 2) ** 0.5))
        except Exception as e:
            print("%-8s rendered, no PIL (%s)" % (code, e))


if __name__ == "__main__":
    sys.exit(main())
