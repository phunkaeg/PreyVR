"""Check H-021 instruction/vtable receipts against the supported disk image.

No process access, injection, or execution of game code. This verifies binary
landmarks, not runtime reachability or successful hand/weapon tracking.
"""
import hashlib
import json
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from verify_h005_skinning import DEFAULT_MODULE


def verify(path):
    spec = json.loads(Path(__file__).with_name("h021_static_landmarks.json").read_text())
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != spec["image_sha256"]:
        raise ValueError(f"Unsupported module SHA-256: {digest}")
    u16 = lambda offset: struct.unpack_from("<H", data, offset)[0]
    u32 = lambda offset: struct.unpack_from("<I", data, offset)[0]
    pe = u32(0x3c)
    opt = pe + 24
    if (data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0"
            or u16(pe + 4) != 0x8664 or u16(opt) != 0x20b):
        raise ValueError("Expected x64 PE32+ image")
    base = struct.unpack_from("<Q", data, opt + 24)[0]
    sections = []
    for index in range(u16(pe + 6)):
        offset = opt + u16(pe + 20) + index * 40
        sections.append((u32(offset + 12), u32(offset + 16), u32(offset + 20)))

    def at(rva, size):
        for start, length, raw in sections:
            if start <= rva and rva + size <= start + length:
                offset = raw + rva - start
                return data[offset:offset + size]
        raise ValueError(f"Unmapped file-backed RVA: {rva:#x}")

    checks = []
    for name, (rva, hexbytes) in spec["anchors"].items():
        expected = bytes.fromhex(hexbytes)
        if at(rva, len(expected)) != expected:
            raise ValueError(f"Instruction mismatch: {name} at {rva:#x}")
        checks.append(name)
    for name, (slot, target) in spec["vtables"].items():
        if struct.unpack("<Q", at(slot, 8))[0] != base + target:
            raise ValueError(f"Vtable mismatch: {name} at {slot:#x}")
        checks.append(name)
    return {"module": str(path), "sha256": digest,
            "static_checks_passed": len(checks), "checks": checks,
            "runtime_tested": False}


if __name__ == "__main__":
    try:
        print(json.dumps(verify(Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_MODULE), indent=2))
    except (OSError, ValueError, struct.error) as error:
        print(json.dumps({"error": str(error), "runtime_tested": False}), file=sys.stderr)
        sys.exit(1)
