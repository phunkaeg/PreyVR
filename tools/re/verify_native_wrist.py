"""Offline Steam native wrist contract verification. Never opens a process."""
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
from verify_h005_skinning import DEFAULT_MODULE, EXPECTED_SHA256

ROOT = Path(__file__).resolve().parents[2]


def verify():
    data = DEFAULT_MODULE.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    assert digest == EXPECTED_SHA256, 'unsupported Steam DLL'
    u16 = lambda p: struct.unpack_from('<H', data, p)[0]
    u32 = lambda p: struct.unpack_from('<I', data, p)[0]
    pe = u32(0x3c)
    assert u16(pe+4) == 0x8664 and u16(pe+24) == 0x20b
    sections = []
    for n in range(u16(pe+6)):
        off = pe+24+u16(pe+20)+n*40
        sections.append((u32(off+12), u32(off+16), u32(off+20)))

    def at(rva, count):
        for va, size, raw in sections:
            if va <= rva and rva+count <= va+size:
                return data[raw+rva-va:raw+rva-va+count]
        raise ValueError(hex(rva))

    def fnv(block):
        h = 14695981039346656037
        for byte in block:
            h = ((h ^ byte)*1099511628211) & ((1 << 64)-1)
        return h

    text = (ROOT/'include/preyvr/NativeWristNative.h').read_text()
    entries = re.findall(r'\{0x([0-9A-F]+),(\d+),0x([0-9A-F]+)ull\}, // (\w+)', text)
    assert len(entries) == 9
    checks = []
    for rva, size, expected, name in entries:
        body = at(int(rva, 16), int(size))
        assert fnv(body) == int(expected, 16), name
        assert fnv(bytes([body[0] ^ 1])+body[1:]) != int(expected, 16), name+' negative control'
        checks.append(name+' complete bytes and mutation refusal')
    for slot, rva in [(0x1EB57E8, 0x18BABA0), (0x1EB5898, 0x18BF0C0),
                      (0x1EB57A8, 0x18B8ED0), (0x1EB4BE8, 0x18ABE10)]:
        assert struct.unpack('<Q', at(slot, 8))[0] == 0x180000000+rva
    assert at(0x18B8ED0, 8).hex() == '488b8118010000c3'
    checks.append('concrete root/sprite vtables and sprite owner accessor')
    return dict(target_sha256=digest, checks=checks, game_launched=False,
                limit='Byte identity and inspected static contracts, not native replay execution or headset acceptance.')


if __name__ == '__main__':
    output = json.dumps(verify(), indent=2)+'\n'
    if len(sys.argv) > 1:
        Path(sys.argv[1]).write_text(output, encoding='utf-8', newline='\n')
    print(output)
