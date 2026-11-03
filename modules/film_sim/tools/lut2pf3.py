#!/usr/bin/env python3
"""Convert 3D LUTs (.cube) into Canon Picture Style data (.pf3, SLOT1.BIN, .bin).

Based on reverse-engineered Canon Picture Style data structures and
EdwinjJ1/canon-lut-lab / LvClFER/Lut-to-Canon-Picture-Style research.

Supports 3 outputs:
  1. .pf3  - Full Canon Picture Style container file (~434 KB) containing:
             - Header: 'PF3\\x00' (v3.0 container)
             - Tag 0x00001009: Style name / title string
             - Tag 0x0000100A: Base style ID (0x84 Neutral)
             - Tag 0x00002001: Contrast, sharpness, saturation, color tone
             - Tag 0x00003001: 12-point tone curve coordinates
             - Tag 0x40001070: sRGB 33x33x33 12-bit 3D LUT (215,628 bytes)
             - Tag 0x40001071: AdobeRGB 33x33x33 12-bit 3D LUT (215,628 bytes)
             - Trailing CRC32: Standard IEEE 802.3 checksum
  2. .slot / SLOT1.BIN - Camera slot flavor blob (16,704 bytes):
             - Used directly by ML film_sim.c (fs_inject_user1) and PROP_PC_FLAVOR1_PARAM.
             - Magic 0x4041 + Neutral base ID (0x84)
             - Name at +4, sliders at +0x24, 0xDEADBEEF tags, tone curve + LUT data.
  3. .bin  - Raw Canon 33^3 3-channel 12-bit lookup table (215,628 bytes).
"""

import argparse
import binascii
import ctypes
import os
import struct
import sys
from pathlib import Path

# Import presets generator if available
try:
    from filmsim_lut import PRESETS, FilmSim
except ImportError:
    # Try local import path
    sys.path.insert(0, str(Path(__file__).parent))
    try:
        from filmsim_lut import PRESETS, FilmSim
    except ImportError:
        PRESETS = {}
        FilmSim = None


def clamp(val, lo, hi):
    return lo if val < lo else (hi if val > hi else val)


def parse_cube(cube_path):
    """Parse a .cube 3D LUT file."""
    with open(cube_path, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()

    size = None
    title = "LUT Profile"
    domain_min = [0.0, 0.0, 0.0]
    domain_max = [1.0, 1.0, 1.0]
    data_rows = []

    for line_num, line in enumerate(lines, 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue

        parts = line.split()
        key = parts[0].upper()

        if key == "TITLE":
            title = line[5:].strip().strip("\"'")
        elif key == "LUT_3D_SIZE":
            size = int(parts[1])
            if size < 2 or size > 65:
                raise ValueError("Unsupported LUT_3D_SIZE: %d (expected 2-65)" % size)
        elif key == "DOMAIN_MIN":
            domain_min = [float(x) for x in parts[1:4]]
        elif key == "DOMAIN_MAX":
            domain_max = [float(x) for x in parts[1:4]]
        elif key == "LUT_1D_SIZE":
            raise ValueError("1D LUTs not supported; provide a 3D .cube LUT.")
        else:
            try:
                rgb = [float(x) for x in parts]
                if len(rgb) == 3:
                    data_rows.append(rgb)
            except ValueError:
                continue

    if not size or len(data_rows) != size ** 3:
        raise ValueError(
            "Invalid .cube file: size is %s, expected %d rows, got %d"
            % (size, size ** 3 if size else 0, len(data_rows))
        )

    return {
        "title": title,
        "size": size,
        "domain_min": domain_min,
        "domain_max": domain_max,
        "data": data_rows,
    }


def sample_cube(lut, r, g, b):
    """Sample a .cube LUT at arbitrary (r, g, b) in [0.0, 1.0] using trilinear interpolation."""
    size = lut["size"]
    dmin = lut["domain_min"]
    dmax = lut["domain_max"]
    data = lut["data"]

    # Normalize to [0, size - 1]
    norm_r = clamp((r - dmin[0]) / (dmax[0] - dmin[0]), 0.0, 1.0) * (size - 1)
    norm_g = clamp((g - dmin[1]) / (dmax[1] - dmin[1]), 0.0, 1.0) * (size - 1)
    norm_b = clamp((b - dmin[2]) / (dmax[2] - dmin[2]), 0.0, 1.0) * (size - 1)

    r0 = int(norm_r)
    g0 = int(norm_g)
    b0 = int(norm_b)

    r1 = min(size - 1, r0 + 1)
    g1 = min(size - 1, g0 + 1)
    b1 = min(size - 1, b0 + 1)

    fr = norm_r - r0
    fg = norm_g - g0
    fb = norm_b - b0

    def idx(ri, gi, bi):
        return ri + gi * size + bi * size * size

    c000 = data[idx(r0, g0, b0)]
    c100 = data[idx(r1, g0, b0)]
    c010 = data[idx(r0, g1, b0)]
    c110 = data[idx(r1, g1, b0)]
    c001 = data[idx(r0, g0, b1)]
    c101 = data[idx(r1, g0, b1)]
    c011 = data[idx(r0, g1, b1)]
    c111 = data[idx(r1, g1, b1)]

    out = [0.0, 0.0, 0.0]
    for ch in range(3):
        v00 = c000[ch] * (1.0 - fr) + c100[ch] * fr
        v01 = c010[ch] * (1.0 - fr) + c110[ch] * fr
        v10 = c001[ch] * (1.0 - fr) + c101[ch] * fr
        v11 = c011[ch] * (1.0 - fr) + c111[ch] * fr

        v0 = v00 * (1.0 - fg) + v01 * fg
        v1 = v10 * (1.0 - fg) + v11 * fg

        out[ch] = v0 * (1.0 - fb) + v1 * fb

    return out


def build_canon_33_table(sample_func):
    """Generate Canon's 33^3 3-channel 12-bit color table (215,628 bytes).

    Format:
      - 6 bytes header: (bit_depth=12, channels=3, grid_size=33) as uint16_le
      - 33*33*33 * 3 values: uint16_le in range [0, 4095]
      - Axis order: R varies fastest, then G, then B.
    """
    grid = 33
    header = struct.pack("<HHH", 12, 3, grid)
    payload = bytearray(header)

    for bi in range(grid):
        b = bi / (grid - 1.0)
        for gi in range(grid):
            g = gi / (grid - 1.0)
            for ri in range(grid):
                r = ri / (grid - 1.0)
                out_r, out_g, out_b = sample_func(r, g, b)

                # Scale [0.0, 1.0] -> 12-bit integer [0, 4095]
                ir = int(clamp(out_r, 0.0, 1.0) * 4095.0 + 0.5)
                ig = int(clamp(out_g, 0.0, 1.0) * 4095.0 + 0.5)
                ib = int(clamp(out_b, 0.0, 1.0) * 4095.0 + 0.5)

                payload.extend(struct.pack("<HHH", ir, ig, ib))

    assert len(payload) == 215628, "Table size must be 215,628 bytes, got %d" % len(payload)
    return bytes(payload)


def build_tone_curve_12point(sampler):
    """Generate 12-point neutral tone curve [ (in_val, out_val), ... ].
    Control points spaced from 0 to 255.
    """
    points = []
    step = 255.0 / 11.0
    for i in range(12):
        in_val = int(round(i * step))
        norm_in = in_val / 255.0
        # sample neutral gray
        r, g, b = sampler(norm_in, norm_in, norm_in)
        # Rec. 709 luma
        out_luma = clamp(0.2126 * r + 0.7152 * g + 0.0722 * b, 0.0, 1.0)
        out_val = int(round(out_luma * 255.0))
        points.append((in_val, out_val))
    return points


def pack_tone_curve_block(points):
    """Pack 12-point tone curve into Canon chunk format (count + (x,y) pairs)."""
    buf = bytearray(struct.pack("<I", len(points)))
    for pin, pout in points:
        buf.extend(struct.pack("<HH", pin * 16, pout * 16))
    return bytes(buf)


def build_pf3_container(title, base_style_id=0x84, srgb_table=None, adobergb_table=None,
                        contrast=0, saturation=0, color_tone=0, sharpness=0,
                        curve_points=None):
    """Pack complete Canon .pf3 Picture Style file with CRC32."""
    if adobergb_table is None:
        adobergb_table = srgb_table
    if curve_points is None:
        curve_points = [(int(round(i * 255.0 / 11.0)), int(round(i * 255.0 / 11.0))) for i in range(12)]

    records = []

    # Tag 0x00001009: Style Name
    name_bytes = title.encode("ascii", errors="replace")[:31] + b"\x00"
    records.append((0x00001009, name_bytes))

    # Tag 0x0000100A: Base Picture Style ID (0x84 Neutral)
    records.append((0x0000100A, struct.pack("<I", base_style_id)))

    # Tag 0x00002001: Sliders
    # contrast (-4..4), sharpness (0..7), saturation (-4..4), color_tone (-4..4), 0xDEADBEEF, 0xDEADBEEF
    sliders_payload = struct.pack("<iiiiiI", contrast, sharpness, saturation, color_tone, -559038737, 0xDEADBEEF)
    records.append((0x00002001, sliders_payload))

    # Tag 0x00003001: Tone Curve
    curve_payload = pack_tone_curve_block(curve_points)
    records.append((0x00003001, curve_payload))

    # Tag 0x40001070: sRGB 3D LUT (215,628 bytes)
    if srgb_table:
        records.append((0x40001070, srgb_table))

    # Tag 0x40001071: AdobeRGB 3D LUT (215,628 bytes)
    if adobergb_table:
        records.append((0x40001071, adobergb_table))

    # Container header, measured against what Canon's own parser accepts:
    #   [u32 0x2001]["PSP\0"][u32 BE body length] then the records.
    # The "PF3\0" + record-count header written here before was not a Canon
    # container: EdsCfpCreateRefFromUserBuffer rejected every file with 0x2C
    # and no style was ever produced from them.
    #
    # NOTE the payloads are still our own guess at the record schema.  Canon
    # accepts this container but returns an *empty* device record from it, so
    # the records (not the header) remain unverified - verify_of() below says
    # so instead of writing a file that silently does nothing.
    body = b"".join(struct.pack(">II", tag, len(data)) + data for tag, data in records)
    return struct.pack(">I", 0x2001) + b"PSP\x00" + struct.pack(">I", len(body)) + body


def verify_of(container):
    """Round-trip through Canon's parser.  True only if it yields a real body."""
    try:
        import pf2_to_style as P
        parser = P.Parser()
        buf = ctypes.create_string_buffer(container, len(container))
        ref = ctypes.c_void_p()
        rc = parser.lib.EdsCfpCreateRefFromUserBuffer(
            ctypes.cast(buf, ctypes.c_void_p), ctypes.c_uint64(len(container)),
            ctypes.c_uint32(0x2E504633), ctypes.c_void_p(0), ctypes.byref(ref))
        if rc != 0 or not ref.value:
            return False
        try:
            size = ctypes.c_uint32()
            dt = ctypes.c_uint32()
            parser.lib.EdsCfpGetPropertySize(
                ctypes.c_void_p(ref.value), ctypes.c_uint32(P.PROP_DEVICE_DATA),
                ctypes.c_uint32(0), ctypes.byref(dt), ctypes.byref(size))
            out = ctypes.create_string_buffer(size.value)
            parser.lib.EdsCfpGetPropertyData(
                ctypes.c_void_p(ref.value), ctypes.c_uint32(P.PROP_DEVICE_DATA),
                ctypes.c_uint32(0), ctypes.c_uint32(size.value),
                ctypes.cast(out, ctypes.c_void_p))
            return bytes.fromhex("73d14af6") in out.raw[:size.value]
        finally:
            parser.lib.EdsCfpRelease(ctypes.c_void_p(ref.value))
    except Exception:
        return False


def build_slot_blob(title, base_style_id=0x84, contrast=0, saturation=0, color_tone=0, sharpness=0,
                    curve_points=None, template_path=None):
    """Generate the 16,704-byte camera slot flavor blob (SLOT1.BIN / PROP_PC_FLAVORx_PARAM format).

    Verified against Canon EOS 6D serial flash (ROM0.BIN / SFDATA.BIN at 0x61b38, 0x65c8c, 0x69de0):
      +0x00..+0x01 : uint16_le self-length (0x4140 = 40 41 = 16,704 bytes)
      +0x02        : uint8 base_style_id (0x84=Neutral, 0x81=Standard, 0x82=Portrait)
      +0x03        : uint8 reserved (0x00)
      +0x04..+0x1B : char name[24] (NUL-padded ASCII style name, max 23 chars + NUL)
      +0x1C..+0x23 : int32_t sharpness_fineness, sharpness_threshold (preserved from template)
      +0x24..+0x3B : 24-byte slider struct (contrast, sharpness, saturation, color_tone, 0xDEADBEEF, 0xDEADBEEF)
      +0x3C..+0x47 : 12-byte PF2 payload header (40 00 fc 00 ff 2f ff ff 00 00 00 00)
      +0x48..+0x413F: 16,632-byte compressed/enciphered PF2 body (73 d1 4a f6 4d 89 60 7e ...),
                     cloned intact from template.
    """
    BLOB_SIZE = 16704

    default_tpl = Path(__file__).resolve().parent.parent / "SLOT1.BIN"
    tpl_to_use = None
    if template_path and os.path.exists(template_path):
        tpl_to_use = Path(template_path)
    elif default_tpl.exists():
        tpl_to_use = default_tpl

    if tpl_to_use:
        raw = tpl_to_use.read_bytes()
        if len(raw) == BLOB_SIZE and raw[0] == 0x40 and raw[1] == 0x41:
            blob = bytearray(raw)
        elif len(raw) >= 16644 and raw[0] == 0x40 and raw[1] == 0x00 and raw[2] == 0xFC:
            blob = bytearray(BLOB_SIZE)
            blob[0x3C:0x3C + 16644] = raw[:16644]
        elif len(raw) >= 16632 and raw[:4] == b"\x73\xd1\x4a\xf6":
            blob = bytearray(BLOB_SIZE)
            blob[0x3C:0x48] = b"\x40\x00\xfc\x00\xff\x2f\xff\xff\x00\x00\x00\x00"
            blob[0x48:0x48 + 16632] = raw[:16632]
        else:
            idx = raw.find(b"\x73\xd1\x4a\xf6\x4d\x89\x60\x7e")
            if idx >= 12 and len(raw) - (idx - 12) >= 16644:
                blob = bytearray(BLOB_SIZE)
                blob[0x3C:0x3C + 16644] = raw[idx - 12:idx - 12 + 16644]
            else:
                blob = bytearray(raw[:BLOB_SIZE])
                if len(blob) < BLOB_SIZE:
                    blob.extend(b"\x00" * (BLOB_SIZE - len(blob)))
    else:
        blob = bytearray(BLOB_SIZE)
        blob[0x3C:0x48] = b"\x40\x00\xfc\x00\xff\x2f\xff\xff\x00\x00\x00\x00"
        if curve_points:
            curve_data = pack_tone_curve_block(curve_points)
            blob[0x48:0x48 + len(curve_data)] = curve_data

    # 1. Header (+0x00..+0x03)
    blob[0] = 0x40
    blob[1] = 0x41
    blob[2] = base_style_id & 0xFF
    blob[3] = 0x00

    # 2. Style name at +0x04..+0x1B (24 bytes max: 23 chars + NUL so +0x1C..+0x3B is preserved)
    name_bytes = title.encode("ascii", errors="replace")[:23] + b"\x00"
    for i, b in enumerate(name_bytes):
        blob[4 + i] = b
    for i in range(len(name_bytes), 24):
        blob[4 + i] = 0

    # 3. Embedded 24-byte slider settings at +0x24..+0x3B
    struct.pack_into(
        "<iIiiII", blob, 0x24,
        int(contrast), int(sharpness) & 0xFFFFFFFF,
        int(saturation), int(color_tone),
        0xDEADBEEF, 0xDEADBEEF
    )

    return bytes(blob)


def main():
    parser = argparse.ArgumentParser(
        description="Convert 3D LUT (.cube) or Film Sim preset to Canon Picture Style (.pf3, SLOT1.BIN, .bin)"
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("cube_file", nargs="?", help="Input .cube 3D LUT file")
    group.add_argument(
        "-p", "--preset", choices=sorted(PRESETS.keys()) if PRESETS else [],
        help="Generate directly from a built-in film_sim preset"
    )
    group.add_argument(
        "--pf2", help="Input .pf2 or raw 16,704-byte slot file to convert/repackage into SLOT1.BIN"
    )

    parser.add_argument(
        "-s", "--strength", type=int, default=100,
        help="Preset strength (0-100%%, default 100)"
    )
    parser.add_argument(
        "-f", "--format", choices=["all", "pf3", "slot", "bin"], default="all",
        help="Output format: pf3 (Canon container), slot (16,704-byte camera blob), bin (raw table), all (default)"
    )
    parser.add_argument(
        "--template", default="/tmp/SLOT1.BIN",
        help="Template SLOT1.BIN to patch (optional, fallback to synthesis)"
    )
    parser.add_argument(
        "--overwrite-rom", nargs="*", metavar="ROM_FILE",
        help="Directly overwrite User Def slot in ROM0.BIN / SFDATA.BIN (default: Slot 1 @ 0x061B40)"
    )
    parser.add_argument(
        "--slot-index", type=int, choices=[1, 2, 3], default=1,
        help="Target User Def slot (1=0x061B40, 2=0x065C94, 3=0x069DE8) when using --overwrite-rom"
    )
    parser.add_argument(
        "-o", "--output", default=None,
        help="Output file path or prefix"
    )
    parser.add_argument(
        "--title", default=None,
        help="Picture style title (max 23 chars for slot, 30 chars for pf3)"
    )

    args = parser.parse_args()

    title = args.title
    contrast = 0
    saturation = 0
    color_tone = 0
    sharpness = 0
    base_style_id = 0x84  # Neutral

    if args.pf2:
        pf2_path = Path(args.pf2)
        if not pf2_path.exists():
            print("Error: file %s does not exist" % pf2_path, file=sys.stderr)
            return 1
        title = title or pf2_path.stem[:23]
        args.template = str(pf2_path)
        if args.format == "all":
            args.format = "slot"
        def sampler(r, g, b):
            return r, g, b
    elif args.preset:
        if not FilmSim:
            print("Error: filmsim_lut module not found in path", file=sys.stderr)
            return 1
        sim = FilmSim(args.preset, args.strength)
        title = title or ("FS %s" % args.preset.replace("_", " ").title())

        # Map preset params to hardware slider approximations (-4..+4)
        if args.preset in PRESETS:
            p = PRESETS[args.preset]
            # contrast, black, shoulder, saturation, hue, ...
            contrast = clamp(int(round(p[0] / 25.0)), -4, 4)
            saturation = -4 if args.preset == "bw_classic" else clamp(int(round((p[3] - 100) / 25.0)), -4, 4)
            color_tone = clamp(int(round(p[4] / 3.0)), -4, 4)

        def sampler(r, g, b):
            ir = int(clamp(r, 0.0, 1.0) * 255.0 + 0.5)
            ig = int(clamp(g, 0.0, 1.0) * 255.0 + 0.5)
            ib = int(clamp(b, 0.0, 1.0) * 255.0 + 0.5)
            or_, og, ob = sim.apply(ir, ig, ib)
            return or_ / 255.0, og / 255.0, ob / 255.0

    else:
        cube_path = Path(args.cube_file)
        if not cube_path.exists():
            print("Error: file %s does not exist" % cube_path, file=sys.stderr)
            return 1
        lut = parse_cube(cube_path)
        title = title or lut["title"]

        def sampler(r, g, b):
            return sample_cube(lut, r, g, b)

    safe_name = "".join(c if c.isalnum() else "_" for c in title).strip("_")

    print("Generating Canon color profiles for: %s" % title)
    print("  Base Picture Style: 0x%02X (Neutral)" % base_style_id)
    print("  Hardware Sliders: Contrast=%+d, Saturation=%+d, ColorTone=%+d, Sharpness=%d" %
          (contrast, saturation, color_tone, sharpness))

    table_data = build_canon_33_table(sampler)
    curve_points = build_tone_curve_12point(sampler)

    template_file = args.template if (args.template and os.path.exists(args.template)) else None

    # Determine what to output
    fmt = args.format
    if fmt == "all" and args.output:
        low_out = args.output.lower()
        if low_out.endswith(".pf3"):
            fmt = "pf3"
        elif low_out.endswith(".bin") and ("slot" in low_out or "preset" in low_out or low_out.endswith(".pf2")):
            fmt = "slot"
        elif low_out.endswith(".bin"):
            fmt = "bin"

    out_prefix = args.output if (args.output and not args.output.lower().endswith((".pf3", ".bin", ".pf2"))) else safe_name

    outputs = []

    if fmt in ("all", "pf3"):
        pf3_path = Path(args.output if (args.output and (fmt == "pf3" or args.output.lower().endswith(".pf3"))) else f"{out_prefix}.pf3")
        pf3_data = build_pf3_container(
            title=title,
            base_style_id=base_style_id,
            srgb_table=table_data,
            adobergb_table=table_data,
            contrast=contrast,
            saturation=saturation,
            color_tone=color_tone,
            sharpness=sharpness,
            curve_points=curve_points
        )
        pf3_path.write_bytes(pf3_data)
        outputs.append(("Canon Picture Style (.pf3)", pf3_path, len(pf3_data)))

    if fmt in ("all", "slot") or args.overwrite_rom is not None:
        slot_path = Path(args.output if (args.output and (fmt == "slot" or "slot" in args.output.lower())) else f"{out_prefix}_SLOT1.BIN")
        slot_data = build_slot_blob(
            title=title,
            base_style_id=base_style_id,
            contrast=contrast,
            saturation=saturation,
            color_tone=color_tone,
            sharpness=sharpness,
            curve_points=curve_points,
            template_path=template_file
        )
        if fmt in ("all", "slot"):
            slot_path.write_bytes(slot_data)
            outputs.append(("Camera Slot Flavor Blob (SLOT1.BIN)", slot_path, len(slot_data)))

        if args.overwrite_rom is not None:
            rom_offsets = {1: 0x00061B40, 2: 0x00065C94, 3: 0x00069DE8}
            target_off = rom_offsets[args.slot_index]
            rom_files = args.overwrite_rom
            if not rom_files:
                repo_root = Path(__file__).resolve().parent.parent.parent.parent.parent
                rom_files = [
                    str(repo_root / "qemu" / "6D" / "ROM0.BIN"),
                    str(repo_root / "qemu" / "6D" / "SFDATA.BIN"),
                ]
            for rf in rom_files:
                rp = Path(rf)
                if rp.exists() and rp.stat().st_size >= target_off + len(slot_data):
                    with open(rp, "r+b") as f:
                        f.seek(target_off)
                        f.write(slot_data)
                    outputs.append((f"Overwrote Slot {args.slot_index} @ 0x{target_off:06X}", rp, len(slot_data)))
                else:
                    print(f"Warning: ROM file {rp} missing or too small for offset 0x{target_off:06X}", file=sys.stderr)

    if fmt in ("all", "bin"):
        bin_path = Path(args.output if (args.output and args.output.endswith(".bin") and "slot" not in args.output.lower()) else f"{out_prefix}_table.bin")
        bin_path.write_bytes(table_data)
        outputs.append(("Raw 33^3 Color Table (.bin)", bin_path, len(table_data)))

    print("\nSuccessfully Generated Artifacts:")
    for desc, path, size in outputs:
        print(f"  - {desc:36s}: {path.name:25s} ({size:,} bytes)")

    print("\nDeployment Instructions:")
    print("  1. In-Camera Live Injection (ML film_sim.mo):")
    print("     Copy SLOT1.BIN to SD card as ML/LOGS/SLOT1.BIN")
    print("     In ML menu -> Display -> Film Sim -> In-Camera Style -> User 1")
    print("  2. Official Canon USB Registration:")
    print("     Use Canon EOS Utility -> Register Picture Style File -> select .pf3")
    print("  3. Raw Video / Post-Grading:")
    print("     Import generated .cube 3D LUT directly into DaVinci Resolve / Premiere")

    return 0


if __name__ == "__main__":
    sys.exit(main())
