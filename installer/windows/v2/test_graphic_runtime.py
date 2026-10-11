"""Exercise the shipped Windows PowerShell path without any attached hardware."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from graphic_assets import THEME_KEY, build_catalog, index_of, read_pack, sha, write_pack
from test_graphic_assets import fixture


@unittest.skipUnless(shutil.which('powershell.exe'), 'Requires Windows PowerShell')
class GraphicRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)
        (self.root / 'firmware').mkdir()
        runtime = Path(__file__).resolve().parents[1] / 'runtime'
        for name in ('installer.ps1', 'graphics.ps1', 'i18n.ps1'):
            shutil.copyfile(runtime / name, self.root / name)
        self.base = write_pack(fixture())
        source = write_pack(fixture(False))
        app = b'test only ' + THEME_KEY.encode() + b'\0'
        (self.root / 'firmware' / 'generated_assets.bin').write_bytes(self.base)
        (self.root / 'firmware' / 'xiaozhi.bin').write_bytes(app)
        (self.root / 'source.bin').write_bytes(source)
        pins = {'source_sha256': sha(source), 'font_sha256': sha(b'F' * 128),
                'source_url': 'https://xiaozhi.me/tools/assets-generator/'}
        self.catalog = build_catalog(self.root / 'firmware' / 'generated_assets.bin', self.root / 'source.bin',
                                     self.root / 'firmware' / 'xiaozhi.bin', self.root / 'graphics', pins)
        self.release = {'supported_board_sku': 'bread-compact-wifi-lcd',
                        'graphics': {'catalog': 'graphics/graphics.json'},
                        'firmware': [{'file': 'generated_assets.bin', 'sha256': sha(self.base)},
                                     {'file': 'xiaozhi.bin', 'sha256': sha(app)}]}
        self.save_catalog()

    def save_catalog(self):
        path = self.root / 'graphics' / 'graphics.json'
        path.write_text(json.dumps(self.catalog), encoding='utf-8')
        self.release['graphics']['sha256'] = sha(path.read_bytes())
        (self.root / 'release.json').write_text(json.dumps(self.release), encoding='utf-8')

    def run_preview(self, profile=None, selection=None):
        args = ['powershell.exe', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
                '-File', str(self.root / 'installer.ps1'), '-PreviewGraphics']
        if profile:
            args += ['-GraphicProfileId', profile]
        if selection:
            args += ['-GraphicSelectionPath', str(selection)]
        environment = os.environ.copy()
        # A test runner launched from PowerShell 7 can export its own module path.
        # Match the normal BAT launcher, which lets Windows PowerShell load its defaults.
        environment['PSMODULEPATH'] = str(Path(os.environ['SystemRoot']) / 'System32' / 'WindowsPowerShell' / 'v1.0' / 'Modules')
        return subprocess.run(args, capture_output=True, text=True, errors='replace', timeout=30, env=environment)

    def assert_rejected(self, result, detail):
        self.assertEqual(result.returncode, 41, result.stdout + result.stderr)
        self.assertIn(detail, result.stdout)

    def test_preview_both_themes_succeeds_without_tools_or_device(self):
        for profile in self.catalog['profiles']:
            result = self.run_preview(profile['profile_id'])
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('"flash_performed":  false', result.stdout)
            self.assertIn(profile['profile_id'], result.stdout)
        self.assertFalse((self.root / 'tools').exists())
        self.assertFalse((self.root / 'backup').exists())

    def test_missing_requested_size_never_falls_back(self):
        self.assert_rejected(self.run_preview('fuente-22-emoji-32-oscuro'), 'No existe un paquete completo')

    def test_modified_binary_is_rejected(self):
        path = self.root / 'graphics' / self.catalog['profiles'][0]['file']
        path.write_bytes(path.read_bytes()[:-1] + b'!')
        self.assert_rejected(self.run_preview(), 'Recursos corruptos')

    def test_changed_voice_rejected_even_with_updated_file_hash(self):
        profile = self.catalog['profiles'][0]
        path = self.root / 'graphics' / profile['file']
        entries = read_pack(path.read_bytes())
        name = index_of(entries)['srmodels']
        entries[name] = entries[name][:-1] + b'!'
        data = write_pack(entries)
        path.write_bytes(data)
        profile['sha256'] = sha(data)
        self.save_catalog()
        self.assert_rejected(self.run_preview(), 'Recursos corruptos')

    def test_incompatible_catalog_and_path_traversal_rejected(self):
        self.catalog['board_sku'] = 'another-board'
        self.save_catalog()
        self.assert_rejected(self.run_preview(), 'Catalogo incompatible')
        self.catalog['board_sku'] = 'bread-compact-wifi-lcd'
        self.catalog['profiles'][0]['file'] = '../firmware/generated_assets.bin'
        self.save_catalog()
        self.assert_rejected(self.run_preview(), 'Perfil grafico invalido')

    def test_catalog_change_rejected_without_manifest_update(self):
        path = self.root / 'graphics' / 'graphics.json'
        path.write_text(path.read_text() + ' ', encoding='utf-8')
        self.assert_rejected(self.run_preview(), 'El catalogo grafico cambio')

    def test_stage_one_preferences_are_consumed_but_not_trusted_as_authorization(self):
        profile = self.catalog['profiles'][0]
        selection = {key: profile[key] for key in ('profile_id', 'font_family', 'font_px', 'emoji_family', 'emoji_px', 'theme')}
        selection.update(chip='ESP32-S3', board_sku='bread-compact-wifi-lcd', lcd_width=240, lcd_height=240,
                         resource_status='pendiente_verificacion')
        record = {'schema': 'xiaozhi-care-graphic-selection', 'schema_version': 1,
                  'flash_authorized': False, 'selection': selection}
        path = self.root / 'stage-one.json'
        path.write_text(json.dumps(record), encoding='utf-8')
        result = self.run_preview(selection=path)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        record['flash_authorized'] = True
        path.write_text(json.dumps(record), encoding='utf-8')
        package = self.root / 'graphics' / profile['file']
        package.write_bytes(package.read_bytes()[:-1])
        self.assert_rejected(self.run_preview(selection=path), 'Recursos corruptos')


if __name__ == '__main__':
    unittest.main()
