"""Read-only Steam PE cross-check of the decompiler-backed pointer contract."""
import hashlib
import json
from pathlib import Path
import struct
import sys

target = Path(sys.argv[1])
data = target.read_bytes()
sha = hashlib.sha256(data).hexdigest()
assert sha == "7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7", sha
pe = struct.unpack_from("<I", data, 0x3C)[0]
assert data[pe:pe+4] == b"PE\0\0"
machine, sections = struct.unpack_from("<HH", data, pe+4)
assert machine == 0x8664
optional_size = struct.unpack_from("<H", data, pe+20)[0]
opt = pe+24
assert struct.unpack_from("<H", data, opt)[0] == 0x20B
base = struct.unpack_from("<Q", data, opt+24)[0]
assert base == 0x180000000
section_table = opt+optional_size

def read(rva, size):
    for index in range(sections):
        pos = section_table+index*40
        virtual_size, va, raw_size, raw = struct.unpack_from("<IIII", data, pos+8)
        if va <= rva and rva+size <= va+raw_size:
            return data[raw+rva-va:raw+rva-va+size]
    raise ValueError(f"No raw coverage for {rva:#x}+{size}")

results = []
for label, table, slot, callee in [
    ("CFlashUI primary SendFlashMouseEvent", 0x1CA6898, 0xE0, 0x2CEF30),
    ("CFlashUI secondary hardware mouse", 0x1CA6008, 8, 0x2CFCC0),
    ("CFlashUIElement SendCursorEvent", 0x1CAB358, 0x300, 0x2FF0C0),
    ("Flash player ScreenToClient", 0x1DB56D8, 0x1C0, 0xE8C9F0),
    ("Flash player SendCursorEvent", 0x1DB56D8, 0xF0, 0xE8CB20),
    ("Hardware mouse Event (reference route)", 0x1D988C8, 0x20, 0xDD0220),
]:
    actual = struct.unpack("<Q", read(table+slot, 8))[0]-base
    assert actual == callee, (label, hex(actual), hex(callee))
    results.append(dict(label=label, table=hex(table), slot=hex(slot), callee=hex(callee)))

head = bytes.fromhex("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 40")
assert read(0x2CFCC0, len(head)) == head
print(json.dumps(dict(target=str(target), sha256=sha, pointer_width=8,
                     guarded_prologue=head.hex(), slots=results, verdict="PASS",
                     limit="Static contract only; UI acceptance requires in-game evidence."), indent=2))
