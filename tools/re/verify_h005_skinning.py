"""Verify H-005 static landmarks against a PE file; never accesses a process.

Usage: py -3 tools/re/verify_h005_skinning.py [path/to/PreyDll.dll]
This verifies the researched bytes, not runtime execution or hand control.
"""
import hashlib
import json
import struct
import sys
from pathlib import Path

EXPECTED_SHA256 = "7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7"
DEFAULT_MODULE = Path(r"D:\SteamLibrary\steamapps\common\Prey\Binaries\Danielle\x64\Release\PreyDll.dll")

# Ghidra instruction listings and raw bytes, recorded 2026-09-05.
ANCHORS = {
    "compute_entry": (0x82EE10, "48 8b c4 53 48 81 ec 00 01 00 00"),
    "read_pose_absolute_and_joint_count": (0x82EE78, "48 8b 84 24 30 01 00 00 44 0f 29 74 24 40 44 0f 29 7c 24 30 4c 8b 78 18 49 8b 00 ff 50 08"),
    "publish_finished_skinning": (0x82F860, "48 8b 46 48 48 8b 18 48 8b 4e 48 48 8b c3 f0 48 0f b1 11 75 eb"),
    "wait_then_upload": (0xF3CC76, "48 8b 73 f8 48 8b 56 18 48 85 d2 74 0d 48 8b 0d 36 0e 31 01 48 8b 01 ff 50 20 44 8b 06 41 b9 01 00 00 00 48 8b 56 08 48 8b 4b f0 49 c1 e0 05 e8 a6 21 14 00"),
    "r077_effector_byte_offset": (0x877F79, "49 6b f2 1c"),
    "render_object_skinning_assignment": (0x81D377, "48 89 ab 98 00 00 00"),
    "joint_count_accessor": (0x8C1190, "48 8b 41 08 8b 40 fc 0f ba f0 1f c3"),
    "joint_name_accessor": (0x8BC300, "85 d2 78 1f 4c 8b 41 08 41 8b 48 fc 0f ba f1 1f 3b d1 7d 0f 48 63 ca 48 69 c1 a8 00 00 00 4a 8b 04 00 c3"),
    "joint_parent_accessor": (0x8BC330, "85 d2 78 21 4c 8b 41 08 41 8b 40 fc 0f ba f0 1f 3b d0 7d 11 48 63 c2 48 69 c8 a8 00 00 00 42 0f bf 44 01 18 c3"),
}
VTABLES = {
    "renderer.create_skinning": (0x1DD2E08 + 0x800, 0xFE06B0),
    "renderer.create_remapped": (0x1DD2E08 + 0x808, 0xFE05F0),
    "renderer.skinning_pool_id": (0x1DD2E08 + 0x810, 0xFE0AE0),
    "renderer.allocate_character_cb": (0x1DD2E08 + 0xAA0, 0xF39450),
    "skeleton.count": (0x1D2A3E8 + 0x08, 0x8C1190),
    "skeleton.parent": (0x1D2A3E8 + 0x10, 0x8BC330),
    "skeleton.name": (0x1D2A3E8 + 0x30, 0x8BC300),
}


def verify(path):
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_SHA256:
        raise ValueError(f"Unsupported module SHA-256: {digest}")
    u16 = lambda off: struct.unpack_from("<H", data, off)[0]
    u32 = lambda off: struct.unpack_from("<I", data, off)[0]
    pe = u32(0x3C)
    opt = pe + 24
    if data[:2] != b"MZ" or data[pe:pe+4] != b"PE\0\0" or u16(pe+4) != 0x8664 or u16(opt) != 0x20B:
        raise ValueError("Expected an x64 PE32+ image")
    base = struct.unpack_from("<Q", data, opt+24)[0]
    sections = []
    for i in range(u16(pe+6)):
        off = opt + u16(pe+20) + i*40
        sections.append((u32(off+12), u32(off+16), u32(off+20)))

    def at(rva, length):
        for start, size, fileoff in sections:
            if start <= rva and rva+length <= start+size:
                offset = fileoff+rva-start
                return data[offset:offset+length]
        raise ValueError(f"Unmapped file-backed RVA {rva:#x}")

    checks = []
    for name, (rva, hexbytes) in ANCHORS.items():
        expected = bytes.fromhex(hexbytes)
        if at(rva, len(expected)) != expected:
            raise ValueError(f"Anchor mismatch: {name} at {rva:#x}")
        checks.append(name)
    for name, (slot, target) in VTABLES.items():
        if struct.unpack("<Q", at(slot, 8))[0] != base+target:
            raise ValueError(f"Vtable mismatch: {name} at {slot:#x}")
        checks.append(name)
    return {"module": str(path), "sha256": digest, "static_checks_passed": len(checks),
            "checks": checks, "runtime_tested": False,
            "active_ik_effector_candidates": {"0x4ec / 0x1c": 45, "0x7e0 / 0x1c": 72}}


if __name__ == "__main__":
    try:
        print(json.dumps(verify(Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_MODULE), indent=2))
    except (OSError, ValueError, struct.error) as error:
        print(json.dumps({"error": str(error), "runtime_tested": False}), file=sys.stderr)
        sys.exit(1)
