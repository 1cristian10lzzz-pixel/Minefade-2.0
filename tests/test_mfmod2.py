#!/usr/bin/env python3
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.mfmod2 import create_package, validate_package


VALID_MOD = (
    "id=phase_three_test\n"
    "name=Phase Three Test\n"
    "version=1.0\n"
    "enabled=false\n"
    "player.speed=1.1\n"
)


class ModPackV2Tests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.gameplay = self.root / "gameplay.mfmod"
        self.gameplay.write_text(VALID_MOD, encoding="utf-8")
        self.package = self.root / "test.mfmod2"

    def tearDown(self):
        self.temp.cleanup()

    def test_create_and_validate_v2_package(self):
        result = create_package(self.gameplay, self.package, "1.2.0", "2.2.0", "2.2.99")
        self.assertEqual(result["id"], "phase_three_test")
        self.assertEqual(result["version"], "1.2.0")
        self.assertEqual(validate_package(self.package)["sha256"], result["sha256"])

    def test_changed_gameplay_payload_fails_sha256(self):
        create_package(self.gameplay, self.package, "1.0.0", "2.2.0", "2.2.99")
        changed = VALID_MOD.replace("player.speed=1.1", "player.speed=1.2")
        with zipfile.ZipFile(self.package, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("manifest.json", (
                '{"format":2,"id":"phase_three_test","name":"Phase Three Test",'
                '"version":"1.0.0","engine_min":"2.2.0","engine_max":"2.2.99",'
                '"entry":"data/gameplay.mfmod","sha256":"00000000000000000000000000000000'
                '00000000000000000000000000000000"}'
            ))
            archive.writestr("data/gameplay.mfmod", changed)
        with self.assertRaisesRegex(ValueError, "sha256"):
            validate_package(self.package)

    def test_zip_slip_path_is_rejected(self):
        with zipfile.ZipFile(self.package, "w") as archive:
            archive.writestr("../outside.mfmod", "enabled=true")
            archive.writestr("manifest.json", "{}")
            archive.writestr("data/gameplay.mfmod", VALID_MOD)
        with self.assertRaisesRegex(ValueError, "ruta o entrada"):
            validate_package(self.package)

    def test_executable_or_unknown_resource_is_rejected(self):
        with zipfile.ZipFile(self.package, "w") as archive:
            archive.writestr("manifest.json", "{}")
            archive.writestr("data/gameplay.mfmod", VALID_MOD)
            archive.writestr("payload.so", "native")
        with self.assertRaisesRegex(ValueError, "ruta o entrada"):
            validate_package(self.package)

    def test_oversized_gameplay_is_rejected(self):
        with zipfile.ZipFile(self.package, "w") as archive:
            archive.writestr("manifest.json", "{}")
            archive.writestr("data/gameplay.mfmod", "x" * 32769)
        with self.assertRaisesRegex(ValueError, "límite de tamaño"):
            validate_package(self.package)

    def test_enabled_mod_cannot_be_packaged(self):
        self.gameplay.write_text(VALID_MOD.replace("enabled=false", "enabled=true"), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "desactiva enabled"):
            create_package(self.gameplay, self.package, "1.0.0", "2.2.0", "2.2.99")

    def test_engine_version_must_be_within_manifest_range(self):
        create_package(self.gameplay, self.package, "1.0.0", "2.2.0", "2.2.99")
        with self.assertRaisesRegex(ValueError, "fuera del rango"):
            validate_package(self.package, "3.0.0")

    def test_duplicate_zip_entry_is_rejected(self):
        with zipfile.ZipFile(self.package, "w") as archive:
            archive.writestr("manifest.json", "{}")
            archive.writestr("manifest.json", "{}")
            archive.writestr("data/gameplay.mfmod", VALID_MOD)
        with self.assertRaisesRegex(ValueError, "duplicada"):
            validate_package(self.package)


if __name__ == "__main__":
    unittest.main()
