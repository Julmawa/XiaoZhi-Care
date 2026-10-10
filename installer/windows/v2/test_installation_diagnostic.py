import struct
import unittest
import zlib

from installation_diagnostic import HardwareEvidence, diagnose
from compatibility import Status

SIZE = 0x1000000
DIGEST = bytes(range(32))
DIGEST_HEX = DIGEST.hex()
CONFIG = 'a' * 64


def fixture(ota=True, project='xiaozhi', digest=DIGEST):
    flash = bytearray(b'\xff' * SIZE)
    entry = bytearray(32)
    entry[:2] = b'\xaa\x50'
    entry[2] = 0
    entry[3] = 0x10
    struct.pack_into('<II', entry, 4, 0x20000, 0x3F0000)
    entry[12:18] = b'ota_0\0'
    flash[0x8000:0x8020] = entry
    flash[0x8020:0x8022] = b'\xff\xff'
    if ota:
        sequence = 1
        struct.pack_into('<I', flash, 0xD000, sequence)
        struct.pack_into('<I', flash, 0xD000 + 24, 2)  # VALID
        struct.pack_into('<I', flash, 0xD000 + 28, zlib.crc32(struct.pack('<I', sequence)))
    offset = 0x20000
    flash[offset] = 0xE9
    flash[offset+1] = 1
    struct.pack_into('<II', flash, offset+24, 0x3C000020, 768)
    desc = offset+32
    struct.pack_into('<I', flash, desc, 0xABCD5432)
    flash[desc+16:desc+48] = b'2.5.1'.ljust(32, b'\0')
    flash[desc+48:desc+80] = project.encode().ljust(32, b'\0')
    flash[desc+112:desc+144] = b'6.0.2'.ljust(32, b'\0')
    flash[desc+144:desc+176] = digest
    return bytes(flash)


GOOD_HW = HardwareEvidence('ESP32-S3', 16, 8, True, True, True)
BUILD = {DIGEST_HEX: {'xiaozhi_version': '2.5.1', 'board_sku': 'bread-compact-wifi-lcd', 'config_sha256': CONFIG}}
CAT = {CONFIG: {'chip': 'ESP32-S3', 'xiaozhi_version': '2.5.1', 'board_sku': 'bread-compact-wifi-lcd', 'package_id': 'care-test'}}


class DiagnosticTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.good_flash = fixture()

    def test_exact_registered_candidate_never_authorizes_flash(self):
        result = diagnose(self.good_flash, GOOD_HW, BUILD, CAT)
        self.assertEqual(result.status, Status.COMPATIBLE)
        self.assertEqual(result.package_id, 'care-test')
        self.assertFalse(result.flash_authorized)

    def test_no_catalog_pending(self):
        self.assertEqual(diagnose(self.good_flash, GOOD_HW, BUILD, {}).status, Status.PENDING)

    def test_unknown_elf_pending(self):
        self.assertEqual(diagnose(self.good_flash, GOOD_HW, {}, CAT).status, Status.PENDING)

    def test_missing_ota_pending(self):
        self.assertEqual(diagnose(fixture(ota=False), GOOD_HW, BUILD, CAT).status, Status.PENDING)

    def test_unknown_security_pending(self):
        hw = HardwareEvidence('ESP32-S3', 16, 8, False, True, True)
        self.assertEqual(diagnose(self.good_flash, hw, BUILD, CAT).status, Status.PENDING)

    def test_incompatible_chip_rejected(self):
        hw = HardwareEvidence('ESP32-S2', 16, 8, True, True, True)
        self.assertEqual(diagnose(self.good_flash, hw, BUILD, CAT).status, Status.INCOMPATIBLE)

    def test_non_xiaozhi_rejected(self):
        self.assertEqual(diagnose(fixture(project='other'), GOOD_HW, BUILD, CAT).status, Status.INCOMPATIBLE)

    def test_corrupt_flash_rejected(self):
        self.assertEqual(diagnose(b'\xff' * 100, GOOD_HW, BUILD, CAT).status, Status.INCOMPATIBLE)

    def test_registered_version_mismatch_pending(self):
        wrong = {DIGEST_HEX: {'xiaozhi_version': '2.5.0', 'board_sku': 'bread-compact-wifi-lcd', 'config_sha256': CONFIG}}
        self.assertEqual(diagnose(self.good_flash, GOOD_HW, wrong, CAT).status, Status.PENDING)


if __name__ == '__main__':
    unittest.main(verbosity=2)
