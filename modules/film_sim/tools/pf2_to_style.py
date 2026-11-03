#!/usr/bin/env python3
"""Convert Canon .pf2 picture-style files into camera slot blobs.

EOS Utility does not send .pf2 to the camera: it converts first, inside
EdsCFParse.bundle (CProfileMaker / MLib, tagPSE_LUT_3D + tagPSE_GAMMA_LUT),
and sends the resulting "device data".  This script calls that same library
so the conversion happens here, on the PC, with no camera and no EU.

    ./pf2_to_style.py <outdir> <file.pf2> [...]

Writes <outdir>/STYLE<nn>.BIN (16,704 bytes) and prints a validation table.

Device data layout (measured, both colour and B&W files):

    +0x0000  u32         record count (1)
    +0x0028  record:     u16 0x4148 | u8 base | u8 pad | name[24]
                         +0x1C  i32[6] settings
                         +0x34  0xDEADBEEF
                         +0x38  0xDEADBEEF
                         +0x3C  u32 (2)      <- not in the camera blob
                         +0x40  u32 (2)      <- not in the camera blob
                         +0x44  48 00 fc 00  == (header_len - 8) in byte 0
                         +0x48  ff 2f ff ff
                         +0x4C  00 00 00 00
                         +0x50  payload[16632]

Camera slot blob (16,704 bytes, header 0x48) is the same record with the
two u32 at +0x3C/+0x40 removed and the length field changed to 0x4140.
"""

import ctypes
import os
import struct
import sys

EDSCFPARSE = (
    "/Applications/Canon Utilities/EOS Utility/EU3/EOS Utility 3.app"
    "/Contents/PlugIns/EdsCFParse.bundle/Contents/MacOS/EdsCFParse"
)

MAGIC_PF2 = 0x2E504632          # ".PF2" byte-reversed, what CEdsParse::Initialize expects
PROP_DEVICE_DATA = 0x01000203   # converted device data
PROP_PSE_DATA = 0x01000210      # the .pf2 bytes

BLOB_SIZE = 16704
REC_LEN = 0x4148
DEV_HDR = 0x50
CAM_HDR = 0x48
PAYLOAD = 16632


class Parser:
    def __init__(self):
        self.lib = ctypes.CDLL(EDSCFPARSE)
        for fn in ("EdsCfpInitialize", "EdsCfpCreateRefFromUserBuffer",
                   "EdsCfpGetPropertySize", "EdsCfpGetPropertyData",
                   "EdsCfpRelease"):
            getattr(self.lib, fn).restype = ctypes.c_int
        if self.lib.EdsCfpInitialize() != 0:
            raise RuntimeError("EdsCfpInitialize failed")

    def to_device_data(self, pf2_bytes):
        buf = ctypes.create_string_buffer(pf2_bytes, len(pf2_bytes))
        ref = ctypes.c_void_p()
        rc = self.lib.EdsCfpCreateRefFromUserBuffer(
            ctypes.cast(buf, ctypes.c_void_p), ctypes.c_uint64(len(pf2_bytes)),
            ctypes.c_uint32(MAGIC_PF2), ctypes.c_void_p(0), ctypes.byref(ref))
        if rc != 0 or not ref.value:
            raise RuntimeError(f"parser rejected the file (rc={rc})")
        try:
            dt = ctypes.c_uint32()
            size = ctypes.c_uint32()
            rc = self.lib.EdsCfpGetPropertySize(
                ctypes.c_void_p(ref.value), ctypes.c_uint32(PROP_DEVICE_DATA),
                ctypes.c_uint32(0), ctypes.byref(dt), ctypes.byref(size))
            if rc != 0 or size.value == 0:
                raise RuntimeError(f"no device data (rc={rc})")
            out = ctypes.create_string_buffer(size.value)
            rc = self.lib.EdsCfpGetPropertyData(
                ctypes.c_void_p(ref.value), ctypes.c_uint32(PROP_DEVICE_DATA),
                ctypes.c_uint32(0), ctypes.c_uint32(size.value),
                ctypes.cast(out, ctypes.c_void_p))
            # rc is a status hint, not a verdict: real files return 0x50 (80)
            # and still deliver complete data, so validate the bytes instead.
            data = out.raw[:size.value]
            if len(data) < 0x100:
                raise RuntimeError(f"short device data (rc={rc}, {len(data)} B)")
            return data
        finally:
            self.lib.EdsCfpRelease(ctypes.c_void_p(ref.value))


def check_pf2(data):
    if len(data) < 16:
        return "too short"
    if data[0:4] not in (b"\x00\x00\x20\x01", b"\x00\x00\x20\x00"):
        return f"bad magic {data[0:4].hex()}"
    if data[4:8] != b"PSP\x00":
        return f"missing PSP marker {data[4:8]!r}"
    declared = int.from_bytes(data[8:12], "big")
    if declared not in (len(data) - 12, len(data) - 8):
        return f"length field {declared} != {len(data) - 12}"
    return None


def device_to_blob(dev):
    """Device data -> 16,704-byte camera slot blob, with validation."""
    if len(dev) < REC_LEN + 4:
        raise ValueError(f"device data too small ({len(dev)} B)")
    rec_off = len(dev) - REC_LEN          # the prefix length varies per file
    r = dev[rec_off:rec_off + REC_LEN]
    if struct.unpack("<H", r[0:2])[0] != REC_LEN:
        raise ValueError(f"record length {struct.unpack('<H', r[0:2])[0]:#x} != {REC_LEN:#x}")
    base = r[2]
    if not (0x81 <= base <= 0x86):
        raise ValueError(f"base style {base:#04x} outside the built-in range")
    name = r[4:0x1C].split(b"\x00")[0]
    if not name or any(b < 0x20 for b in name):
        raise ValueError(f"implausible name {name!r}")
    if r[0x34:0x38] != b"\xef\xbe\xad\xde" or r[0x38:0x3C] != b"\xef\xbe\xad\xde":
        raise ValueError("DEADBEEF sentinels not where expected")
    marker = r.find(bytes.fromhex("73d14af6"), 0x40)
    if marker < 0:
        raise ValueError("payload marker not found")
    if marker != DEV_HDR:
        raise ValueError(f"payload starts at {marker:#x}, expected {DEV_HDR:#x}")
    if r[0x44] != DEV_HDR - 8:            # "48 00 fc 00": low byte is header_len-8
        raise ValueError(f"header marker {r[0x44:0x48].hex()} low byte != {DEV_HDR - 8:#x}")
    if len(r) - marker != PAYLOAD:
        raise ValueError(f"payload is {len(r) - marker} B, expected {PAYLOAD}")

    # drop the two u32 the camera blob does not carry
    head = r[0:CAM_HDR - 0x0C] + r[0x44:DEV_HDR]
    head = bytearray(head)
    head[0x3C] = CAM_HDR - 8                     # 0x40, mirrors the length field
    blob = struct.pack("<H", BLOB_SIZE) + head[2:] + r[DEV_HDR:]
    if len(blob) != BLOB_SIZE:
        raise ValueError(f"built {len(blob)} bytes, want {BLOB_SIZE}")
    return bytes(blob), base, name


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    outdir = sys.argv[1]
    os.makedirs(outdir, exist_ok=True)
    parser = Parser()

    rows = []
    for path in sys.argv[2:]:
        fn = os.path.basename(path)
        data = open(path, "rb").read()
        err = check_pf2(data)
        if err:
            rows.append((fn, "-", "-", f"REJECT {err}"))
            continue
        try:
            dev = parser.to_device_data(data)
            blob, base, name = device_to_blob(dev)
        except Exception as exc:                      # noqa: BLE001 - report, keep going
            rows.append((fn, "-", "-", f"FAIL {exc}"))
            continue
        rows.append((fn, name.decode("latin1"), f"{base:#04x}", blob))

    ok = [r for r in rows if isinstance(r[3], bytes)]
    def mono_first(row):
        fn, name = row[0].lower(), row[1].lower()
        is_bw = ("bw" in fn or "bw" in name or "mono" in fn or "mono" in name
                 or "黑白" in row[0] or "黑白" in row[1])
        return (0 if is_bw else 1, fn)

    ok.sort(key=mono_first)
    for i, (fn, name, base, blob) in enumerate(ok, 1):
        with open(os.path.join(outdir, f"STYLE{i:02d}.BIN"), "wb") as f:
            f.write(blob)

    print(f"{'file':34s} {'style name':16s} {'base':5s} result")
    for fn, name, base, res in rows:
        shown = res if isinstance(res, str) else "ok"
        print(f"{fn[:34]:34s} {name[:16]:16s} {base:5s} {shown}")
    print(f"\n{len(ok)}/{len(rows)} written to {outdir}")
    return 0 if len(ok) == len(rows) else 1


if __name__ == "__main__":
    sys.exit(main())
