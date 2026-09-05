"""Offline H-005B byte landmarks and transform fixtures; never opens a process.

Usage: py -3 tools/re/verify_h005b_model_frame.py [path/to/PreyDll.dll]
The synthetic fixtures check the algebra, not engine execution or headset behavior.
"""
import hashlib
import json
import math
import struct
import sys
from pathlib import Path

from verify_h005_skinning import DEFAULT_MODULE, EXPECTED_SHA256

ANCHORS = {
    "render_matrix_copy_all_12_floats": (0x81D194, "41 8b 06 89 03 41 8b 46 04 89 43 04 41 8b 46 08 89 43 08 41 8b 46 0c 89 43 0c 41 8b 46 10 89 43 10 41 8b 46 14 89 43 14 41 8b 46 18 89 43 18 41 8b 46 1c 89 43 1c 41 8b 46 20 89 43 20 41 8b 46 24 89 43 24 41 8b 46 28 89 43 28 41 8b 46 2c 89 43 2c"),
    "render_instance_field": (0x81D20A, "48 8b 46 48 48 89 83 90 00 00 00"),
    "pool_id_and_earlier_marker": (0x81D26C, "ff 90 10 08 00 00 44 8b e0 44 8d 40 ff"),
    "dispatch_before_current_marker": (0x81D35F, "e8 3c 04 01 00"),
    "current_marker": (0x81D377, "48 89 ab 98 00 00 00"),
    "near_character_camera_position_subtraction": (0x974735, "ff 90 88 03 00 00 f3 0f 10 05 2d a4 30 01 f3 0f 10 48 0c f3 0f 10 58 2c 0f 57 c8 f3 0f 10 50 1c 0f 57 d8 0f 57 d0 f3 0f 10 45 ec f3 0f 58 c1 f3 0f 10 4d fc f3 0f 58 ca f3 0f 11 45 ec f3 0f 10 45 0c f3 0f 58 c3 f3 0f 11 4d fc f3 0f 11 45 0c"),
    "ik_leaf_pose_arrays_and_chain_indices": (0x871CA0, "4c 8b dc 53 56 41 54 41 57 48 81 ec c8 00 00 00 48 8b 42 18 48 8b f1 4d 8b 48 18 4c 8b fa 4d 8b 50 10 4c 63 20 48 63 48 10 48 63 50 20 48 63 40 30"),
    "adik_2bik_tag": (0x877F7D, "41 81 7e 08 32 42 49 4b"),
    "adik_native_2bone_call": (0x877FE1, "e8 ba 9c ff ff"),
    "absolute_setter_and_projected_bit_clear": (0x828D70, "8b 02 89 81 14 01 00 00 8b 42 04 89 81 18 01 00 00 8b 42 08 89 81 1c 01 00 00 8b 42 0c 89 81 20 01 00 00 f2 0f 10 42 10 f2 0f 11 81 24 01 00 00 8b 42 18 89 81 2c 01 00 00 81 61 08 ff bf ff ff c3"),
    "relative_setter_only_copies": (0x828DC0, "8b 02 89 81 f8 00 00 00 8b 42 04 89 81 fc 00 00 00 8b 42 08 89 81 00 01 00 00 8b 42 0c 89 81 04 01 00 00 f2 0f 10 42 10 f2 0f 11 81 08 01 00 00 8b 42 18 89 81 10 01 00 00 c3"),
    "absolute_getter": (0x822C70, "48 8d 81 14 01 00 00 c3"),
    "post_physics_queue_repeats_first_entry": (0x87799E, "0f b6 81 90 05 00 00 48 8b 94 c1 80 05 00 00 8b 42 fc 25 ff ff ff 7f 4c 0f 45 fa 85 c0 74 17 8b d8 90 49 8b 4f 08 48 8d 55 30 48 8b 01 ff 50 28 48 83 eb 01 75 ec"),
    "actor_limb_update_call": (0x16D82F0, "48 8b 8b 90 13 00 00 0f 28 d6 48 03 ce 48 8b d5 e8 8b 3e 00 00"),
    "character_skeleton_pose_accessor": (0x82DFF0, "48 8d 81 00 07 00 00 c3"),
}
VTABLES = {
    "character.render": (0x1D22200 + 0xC0, 0x81BCB0),
    "skeleton_pose.set_human_limb_ik": (0x1D227A0 + 0x148, 0x8345E0),
    "skeleton_anim.push_pose_modifier": (0x1D22D08 + 0x120, 0x839860),
    "attachment.set_absolute": (0x1D212B8 + 0x48, 0x828D70),
    "attachment.get_absolute": (0x1D212B8 + 0x50, 0x822C70),
    "attachment.set_relative": (0x1D212B8 + 0x58, 0x828DC0),
    "attachment.get_model_relative": (0x1D212B8 + 0x68, 0x822C80),
    "attachment.project": (0x1D212B8 + 0x80, 0x7A2560),
    "attachment.align_resets_default": (0x1D212B8 + 0xB8, 0x7A21B0),
    "attachment.add_binding": (0x1D212B8 + 0xD8, 0x7A2110),
    "ik2segments.prepare": (0x1D1FBF8 + 0x20, 0x7E3A20),
    "ik2segments.execute": (0x1D1FBF8 + 0x28, 0x7E3B00),
    "modifier_stack.execute": (0x1D203F8 + 0x28, 0x7F31A0),
}


def mm(a, b):
    return [[sum(a[i][k]*b[k][j] for k in range(len(b)))
             for j in range(len(b[0]))] for i in range(len(a))]


def inverse(a):
    n = len(a)
    m = [list(row)+[float(i == j) for j in range(n)] for i, row in enumerate(a)]
    for j in range(n):
        pivot = max(range(j, n), key=lambda i: abs(m[i][j]))
        if abs(m[pivot][j]) < 1e-12:
            raise ValueError("Singular model frame")
        m[j], m[pivot] = m[pivot], m[j]
        scale = m[j][j]
        m[j] = [v/scale for v in m[j]]
        for i in range(n):
            if i != j:
                scale = m[i][j]
                m[i] = [v-scale*w for v, w in zip(m[i], m[j])]
    return [row[n:] for row in m]


def translation(v):
    return [[1, 0, 0, v[0]], [0, 1, 0, v[1]], [0, 0, 1, v[2]], [0, 0, 0, 1]]


def rotation(axis, angle):
    a = [[float(i == j) for j in range(4)] for i in range(4)]
    i, j = ((1, 2), (2, 0), (0, 1))[axis]
    a[i][i] = a[j][j] = math.cos(angle)
    a[i][j], a[j][i] = -math.sin(angle), math.sin(angle)
    return a


def near(a, b, message):
    error = max(abs(v-w) for row, ref in zip(a, b) for v, w in zip(row, ref))
    if error > 1e-9:
        raise ValueError(f"{message}: {error}")


def fixtures():
    checks = []
    world = mm(translation((12, -4, 7)), mm(rotation(2, .7), mm(rotation(0, -.4), rotation(1, .25))))
    point = [[.12], [.38], [1.2], [1]]
    camera = (11.8, -3.6, 7.3)
    shift = translation(tuple(-v for v in camera))
    rendered = mm(world, point)
    near_model = mm(shift, world)
    near(mm(inverse(world), rendered), point, "World frame inverse")
    checks.append("world_inverse_with_pitch_roll_and_translation")
    near(mm(inverse(near_model), mm(shift, rendered)), point, "Near frame inverse")
    checks.append("near_camera_position_relative_inverse")
    wrong = mm(inverse(near_model), rendered)
    if max(abs(wrong[i][0]-point[i][0]) for i in range(3)) < 1:
        raise ValueError("Fixture failed to distinguish omitted camera origin")
    checks.append("omitting_camera_origin_is_detectably_wrong")
    # Camera orientation is not baked into the near model axes.
    near([r[:3] for r in near_model[:3]], [r[:3] for r in world[:3]], "Near basis")
    checks.append("near_retains_world_oriented_basis")
    scale = [[1.1,0,0,0], [0,.9,0,0], [0,0,1.2,0], [0,0,0,1]]
    scaled = mm(near_model, scale)
    near(mm(inverse(scaled), mm(scaled, point)), point, "Scaled inverse")
    checks.append("full_affine_inverse_handles_scale")
    bind = mm(translation((.2, -.1, 1)), rotation(2, .5))
    posed = mm(translation((.4, .3, .8)), rotation(0, -.8))
    desired = mm(translation((.3, .8, 1.1)), rotation(1, .9))
    absolute_default = mm(bind, mm(inverse(posed), desired))
    near(mm(posed, mm(inverse(bind), absolute_default)), desired, "Attachment default compensation")
    checks.append("attachment_bind_and_current_pose_compensation")
    # Prey applies an extra right-multiplied quaternion at attachment+0x14C.
    mount_rotation = rotation(2, -.3)
    compensated = mm(bind, mm(inverse(posed), mm(desired, inverse(mount_rotation))))
    near(mm(mm(posed, mm(inverse(bind), compensated)), mount_rotation), desired, "Extra mount rotation")
    checks.append("attachment_extra_mount_rotation_compensation")
    try:
        inverse([[0.0]*4 for _ in range(4)])
    except ValueError:
        checks.append("singular_matrix_refused")
    else:
        raise ValueError("Singular matrix accepted")
    return checks


def verify(path):
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_SHA256:
        raise ValueError(f"Unsupported module SHA-256: {digest}")
    u16 = lambda off: struct.unpack_from("<H", data, off)[0]
    u32 = lambda off: struct.unpack_from("<I", data, off)[0]
    pe = u32(0x3C)
    opt = pe+24
    if data[:2] != b"MZ" or data[pe:pe+4] != b"PE\0\0" or u16(pe+4) != 0x8664 or u16(opt) != 0x20B:
        raise ValueError("Expected an x64 PE32+ image")
    base = struct.unpack_from("<Q", data, opt+24)[0]
    sections = []
    for i in range(u16(pe+6)):
        off = opt+u16(pe+20)+40*i
        sections.append((u32(off+12), u32(off+16), u32(off+20)))

    def at(rva, length):
        for start, size, offset in sections:
            if start <= rva and rva+length <= start+size:
                return data[offset+rva-start:offset+rva-start+length]
        raise ValueError(f"Unmapped RVA {rva:#x}")

    checks = []
    for name, (rva, expected) in ANCHORS.items():
        pattern = bytes.fromhex(expected)
        if at(rva, len(pattern)) != pattern:
            raise ValueError(f"Byte mismatch: {name} at {rva:#x}")
        checks.append(name)
    for name, (slot, target) in VTABLES.items():
        if struct.unpack("<Q", at(slot, 8))[0] != base+target:
            raise ValueError(f"Vtable mismatch: {name} at {slot:#x}")
        checks.append(name)
    synthetic = fixtures()
    return {"module": str(path), "sha256": digest,
            "static_checks_passed": len(checks), "checks": checks,
            "synthetic_checks_passed": len(synthetic), "synthetic_checks": synthetic,
            "runtime_tested": False}


if __name__ == "__main__":
    try:
        print(json.dumps(verify(Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_MODULE), indent=2))
    except (OSError, ValueError, struct.error) as error:
        print(json.dumps({"error": str(error), "runtime_tested": False}), file=sys.stderr)
        sys.exit(1)
