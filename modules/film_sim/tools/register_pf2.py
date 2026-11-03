#!/usr/bin/env python3
"""Register .pf2/.pf3 picture styles on a connected Canon body, then archive
the expanded body the camera builds.

Why this exists
---------------
A .pf2 is only 1-4 KB: a compact description (tone curve + colour axes).  The
camera expands it into the 16,632-byte "flavour" body a Picture Style slot
actually renders from, and that body is whitened - it cannot be authored or
edited outside the camera.

A Magic Lantern module cannot do the expansion either: it would have to send
PTP 0x915C (flavour edit mode) and 0x915D (the param), and modules only see
properties, not PTP opcodes.  EOS Utility can, through EDSDK.  So this script
calls exactly the same EDSDK entry points EOS Utility does:

    EdsSetLvPcFlavoreditMode (cam, slot, 1)   -> PTP 0x915C  mode on
    EdsSetLvPcFlavoreditParam(cam, slot, pf2) -> PTP 0x915D  the file verbatim
    EdsSetLvPcFlavoreditMode (cam, slot, 0)   -> PTP 0x915C  mode off

then reads the slot's flavour property back (0x04010001 / 3 / 5) and writes
the 16,704-byte result to STYLE<slot>_<name>.BIN, ready for the module's

    Developer > JPEG: Load Style      (writes that body back into a slot)

Because the body is copied byte for byte, cloning is exact - unlike anything
we ever derived ourselves.

Usage
-----
    python3 register_pf2.py --list
    python3 register_pf2.py --slot 2 --files pf2/Eyer_黑白风格_EyerBW99.pf2
    python3 register_pf2.py --slot 2 --dir pf2 --limit 5

The camera must be connected by USB and not mounted as a disk, and EOS Utility
must be closed (it holds the session).
"""

import argparse
import ctypes
import os
import struct
import sys
import zlib

# ---------------------------------------------------------------- EDSDK glue

FRAMEWORK_CANDIDATES = [
    "/Applications/Canon Utilities/Picture Style Editor/PictureStyleEditor.app"
    "/Contents/Frameworks/EDSDK.framework/EDSDK",
    "/Applications/Canon Utilities/EOS Utility/EOS Utility.app"
    "/Contents/Frameworks/EDSDK.framework/EDSDK",
    "/Library/Frameworks/EDSDK.framework/EDSDK",
]

# The same numbers the module uses (src/property.h).
PROP_PICTURE_STYLE = 0x01000406
FLAVOR_PROP = {1: 0x04010001, 2: 0x04010003, 3: 0x04010005}
STYLE_ID = {1: 0x21, 2: 0x22, 3: 0x23}
BLOB_SIZE = 16704
EDS_ERR_OK = 0


def load_edsdk():
    path = os.environ.get("EDSDK_PATH")
    for c in ([path] if path else FRAMEWORK_CANDIDATES):
        if c and os.path.exists(c):
            return ctypes.CDLL(c)
    sys.exit("EDSDK not found. Set EDSDK_PATH to the framework binary.")


def bind(lib):
    u32 = ctypes.c_uint32
    lib.EdsInitializeSDK.restype = u32
    lib.EdsTerminateSDK.restype = u32
    lib.EdsGetCameraList.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    lib.EdsGetCameraList.restype = u32
    lib.EdsRelease.argtypes = [ctypes.c_void_p]
    lib.EdsGetChildCount.argtypes = [ctypes.c_void_p, ctypes.POINTER(u32)]
    lib.EdsGetChildAtIndex.argtypes = [ctypes.c_void_p, u32, ctypes.POINTER(ctypes.c_void_p)]
    lib.EdsOpenSession.argtypes = [ctypes.c_void_p]
    lib.EdsOpenSession.restype = u32
    lib.EdsCloseSession.argtypes = [ctypes.c_void_p]
    lib.EdsSetPropertyData.argtypes = [ctypes.c_void_p, u32, ctypes.c_int32, u32, ctypes.c_void_p]
    lib.EdsSetPropertyData.restype = u32
    lib.EdsGetPropertyData.argtypes = [ctypes.c_void_p, u32, ctypes.c_int32, u32, ctypes.c_void_p]
    lib.EdsGetPropertyData.restype = u32
    lib.EdsSetLvPcFlavoreditMode.argtypes = [ctypes.c_void_p, u32, u32]
    lib.EdsSetLvPcFlavoreditMode.restype = u32
    lib.EdsSetLvPcFlavoreditParam.argtypes = [ctypes.c_void_p, u32, ctypes.c_void_p, u32]
    lib.EdsSetLvPcFlavoreditParam.restype = u32
    return lib


def first_camera(lib):
    camlist = ctypes.c_void_p()
    err = lib.EdsGetCameraList(ctypes.byref(camlist))
    if err != EDS_ERR_OK:
        sys.exit("EdsGetCameraList failed: 0x%08x" % err)
    n = ctypes.c_uint32(0)
    lib.EdsGetChildCount(camlist, ctypes.byref(n))
    if n.value == 0:
        lib.EdsRelease(camlist)
        sys.exit("No camera found. Connect the 6D by USB and close EOS Utility.")
    cam = ctypes.c_void_p()
    lib.EdsGetChildAtIndex(camlist, 0, ctypes.byref(cam))
    lib.EdsRelease(camlist)
    return cam


# ------------------------------------------------------------- registration


def send_style(lib, cam, slot, path):
    data = open(path, "rb").read()
    print("  %-46s %6d B" % (os.path.basename(path), len(data)))
    buf = ctypes.create_string_buffer(data, len(data))

    err = lib.EdsSetLvPcFlavoreditMode(cam, slot, 1)
    if err != EDS_ERR_OK:
        print("    edit-mode ON refused: 0x%08x" % err)
        return False

    err = lib.EdsSetLvPcFlavoreditParam(
        cam, slot, ctypes.cast(buf, ctypes.c_void_p), len(data))
    if err != EDS_ERR_OK:
        lib.EdsSetLvPcFlavoreditMode(cam, slot, 0)
        print("    param refused: 0x%08x" % err)
        return False

    lib.EdsSetLvPcFlavoreditMode(cam, slot, 0)
    return True


def read_body(lib, cam, slot):
    prop = FLAVOR_PROP[slot]
    buf = ctypes.create_string_buffer(BLOB_SIZE)
    # EDSDK returns 0x50 as a status hint while the data is still valid
    err = lib.EdsGetPropertyData(cam, prop, 0, BLOB_SIZE,
                                 ctypes.cast(buf, ctypes.c_void_p))
    body = buf.raw
    if err not in (EDS_ERR_OK, 0x50):
        print("    read-back 0x%08x for prop 0x%08x" % (err, prop))
        return None
    if body[0x48:0x50] == b"\x00" * 8:
        print("    read-back empty (prop 0x%08x)" % prop)
        return None
    return body


def body_is_camera_built(body):
    """A real body repeats itself 8 KB later (65-97%); a blob that merely
    echoes what we sent has no such structure (~12%)."""
    if len(body) != BLOB_SIZE or body[0x48:0x4C] != bytes.fromhex("73d14af6"):
        return False, 0.0
    p = body[0x48:]
    same = sum(1 for i in range(len(p) - 0x2000) if p[i] == p[i + 0x2000])
    ratio = same / (len(p) - 0x2000)
    return ratio > 0.40, ratio


def describe(body):
    base, = struct.unpack("<H", body[2:4])
    name = body[4:28].split(b"\x00")[0].decode("latin1", "replace")
    st = struct.unpack("<6i", body[0x1C:0x34])
    built, ratio = body_is_camera_built(body)
    return "base=0x%02x name='%s' sliders=%s crc=0x%08x  %s (%.0f%% self-similar)" % (
        base, name, st[:4], zlib.crc32(body) & 0xFFFFFFFF,
        "CAMERA-BUILT" if built else "NOT EXPANDED", 100 * ratio)


def compare_with_parser(pf2_path, camera_body, outdir):
    """Save what Canon's parser makes of the same .pf2 and report the delta.

    This is the known-plaintext pair the body format needs: one input, two
    renderings (parser output and what the camera actually stores).  If the two
    are identical there is no transform and the payload can be authored
    directly; if they differ, the diff says how much of the file the camera
    rewrites.
    """
    try:
        import pf2_to_style as P
        dev = P.Parser().to_device_data(open(pf2_path, "rb").read())
        blob, base, name = P.device_to_blob(dev)
    except Exception as e:
        print("    compare: parser failed (%s)" % e)
        return

    stem = os.path.splitext(os.path.basename(pf2_path))[0][:16]
    local = os.path.join(outdir, "CMP_%s.parser.BIN" % stem)
    with open(local, "wb") as fh:
        fh.write(blob)

    a, b = camera_body[0x48:0x48 + 16632], blob[0x48:0x48 + 16632]
    same = sum(1 for x, y in zip(a, b) if x == y)
    print("    compare: parser says base=0x%02x name='%s'" % (base, name.decode("latin1")))
    print("    compare: camera body vs parser output -> %d/%d identical (%.1f%%)"
          % (same, 16632, 100.0 * same / 16632))
    print("    compare: saved %s" % local)
    if same == 16632:
        print("    compare: IDENTICAL - the camera stores what the parser produces;")
        print("             the payload can be authored directly.")
    else:
        print("    compare: DIFFERENT - the camera rewrites the payload on registration.")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--slot", type=int, choices=[1, 2, 3],
                    help="User Def slot to register into")
    ap.add_argument("--files", nargs="*", default=[])
    ap.add_argument("--dir", help="register every .pf2/.pf3 in this directory")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--shortlist", metavar="FILE",
                    help="text file of .pf2 names, one per line (see pf2_shortlist.txt)")
    ap.add_argument("--compare", action="store_true",
                    help="also save Canon's own conversion of each .pf2 and report how the "
                         "camera's stored body differs from it (the known-plaintext pair "
                         "needed to read the body format)")
    ap.add_argument("--out", default="styles_registered")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()

    lib = bind(load_edsdk())
    err = lib.EdsInitializeSDK()
    if err != EDS_ERR_OK:
        sys.exit("EdsInitializeSDK failed: 0x%08x" % err)

    cam = None
    try:
        cam = first_camera(lib)
        if args.list:
            print("camera found: %s" % (cam,))
            return 0

        files = list(args.files)
        if args.shortlist:
            base = args.dir or os.path.join(os.path.dirname(os.path.abspath(__file__)), "pf2")
            for line in open(args.shortlist):
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                cand = os.path.join(base, line)
                if not os.path.exists(cand):
                    print("  missing: %s" % cand)
                    continue
                files.append(cand)
        if args.dir:
            for f in sorted(os.listdir(args.dir)):
                if f.lower().endswith((".pf2", ".pf3")):
                    files.append(os.path.join(args.dir, f))
        if not files:
            ap.error("give --files or --dir")
        if args.limit:
            files = files[: args.limit]

        err = lib.EdsOpenSession(cam)
        if err != EDS_ERR_OK:
            sys.exit("EdsOpenSession failed: 0x%08x (is EOS Utility running?)" % err)

        style = ctypes.c_uint32(STYLE_ID[args.slot])
        err = lib.EdsSetPropertyData(cam, PROP_PICTURE_STYLE, 0, 4,
                                     ctypes.byref(style))
        print("slot %d selected (0x%02x) ack=0x%08x" % (args.slot, STYLE_ID[args.slot], err))

        os.makedirs(args.out, exist_ok=True)
        ok = 0
        for f in files:
            if not send_style(lib, cam, args.slot, f):
                continue
            body = read_body(lib, cam, args.slot)
            if body is None:
                continue
            stem = os.path.splitext(os.path.basename(f))[0][:16]
            out = os.path.join(args.out, "STYLE%d_%s.BIN" % (args.slot, stem))
            with open(out, "wb") as fh:
                fh.write(body)
            print("    -> %s" % describe(body))
            print("       saved %s" % out)
            if args.compare:
                compare_with_parser(f, body, args.out)
            ok += 1

        print("\n%d of %d registered and archived." % (ok, len(files)))
        if ok:
            print("Copy %s into ML/FILMSIM/STYLES/ on the card, then use\n"
                  "Developer > JPEG: Load Style to put a body into a slot." % args.out)
        return 0 if ok else 1

    finally:
        if cam is not None:
            lib.EdsCloseSession(cam)
        lib.EdsTerminateSDK()


if __name__ == "__main__":
    sys.exit(main())
