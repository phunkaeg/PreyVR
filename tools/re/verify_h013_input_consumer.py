"""Verify H-013 input-consumer landmarks on disk; never opens a process.

Usage: py -3 tools/re/verify_h013_input_consumer.py [PreyDll.dll]
These checks establish static routing and predicates, not live input acceptance.
"""
import hashlib
import json
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from verify_h005_skinning import DEFAULT_MODULE, EXPECTED_SHA256

# Independently read from Ghidra after following string anchors and constructors.
ANCHORS = {
    "add_listener_list": (0x9d5d2a, "488d5928488b4928488b01483bc1741d"),
    "set_exclusive_listener": (0x9843b0, "48895148c3"),
    "game_installs_exclusive": (0x16fee32, "488b0d9febb4004885c9740a488b01488d5718ff5038"),
    "game_keyboard_and_forward": (0x17016bd, "83f8017713488b0d0fc3b40083caff488b01ff9080000000488b8e3001000032c04885c97409488b01498bd6ff5008"),
    "game_ui_forward": (0x1701710, "488b89300100004885c97407488b0148ff601032c0c3"),
    "launcher_raw_handler_entire": (0x138a930, "4883ec28833a017718837a040175124883c1c033d2e856140000b0014883c428c332c04883c428c3"),
    "attract_sets_override": (0x138b3e7, "488b0552b48801488d4b4048898848010000"),
    "main_menu_clears_override": (0x138bdf0, "48c7804801000000000000"),
    "main_menu_state_three": (0x138be75, "c7838000000003000000"),
    "active_user_listening_flag": (0x1324a90, "885f2084db"),
    "return_true_leaf": (0xacf540, "b001c3"),
    "no_op_leaf": (0x1706520, "c20000"),
}
VTABLES = {
    "input.add_listener": (0x1d5c4f8, 0x9d5d20),
    "input.set_exclusive": (0x1d5c528, 0x9843b0),
    "input.blocking_query": (0x1d5c678, 0x9d7790),
    "game.input_event": (0x1e76ab0, 0x1701540),
    "game.input_event_ui": (0x1e76ab8, 0x1701710),
    "launcher.input_event": (0x1e2c2a0, 0x138a930),
    "launcher.input_event_ui": (0x1e2c2a8, 0x16d2100),
    "active_user.register_no_op": (0x1e24f10, 0x1706520),
    "active_user.clear_no_op": (0x1e24f18, 0x1706520),
    "active_user.logged_in_true": (0x1e24f20, 0xacf540),
    "active_user.ensure_no_op": (0x1e24f28, 0x1706520),
}


def verify(path):
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_SHA256:
        raise ValueError(f"Unsupported module SHA-256: {digest}")
    u16 = lambda offset: struct.unpack_from("<H", data, offset)[0]
    u32 = lambda offset: struct.unpack_from("<I", data, offset)[0]
    pe = u32(0x3C)
    opt = pe + 24
    if (data[:2] != b"MZ" or data[pe:pe+4] != b"PE\0\0"
            or u16(pe+4) != 0x8664 or u16(opt) != 0x20B):
        raise ValueError("Expected x64 PE32+")
    base = struct.unpack_from("<Q", data, opt+24)[0]
    sections = []
    for i in range(u16(pe+6)):
        offset = opt + u16(pe+20) + 40*i
        sections.append((u32(offset+12), u32(offset+16), u32(offset+20)))

    def at(rva, length):
        for start, size, offset in sections:
            if start <= rva and rva+length <= start+size:
                return data[offset+rva-start:offset+rva-start+length]
        raise ValueError(f"Unmapped file-backed RVA {rva:#x}")

    checks = []
    for name, (rva, expected) in ANCHORS.items():
        expected = bytes.fromhex(expected)
        if at(rva, len(expected)) != expected:
            raise ValueError(f"Byte mismatch: {name} at {rva:#x}")
        checks.append(name)
    for name, (slot, target) in VTABLES.items():
        if struct.unpack("<Q", at(slot, 8))[0] != base+target:
            raise ValueError(f"Vtable mismatch: {name} at {slot:#x}")
        checks.append(name)
    # Decode the handler's CALL independently of its surrounding signature.
    call = 0x138A945
    target = call + 5 + struct.unpack("<i", at(call+1, 4))[0]
    if at(call, 1) != b"\xe8" or target != 0x138BDA0:
        raise ValueError("Launcher handler does not call SetMainMenuMode")
    checks.append("launcher_calls_main_menu_mode")

    return {
        "module": str(path),
        "sha256": digest,
        "static_checks_passed": len(checks),
        "checks": checks,
        "live_tested": False,
        "conclusion": "Keyboard/mouse Pressed is sufficient at the launcher raw handler; verify exclusive/override routing live.",
    }


if __name__ == "__main__":
    try:
        print(json.dumps(verify(Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_MODULE),
                         indent=2))
    except (OSError, ValueError, struct.error) as error:
        print(json.dumps({"error": str(error), "live_tested": False}), file=sys.stderr)
        sys.exit(1)

