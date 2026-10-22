#!/usr/bin/env python3
"""Protect CIA field boundaries and catch makerom's version macro collision."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import sys
sys.dont_write_bytecode = True
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('title_version', ROOT / 'tools/build/title-version.py')
v = importlib.util.module_from_spec(spec)
spec.loader.exec_module(v)


class Versions(unittest.TestCase):
    def test_released_mapping(self):
        for version, counter in [('0.3.1', 3076), ('0.4.1', 4100), ('0.5.0', 5123),
                                 ('0.5.12', 5135), ('1.0.0', 16387), ('3.15.12', 64527)]:
            self.assertEqual(v.encode(version), counter)

    def test_no_alias_or_field_carry(self):
        for version in ['0.5.13', '0.16.0', '4.0.0', '3.16.0', '0.0.1024',
                        '0.05.0', '-1.0.0', '0.5.0-rc4', '0.5']:
            with self.subTest(version=version), self.assertRaises(ValueError):
                v.encode(version)
        # Exhaustively prove every permitted semantic version has its own counter
        # and fills only the intended major and micro fields.
        seen = set()
        for major in range(4):
            for minor in range(16):
                for patch in range(13):
                    counter = v.encode(f'{major}.{minor}.{patch}')
                    self.assertNotIn(counter, seen)
                    seen.add(counter)
                    self.assertEqual(counter >> 10, major * 16 + minor)
                    self.assertEqual(counter & 1023, patch + 3)

    def test_private_counters(self):
        for n in [0, 5136, 5147, 65535]:
            self.assertEqual(v.counter(str(n)), n)
        for n in ['-1', '65536', '5.0', '', '05136']:
            with self.subTest(n=n), self.assertRaises(ValueError):
                v.counter(n)

    def test_real_tmd_offsets(self):
        # Nonaligned section sizes, all supported RSA/ECDSA signature layouts.
        # 5136 is the first current private counter corrupted by stock makerom.
        for signature, size in [(0x10000, 0x240), (0x10001, 0x140), (0x10002, 0x80),
                                (0x10003, 0x240), (0x10004, 0x140), (0x10005, 0x80)]:
            align = lambda n: (n + 63) & ~63
            tmd_at = align(0x2020) + align(23) + align(81)
            data = bytearray(tmd_at + size + 0x9e)
            struct.pack_into('<IHHIII', data, 0, 0x2020, 0, 0, 23, 81, size + 0x9e)
            struct.pack_into('>I', data, tmd_at, signature)
            struct.pack_into('>H', data, tmd_at + size + 0x9c, 5136)
            with tempfile.TemporaryDirectory() as tmp:
                cia = Path(tmp) / 'test.cia'
                cia.write_bytes(data)
                self.assertEqual(v.cia_version(cia), 5136)
                # Truncation and invalid signature cannot silently pass validation.
                cia.write_bytes(data[:-1])
                with self.assertRaises(ValueError):
                    v.cia_version(cia)
                struct.pack_into('>I', data, tmd_at, 0xffffffff)
                cia.write_bytes(data)
                with self.assertRaises(ValueError):
                    v.cia_version(cia)


if __name__ == '__main__':
    unittest.main()
