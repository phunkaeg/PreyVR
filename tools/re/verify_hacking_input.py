"""Verify installed Steam hacking input assets and native witnesses offline.

Reads only the supported game. Emits derived binding metadata, never movie/XML
assets or key material. Requires the existing extractor dependencies via --deps.
No game process, DLL loading, network or runtime configuration is involved.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct
import sys

from extract_native_hud import (Pak, TARGET_SHA256, KEY_FILE_OFFSET, KEY_BYTES,
                                cipher_self_test, decode_xml, read_at, require, sha256_file)

BINDINGS = {
    "hack_moveprompt_x": "xi_thumblx", "hack_moveprompt_y": "xi_thumbly",
    "hack_final1": "xi_x", "hack_final2": "xi_y",
    "hack_final3": "xi_a", "hack_final4": "xi_b",
}
KEYBOARD_ONLY = {
    "hack_moveup": "w", "hack_movedown": "s",
    "hack_moveleft": "a", "hack_moveright": "d",
}
TOKEN_PRODUCERS = [
    (0x1709C6C, 0xA30, "hack_moveprompt_y"),
    (0x1709C7F, 0xA38, "hack_moveprompt_x"),
    (0x1709C92, 0xA40, "hack_moveup"),
    (0x1709CA5, 0xA48, "hack_movedown"),
    (0x1709CB8, 0xA50, "hack_moveleft"),
    (0x1709CCB, 0xA58, "hack_moveright"),
    (0x1709CDE, 0xA60, "hack_final1"),
    (0x1709CF1, 0xA68, "hack_final2"),
    (0x1709D04, 0xA70, "hack_final3"),
    (0x1709D17, 0xA78, "hack_final4"),
]
WITNESSES = {
    "hacking_movie_native_lookup": (0x1626418, "488d1509db8300488b01ff5060"),
    "analog_x_token_and_store": (0x1626D39, "488b05f8fa5e01488b16483b90380a0000750af30f117348"),
    "analog_y_token_and_store": (0x1626D56, "483b90300a000075110f57350a7e6500f30f11734ce9a7040000"),
    "keyboard_left_token_and_activation": (0x1626D70, "483b90500a000075494080e70140387b58"),
    "keyboard_right_token_and_activation": (0x1626DC2, "483b90580a000075494080e70140387b59"),
    "keyboard_up_token_and_activation": (0x1626E14, "483b90400a000075494080e70140387b5a"),
    "keyboard_down_token_and_activation": (0x1626E66, "483b90480a000075494080e70140387b5b"),
    "game_receiver_to_hacking_receiver": (0x119C71A, "488b8938010000418bd80f29742420488bfa0f28f3e89ca54800"),
    "final_branch_state_and_correct_token": (0x1626EB8, "807b5c000f845503000083bbf0000000050f8548030000483b53600f85d6000000"),
}


def image_reader(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    require(data[:2] == b"MZ" and data[pe:pe + 4] == b"PE\0\0", "invalid PE")
    require(struct.unpack_from("<H", data, pe + 4)[0] == 0x8664, "not x64")
    optional = pe + 24
    require(struct.unpack_from("<H", data, optional)[0] == 0x20B, "not PE32+")
    sections = []
    for index in range(struct.unpack_from("<H", data, pe + 6)[0]):
        at = optional + struct.unpack_from("<H", data, pe + 20)[0] + index * 40
        rva, size, raw = struct.unpack_from("<III", data, at + 12)
        sections.append((rva, size, raw))

    def read(rva, size):
        for begin, count, raw in sections:
            if begin <= rva and rva + size <= begin + count:
                result = data[raw + rva - begin:raw + rva - begin + size]
                require(len(result) == size, "truncated PE read")
                return result
        raise ValueError("RVA outside file-backed section")
    return read


def verify_witnesses(data):
    read = image_reader(data)
    for name, (rva, expected) in WITNESSES.items():
        require(read(rva, len(bytes.fromhex(expected))) == bytes.fromhex(expected), "native witness mismatch: " + name)
    movie = b"DanielleHacking2\0"
    require(read(0x1E63F28, len(movie)) == movie, "wrong hacking movie string")
    # Independent producer: CCryName assignments identify the same token offsets
    # used by ProcessInput. These are observations, not a new callable ABI.
    for at, slot, name in TOKEN_PRODUCERS:
        code = read(at, 14)
        require(code[:3] == b"\x48\x8d\x15" and code[7:10] == b"\x48\x8d\x8b", "token producer opcode mismatch")
        string_rva = at + 7 + struct.unpack_from("<i", code, 3)[0]
        require(struct.unpack_from("<I", code, 10)[0] == slot, "token slot mismatch")
        require(read(string_rva, len(name) + 1) == name.encode() + b"\0", "token name mismatch")


def verify_action_map(action_map):
    nodes = action_map.findall("action")
    actions = {a.get("name"): a for a in nodes}
    expected = set(BINDINGS) | set(KEYBOARD_ONLY)
    require(len(nodes) == len(actions) == len(expected) and set(actions) == expected,
            "unexpected/duplicate/missing hacking action")
    require(all(len(a) == 0 for a in nodes), "unexpected nested hacking binding")
    derived = {}
    for action, button in BINDINGS.items():
        require(actions[action].get("xboxpad") == button, "binding mismatch: " + action)
        if action.startswith("hack_final"):
            require(actions[action].get("onPress") == "1" and actions[action].get("onRelease") is None,
                    "final-answer activation mismatch")
        derived[action] = dict(key=button, on_press=actions[action].get("onPress") == "1",
                               on_release=actions[action].get("onRelease") == "1")
    keyboard = {}
    for action, key in KEYBOARD_ONLY.items():
        require(actions[action].attrib == dict(name=action, onPress="1", onRelease="1", keyboard=key),
                "keyboard-only directional binding mismatch: " + action)
        keyboard[action] = dict(key=key, on_press=True, on_release=True)
    return dict(action_count=len(nodes), actions=sorted(actions), xbox_binding_count=len(derived),
                bindings=derived, keyboard_only_actions=keyboard)


def inspect(game):
    from Crypto.PublicKey import RSA
    module = game / "Binaries/Danielle/x64/Release/PreyDll.dll"
    data = module.read_bytes()
    require(hashlib.sha256(data).hexdigest() == TARGET_SHA256, "unsupported Steam module")
    verify_witnesses(data)
    corrupt = bytearray(data)
    # Header/source identity is unchanged in this negative control; mutate the
    # concrete witness byte so the semantic gate itself must reject it.
    target = WITNESSES["hacking_movie_native_lookup"][0]
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    table = pe + 24 + struct.unpack_from("<H", data, pe + 20)[0]
    for n in range(struct.unpack_from("<H", data, pe + 6)[0]):
        at = table + n * 40
        rva, size, raw = struct.unpack_from("<III", data, at + 12)
        if rva <= target < rva + size:
            corrupt[raw + target - rva] ^= 1
            break
    rejected = False
    try:
        verify_witnesses(corrupt)
    except ValueError:
        rejected = True
    require(rejected, "negative witness control was accepted")
    with module.open("rb") as stream:
        key = RSA.import_key(read_at(stream, KEY_FILE_OFFSET, KEY_BYTES))
    cipher_self_test()
    report = {"target_sha256": TARGET_SHA256, "environment": "offline",
              "method": "Steam bytes plus bounded read-only PAK extraction and CryXmlB decoding",
              "controls": {"native_witness_corruption_rejected": True, "twofish_known_answer": "PASS"},
              "native_witnesses": {name: {"rva": hex(rva), "bytes": raw} for name, (rva, raw) in WITNESSES.items()},
              "native_token_producers": [dict(rva=hex(at), slot=hex(slot), action=name)
                                         for at, slot, name in TOKEN_PRODUCERS],
              "profiles": [], "movie": None}
    for relative in ("GameSDK/GameData.pak", "GameSDK/Precache/patch.pak"):
        pak = Pak(game / relative, key)
        archive_hash = sha256_file(game / relative)
        name = "libs/config/defaultprofile.xml"
        payload = pak.read(name)
        root = decode_xml(payload)
        require(root.tag == "profile" and root.get("version") == "73", "unexpected profile")
        maps = [m for m in root.iter("actionmap") if m.get("name") == "hacking"]
        require(len(maps) == 1, "missing/ambiguous hacking map")
        derived = verify_action_map(maps[0])
        # Completeness controls exercise the full-map gate, not just the six
        # selected pad bindings: a removed keyboard action or extra pad binding
        # must be rejected instead of disappearing from the derived report.
        for change in ("missing_direction", "extra_pad_action"):
            mutated = copy.deepcopy(maps[0])
            if change == "missing_direction":
                mutated.remove(next(a for a in mutated if a.get("name") == "hack_moveup"))
            else:
                extra = copy.deepcopy(mutated.find("action"))
                extra.set("name", "unexpected_hack_jump")
                extra.set("xboxpad", "xi_a")
                mutated.append(extra)
            rejected = False
            try:
                verify_action_map(mutated)
            except ValueError:
                rejected = True
            require(rejected, "full action-map negative control was accepted: " + change)
        report["controls"]["full_action_map_mutations_rejected"] = ["missing_direction", "extra_pad_action"]
        entry = pak.entries[name]
        report["profiles"].append(dict(archive=relative, archive_sha256=archive_hash,
            member=name, sha256=hashlib.sha256(payload).hexdigest(), crc32=f"{entry['crc32']:08x}",
            bytes=len(payload), **derived))
        movie_name = "libs/ui/uielements/daniellehacking2.xml"
        if movie_name in pak.entries:
            raw = pak.read(movie_name)
            ui = decode_xml(raw)
            require(ui.tag == "UIElements" and ui.get("name") == "DanielleHacking2", "wrong movie definition")
            report["movie"] = dict(archive=relative, member=movie_name,
                sha256=hashlib.sha256(raw).hexdigest(), bytes=len(raw), name="DanielleHacking2",
                source=ui.find("UIElement/GFx").get("file"),
                constraints=ui.find("UIElement/GFx/Constraints/Align").attrib)
    require(report["movie"] is not None, "missing hacking movie definition")
    report["effective_profile"] = report["profiles"][-1]["archive"]
    report["limits"] = "Default installed profile only; a custom user rebind may differ. No native input acceptance or rendering/headset test. Assets and key material are not emitted."
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=Path("D:/SteamLibrary/steamapps/common/Prey"))
    parser.add_argument("--deps", type=Path)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    if args.deps:
        sys.path.insert(0, str(args.deps.resolve()))
    result = inspect(args.game.resolve())
    rendered = json.dumps(result, indent=2) + "\n"
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(rendered, encoding="utf-8")
    print(rendered)
