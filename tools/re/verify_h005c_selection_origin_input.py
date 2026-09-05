"""Offline H-005C byte, ABI and transform checks. Never opens a process.

py -3 tools/re/verify_h005c_selection_origin_input.py [PreyDll.dll]
py -3 tools/re/verify_h005c_selection_origin_input.py --fixtures-only

The fixtures distinguish matched native origins, the added near-VP translation,
and mixed-eye samples. They do not prove runtime binding or rendered behavior.
"""
import argparse
import ctypes as ct
import hashlib
import itertools
import json
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True

from verify_h005_skinning import DEFAULT_MODULE, EXPECTED_SHA256
from verify_h005b_model_frame import inverse, mm, near, rotation, translation


ANCHORS = {
    "render_entry": (0x81D0D0, "40 53 56 57 41 56 41 57 48 83 ec 40 48 8b f9 48 8b f2"),
    "near_inputs_and_explicit_clear_set": (0x81D127, "8b 86 80 00 00 00 4c 89 64 24 78 4c 89 ac 24 80 00 00 00 48 0f ba e0 17 72 14 f6 87 c8 0a 00 00 02 75 0b 48 0f ba f1 17 48 89 4b 40 eb 10 48 0f ba e9 17 48 89 4b 40 c6 86 ad 00 00 00 01"),
    "final_params_flags_or": (0x81D189, "48 8b 86 80 00 00 00 48 09 43 40"),
    "instance_token": (0x81D20A, "48 8b 46 48 48 89 83 90 00 00 00"),
    "camera_getter_this_plus_788": (0xDF2BB0, "48 8d 81 88 07 00 00 c3"),
    "post_enabled_and_key_state_gate": (0x9D6D40, "45 84 c0 75 0a 44 38 41 78 0f 84 76 02 00 00 83 7a 18 ff 75 0a 83 7a 04 10 0f 85 66 02 00 00"),
    "x_symbol_key_id": (0x9DA1BC, "41 b8 10 02 00 00 48 8b cb e8 06 f1 ff ff"),
    "y_symbol_key_id": (0x9DA1EA, "41 b8 11 02 00 00 48 8b cb e8 d8 f0 ff ff"),
    "player_x_cinematic_guard_and_store": (0x158FD20, "83 b9 94 00 00 00 00 f3 0f 10 44 24 28 74 03 0f 57 c0 f3 0f 11 41 5c"),
    "player_y_store_and_digital_clear": (0x158FDB1, "89 41 6c f3 0f 11 49 60"),
    "register_x_handler_against_action_2f0": (0x158D6C0, "48 8d 05 59 26 00 00 44 89 65 d8 48 89 45 d0 48 8b 83 f0 02 00 00"),
    "register_y_handler_against_action_2f8": (0x158D739, "48 8d 05 40 26 00 00 44 89 65 d8 48 89 45 d0 48 8b 83 f8 02 00 00"),
    "name_x_action_at_2f0": (0x1708B34, "48 8d 15 2d ff 76 00 48 8d 8b f0 02 00 00"),
    "name_y_action_at_2f8": (0x1708B47, "48 8d 15 2a ff 76 00 48 8d 8b f8 02 00 00"),
}
STRINGS = {
    "xi_thumblx": 0x1D5DE78,
    "xi_thumbly": 0x1D5DE88,
    "xi_movex": 0x1E78A68,
    "xi_movey": 0x1E78A78,
}


class InputEvent(ct.LittleEndianStructure):
    # Explicit fixed-width pointer representations; no host wchar/pointer ABI.
    _fields_ = [
        ("deviceType", ct.c_int32), ("state", ct.c_int32),
        ("inputChar", ct.c_uint16), ("keyName", ct.c_uint64),
        ("keyId", ct.c_int32), ("modifiers", ct.c_int32),
        ("value", ct.c_float), ("pSymbol", ct.c_uint64),
        ("deviceIndex", ct.c_uint8),
    ]


def require(ok, message):
    if not ok:
        raise ValueError(message)


def byte_checks(path):
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    require(digest == EXPECTED_SHA256, f"Unsupported module SHA-256: {digest}")
    u16 = lambda o: struct.unpack_from("<H", data, o)[0]
    u32 = lambda o: struct.unpack_from("<I", data, o)[0]
    pe = u32(0x3C)
    opt = pe + 24
    require(data[:2] == b"MZ" and data[pe:pe+4] == b"PE\0\0"
            and u16(pe+4) == 0x8664 and u16(opt) == 0x20B, "Expected x64 PE32+")
    base = struct.unpack_from("<Q", data, opt+24)[0]
    sections = []
    for i in range(u16(pe+6)):
        o = opt + u16(pe+20) + 40*i
        sections.append((u32(o+12), u32(o+16), u32(o+20)))

    def at(rva, length):
        for start, size, offset in sections:
            if start <= rva and rva+length <= start+size:
                return data[offset+rva-start:offset+rva-start+length]
        raise ValueError(f"Unmapped file-backed RVA {rva:#x}")

    checks = []
    for name, (rva, expected) in ANCHORS.items():
        pattern = bytes.fromhex(expected)
        require(at(rva, len(pattern)) == pattern, f"Byte mismatch: {name} at {rva:#x}")
        checks.append(name)
    for name, rva in STRINGS.items():
        require(at(rva, len(name)+1) == name.encode()+b"\0", f"String mismatch: {name}")
        checks.append("string_"+name)
    require(struct.unpack("<Q", at(0x1E580E8, 8))[0] == base+0x158F910,
            "ArkPlayerInput::OnAction vtable mismatch")
    checks.append("player_on_action_vtable")
    return digest, checks


def fixtures():
    checks = []
    # An initial pooled-object flag must not affect the resulting near bit.
    for p, c, old in itertools.product((False, True), repeat=3):
        flags = 0x1100007 | (0x800000 if old else 0)
        if p or c:
            flags |= 0x800000
        else:
            flags &= ~0x800000
        flags |= 0x800000 if p else 0
        require(bool(flags & 0x800000) == (p or c), "Near predicate depends on stale flags")
    checks.append("near_truth_table_including_stale_object_flags")

    expected = {"deviceType": 0, "state": 4, "inputChar": 8, "keyName": 0x10,
                "keyId": 0x18, "modifiers": 0x1C, "value": 0x20,
                "pSymbol": 0x28, "deviceIndex": 0x30}
    require(ct.sizeof(InputEvent) == 0x38 and ct.alignment(InputEvent) == 8,
            "Input event size/alignment mismatch")
    require(all(getattr(InputEvent, n).offset == o for n, o in expected.items()),
            "Input event field offset mismatch")
    checks.append("prey_event_layout_56_bytes_not_ce5")
    for key_id, value in ((0x210, .375), (0x211, -.625), (0x210, 0.), (0x211, 0.)):
        event = InputEvent(deviceType=3, state=8, keyName=0x123456789ABCDEF0,
                           keyId=key_id, value=value, deviceIndex=2)
        raw = bytes(event)
        require(struct.unpack_from("<ii", raw)[0:2] == (3, 8), "Device/state serialization")
        require(struct.unpack_from("<Q", raw, 0x10)[0] == 0x123456789ABCDEF0,
                "Key-name pointer serialization")
        require(struct.unpack_from("<i", raw, 0x18)[0] == key_id, "Key ID serialization")
        require(struct.unpack_from("<f", raw, 0x20)[0] == value and raw[0x30] == 2,
                "Axis/neutral serialization")
        require(raw[8:16] == bytes(8) and raw[0x28:0x30] == bytes(8), "Zero optional fields")
    checks.append("signed_axis_and_changed_zero_wire_examples")

    world = mm(translation((12, -4, 7)), mm(rotation(2, .7), rotation(0, -.4)))
    base_camera = (11.8, -3.6, 7.3)
    point = [[12.4], [-3.1], [7.7], [1]]
    special = mm(translation((.2, .4, -.1)), rotation(2, .8))
    right = (.8, .6, 0.)
    native_points, special_points, ordinary_patched_points = [], [], []
    for eye in (-1, 1):
        e = tuple(eye*.032*r for r in right)
        camera = tuple(c+x for c, x in zip(base_camera, e))
        shifted = mm(translation(tuple(-c for c in camera)), world)
        target_eye = mm(translation(tuple(-c for c in camera)), point)
        native = mm(inverse(shifted), target_eye)
        near(native, mm(inverse(world), point), "Matched native origins must cancel")
        native_points.append(native)
        for d in (e, (0., 0., 0.), tuple(.8*x for x in e)):
            # General formula: M^-1(P - Ceye + d); d need not equal eye offset.
            for model in (special, shifted):
                model_point = mm(inverse(model), mm(translation(d), target_eye))
                actual = mm(translation(tuple(-x for x in d)), mm(model, model_point))
                near(actual, target_eye, "Active near VP must land on desired eye-relative target")
        stable = mm(inverse(special), mm(translation(tuple(-c for c in base_camera)), point))
        special_points.append(stable)
        ordinary_patched_points.append(mm(inverse(shifted),
                                         mm(translation(tuple(-c for c in base_camera)), point)))
        wrong = mm(inverse(special), target_eye)
        wrong_final = mm(translation(tuple(-x for x in e)), mm(special, wrong))
        require(max(abs(wrong_final[i][0]-target_eye[i][0]) for i in range(3)) > .02,
                "Fixture must detect subtracting eye origin twice")
    near(native_points[0], native_points[1], "Native model target stable across eyes")
    near(special_points[0], special_points[1], "Patched stable-M target stable across eyes")
    require(max(abs(ordinary_patched_points[0][i][0]-ordinary_patched_points[1][i][0])
                for i in range(3)) > .03, "Ordinary branch must distinguish stable bones from correct clips")
    checks.extend(("matched_native_origins_cancel_eye_translation",
                   "near_patch_general_formula_with_zero_and_unequal_ipd",
                   "stable_camera_space_matrix_uses_matched_cyclops_origin",
                   "eye_origin_plus_near_patch_double_shift_detected",
                   "ordinary_branch_correct_draw_can_require_eye_dependent_model_target"))
    # A right-eye origin paired with a left-eye raw matrix is deliberately wrong.
    left_c = tuple(c-.032*r for c, r in zip(base_camera, right))
    right_c = tuple(c+.032*r for c, r in zip(base_camera, right))
    left_m = mm(translation(tuple(-c for c in left_c)), world)
    mixed = mm(inverse(left_m), mm(translation(tuple(-c for c in right_c)), point))
    reference = mm(inverse(world), point)
    require(max(abs(mixed[i][0]-reference[i][0]) for i in range(3)) > .03,
            "Fixture must detect mixed-eye tuple")
    checks.append("mixed_eye_origin_matrix_detected")
    return checks


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", nargs="?", type=Path, default=DEFAULT_MODULE)
    parser.add_argument("--fixtures-only", action="store_true")
    args = parser.parse_args()
    try:
        digest, static = (None, []) if args.fixtures_only else byte_checks(args.module)
        synthetic = fixtures()
        print(json.dumps({"module": None if args.fixtures_only else str(args.module),
                          "sha256": digest, "static_checks_passed": len(static), "checks": static,
                          "synthetic_checks_passed": len(synthetic), "synthetic_checks": synthetic,
                          "runtime_tested": False, "active_bindings_verified": False}, indent=2))
    except (OSError, ValueError, struct.error) as error:
        print(json.dumps({"error": str(error), "runtime_tested": False}), file=sys.stderr)
        sys.exit(1)
