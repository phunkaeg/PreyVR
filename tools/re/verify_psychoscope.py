"""Read-only Steam PE checks for the new scope action. Never opens a process."""
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
    assert digest == EXPECTED_SHA256, digest
    u16 = lambda off: struct.unpack_from("<H", data, off)[0]
    u32 = lambda off: struct.unpack_from("<I", data, off)[0]
    pe = u32(0x3c)
    opt = pe + 24
    assert u16(pe+4) == 0x8664 and u16(opt) == 0x20b
    sections = []
    for n in range(u16(pe+6)):
        off = opt + u16(pe+20) + n*40
        sections.append((u32(off+12), u32(off+16), u32(off+20)))
    def at(rva, count):
        for start, size, fileoff in sections:
            if start <= rva and rva+count <= start+size:
                return data[fileoff+rva-start:fileoff+rva-start+count]
        raise ValueError(hex(rva))
    checks = []
    def check(label, rva, expected):
        assert at(rva, len(expected)) == expected, label
        checks.append(label)
    header = (ROOT/"include/preyvr/PsychoscopeNative.h").read_text()
    table = header.split("HandlerBytes{", 1)[1].split("}", 1)[0]
    expected = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", table))
    assert len(expected) == 208
    check("complete native handler, including all gates and callees", 0x1590cc0, expected)
    altered = bytearray(expected)
    altered[146] ^= 1
    assert at(0x1590cc0, 208) != altered, "negative byte control"
    checks.append("altered handler refused")
    check("player embeds input at +0x8e0 and passes same owner", 0x157a563,
          bytes.fromhex("488d8ee0080000488bd6e84e2c0100"))
    check("scope action name producer at actions+0x968", 0x1709a91,
          bytes.fromhex("488d15e8ff7600488d8b68090000e8cc20a7fe"))
    check("literal resolves to toggle_scope", 0x1e79a80, b"toggle_scope\0")
    check("same action field registers native scope handler", 0x158dc6c,
          bytes.fromhex("488d054d300000448965d8488945d0488b8368090000"))
    check("scope component accessor +0xe8", 0x119a470, bytes.fromhex("488b81e8000000c3"))
    # Independently decode rel32s rather than just naming the byte patterns.
    relative = lambda rva: rva+5+struct.unpack("<i", at(rva+1,4))[0]
    assert relative(0x157a56d) == 0x158d1c0
    assert relative(0x1590d6c) == 0x119a470
    assert relative(0x1590d74) == 0x1272d10
    checks.append("constructor and consumer call targets decoded")
    return dict(target=str(DEFAULT_MODULE), sha256=digest, checks=checks,
                static_checks_passed=len(checks), runtime_tested=False,
                claim="Native toggle_scope handler and live-player embedded input receiver mapped on Steam.")

if __name__ == "__main__":
    result=json.dumps(verify(), indent=2)
    if len(sys.argv)>1:
        Path(sys.argv[1]).write_text(result+"\n", encoding="utf-8", newline="\n")
    print(result)
