import json
import tempfile
import unittest
from pathlib import Path
from graphic_profiles import choose, prepare_selection, may_install


class GraphicProfileTests(unittest.TestCase):
    def test_accessible_default(self):
        p = choose()
        self.assertEqual((p.font_px, p.emoji_px, p.theme), (30, 64, 'oscuro'))
        self.assertEqual((p.lcd_width, p.lcd_height), (240, 240))

    def test_supported_preferences(self):
        self.assertEqual(choose(22, 32, 'claro').theme, 'claro')

    def test_reject_unsupported(self):
        for args in [(36, 64, 'oscuro'), (30, 128, 'oscuro'), (30, 64, 'auto')]:
            with self.subTest(args=args), self.assertRaises(ValueError):
                choose(*args)

    def test_selection_never_authorizes_flash(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / 'selection.json'
            prepare_selection(out, choose())
            record = json.loads(out.read_text(encoding='utf-8'))
            self.assertIs(record['flash_authorized'], False)
            self.assertEqual(record['selection']['resource_status'], 'pendiente_verificacion')

    def test_no_package_is_never_installable(self):
        p = choose()
        self.assertFalse(may_install(p, resources_authenticated=True,
                                     complete_resources=True,
                                     available_partition_bytes=6291456, package_bytes=4092764,
                                     target_chip='ESP32-S3', target_board=p.board_sku,
                                     target_resolution=(240, 240)))


if __name__ == '__main__':
    unittest.main()
