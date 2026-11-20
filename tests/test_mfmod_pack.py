#!/usr/bin/env python3
from pathlib import Path
import tempfile
import unittest
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.validate_mfmod import parse_mod


class ModPackTests(unittest.TestCase):
    def test_demo_and_automatic_core_packs_are_not_shipped(self):
        self.assertFalse((ROOT / "mods" / "Minefade_Ultimate_Core_0.1.mfmod").exists())
        self.assertFalse((ROOT / "app/src/main/assets/mods/minefade_ultimate_core.mfmod").exists())

    def test_unknown_setting_is_rejected(self):
        self.assert_invalid(
            "id=test_mod\nname=Test\nversion=1.0\nenabled=false\nshader=ultra\n",
            "no soportada",
        )

    def test_duplicate_key_is_rejected(self):
        self.assert_invalid(
            "id=test_mod\nname=Test\nversion=1.0\nenabled=false\nenabled=true\n",
            "duplicada",
        )

    def test_out_of_range_setting_is_rejected(self):
        self.assert_invalid(
            "id=test_mod\nname=Test\nversion=1.0\nenabled=true\nplayer.speed=9.0\n",
            "fuera de rango",
        )

    def test_new_entity_spawn_types_are_supported(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "creatures.mfmod"
            path.write_text(
                "id=creature_mod\nname=Creatures\nversion=1.0\nenabled=true\n"
                "spawn.rabbit=2\nspawn.zombie=1\nspawn.skeleton=1\n",
                encoding="utf-8",
            )
            values = parse_mod(path)
            self.assertEqual(values["spawn.rabbit"], "2")
            self.assertEqual(values["spawn.zombie"], "1")
            self.assertEqual(values["spawn.skeleton"], "1")

    def test_new_gameplay_tuning_settings_are_supported(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "advanced.mfmod"
            path.write_text(
                "id=advanced_mod\nname=Advanced\nversion=1.0\nenabled=true\n"
                "player.jump=1.2\nentity.speed=1.4\n",
                encoding="utf-8",
            )
            values = parse_mod(path)
            self.assertEqual(values["player.jump"], "1.2")
            self.assertEqual(values["entity.speed"], "1.4")

    def test_entity_speed_out_of_range_is_rejected(self):
        self.assert_invalid(
            "id=test_mod\nname=Test\nversion=1.0\nenabled=true\nentity.speed=4.0\n",
            "fuera de rango",
        )

    def test_jump_out_of_range_is_rejected(self):
        self.assert_invalid(
            "id=test_mod\nname=Test\nversion=1.0\nenabled=true\nplayer.jump=0.1\n",
            "fuera de rango",
        )


    def test_phase_2_3_gameplay_settings_are_supported(self):
        content = (
            "id=expanded_mod\nname=Expanded\nversion=1.0\nenabled=true\n"
            "player.crouch_speed=0.52\nentity.passive_speed=1.1\n"
            "entity.hostile_speed=1.2\nentity.flee_radius=7\n"
            "entity.alert_radius=16\nentity.spawn_radius=3.6\n"
            "world.entity_cap=96\nworld.spawn_interval=90\n"
            "world.spawn_distance=20\nworld.passive_cap=36\n"
            "drops.pickup_radius=1.85\ndrops.lifetime=360\n"
            "drops.quantity=2\nmining.speed=1.25\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "expanded.mfmod"
            path.write_text(content, encoding="utf-8")
            values = parse_mod(path)
            self.assertEqual(values["entity.alert_radius"], "16")
            self.assertEqual(values["world.entity_cap"], "96")
            self.assertEqual(values["mining.speed"], "1.25")

    def test_phase_2_3_settings_reject_out_of_range_values(self):
        cases = [
            ("player.crouch_speed=1.5", "fuera de rango"),
            ("entity.flee_radius=1", "fuera de rango"),
            ("entity.alert_radius=40", "fuera de rango"),
            ("drops.lifetime=10", "fuera de rango"),
            ("mining.speed=4", "fuera de rango"),
            ("world.entity_cap=129", "fuera de rango"),
            ("world.spawn_interval=10", "fuera de rango"),
            ("world.spawn_distance=50", "fuera de rango"),
            ("world.passive_cap=65", "fuera de rango"),
            ("drops.quantity=1.5", "entero"),
        ]
        for setting, expected in cases:
            self.assert_invalid(
                "id=bad_mod\nname=Bad\nversion=1.0\nenabled=true\n"
                + setting + "\n",
                expected,
            )

    def assert_invalid(self, content, message):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "invalid.mfmod"
            path.write_text(content, encoding="utf-8")
            with self.assertRaisesRegex(ValueError, message):
                parse_mod(path)


if __name__ == "__main__":
    unittest.main()
