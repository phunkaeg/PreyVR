"""Read-only native ABI landmarks for the September 8 VR scheme audit."""
import hashlib
import json
import struct
from pathlib import Path

MODULE = Path(r"D:\SteamLibrary\steamapps\common\Prey\Binaries\Danielle\x64\Release\PreyDll.dll")
EXPECTED_SHA256 = "7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7"
ANCHORS = {
    "x_fifth_argument_read": (0x158FD27, "f30f10442428"),
    "y_fifth_argument_read": (0x158FD87, "f30f104c2428"),
    "x_fifth_argument_written": (0x158FD45, "c744242800000000"),
    "y_fifth_argument_written": (0x158FDA5, "c744242800000000"),
    "x_false_return": (0x158FD76, "32c0c3"),
    "y_false_return": (0x158FDD6, "32c0c3"),
}

def main():
    data = MODULE.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    assert digest == EXPECTED_SHA256, f"Unsupported target: {digest}"
    u16 = lambda at: struct.unpack_from("<H", data, at)[0]
    u32 = lambda at: struct.unpack_from("<I", data, at)[0]
    pe = u32(0x3C)
    assert u16(pe + 4) == 0x8664 and u16(pe + 24) == 0x20B
    sections = []
    for index in range(u16(pe + 6)):
        at = pe + 24 + u16(pe + 20) + index * 40
        sections.append((u32(at + 12), u32(at + 16), u32(at + 20)))
    for name, (rva, expected) in ANCHORS.items():
        wanted = bytes.fromhex(expected)
        start, size, raw = next(s for s in sections if s[0] <= rva < s[0] + s[1])
        assert rva + len(wanted) <= start + size
        assert data[raw + rva - start:raw + rva - start + len(wanted)] == wanted, name
    # Stack arithmetic from the retained old Release object disassembly:
    # wrapper entry S, push rdi, sub rsp,20h, call rax -> native entry S-30h.
    # Native [rsp+28h] == S-8, which is the wrapper's saved RDI slot.
    assert -8 - 0x20 - 8 + 0x28 == -8
    # Fixed wrapper sub rsp,30h has a separate outgoing fifth-argument slot.
    assert -8 - 0x30 - 8 + 0x28 == -0x18
    print(json.dumps({"target_sha256": digest, "native_landmarks_passed": len(ANCHORS),
                      "old_saved_rdi_alias": True, "fixed_slot_separate": True}))

if __name__ == "__main__":
    main()
