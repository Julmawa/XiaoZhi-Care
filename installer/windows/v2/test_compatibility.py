"""Offline tests for the V2 decision engine. No board required."""
import sys
import unittest
from dataclasses import replace
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from compatibility import Device, Status, evaluate

FINGERPRINT = "a" * 64  # Synthetic test value, not a real firmware fingerprint.
DEVICE = Device(
    chip="ESP32-S3", flash_mb=16, psram_mb=8,
    xiaozhi_version="2.5.1", board_sku="bread-compact-wifi-lcd",
    config_sha256=FINGERPRINT, security_verified=True,
    secure_boot_disabled=True, flash_encryption_disabled=True,
)
CATALOG = {FINGERPRINT: {"chip": "ESP32-S3", "xiaozhi_version": "2.5.1",
                         "board_sku": "bread-compact-wifi-lcd", "package_id": "sample-only"}}


class CompatibilityTests(unittest.TestCase):
    def test_exact_match(self):
        result = evaluate(DEVICE, CATALOG)
        self.assertEqual(result.status, Status.COMPATIBLE)
        self.assertEqual(result.package_id, "sample-only")

    def test_s2_rejected(self):
        self.assertEqual(evaluate(replace(DEVICE, chip="ESP32-S2"), CATALOG).status, Status.INCOMPATIBLE)

    def test_wrong_memory_rejected(self):
        self.assertEqual(evaluate(replace(DEVICE, flash_mb=8), CATALOG).status, Status.INCOMPATIBLE)
        self.assertEqual(evaluate(replace(DEVICE, psram_mb=2), CATALOG).status, Status.INCOMPATIBLE)

    def test_unknown_security_pending(self):
        self.assertEqual(evaluate(replace(DEVICE, security_verified=False), CATALOG).status, Status.PENDING)

    def test_security_protections_rejected(self):
        self.assertEqual(evaluate(replace(DEVICE, secure_boot_disabled=False), CATALOG).status, Status.INCOMPATIBLE)
        self.assertEqual(evaluate(replace(DEVICE, flash_encryption_disabled=False), CATALOG).status, Status.INCOMPATIBLE)

    def test_custom_configuration_not_assumed(self):
        self.assertEqual(evaluate(replace(DEVICE, config_sha256=None), CATALOG).status, Status.PENDING)
        self.assertEqual(evaluate(replace(DEVICE, config_sha256="b" * 64), CATALOG).status, Status.PENDING)

    def test_version_and_profile_mismatch_pending(self):
        self.assertEqual(evaluate(replace(DEVICE, xiaozhi_version="2.5.0"), CATALOG).status, Status.PENDING)
        self.assertEqual(evaluate(replace(DEVICE, board_sku="bread-compact-wifi"), CATALOG).status, Status.PENDING)

    def test_empty_catalog_pending(self):
        self.assertEqual(evaluate(DEVICE, {}).status, Status.PENDING)


if __name__ == "__main__":
    unittest.main(verbosity=2)
