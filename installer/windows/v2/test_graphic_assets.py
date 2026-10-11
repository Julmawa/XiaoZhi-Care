import copy
import json
from pathlib import Path
import struct
import tempfile
import unittest

from graphic_assets import (EMOTIONS, THEME_KEY, build_catalog, compose, index_of,
                            read_pack, sha, voice_of, write_pack)


def fixture(voice=True):
    index = {'version': 1, 'text_font': 'font.bin',
             'text_font_meta': {'bundle': 'noto-v1'},
             'emoji_collection': [{'name': n, 'file': n + '.gif'} for n in sorted(EMOTIONS)]}
    entries = {'font.bin': b'F' * 128, 'care-extra.bin': b'keep me'}
    for name in EMOTIONS:
        entries[name + '.gif'] = b'GIF89a' + struct.pack('<HH', 64, 64) + b'\0\0\0;'
    if voice:
        index['srmodels'] = 'srmodels.bin'
        entries['srmodels.bin'] = struct.pack('<I', 1) + b'model\0'.ljust(32, b'\0') + struct.pack('<I', 0)
    entries['index.json'] = json.dumps(index).encode()
    return entries


class GraphicAssetsTests(unittest.TestCase):
    def setUp(self):
        self.base = write_pack(fixture())
        self.source = write_pack(fixture(False))

    def test_roundtrip_and_complete_voice(self):
        self.assertEqual(read_pack(self.base), fixture())
        self.assertTrue(voice_of(read_pack(self.base)))

    def test_reject_truncated_corrupt_and_oversized(self):
        for data in (self.base[:-1], self.base + b'X', self.base[:30] + b'X' + self.base[31:], b'X' * (0x600000 + 1)):
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                read_pack(data)

    def test_reject_overlapping_entries_even_with_recomputed_checksum(self):
        data = bytearray(self.base)
        struct.pack_into('<I', data, 12 + 44 + 36, 0)
        struct.pack_into('<I', data, 4, sum(data[12:]) & 65535)
        with self.assertRaises(ValueError):
            read_pack(data)

    def test_reject_duplicate_names(self):
        data = bytearray(self.base)
        data[56:88] = data[12:44]
        struct.pack_into('<I', data, 4, sum(data[12:]) & 65535)
        with self.assertRaises(ValueError):
            read_pack(data)

    def test_graphic_only_is_not_complete(self):
        with self.assertRaises(ValueError):
            compose(self.source, self.source, 'oscuro')

    def test_compose_preserves_voice_and_other_resources(self):
        output = read_pack(compose(self.base, self.source, 'oscuro'))
        self.assertEqual(voice_of(output), voice_of(read_pack(self.base)))
        self.assertEqual(output['care-extra.bin'], b'keep me')
        self.assertNotIn('text_font_meta', index_of(output))
        self.assertEqual(index_of(output)[THEME_KEY], 'dark')
        self.assertEqual(index_of(read_pack(compose(self.base, self.source, 'claro')))[THEME_KEY], 'light')

    def test_missing_or_wrong_resolution_emoji_rejected(self):
        for mode in ('missing', 'wrong_size'):
            source = fixture(False)
            if mode == 'missing':
                del source['happy.gif']
            else:
                source['happy.gif'] = b'GIF89a' + struct.pack('<HH', 32, 32) + b'\0\0\0;'
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                compose(self.base, write_pack(source), 'oscuro')

    def test_unknown_theme_rejected(self):
        with self.assertRaises(ValueError):
            compose(self.base, self.source, 'auto')

    def test_catalog_binds_real_bytes_and_never_claims_hardware_test(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, data in [('base.bin', self.base), ('source.bin', self.source),
                               ('app.bin', THEME_KEY.encode() + b'\0')]:
                (root / name).write_bytes(data)
            pins = {'source_sha256': sha(self.source), 'font_sha256': sha(b'F' * 128),
                    'source_url': 'https://xiaozhi.me/tools/assets-generator/'}
            catalog = build_catalog(root / 'base.bin', root / 'source.bin', root / 'app.bin', root / 'out', pins)
            self.assertFalse(catalog['hardware_tested'])
            self.assertEqual(len(catalog['profiles']), 2)
            for profile in catalog['profiles']:
                content = (root / 'out' / profile['file']).read_bytes()
                self.assertEqual(sha(content), profile['sha256'])
                self.assertEqual(sha(voice_of(read_pack(content))), catalog['srmodels_sha256'])
            with self.assertRaises(ValueError):
                build_catalog(root / 'base.bin', root / 'source.bin', root / 'app.bin', root / 'out', pins)
            (root / 'app.bin').write_bytes(b'old firmware')
            with self.assertRaisesRegex(ValueError, 'Recompilar'):
                build_catalog(root / 'base.bin', root / 'source.bin', root / 'app.bin', root / 'old', pins)
            changed = copy.deepcopy(pins)
            changed['source_sha256'] = '0' * 64
            with self.assertRaisesRegex(ValueError, 'origen'):
                build_catalog(root / 'base.bin', root / 'source.bin', root / 'app.bin', root / 'bad', changed)


if __name__ == '__main__':
    unittest.main()
