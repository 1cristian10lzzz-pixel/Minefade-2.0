#!/usr/bin/env python3
from pathlib import Path
import tempfile
import json
import struct
import zlib
import zipfile
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


    def test_adventure_plus_example_file_is_valid(self):
        values = parse_mod(ROOT / "examples" / "Minefade_Aventura_Plus_Fase_1_2_3.mfmod")
        self.assertEqual(values["enabled"], "false")
        self.assertEqual(values["world.spawn_interval"], "90")
        self.assertEqual(values["mining.speed"], "1.25")

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

    @staticmethod
    def make_png(width=16, height=16):
        def chunk(kind, data):
            return (struct.pack(">I", len(data)) + kind + data +
                    struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff))
        header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
        row = b"\x00" + bytes((210, 40, 40, 255)) * width
        raw = row * height
        return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
                chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))

    def write_v2_zip(self, path, extra_files=None, manifest_overrides=None):
        manifest = {
            "format": 2,
            "id": "ruby_pack",
            "name": "Ruby Pack",
            "version": "1.0.0",
            "description": "Adds ruby content",
            "blocks": [{
                "id": "ruby_ore",
                "name": "Mena de rubí",
                "hardness": 3.0,
                "sound": "stone",
                "textures": {
                    "top": "textures/ruby.png",
                    "side": "textures/ruby.png",
                    "bottom": "textures/ruby.png"
                },
                "tool": "pickaxe",
                "stack": 64,
                "light": 0
            }],
            "items": [{
                "id": "ruby",
                "name": "Rubí",
                "texture": "textures/ruby.png",
                "stack": 64
            }],
            "recipes": [{
                "id": "ruby_gem",
                "name": "Pulir rubí",
                "output": {"id": "ruby_pack:ruby", "count": 1},
                "requiresTable": True,
                "ingredients": [{"id": "ruby_pack:ruby_ore", "count": 1}]
            }]
        }
        if manifest_overrides:
            manifest.update(manifest_overrides)
        with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("manifest.json", json.dumps(manifest, ensure_ascii=False))
            archive.writestr("textures/ruby.png", self.make_png())
            for name, content in (extra_files or {}).items():
                archive.writestr(name, content)

    def test_v2_zip_registers_blocks_items_recipes_and_texture_assets(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "ruby.mfmod"
            self.write_v2_zip(path)
            values = parse_mod(path)
            self.assertEqual(values["id"], "ruby_pack")
            self.assertEqual(values["format"], "2")

    def test_v2_zip_rejects_zip_slip_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "unsafe.mfmod"
            self.write_v2_zip(path, {"../escape.txt": b"bad"})
            with self.assertRaisesRegex(ValueError, "insegura"):
                parse_mod(path)

    def test_v2_zip_rejects_executable_or_unknown_payloads(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "unsafe.mfmod"
            self.write_v2_zip(path, {"scripts/mod.lua": b"print('no')"})
            with self.assertRaisesRegex(ValueError, "no permitido"):
                parse_mod(path)

    def test_v2_zip_accepts_data_driven_climate_biomes(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "biome.mfmod"
            biome = {
                "id": "crystal_forest",
                "name": "Bosque de cristal",
                "temperature": {"min": -0.2, "max": 0.7},
                "humidity": {"min": 0.2, "max": 1.0},
                "continental": {"min": -0.1, "max": 0.9},
                "topBlock": "minecraft:grass",
                "fillerBlock": "minecraft:dirt",
                "priority": 10,
                "surfaceOffset": 2
            }
            self.write_v2_zip(path, manifest_overrides={"biomes": [biome]})
            values = parse_mod(path)
            self.assertEqual(values["id"], "ruby_pack")

    def test_v2_zip_rejects_invalid_biome_climate_ranges(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad-biome.mfmod"
            biome = {
                "id": "bad_climate",
                "name": "Bad climate",
                "temperature": {"min": 0.8, "max": -0.8},
                "humidity": {"min": 0.0, "max": 1.0},
                "continental": {"min": -1.0, "max": 1.0},
                "topBlock": "minecraft:grass",
                "fillerBlock": "minecraft:dirt"
            }
            self.write_v2_zip(path, manifest_overrides={"biomes": [biome]})
            with self.assertRaisesRegex(ValueError, "climático"):
                parse_mod(path)

    def test_v2_zip_accepts_deterministic_chunk_structures(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "structure.mfmod"
            structure = {
                "id": "ruined_shrine",
                "name": "Santuario en ruinas",
                "chance": 0.025,
                "blocks": [
                    {"x": 7, "y": 0, "z": 7, "id": "minecraft:stonebrick"},
                    {"x": 7, "y": 1, "z": 7, "id": "ruby_pack:ruby_ore"},
                    {"x": 8, "y": 0, "z": 7, "id": "minecraft:mossy_cobblestone"}
                ]
            }
            self.write_v2_zip(path, manifest_overrides={"structures": [structure]})
            values = parse_mod(path)
            self.assertEqual(values["format"], "2")

    def test_v2_zip_rejects_structure_blocks_outside_chunk(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad-structure.mfmod"
            structure = {
                "id": "giant_tower",
                "name": "Torre",
                "chance": 0.2,
                "blocks": [{"x": 16, "y": 0, "z": 7, "id": "minecraft:stone"}]
            }
            self.write_v2_zip(path, manifest_overrides={"structures": [structure]})
            with self.assertRaisesRegex(ValueError, "x fuera de rango"):
                parse_mod(path)

    def test_v2_zip_accepts_low_poly_custom_creature(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "creature.mfmod"
            creature = {
                "id": "crystal_golem",
                "name": "Gólem de cristal",
                "hostile": True,
                "speed": 0.34,
                "spawnChance": 0.12,
                "behavior": "pursue",
                "model": [
                    {"center": [0, 1.0, 0], "size": [0.65, 0.8, 0.45], "color": "#55CCFF"},
                    {"center": [0, 2.0, 0], "size": [0.4, 0.35, 0.35], "color": "#B8EEFF"},
                    {"center": [-0.45, 1.0, 0], "size": [0.18, 0.6, 0.2], "color": "#3288AA"},
                    {"center": [0.45, 1.0, 0], "size": [0.18, 0.6, 0.2], "color": "#3288AA"}
                ]
            }
            self.write_v2_zip(path, manifest_overrides={"entities": [creature]})
            values = parse_mod(path)
            self.assertEqual(values["id"], "ruby_pack")

    def test_v2_zip_rejects_creature_model_outside_bounds(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad-creature.mfmod"
            creature = {
                "id": "giant",
                "name": "Giant",
                "hostile": False,
                "speed": 0.2,
                "spawnChance": 0.1,
                "behavior": "wander",
                "model": [
                    {"center": [4, 1, 0], "size": [0.5, 0.5, 0.5], "color": "#FFFFFF"}
                ]
            }
            self.write_v2_zip(path, manifest_overrides={"entities": [creature]})
            with self.assertRaisesRegex(ValueError, "centro de pieza"):
                parse_mod(path)

    def test_v2_zip_rejects_hostile_grazer_behavior(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad-behavior.mfmod"
            creature = {
                "id": "hostile_cow",
                "name": "Hostile Cow",
                "hostile": True,
                "speed": 0.2,
                "behavior": "graze",
                "model": [
                    {"center": [0, 1, 0], "size": [0.5, 0.5, 0.5], "color": "#FFFFFF"}
                ]
            }
            self.write_v2_zip(path, manifest_overrides={"entities": [creature]})
            with self.assertRaisesRegex(ValueError, "comportamiento"):
                parse_mod(path)

    def test_v2_zip_rejects_duplicate_resource_ids(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "duplicate.mfmod"
            duplicate = [{
                "id": "ruby_ore", "name": "Duplicado", "texture": "textures/ruby.png"
            }]
            self.write_v2_zip(path, manifest_overrides={"items": duplicate})
            with self.assertRaisesRegex(ValueError, "duplicado"):
                parse_mod(path)

    def test_v2_zip_rejects_textures_above_128_pixels(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "large-texture.mfmod"
            manifest = {
                "format": 2, "id": "large_pack", "name": "Large",
                "version": "1.0.0", "blocks": [{
                    "id": "large", "name": "Large block",
                    "textures": {"top": "textures/large.png",
                                 "side": "textures/large.png",
                                 "bottom": "textures/large.png"}
                }]
            }
            with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                archive.writestr("manifest.json", json.dumps(manifest))
                archive.writestr("textures/large.png", self.make_png(256, 256))
            with self.assertRaisesRegex(ValueError, "tamaño"):
                parse_mod(path)

    def assert_invalid(self, content, message):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "invalid.mfmod"
            path.write_text(content, encoding="utf-8")
            with self.assertRaisesRegex(ValueError, message):
                parse_mod(path)


if __name__ == "__main__":
    unittest.main()
