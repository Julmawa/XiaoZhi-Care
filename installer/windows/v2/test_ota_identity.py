"""Pruebas offline del lector conservador de otadata."""
import struct
import unittest
import zlib
from ota_identity import analyze_otadata, analyze_flash


def fixture(a=None, b=None):
    data = bytearray(b'\xff' * 0x2000)
    for i, entry in enumerate((a, b)):
        if entry is None:
            continue
        seq, state, corrupt = entry
        pos = i * 0x1000
        crc = zlib.crc32(struct.pack('<I', seq)) & 0xffffffff
        if corrupt:
            crc ^= 1
        struct.pack_into('<I', data, pos, seq)
        struct.pack_into('<I', data, pos + 24, state)
        struct.pack_into('<I', data, pos + 28, crc)
    return bytes(data)


class OtaTests(unittest.TestCase):
    def test_blank_unknown(self):
        self.assertEqual(analyze_otadata(fixture()).status, 'unknown')

    def test_valid_ota0(self):
        r = analyze_otadata(fixture((1, 2, False)))
        self.assertEqual((r.status, r.selected_slot), ('selected', 'ota_0'))

    def test_latest_ota1(self):
        r = analyze_otadata(fixture((1, 2, False), (2, 2, False)))
        self.assertEqual((r.status, r.selected_slot), ('selected', 'ota_1'))

    def test_corrupt_crc_ignored(self):
        r = analyze_otadata(fixture((1, 2, False), (2, 2, True)))
        self.assertEqual((r.status, r.selected_slot), ('selected', 'ota_0'))

    def test_invalid_aborted_ignored(self):
        for state in (3, 4):
            with self.subTest(state=state):
                r = analyze_otadata(fixture((1, 2, False), (2, state, False)))
                self.assertEqual((r.status, r.selected_slot), ('selected', 'ota_0'))

    def test_new_pending(self):
        for state in (0, 1):
            with self.subTest(state=state):
                self.assertEqual(analyze_otadata(fixture((2, state, False))).status, 'pending')

    def test_tie_ambiguous(self):
        self.assertEqual(analyze_otadata(fixture((1, 2, False), (1, 2, False))).status, 'pending')

    def test_sequence_slot_wrap(self):
        self.assertEqual(analyze_otadata(fixture((4, 2, False))).selected_slot, 'ota_1')

    def test_reject_wrong_size_and_slots(self):
        with self.assertRaises(ValueError): analyze_otadata(b'1234')
        with self.assertRaises(ValueError): analyze_otadata(fixture(), ota_slots=3)

    def test_from_full_flash(self):
        flash = bytearray(b'\xff' * 0x1000000)
        flash[0xD000:0xF000] = fixture((1, 2, False))
        self.assertEqual(analyze_flash(bytes(flash)).selected_slot, 'ota_0')


if __name__ == '__main__':
    unittest.main(verbosity=2)
