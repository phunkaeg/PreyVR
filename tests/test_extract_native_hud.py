"""Offline parser/crypto tests. Synthetic archives only; no game execution."""
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/re"))
sys.path.insert(0, str(ROOT / "build/re-tools"))
import extract_native_hud as hud
from Crypto.PublicKey import RSA


def wrap_key(value, key):
    seed = bytes(range(32))
    db = hud.hashlib.sha256(b"").digest() + bytes(46) + b"\1" + value
    masked = hud.xor(db, hud.mgf(seed, 95))
    message = b"\0" + hud.xor(seed, hud.mgf(masked, 32)) + masked
    return pow(int.from_bytes(message, "big"), key.d, key.n).to_bytes(128, "big")


def archive(key, payload=b"verified HUD fixture", method=14, name=b"libs/ui/test.gfx", crc_override=None):
    iv, keys = bytes(range(16)), [bytes([i]) * 16 for i in range(16)]
    crc = zlib.crc32(payload) if crc_override is None else crc_override
    size = len(payload)
    if method == 14:
        compressor = zlib.compressobj(wbits=-15)
        packed = compressor.compress(payload) + compressor.flush()
    else:
        packed = payload
    cs = len(packed)
    file_iv = struct.pack("<4I", (size ^ (cs << 12)) & 0xffffffff, 0,
                          ((cs << 12) ^ crc) & 0xffffffff, cs)
    body = bytes(30 + len(name)) + hud.ctr(packed, keys[(~(crc >> 2)) & 15], file_iv)
    directory = struct.pack("<4s6H3I5H2I", b"PK\1\2", 20, 20, 0, method, 0, 0,
                            crc, cs, size, len(name), 0, 0, 0, 0, 0, 0) + name
    encrypted = hud.ctr(directory, keys[0], iv)
    header = struct.pack("<IHHI", 8, 3, 1, 132) + bytes(128)
    header += struct.pack("<I", 2180) + wrap_key(iv, key) + b"".join(wrap_key(k, key) for k in keys)
    return body + encrypted + struct.pack("<4s4H2IH", b"PK\5\6", 0, 0, 1, 1,
                                          len(directory), len(body), len(header)) + header


def xml_fixture():
    strings = b"UIElements\0\0name\0DanielleHUD\0"
    node = struct.pack("<IIHHIIII", 0, 11, 1, 0, 0xffffffff, 0, 0, 0)
    attr = struct.pack("<II", 12, 17)
    return b"CryXmlB\0" + struct.pack("<9I", 80 + len(strings), 44, 1, 72, 1, 80, 0, 80, len(strings)) + node + attr + strings


class ExtractionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.key = RSA.generate(1024)

    def test_twofish_known_answer(self):
        hud.cipher_self_test()

    def test_oaep_roundtrip_and_corruption(self):
        wrapped = wrap_key(bytes(range(16)), self.key)
        self.assertEqual(hud.unwrap_key(wrapped, self.key.public_key()), bytes(range(16)))
        bad = bytearray(wrapped)
        bad[19] ^= 0x80
        with self.assertRaises(ValueError):
            hud.unwrap_key(bytes(bad), self.key.public_key())

    def test_ctr_endianness_and_partial_block(self):
        key, iv = bytes(16), bytes([255, 0]) + bytes(14)
        stream = hud.Twofish(key)
        expected = stream.encrypt(iv) + stream.encrypt(bytes([0, 1]) + bytes(14))[:3]
        self.assertEqual(hud.ctr(bytes(19), key, iv), expected)

    def test_stored_and_deflated_entries(self):
        with tempfile.TemporaryDirectory() as folder:
            for method in (13, 14):
                p = Path(folder) / "test.pak"
                p.write_bytes(archive(self.key, method=method))
                pak = hud.Pak(p, self.key.public_key())
                self.assertEqual(pak.entry_count, 1)
                self.assertEqual(pak.read("LIBS/UI/TEST.GFX"), b"verified HUD fixture")

    def test_wrong_crc_and_truncated_archive_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder) / "test.pak"
            p.write_bytes(archive(self.key, crc_override=0))
            with self.assertRaisesRegex(ValueError, "CRC"):
                hud.Pak(p, self.key.public_key()).read("libs/ui/test.gfx")
            p.write_bytes(p.read_bytes()[:-1])
            with self.assertRaises(ValueError):
                hud.Pak(p, self.key.public_key())

    def test_untrusted_paths_refused(self):
        for name in ("../escape", "a/../escape", "/root", "C:/file", "a//file"):
            with self.assertRaises(ValueError):
                hud.normal_name(name)

    def test_xml_valid_and_corrupt_bounds(self):
        original = xml_fixture()
        root = hud.decode_xml(original)
        self.assertEqual((root.tag, root.get("name")), ("UIElements", "DanielleHUD"))
        for offset, value in ((12, 0xffffffff), (16, 0), (36, len(original)), (44, 999)):
            damaged = bytearray(original)
            struct.pack_into("<I", damaged, offset, value)
            with self.assertRaises(ValueError):
                hud.decode_xml(damaged)

    def test_xml_cycle_refused(self):
        # The root claims itself as its only child, with all extents still valid.
        data = bytearray(xml_fixture())
        string_start = 84
        data[80:80] = struct.pack("<I", 0)
        struct.pack_into("<I", data, 8, len(data))
        struct.pack_into("<I", data, 32, 1)
        struct.pack_into("<I", data, 36, string_start)
        struct.pack_into("<H", data, 54, 1)
        with self.assertRaisesRegex(ValueError, "cyclic"):
            hud.decode_xml(data)

    def test_read_bounds_and_unsupported_dll(self):
        with self.assertRaises(ValueError):
            hud.read_at(io.BytesIO(b"123"), 1, 3)
        with tempfile.TemporaryDirectory() as folder:
            game = Path(folder) / "game"
            dll = game / "Binaries/Danielle/x64/Release/PreyDll.dll"
            dll.parent.mkdir(parents=True)
            dll.write_bytes(b"unsupported")
            out = Path(folder) / "out"
            with self.assertRaisesRegex(ValueError, "unsupported game DLL"):
                hud.extract(game, out)
            self.assertFalse(out.exists())
            with self.assertRaisesRegex(ValueError, "outside the game"):
                hud.extract(game, game / "out")


if __name__ == "__main__":
    unittest.main()
