"""Verify H-018 contracts on disk and synthetic snapshots; no process access.
Usage: C:/Python314/python.exe -B tools/re/verify_h018_static_gaps.py [PreyDll.dll]
"""
import hashlib
import json
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from verify_h005_skinning import DEFAULT_MODULE, EXPECTED_SHA256
from decode_h018_bind_snapshot import fixture_checks

# Exact readings from the target Ghidra program, independently checked on disk.
ANCHORS = {
    "chr_binding_identity": [
        3362308,
        "488b6a28488bf948894a48498bf0488b4908"
    ],
    "chr_render_dispatch": [
        3362444,
        "488b0148c744242000000000ff90c0000000"
    ],
    "bone_binding_dispatch": [
        8529442,
        "488b4920488b01ff5018"
    ],
    "wrench_installs_vtable": [
        24311320,
        "488d05e1387600488907"
    ],
    "attach_unwind_record": [
        47581624,
        "f0146901eb176901f0421d02"
    ],
    "firing_getter_dispatch": [
        23677145,
        "e8b280eeff4c8b4040488d542440488bd8488d484041ff10"
    ],
    "cached_ray_getter": [
        22530992,
        "8b819417000089028b81981700008942048b819c1700008942088b81a017000089420c8b81a41700008942108b81a817000089"
    ],
    "ik_chain_and_pose": [
        8854704,
        "488b4218488bf14d8b48184c8bfa4d8b50104c6320486348104863502048634030"
    ],
    "ik_root_path": [
        8855100,
        "498b4728"
    ],
    "ik_path_count": [
        8855230,
        "448b70fc410fbaf61f"
    ],
    "ik_path_indices": [
        8855248,
        "498b57284863c7480fbf0c428d47ff4c6bc11c"
    ],
    "ik_dispatch_tag": [
        8865055,
        "817b083242494b"
    ],
    "ccd_iterations": [
        8866146,
        "4439660c"
    ],
    "ccd_step": [
        8867026,
        "f30f594e14"
    ],
    "ccd_threshold": [
        8868469,
        "0f2f4610"
    ],
    "manager_global": [
        3988206,
        "48893debe10b02"
    ],
    "manager_tree_init": [
        3987964,
        "488d4f584c8967584c896760e84351000048894758"
    ],
    "manager_enabled": [
        3988156,
        "c787dc00000001000000"
    ],
    "bind_tree_walk": [
        3992297,
        "498b7858f7d5488bdf488b470880781900751b0f1f400039"
    ],
    "bind_three_pointers": [
        3992393,
        "4c8b73304c8d7b284d8b2f498bce4d8b6710498b30498b06ff"
    ],
    "action_id_getter": [
        17737440,
        "488d4140c3"
    ],
    "map_name_getter": [
        3958096,
        "488b4158c3"
    ],
    "input_name_pointer": [
        3951377,
        "48894118"
    ],
    "input_default_pointer": [
        3951437,
        "488d435848896b404088284889435048c743481f0000"
    ],
    "input_crc_load": [
        3990032,
        "48895c24104889742418574883ec70458b9998000000498bd9498bf8"
    ]
}
CALLS = {
    "wrench_base_ctor": [
        24311315,
        23658544
    ],
    "wrench_shared_equip_setup": [
        23697919,
        23704656
    ],
    "setup_calls_attach": [
        23705339,
        23663856
    ],
    "gloo_shot_reads_ray": [
        23721101,
        23677072
    ],
    "gloo_spawns_projectile": [
        23721378,
        23556160
    ],
    "shotgun_shot_reads_ray": [
        23769278,
        23677072
    ],
    "shotgun_spawns_pellets": [
        23770193,
        23767920
    ],
    "hud_updates_cached_ray": [
        23492376,
        22565664
    ],
    "ik_dispatches_two_bone": [
        8865073,
        8854688
    ]
}
VTABLES = {
    "chr_binding_render": [
        30085952,
        3362288
    ],
    "character_render": [
        30548672,
        8502448
    ],
    "wrench_factory_create": [
        32069616,
        24311264
    ],
    "wrench_equip": [
        32059144,
        23697408
    ],
    "base_weapon_equip": [
        31901256,
        23697408
    ],
    "gloo_factory_create": [
        32069328,
        24312064
    ],
    "gloo_prerender": [
        31910000,
        23720992
    ],
    "shotgun_factory_create": [
        32069472,
        24312448
    ],
    "shotgun_start_attack": [
        31914088,
        23769072
    ],
    "manager_input_handler": [
        30146064,
        4000096
    ],
    "manager_filter_query": [
        30146032,
        3989472
    ],
    "action_name_getter": [
        30139616,
        17737440
    ],
    "map_name_getter": [
        30139832,
        3958096
    ]
}


def verify(path):
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_SHA256:
        raise ValueError(f"Unsupported module SHA-256: {digest}")
    def u16(offset): return struct.unpack_from("<H", data, offset)[0]
    def u32(offset): return struct.unpack_from("<I", data, offset)[0]
    pe = u32(0x3C)
    opt = pe+24
    if (data[:2] != b"MZ" or data[pe:pe+4] != b"PE"+bytes(2)
            or u16(pe+4) != 0x8664 or u16(opt) != 0x20B):
        raise ValueError("Expected x64 PE32+")
    base = struct.unpack_from("<Q", data, opt+24)[0]
    sections = []
    for i in range(u16(pe+6)):
        offset = opt+u16(pe+20)+40*i
        sections.append((u32(offset+12), u32(offset+16), u32(offset+20)))
    def at(rva, size):
        for start, length, file_offset in sections:
            if start <= rva and rva+size <= start+length:
                return data[file_offset+rva-start:file_offset+rva-start+size]
        raise ValueError(f"Unmapped file-backed RVA {rva:#x}")
    checks = []
    for name, (rva, expected) in ANCHORS.items():
        expected = bytes.fromhex(expected)
        if at(rva, len(expected)) != expected:
            raise ValueError(f"Byte mismatch: {name} at {rva:#x}")
        checks.append(name)
    for name, (call, target) in CALLS.items():
        actual = call+5+struct.unpack("<i", at(call+1, 4))[0]
        if at(call, 1) != bytes([0xE8]) or actual != target:
            raise ValueError(f"Call mismatch: {name} at {call:#x}: {actual:#x}")
        checks.append(name)
    for name, (slot, target) in VTABLES.items():
        if struct.unpack("<Q", at(slot, 8))[0] != base+target:
            raise ValueError(f"Vtable mismatch: {name} at {slot:#x}")
        checks.append(name)
    # Exception directory (index 3) uses RVA triples, not 64-bit vtable entries.
    exception_rva, exception_size = struct.unpack_from("<II", data, opt+112+3*8)
    record = 0x2D609B8
    if not (exception_rva <= record and record+12 <= exception_rva+exception_size
            and (record-exception_rva) % 12 == 0
            and struct.unpack("<III", at(record, 12)) == (0x16914F0, 0x16917EB, 0x21D42F0)):
        raise ValueError("AttachToHand reference is not the expected RUNTIME_FUNCTION")
    checks.append("attach_reference_is_exception_record")
    if struct.pack("<Q", base+0x16914F0) in data:
        raise ValueError("Unexpected absolute pointer reference to AttachToHand")
    checks.append("attach_has_no_absolute_pointer_reference")
    # Verify the game's whole lookup table before using zlib CRCs in snapshots.
    crc_table = []
    for value in range(256):
        for _ in range(8):
            value = (value >> 1) ^ (0xEDB88320 if value & 1 else 0)
        crc_table.append(value)
    if struct.unpack("<256I", at(0x1C8AE40, 1024)) != tuple(crc_table):
        raise ValueError("Input-name CRC table is not reflected IEEE CRC32")
    checks.append("binding_crc_table_matches_ieee_crc32")
    synthetic = fixture_checks()
    return {"module": str(path), "sha256": digest, "static_checks_passed": len(checks),
            "synthetic_checks_passed": synthetic, "checks": checks,
            "live_tested": False, "installed_files_modified": False}


if __name__ == "__main__":
    try:
        print(json.dumps(verify(Path(sys.argv[1]) if len(sys.argv)>1 else DEFAULT_MODULE), indent=2))
    except (OSError, ValueError, struct.error) as error:
        print(json.dumps({"error": str(error), "live_tested": False}), file=sys.stderr)
        sys.exit(1)
