import json
import tempfile
import unittest
from pathlib import Path
from build_catalog import CatalogError, load_catalog, parse_catalog

A = 'a' * 64
B = 'b' * 64


def document():
    return {
        'schema': 'xiaozhi-care-build-catalog-v2', 'schema_version': 1,
        'builds': [{'app_elf_sha256': A, 'config_sha256': B,
                    'xiaozhi_version': '2.5.1', 'board_sku': 'bread-compact-wifi-lcd'}],
        'packages': [{'config_sha256': B, 'xiaozhi_version': '2.5.1',
                      'board_sku': 'bread-compact-wifi-lcd', 'chip': 'ESP32-S3',
                      'package_id': 'care-test-only'}],
    }


class CatalogTests(unittest.TestCase):
    def test_valid_structure_never_claims_authenticity(self):
        c = parse_catalog(document())
        self.assertFalse(c.authenticated)
        self.assertEqual(c.build_registry[A]['config_sha256'], B)
        self.assertEqual(c.package_catalog[B]['package_id'], 'care-test-only')

    def test_no_packages_allowed_as_pending(self):
        d = document(); d['packages'] = []
        self.assertEqual(parse_catalog(d).package_catalog, {})

    def test_unknown_schema_rejected(self):
        d = document(); d['schema_version'] = 2
        with self.assertRaises(CatalogError): parse_catalog(d)

    def test_bad_sha_rejected(self):
        d = document(); d['builds'][0]['app_elf_sha256'] = 'bad'
        with self.assertRaises(CatalogError): parse_catalog(d)

    def test_duplicate_elf_rejected(self):
        d = document(); d['builds'].append(d['builds'][0].copy())
        with self.assertRaises(CatalogError): parse_catalog(d)

    def test_duplicate_config_rejected(self):
        d = document(); d['packages'].append(d['packages'][0].copy())
        with self.assertRaises(CatalogError): parse_catalog(d)

    def test_version_disagreement_rejected(self):
        d = document(); d['packages'][0]['xiaozhi_version'] = '2.5.0'
        with self.assertRaises(CatalogError): parse_catalog(d)

    def test_other_chip_rejected(self):
        d = document(); d['packages'][0]['chip'] = 'ESP32-S2'
        with self.assertRaises(CatalogError): parse_catalog(d)

    def test_load_local_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / 'catalog.json'
            p.write_text(json.dumps(document()), encoding='utf-8')
            self.assertEqual(len(load_catalog(p).build_registry), 1)


if __name__ == '__main__':
    unittest.main(verbosity=2)
