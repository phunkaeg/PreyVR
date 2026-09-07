"""Offline H-022 native landmarks and counterexample models; no game access.

The models demonstrate why the existing counters cannot rule out eye-age or
coverage problems. They do not reproduce or diagnose the user's actual pixels.
"""
import hashlib
import json
import struct
from pathlib import Path

MODULE = Path(r"D:\SteamLibrary\steamapps\common\Prey\Binaries\Danielle\x64\Release\PreyDll.dll")
SHA256 = "7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7"
ANCHORS = {
    "legacy_zero_vp_to_parameter_cache": (0xF18ABE, "8b813002000089843e648d0000"),
    "builder_selected_render_view": (0xFB1732, "498b96e0020000"),
    "builder_current_camera": (0xFB1754, "488d9aa0110000"),
    "builder_previous_camera": (0xFB1763, "4c8d8ae0130000"),
    "builder_derived_camera": (0xFB1770, "4881c220160000"),
}


def verify_native(path):
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SHA256:
        raise ValueError(f"Unsupported image: {digest}")
    u16 = lambda offset: struct.unpack_from("<H", data, offset)[0]
    u32 = lambda offset: struct.unpack_from("<I", data, offset)[0]
    pe = u32(0x3C)
    opt = pe + 24
    if u16(pe + 4) != 0x8664 or u16(opt) != 0x20B:
        raise ValueError("Expected x64 PE32+")
    sections = []
    for index in range(u16(pe + 6)):
        off = opt + u16(pe + 20) + index * 40
        sections.append((u32(off + 12), u32(off + 16), u32(off + 20)))
    for name, (rva, hexbytes) in ANCHORS.items():
        expected = bytes.fromhex(hexbytes)
        section = next((s for s in sections if s[0] <= rva and
                        rva + len(expected) <= s[0] + s[1]), None)
        if section is None:
            raise ValueError(f"Unmapped landmark: {name}")
        start, _, raw = section
        if data[raw + rva - start:raw + rva - start + len(expected)] != expected:
            raise ValueError(f"Landmark mismatch: {name}")
    return {"sha256": digest, "landmarks": list(ANCHORS)}


def counterexamples():
    # A valid latest tag does not identify an older frame still rendering.
    queue = [(100, 0)]
    latest_eye = 0
    first_pass_eye = latest_eye
    queue.append((101, 1))
    latest_eye = 1
    second_pass_eye = latest_eye
    submitted_frame, submitted_eye = queue.pop(0)
    assert first_pass_eye == submitted_eye == 0
    assert second_pass_eye == 1 and second_pass_eye != submitted_eye
    assert all(eye in (0, 1) for eye in (first_pass_eye, second_pass_eye))

    # Simple perspective: clip X = f*(x-eyeX), W=z. A swapped sign produces
    # an outward jump relative to the correct image in BOTH eyes.
    h, focal, x, z = 0.032, 1.0, 0.0, 1.0
    jumps = []
    for eye_x in (-h, h):
        correct = focal * (x - eye_x) / z
        wrong = focal * (x + eye_x) / z
        jumps.append(wrong - correct)
    assert jumps[0] < 0 < jumps[1]
    assert abs(jumps[0] + 2*h) < 1e-12 and abs(jumps[1] - 2*h) < 1e-12

    # The current remembered-row key includes address, so equal matrix bytes
    # at another address do not match it. This is a coverage counterexample.
    written = ("view-A", 0, (0.032, 0.0, 0.0, 0.0))
    incoming_copy = ("view-B", 0, written[2])
    assert written != incoming_copy

    # A cross-pass difference vanishes under zero offset without identifying
    # either pass as correct, or establishing an extra geometry draw.
    for scale in (1.0, 0.0):
        color_x, depth_x = 0.0, scale*h
        assert (depth_x - color_x == 0.0) == (scale == 0.0)
    return {
        "delayed_frame": submitted_frame,
        "two_valid_pass_tags": [first_pass_eye, second_pass_eye],
        "submitted_eye": submitted_eye,
        "wrong_sign_jump_ndc_left_right": jumps,
        "different_pointer_copy_not_detected": True,
        "zero_delta_collapses_cross_pass_difference": True,
    }


if __name__ == "__main__":
    print(json.dumps({"native": verify_native(MODULE),
                      "analytic_counterexamples": counterexamples(),
                      "runtime_tested": False}, indent=2))
