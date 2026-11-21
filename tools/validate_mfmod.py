#!/usr/bin/env python3
"""Validate Minefade's data-only .mfmod v1 format and gameplay extensions."""
from __future__ import annotations
import argparse
import math
import re
import json
import struct
import zipfile
from pathlib import Path

MAX_BYTES, MAX_LINES = 32768, 128
ALLOWED = {
    "id", "name", "version", "description", "enabled",
    "spawn.sheep", "spawn.cow", "spawn.bird", "spawn.pig",
    "spawn.rabbit", "spawn.zombie", "spawn.skeleton",
    "player.speed", "player.jump", "player.crouch_speed",
    "entity.speed", "entity.passive_speed", "entity.hostile_speed",
    "entity.flee_radius", "entity.alert_radius", "entity.spawn_radius",
    "world.day_speed", "world.entity_cap", "world.spawn_interval",
    "world.spawn_distance", "world.passive_cap",
    "drops.pickup_radius", "drops.lifetime", "drops.quantity",
    "mining.speed",
}
SPAWN = {
    "spawn.sheep", "spawn.cow", "spawn.bird", "spawn.pig",
    "spawn.rabbit", "spawn.zombie", "spawn.skeleton",
}
INTEGER_RANGES = {
    "world.entity_cap": (8, 128),
    "world.passive_cap": (1, 64),
    "drops.quantity": (1, 4),
}
FLOAT_RANGES = {
    "player.speed": (0.5, 2.0),
    "player.jump": (0.75, 1.5),
    "player.crouch_speed": (0.25, 1.0),
    "entity.speed": (0.5, 2.0),
    "entity.passive_speed": (0.5, 2.0),
    "entity.hostile_speed": (0.5, 2.0),
    "entity.flee_radius": (2.0, 16.0),
    "entity.alert_radius": (4.0, 32.0),
    "entity.spawn_radius": (1.0, 8.0),
    "world.day_speed": (0.25, 4.0),
    "world.spawn_interval": (30.0, 900.0),
    "world.spawn_distance": (8.0, 48.0),
    "drops.pickup_radius": (0.8, 4.0),
    "drops.lifetime": (30.0, 900.0),
    "mining.speed": (0.5, 3.0),
}


def parse_mod(path: Path) -> dict[str, str]:
    raw = path.read_bytes()
    if raw[:2] == b"PK":
        return parse_mfmod_v2_zip(path)
    if not raw or len(raw) > MAX_BYTES:
        raise ValueError("tamaño fuera del límite")
    text = raw.decode("utf-8")
    lines = text.splitlines()
    if len(lines) > MAX_LINES:
        raise ValueError("demasiadas líneas")
    values: dict[str, str] = {}
    for number, raw_line in enumerate(lines, 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ValueError(f"línea {number}: se esperaba clave=valor")
        key, value = (part.strip() for part in line.split("=", 1))
        if not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", key):
            raise ValueError(f"línea {number}: clave inválida")
        if key not in ALLOWED:
            raise ValueError(f"línea {number}: propiedad no soportada: {key}")
        if key in values:
            raise ValueError(f"línea {number}: propiedad duplicada: {key}")
        if len(value) > 512:
            raise ValueError(f"línea {number}: valor demasiado largo")
        values[key] = value

    required = {"id", "name", "version", "enabled"}
    missing = required - values.keys()
    if missing:
        raise ValueError("faltan campos: " + ", ".join(sorted(missing)))
    if not re.fullmatch(r"[a-z0-9][a-z0-9_.-]{1,63}", values["id"]):
        raise ValueError("id inválido")
    if not values["name"].strip() or len(values["name"]) > 96:
        raise ValueError("name inválido")
    if values["version"] not in {"1", "1.0"}:
        raise ValueError("versión de formato no compatible")
    if values["enabled"] not in {"true", "false"}:
        raise ValueError("enabled debe ser true o false")
    if len(values.get("description", "")) > 512:
        raise ValueError("description demasiado largo")

    for key in SPAWN & values.keys():
        try:
            count = int(values[key], 10)
        except ValueError as exc:
            raise ValueError(f"{key} debe ser entero") from exc
        if not 0 <= count <= 24:
            raise ValueError(f"{key} fuera de rango 0..24")

    for key, (minimum, maximum) in INTEGER_RANGES.items():
        if key not in values:
            continue
        try:
            number = int(values[key], 10)
        except ValueError as exc:
            raise ValueError(f"{key} debe ser entero") from exc
        if not minimum <= number <= maximum:
            raise ValueError(f"{key} fuera de rango {minimum}..{maximum}")

    for key, (minimum, maximum) in FLOAT_RANGES.items():
        if key not in values:
            continue
        try:
            number = float(values[key])
        except ValueError as exc:
            raise ValueError(f"{key} debe ser numérico") from exc
        if not math.isfinite(number) or not minimum <= number <= maximum:
            raise ValueError(f"{key} fuera de rango {minimum}..{maximum}")
    return values


def parse_mfmod_v2_zip(path: Path) -> dict[str, str]:
    if path.stat().st_size > 16 * 1024 * 1024:
        raise ValueError("ZIP comprimido supera 16 MB")
    allowed_sounds = {"stone", "wood", "gravel", "sand", "grass", "glass", "metal"}
    allowed_shapes = {"cube", "slab", "stairs", "cross_plant", "thin", "pane",
                      "fence", "door", "trapdoor", "fluid", "scaffolding"}
    allowed_tools = {"none", "pickaxe", "axe", "shovel", "shears"}

    def local_id(value: object) -> str:
        if not isinstance(value, str) or not re.fullmatch(r"[a-z0-9][a-z0-9_.-]{0,63}", value):
            raise ValueError("ID de recurso inválido")
        return value

    def label(value: object) -> str:
        if not isinstance(value, str) or not value.strip() or len(value) > 96 or any(c in value for c in "|\r\n"):
            raise ValueError("Nombre de recurso inválido")
        return value

    def integer(value: object, low: int, high: int, field: str) -> int:
        if isinstance(value, bool) or not isinstance(value, int) or not low <= value <= high:
            raise ValueError(f"{field} fuera de rango")
        return value

    def texture(zf: zipfile.ZipFile, path_value: object) -> None:
        if not isinstance(path_value, str) or not path_value.startswith("textures/") or not path_value.lower().endswith(".png") or "\\" in path_value or ".." in path_value:
            raise ValueError("Ruta de textura inválida")
        try:
            data = zf.read(path_value)
        except KeyError as exc:
            raise ValueError("No existe una textura declarada") from exc
        if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
            raise ValueError("Textura PNG inválida")
        width, height = struct.unpack(">II", data[16:24])
        if not 1 <= width <= 128 or not 1 <= height <= 128:
            raise ValueError("Textura fuera de tamaño 1..128")

    def reference(value: object) -> None:
        if not isinstance(value, str) or not value or len(value) > 128 or any(c in value for c in "|\r\n"):
            raise ValueError("Referencia de recurso inválida")
        if value.startswith("minecraft:"):
            value = value[len("minecraft:"):]
        if ":" in value:
            parts = value.split(":")
            if len(parts) != 2 or not re.fullmatch(r"[a-z0-9][a-z0-9_.-]{1,63}", parts[0]) or not re.fullmatch(r"[a-z0-9][a-z0-9_.-]{0,63}", parts[1]):
                raise ValueError("Namespace de recurso inválido")
        else:
            local_id(value)

    try:
        with zipfile.ZipFile(path) as zf:
            infos = zf.infolist()
            if not 1 <= len(infos) <= 256:
                raise ValueError("cantidad de archivos ZIP fuera del límite")
            seen: set[str] = set()
            total = 0
            for info in infos:
                name = info.filename
                safe_name = name[:-1] if info.is_dir() and name.endswith("/") else name
                if not safe_name or safe_name.startswith("/") or "\\" in safe_name or any(part in {"", ".", ".."} for part in safe_name.split("/")):
                    raise ValueError("ruta ZIP insegura")
                if safe_name in seen:
                    raise ValueError("archivo ZIP duplicado")
                seen.add(safe_name)
                if info.is_dir():
                    continue
                if safe_name != "manifest.json" and not (safe_name.startswith("textures/") and safe_name.lower().endswith(".png")):
                    raise ValueError("tipo de archivo no permitido en el mod")
                if info.file_size > 4 * 1024 * 1024:
                    raise ValueError("archivo descomprimido supera 4 MB")
                total += info.file_size
                if total > 32 * 1024 * 1024:
                    raise ValueError("contenido descomprimido supera 32 MB")
            if "manifest.json" not in seen:
                raise ValueError("falta manifest.json")
            manifest_raw = zf.read("manifest.json")
            if not manifest_raw or len(manifest_raw) > 1024 * 1024:
                raise ValueError("manifest.json demasiado grande")
            manifest = json.loads(manifest_raw.decode("utf-8"))
            if not isinstance(manifest, dict) or manifest.get("format") != 2:
                raise ValueError("formato MFMod no compatible")
            mod_id = manifest.get("id")
            if not isinstance(mod_id, str) or not re.fullmatch(r"[a-z0-9][a-z0-9_.-]{1,63}", mod_id):
                raise ValueError("id de mod inválido")
            name = label(manifest.get("name"))
            version = manifest.get("version")
            if not isinstance(version, str) or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+([+-][A-Za-z0-9.-]+)?", version):
                raise ValueError("versión de mod inválida")
            description = manifest.get("description", "")
            if not isinstance(description, str) or len(description) > 512 or any(c in description for c in "|\r\n"):
                raise ValueError("descripción inválida")

            blocks = manifest.get("blocks", [])
            items = manifest.get("items", [])
            recipes = manifest.get("recipes", [])
            biomes = manifest.get("biomes", [])
            structures = manifest.get("structures", [])
            entities = manifest.get("entities", [])
            if not all(isinstance(x, list) for x in (blocks, items, recipes, biomes, structures, entities)):
                raise ValueError("blocks, items, recipes, biomes, structures y entities deben ser listas")
            if len(blocks) + len(items) > 128 or len(recipes) > 128 or len(biomes) > 32 or len(structures) > 16 or len(entities) > 16:
                raise ValueError("demasiados recursos definidos")
            ids: set[str] = set()
            for block in blocks:
                if not isinstance(block, dict):
                    raise ValueError("definición de bloque inválida")
                rid = local_id(block.get("id"))
                if rid in ids:
                    raise ValueError("ID duplicado")
                ids.add(rid)
                label(block.get("name"))
                textures = block.get("textures")
                if not isinstance(textures, dict):
                    raise ValueError("texturas de bloque inválidas")
                for face in ("top", "side", "bottom"):
                    texture(zf, textures.get(face))
                hardness = block.get("hardness", 1.5)
                if isinstance(hardness, bool) or not isinstance(hardness, (int, float)) or not math.isfinite(hardness) or not 0 <= hardness <= 100:
                    raise ValueError("hardness fuera de rango")
                if block.get("sound", "stone") not in allowed_sounds:
                    raise ValueError("sonido no permitido")
                integer(block.get("stack", 64), 1, 64, "stack")
                integer(block.get("light", 0), 0, 15, "light")
                if block.get("shape", "cube") not in allowed_shapes or block.get("tool", "pickaxe") not in allowed_tools:
                    raise ValueError("shape o tool no permitido")
            for item in items:
                if not isinstance(item, dict):
                    raise ValueError("definición de objeto inválida")
                rid = local_id(item.get("id"))
                if rid in ids:
                    raise ValueError("ID duplicado")
                ids.add(rid)
                label(item.get("name"))
                if "texture" in item:
                    texture(zf, item["texture"])
                else:
                    textures = item.get("textures")
                    if not isinstance(textures, dict):
                        raise ValueError("texturas de objeto inválidas")
                    for face in ("top", "side", "bottom"):
                        texture(zf, textures.get(face))
                integer(item.get("stack", 64), 1, 64, "stack")
            for biome in biomes:
                if not isinstance(biome, dict):
                    raise ValueError("definición de bioma inválida")
                rid = local_id(biome.get("id"))
                if rid in ids:
                    raise ValueError("ID duplicado")
                ids.add(rid)
                label(biome.get("name"))
                for climate_key in ("temperature", "humidity", "continental"):
                    climate = biome.get(climate_key)
                    if not isinstance(climate, dict):
                        raise ValueError("rango climático inválido")
                    low, high = climate.get("min"), climate.get("max")
                    if isinstance(low, bool) or isinstance(high, bool) or not isinstance(low, (int, float)) or not isinstance(high, (int, float)) or \
                            not math.isfinite(low) or not math.isfinite(high) or not -1 <= low <= high <= 1:
                        raise ValueError("rango climático fuera de límites")
                reference(biome.get("topBlock"))
                reference(biome.get("fillerBlock"))
                integer(biome.get("priority", 0), -100, 100, "priority")
                integer(biome.get("surfaceOffset", 0), -16, 16, "surfaceOffset")
            for structure in structures:
                if not isinstance(structure, dict):
                    raise ValueError("definición de estructura inválida")
                rid = local_id(structure.get("id"))
                structure_id = "structure:" + rid
                if structure_id in ids:
                    raise ValueError("ID de estructura duplicado")
                ids.add(structure_id)
                label(structure.get("name"))
                chance = structure.get("chance", 0.02)
                if isinstance(chance, bool) or not isinstance(chance, (int, float)) or \
                        not math.isfinite(chance) or not 0.0001 <= chance <= 1.0:
                    raise ValueError("probabilidad de estructura fuera de rango")
                placements = structure.get("blocks")
                if not isinstance(placements, list) or not 1 <= len(placements) <= 128:
                    raise ValueError("una estructura necesita entre 1 y 128 bloques")
                for placement in placements:
                    if not isinstance(placement, dict):
                        raise ValueError("bloque de estructura inválido")
                    integer(placement.get("x"), 0, 15, "x")
                    integer(placement.get("y"), -16, 48, "y")
                    integer(placement.get("z"), 0, 15, "z")
                    reference(placement.get("id"))
            for entity in entities:
                if not isinstance(entity, dict):
                    raise ValueError("definición de criatura inválida")
                rid = local_id(entity.get("id"))
                entity_id = "entity:" + rid
                if entity_id in ids:
                    raise ValueError("ID de criatura duplicado")
                ids.add(entity_id)
                label(entity.get("name"))
                hostile = entity.get("hostile")
                if not isinstance(hostile, bool):
                    raise ValueError("hostile debe ser booleano")
                speed = entity.get("speed")
                chance = entity.get("spawnChance", 0.1)
                if isinstance(speed, bool) or not isinstance(speed, (int, float)) or \
                        not math.isfinite(speed) or not 0.05 <= speed <= 2.0:
                    raise ValueError("velocidad de criatura fuera de rango")
                if isinstance(chance, bool) or not isinstance(chance, (int, float)) or \
                        not math.isfinite(chance) or not 0.0 <= chance <= 1.0:
                    raise ValueError("probabilidad de aparición fuera de rango")
                behavior = entity.get("behavior", "wander")
                if behavior not in {"wander", "flee", "graze", "pursue"} or \
                        (hostile and behavior == "graze") or (not hostile and behavior == "pursue"):
                    raise ValueError("comportamiento de criatura inválido")
                model = entity.get("model")
                if not isinstance(model, list) or not 1 <= len(model) <= 24:
                    raise ValueError("el modelo necesita entre 1 y 24 piezas")
                for part in model:
                    if not isinstance(part, dict):
                        raise ValueError("pieza de modelo inválida")
                    center, size = part.get("center"), part.get("size")
                    if not isinstance(center, list) or len(center) != 3 or \
                            not isinstance(size, list) or len(size) != 3:
                        raise ValueError("cada pieza necesita center y size XYZ")
                    for index, value in enumerate(center):
                        low, high = (-2.0, 2.0) if index != 1 else (0.0, 3.5)
                        if isinstance(value, bool) or not isinstance(value, (int, float)) or \
                                not math.isfinite(value) or not low <= value <= high:
                            raise ValueError("centro de pieza fuera de rango")
                    for value in size:
                        if isinstance(value, bool) or not isinstance(value, (int, float)) or \
                                not math.isfinite(value) or not 0.04 <= value <= 1.5:
                            raise ValueError("tamaño de pieza fuera de rango")
                    if not isinstance(part.get("color"), str) or \
                            not re.fullmatch(r"#[0-9a-fA-F]{6}", part["color"]):
                        raise ValueError("color de pieza inválido")
            for recipe in recipes:
                if not isinstance(recipe, dict):
                    raise ValueError("receta inválida")
                local_id(recipe.get("id"))
                label(recipe.get("name"))
                output = recipe.get("output")
                ingredients = recipe.get("ingredients")
                if not isinstance(output, dict) or not isinstance(ingredients, list) or not 1 <= len(ingredients) <= 5:
                    raise ValueError("estructura de receta inválida")
                reference(output.get("id"))
                integer(output.get("count", 1), 1, 64, "output.count")
                for ingredient in ingredients:
                    if not isinstance(ingredient, dict):
                        raise ValueError("ingrediente inválido")
                    reference(ingredient.get("id"))
                    integer(ingredient.get("count", 1), 1, 64, "ingredient.count")
            return {"id": mod_id, "name": name, "version": version, "format": "2"}
    except (zipfile.BadZipFile, json.JSONDecodeError, UnicodeDecodeError) as exc:
        raise ValueError(f"paquete ZIP/JSON inválido: {exc}") from exc


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("packs", nargs="+", type=Path)
    args = parser.parse_args()
    failed = False
    for pack in args.packs:
        try:
            data = parse_mod(pack)
            print(f"OK {pack}: {data['id']} (formato {data['version']})")
        except (OSError, UnicodeDecodeError, ValueError) as error:
            failed = True
            print(f"ERROR {pack}: {error}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
