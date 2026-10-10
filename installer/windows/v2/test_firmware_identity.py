"""Offline tests for V2 firmware identity parser. No hardware access."""
import struct
import unittest
from firmware_identity import FLASH_SIZE, inspect_flash, read_app


def fake_flash(elf=bytes(range(32)), version='2.5.1'):
    mem = bytearray(b'\xff' * FLASH_SIZE)
    part = bytearray(32)
    part[:2] = b'\xaa\x50'
    part[2:4] = bytes([0, 0x10])
    struct.pack_into('<II', part, 4, 0x20000, 0x3F0000)
    part[12:28] = b'ota_0\0'.ljust(16, b'\0')
    mem[0x8000:0x8020] = part
    # Synthetic image with one data segment containing the app descriptor.
    off = 0x20000
    mem[off] = 0xE9
    mem[off + 1] = 1
    struct.pack_into('<II', mem, off + 24, 0x3C000020, 256)
    desc = off + 32
    struct.pack_into('<I', mem, desc, 0xABCD5432)
    mem[desc + 16:desc + 48] = version.encode().ljust(32, b'\0')
    mem[desc + 48:desc + 80] = b'xiaozhi'.ljust(32, b'\0')
    mem[desc + 112:desc + 144] = b'v6.0.2'.ljust(32, b'\0')
    mem[desc + 144:desc + 176] = elf
    return mem


class FirmwareIdentityTests(unittest.TestCase):
    def test_reads_elf_and_version(self):
        inv = inspect_flash(fake_flash())
        self.assertEqual(len(inv.applications), 1)
        app = inv.applications[0]
        self.assertEqual(app.version, '2.5.1')
        self.assertEqual(app.project, 'xiaozhi')
        self.assertEqual(app.idf_version, 'v6.0.2')
        self.assertEqual(app.app_elf_sha256, bytes(range(32)).hex())

    def test_does_not_guess_active_ota(self):
        self.assertIsNone(inspect_flash(fake_flash()).running_partition)

    def test_rejects_wrong_size(self):
        with self.assertRaises(ValueError):
            inspect_flash(b'bad')

    def test_does_not_claim_missing_digest(self):
        inv = inspect_flash(fake_flash(bytes(32)))
        self.assertIsNone(inv.applications[0].app_elf_sha256)

    def test_rejects_bad_magic(self):
        mem = fake_flash()
        mem[0x20000 + 32] = 0
        self.assertEqual(inspect_flash(mem).applications, ())

    def test_rejects_broken_segment(self):
        mem = fake_flash()
        struct.pack_into('<I', mem, 0x20000 + 24 + 4, 0x400000)
        self.assertEqual(inspect_flash(mem).applications, ())


if __name__ == '__main__':
    unittest.main(verbosity=2)
