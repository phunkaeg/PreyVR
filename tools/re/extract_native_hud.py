"""Offline, read-only extraction of the installed Steam HUD movie and UI definition.

No game DLL is loaded. Game assets are written only to the requested local output;
do not redistribute them. Requires pycryptodome and the twofish 0.3.0 C extension.
See docs/NATIVE-WRIST-HUD-2026-09-27.md for provenance and commands.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import xml.etree.ElementTree as ET
import zlib

TARGET_SHA256 = "7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7"
# DER public key in the hash-pinned Steam image; no key material is distributed.
KEY_FILE_OFFSET, KEY_BYTES = 0x228BBF0, 140
HUD_FILES = ("libs/ui/gfx/danielle_hud.gfx", "libs/ui/uielements/daniellehud.xml")
MAX_ENTRY = 32 * 1024 * 1024


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_at(stream, offset, count):
    require(offset >= 0 and count >= 0, "negative archive range")
    stream.seek(offset)
    data = stream.read(count)
    require(len(data) == count, "truncated archive range")
    return data


def sha256_file(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


class Twofish:
    """Small ctypes binding to the installed package's public C exports.

    Avoids twofish.py's removed Python 3.12 `imp` dependency. The implementation
    remains the external package; do not copy it into this repository.
    """
    class Key(ctypes.Structure):
        _fields_ = [("s", (ctypes.c_uint32 * 4) * 256), ("k", ctypes.c_uint32 * 40)]

    _lib = None

    def __init__(self, key):
        require(len(key) == 16, "expected 128-bit Twofish key")
        if Twofish._lib is None:
            spec = importlib.util.find_spec("_twofish")
            if spec is None:
                raise RuntimeError("Install twofish==0.3.0; supply its directory with --deps")
            lib = ctypes.CDLL(spec.origin)
            lib.exp_Twofish_initialise.argtypes = []
            lib.exp_Twofish_initialise.restype = None
            lib.exp_Twofish_prepare_key.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(self.Key)]
            lib.exp_Twofish_prepare_key.restype = None
            lib.exp_Twofish_encrypt.argtypes = [ctypes.POINTER(self.Key), ctypes.c_char_p, ctypes.c_void_p]
            lib.exp_Twofish_encrypt.restype = None
            lib.exp_Twofish_initialise()
            Twofish._lib = lib
        self.key = self.Key()
        self._lib.exp_Twofish_prepare_key(key, len(key), ctypes.byref(self.key))

    def encrypt(self, block):
        require(len(block) == 16, "expected 16-byte block")
        output = ctypes.create_string_buffer(16)
        self._lib.exp_Twofish_encrypt(ctypes.byref(self.key), block, output)
        return output.raw


def cipher_self_test():
    # Published Twofish 128-bit all-zero known-answer vector.
    require(Twofish(bytes(16)).encrypt(bytes(16)).hex() == "9f589f5cf6122c32b6bfec2f2ae8c35a",
            "Twofish known-answer test failed")


def xor(a, b):
    require(len(a) == len(b), "XOR length mismatch")
    return bytes(x ^ y for x, y in zip(a, b))


def mgf(seed, size):
    return b"".join(hashlib.sha256(seed + i.to_bytes(4, "big")).digest()
                    for i in range((size + 31) // 32))[:size]


def unwrap_key(ciphertext, key):
    # Native custom_rsa_decrypt_key_ex uses the PUBLIC exponent then OAEP/SHA256.
    require(len(ciphertext) == 128 and key.size_in_bits() == 1024, "RSA block size mismatch")
    number = int.from_bytes(ciphertext, "big")
    require(number < key.n, "RSA value exceeds modulus")
    message = pow(number, key.e, key.n).to_bytes(128, "big")
    require(message[0] == 0, "invalid OAEP prefix")
    seed = xor(message[1:33], mgf(message[33:], 32))
    db = xor(message[33:], mgf(seed, 95))
    require(db[:32] == hashlib.sha256(b"").digest(), "OAEP label hash mismatch")
    payload = db[32:].lstrip(b"\0")
    require(len(payload) == 17 and payload[0] == 1, "invalid OAEP key padding/length")
    return payload[1:]


def ctr(data, key, iv):
    require(len(iv) == 16, "invalid CTR IV")
    cipher = Twofish(key)
    counter = int.from_bytes(iv, "little")
    result = bytearray()
    for start in range(0, len(data), 16):
        chunk = data[start:start + 16]
        result += xor(chunk, cipher.encrypt(counter.to_bytes(16, "little"))[:len(chunk)])
        counter = (counter + 1) % (1 << 128)
    return bytes(result)


def normal_name(name):
    name = name.replace("\\", "/").lower()
    require(not name.startswith("/") and ":" not in name and
            all(part not in ("", ".", "..") for part in name.split("/")), "unsafe entry name")
    return name


class Pak:
    def __init__(self, path, key):
        self.path = Path(path)
        length = self.path.stat().st_size
        with self.path.open("rb") as stream:
            tail = read_at(stream, max(0, length - 65557), min(length, 65557))
            at = tail.rfind(b"PK\x05\x06")
            require(at >= 0 and at + 22 <= len(tail), "missing ZIP end record")
            _, disk, cdisk, count, total, size, offset, comment = struct.unpack_from("<4s4H2IH", tail, at)
            require(disk == cdisk == 0 and count == total, "unsupported multivolume archive")
            require(at + 22 + comment == len(tail), "invalid ZIP comment extent")
            require(0 < size <= MAX_ENTRY and offset + size == length - len(tail) + at,
                    "invalid central-directory extent")
            header = tail[at + 22:]
            require(len(header) == 2320 and struct.unpack_from("<IHH", header) == (8, 3, 1),
                    "unsupported PAK encryption/signature header")
            require(struct.unpack_from("<I", header, 8)[0] == 132, "invalid signature header size")
            enc = header[140:]
            require(struct.unpack_from("<I", enc)[0] == 2180, "invalid encryption header size")
            iv = unwrap_key(enc[4:132], key)
            self.keys = [unwrap_key(enc[132 + i * 128:260 + i * 128], key) for i in range(16)]
            directory = ctr(read_at(stream, offset, size), self.keys[0], iv)
        self.directory_sha256 = hashlib.sha256(directory).hexdigest()
        self.entries = {}
        self.directory_offset = offset
        pos = 0
        for _ in range(count):
            require(pos + 46 <= size, "truncated central-directory entry")
            fields = struct.unpack_from("<4s6H3I5H2I", directory, pos)
            signature, _, need, flags, method, _, _, crc, cs, us, nl, el, cl, disk, _, _, local = fields
            require(signature == b"PK\x01\x02" and need <= 20 and disk == 0,
                    "invalid/unsupported directory entry")
            require(pos + 46 + nl + el + cl <= size and nl > 0, "invalid entry name/extra extent")
            raw_name = directory[pos + 46:pos + 46 + nl].decode("utf-8")
            # Ignore directory records, while retaining a complete entry-count check.
            if not raw_name.endswith(("/", "\\")):
                name = normal_name(raw_name)
                require(name not in self.entries, "duplicate archive entry")
                self.entries[name] = dict(name=name, method=method, crc32=crc, compressed_size=cs,
                                          size=us, offset=local, name_bytes=nl, flags=flags)
            pos += 46 + nl + el + cl
        require(pos == size, "directory length/count mismatch")
        self.entry_count = count

    def read(self, name):
        entry = self.entries[normal_name(name)]
        cs, size, crc, method = (entry[k] for k in ("compressed_size", "size", "crc32", "method"))
        require(method in (13, 14) and 0 < size <= MAX_ENTRY and 0 < cs <= MAX_ENTRY,
                "unsupported method or unbounded entry")
        # With encrypted headers the native reader derives this from CDR metadata,
        # not from the encrypted local-header length fields. CRC is checked below.
        start = entry["offset"] + 30 + entry["name_bytes"]
        require(start + cs <= self.directory_offset, "entry overlaps archive directory")
        iv = struct.pack("<4I", (size ^ (cs << 12)) & 0xffffffff, int(cs == 0),
                         ((cs << 12) ^ crc) & 0xffffffff, cs ^ int(size == 0))
        with self.path.open("rb") as stream:
            payload = ctr(read_at(stream, start, cs), self.keys[(~(crc >> 2)) & 15], iv)
        if method == 14:
            inflater = zlib.decompressobj(-15)
            payload = inflater.decompress(payload, size + 1)
            require(inflater.eof and not inflater.unused_data and not inflater.unconsumed_tail,
                    "invalid deflate stream or declared size")
        require(len(payload) == size and zlib.crc32(payload) == crc, "entry size/CRC mismatch")
        return payload


def decode_xml(data):
    require(data[:8] == b"CryXmlB\0" and len(data) >= 44, "expected CryXmlB")
    size, no, nc, ao, ac, co, cc, so, ss = struct.unpack_from("<9I", data, 8)
    require(size == len(data) and 0 < nc <= 100000, "invalid XML size/node count")
    for offset, count, stride in ((no, nc, 28), (ao, ac, 8), (co, cc, 4), (so, ss, 1)):
        require(offset >= 44 and offset + count * stride <= size, "XML table out of bounds")

    def string(index):
        require(index < ss, "XML string offset out of bounds")
        end = data.find(b"\0", so + index, so + ss)
        require(end >= 0, "unterminated XML string")
        return data[so + index:end].decode("utf-8")

    visited = set()

    def node(index, parent, depth):
        require(index < nc and index not in visited and depth < 128, "cyclic/deep/invalid XML node")
        visited.add(index)
        tag, content, na, nch, owner, fa, fc, _ = struct.unpack_from("<IIHHIIII", data, no + index * 28)
        require(owner == parent and fa + na <= ac and fc + nch <= cc, "invalid XML node links")
        element = ET.Element(string(tag))
        for i in range(fa, fa + na):
            k, v = struct.unpack_from("<II", data, ao + i * 8)
            attribute = string(k)
            require(attribute not in element.attrib, "duplicate XML attribute")
            element.set(attribute, string(v))
        element.text = string(content) or None
        for i in range(fc, fc + nch):
            element.append(node(struct.unpack_from("<I", data, co + i * 4)[0], index, depth + 1))
        return element

    root = node(0, 0xffffffff, 0)
    require(len(visited) == nc, "unreachable XML nodes")
    return root


def extract(game, output):
    from Crypto.PublicKey import RSA
    game, output = Path(game).resolve(), Path(output).resolve()
    require(game != output and game not in output.parents, "output must be outside the game installation")
    dll = game / "Binaries/Danielle/x64/Release/PreyDll.dll"
    require(sha256_file(dll) == TARGET_SHA256, "unsupported game DLL; extraction refused")
    with dll.open("rb") as stream:
        key = RSA.import_key(read_at(stream, KEY_FILE_OFFSET, KEY_BYTES))
    cipher_self_test()
    report = dict(target_sha256=TARGET_SHA256, environment="offline", archives=[], assets={},
                  verification="OAEP padding, directory bounds, decompression, per-entry size and CRC; not signature authentication")
    # Base then native patch override. Both are required for this supported installation.
    for relative in ("GameSDK/GameData.pak", "GameSDK/Precache/patch.pak"):
        path = game / relative
        pak = Pak(path, key)
        report["archives"].append(dict(path=relative, sha256=sha256_file(path), entries=pak.entry_count,
                                       directory_sha256=pak.directory_sha256))
        for name in HUD_FILES:
            if name not in pak.entries:
                continue
            payload = pak.read(name)
            if name.endswith(".gfx"):
                require(payload[:3] == b"GFX" and struct.unpack_from("<I", payload, 4)[0] == len(payload),
                        "unexpected HUD movie format/length")
            else:
                decoded = decode_xml(payload)
                require(decoded.tag == "UIElements" and decoded.get("name") == "DanielleHUD", "wrong UI definition")
            destination = output / Path(relative).stem / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(payload)
            if name.endswith(".xml"):
                ET.indent(decoded)
                ET.ElementTree(decoded).write(destination.with_suffix(".decoded.xml"), encoding="utf-8", xml_declaration=True)
            report["assets"].setdefault(name, []).append(dict(archive=relative, size=len(payload),
                crc32=f"{pak.entries[name]['crc32']:08x}", sha256=hashlib.sha256(payload).hexdigest(),
                output=str(destination.relative_to(output))))
    require(set(report["assets"]) == set(HUD_FILES), "missing HUD assets")
    report["effective"] = {name: versions[-1] for name, versions in report["assets"].items()}
    (output / "extraction.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--deps", type=Path, help="directory containing the installed twofish/PyCryptodome packages")
    args = parser.parse_args()
    if args.deps:
        sys.path.insert(0, str(args.deps.resolve()))
    try:
        report = extract(args.game, args.out)
        print(json.dumps(report, indent=2))
    except (ValueError, RuntimeError, OSError, struct.error, zlib.error) as error:
        parser.exit(1, f"Extraction refused: {error}\n")
