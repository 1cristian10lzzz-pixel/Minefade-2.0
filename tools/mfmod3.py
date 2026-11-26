#!/usr/bin/env python3
"""Create and validate Minefade MFMod v3 content packages (data + allowlisted assets)."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import zipfile
from pathlib import Path
from typing import Any

ENGINE_VERSION = "2.3.1"
MAX_ENTRIES = 256
MAX_MANIFEST = 64 * 1024
MAX_JSON = 1024 * 1024
MAX_MODEL = 8 * 1024 * 1024
MAX_IMAGE = 2 * 1024 * 1024
MAX_SOUND = 4 * 1024 * 1024
MAX_EXPANDED = 32 * 1024 * 1024
MAX_ARCHIVE = 20 * 1024 * 1024
ID_RE = re.compile(r"[a-z0-9][a-z0-9_.-]{1,63}")
VERSION_RE = re.compile(r"[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}")
ALLOWED_DATA = {
    "data/gameplay.mfmod", "data/blocks.json", "data/items.json",
    "data/recipes.json", "data/entities.json", "data/biomes.json",
    "data/structures.json", "data/ui.json", "data/sounds.json",
}
SAFE_UI_ACTIONS = {
    "open_inventory", "open_crafting", "close_screen", "pause",
    "resume", "open_settings", "show_chat",
}
SHAPES = {"cube", "slab", "stairs", "cross_plant", "thin", "pane", "fence", "door", "trapdoor"}
TOOLS = {"none", "pickaxe", "axe", "shovel", "shears"}
ITEM_TYPES = {"item", "weapon", "tool", "armor"}
ARMOR_SLOTS = {"helmet", "chestplate", "leggings", "boots"}
STATIONS = {"inventory", "crafting_table", "furnace", "anvil", "stonecutter"}
CAPABILITIES = {"blocks", "items", "recipes", "entities", "biomes", "structures",
                "textures", "models", "animations", "sounds", "ui"}
ALLOWED_ASSET_SUFFIXES = {
    ".rgba", ".png", ".webp", ".mfmodel", ".json", ".wav", ".ogg", ".rml", ".rcss",
}


def version_tuple(value: str) -> tuple[int, int, int]:
    if not isinstance(value, str) or not VERSION_RE.fullmatch(value):
        raise ValueError(f"versión semántica inválida: {value!r}")
    return tuple(int(part) for part in value.split("."))


def namespaced_id(value: Any, mod_id: str, field: str) -> str:
    if not isinstance(value, str) or ":" not in value:
        raise ValueError(f"{field}: se esperaba id con namespace")
    namespace, path = value.split(":", 1)
    if not ID_RE.fullmatch(namespace) or not ID_RE.fullmatch(path):
        raise ValueError(f"{field}: id namespaced inválido")
    if namespace != mod_id:
        raise ValueError(f"{field}: namespace debe ser {mod_id}")
    return value


def namespaced_reference(value: Any, mod_id: str, field: str) -> str:
    if not isinstance(value, str) or ":" not in value:
        raise ValueError(f"{field}: se esperaba id namespaced")
    namespace, path = value.split(":", 1)
    if namespace not in {mod_id, "minecraft", "minefade"} or not ID_RE.fullmatch(namespace) or not ID_RE.fullmatch(path):
        raise ValueError(f"{field}: referencia namespaced inválida")
    return value


def _safe_path(name: str) -> bool:
    if not isinstance(name, str) or not name or name.startswith("/") or "\\" in name or ":" in name:
        return False
    parts = name.split("/")
    if any(part in {"", ".", ".."} for part in parts):
        return False
    if name in ALLOWED_DATA:
        return True
    if name.startswith("assets/"):
        suffix = Path(name).suffix.lower()
        return suffix in ALLOWED_ASSET_SUFFIXES and len(parts) >= 3
    return False


def _read_json(files: dict[str, bytes], name: str, optional: bool = True) -> Any:
    raw = files.get(name)
    if raw is None:
        if optional:
            return []
        raise ValueError(f"falta {name}")
    if len(raw) > MAX_JSON:
        raise ValueError(f"{name}: JSON demasiado grande")
    try:
        return json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"{name}: JSON UTF-8 inválido") from error


def _array(value: Any, name: str, maximum: int) -> list[dict[str, Any]]:
    if not isinstance(value, list) or len(value) > maximum or any(not isinstance(x, dict) for x in value):
        raise ValueError(f"{name}: se esperaba lista de objetos (máximo {maximum})")
    return value


def _number(value: Any, name: str, minimum: float, maximum: float) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{name}: debe ser numérico")
    number = float(value)
    if not minimum <= number <= maximum:
        raise ValueError(f"{name}: fuera del rango {minimum}..{maximum}")
    return number


def _int(value: Any, name: str, minimum: int, maximum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not minimum <= value <= maximum:
        raise ValueError(f"{name}: fuera del rango entero {minimum}..{maximum}")
    return value


def _resource(value: Any, files: dict[str, bytes], name: str,
              extensions: set[str]) -> str:
    if not isinstance(value, str) or value not in files:
        raise ValueError(f"{name}: recurso inexistente")
    if Path(value).suffix.lower() not in extensions:
        raise ValueError(f"{name}: extensión de recurso no permitida")
    return value


def validate_definitions(files: dict[str, bytes], mod_id: str) -> set[str]:
    used_capabilities: set[str] = set()
    seen_ids: set[str] = set()

    blocks = _array(_read_json(files, "data/blocks.json"), "blocks", 256)
    for i, block in enumerate(blocks):
        identifier = namespaced_id(block.get("id"), mod_id, f"blocks[{i}].id")
        if identifier in seen_ids:
            raise ValueError(f"definición duplicada: {identifier}")
        seen_ids.add(identifier)
        if not isinstance(block.get("name"), str) or not block["name"].strip() or len(block["name"]) > 96:
            raise ValueError(f"{identifier}: nombre inválido")
        if block.get("shape", "cube") not in SHAPES:
            raise ValueError(f"{identifier}: forma no compatible")
        _number(block.get("hardness", 1.0), f"{identifier}.hardness", 0.0, 100.0)
        _int(block.get("stack_limit", 64), f"{identifier}.stack_limit", 1, 99)
        if block.get("tool", "none") not in TOOLS:
            raise ValueError(f"{identifier}: herramienta no compatible")
        textures = block.get("textures")
        if not isinstance(textures, dict) or set(textures) - {"top", "side", "bottom"}:
            raise ValueError(f"{identifier}: textures debe contener top/side/bottom")
        for face in ("top", "side", "bottom"):
            _resource(textures.get(face), files, f"{identifier}.textures.{face}", {".rgba"})
        used_capabilities.update({"blocks", "textures"})

    items = _array(_read_json(files, "data/items.json"), "items", 256)
    for i, item in enumerate(items):
        identifier = namespaced_id(item.get("id"), mod_id, f"items[{i}].id")
        if identifier in seen_ids:
            raise ValueError(f"definición duplicada: {identifier}")
        seen_ids.add(identifier)
        if not isinstance(item.get("name"), str) or not item["name"].strip() or len(item["name"]) > 96:
            raise ValueError(f"{identifier}: nombre inválido")
        item_type = item.get("type", "item")
        if item_type not in ITEM_TYPES:
            raise ValueError(f"{identifier}: tipo de objeto no compatible")
        _resource(item.get("icon"), files, f"{identifier}.icon", {".rgba"})
        _int(item.get("stack_limit", 64), f"{identifier}.stack_limit", 1, 99)
        if item_type in {"weapon", "tool"}:
            _number(item.get("damage", 1.0), f"{identifier}.damage", 0.0, 100.0)
            _int(item.get("durability", 100), f"{identifier}.durability", 1, 100000)
            if item.get("tool", "none") not in TOOLS:
                raise ValueError(f"{identifier}: herramienta no compatible")
        if item_type == "armor":
            if item.get("armor_slot") not in ARMOR_SLOTS:
                raise ValueError(f"{identifier}: armor_slot inválido")
            _int(item.get("protection", 1), f"{identifier}.protection", 0, 20)
            _int(item.get("durability", 100), f"{identifier}.durability", 1, 100000)
        used_capabilities.add("items")
        if "models" in item:
            _resource(item["models"], files, f"{identifier}.models", {".mfmodel"})
            used_capabilities.add("models")

    recipes = _array(_read_json(files, "data/recipes.json"), "recipes", 512)
    for i, recipe in enumerate(recipes):
        identifier = namespaced_id(recipe.get("id"), mod_id, f"recipes[{i}].id")
        if identifier in seen_ids:
            raise ValueError(f"definición duplicada: {identifier}")
        seen_ids.add(identifier)
        output = recipe.get("output")
        if not isinstance(output, dict):
            raise ValueError(f"{identifier}: output inválido")
        namespaced_id(output.get("id"), mod_id, f"{identifier}.output.id")
        _int(output.get("count", 1), f"{identifier}.output.count", 1, 999)
        ingredients = _array(recipe.get("ingredients"), f"{identifier}.ingredients", 9)
        if not ingredients:
            raise ValueError(f"{identifier}: receta sin ingredientes")
        for j, ingredient in enumerate(ingredients):
            namespaced_reference(ingredient.get("id"), mod_id, f"{identifier}.ingredients[{j}].id")
            _int(ingredient.get("count", 1), f"{identifier}.ingredients[{j}].count", 1, 999)
        if recipe.get("station", "inventory") not in STATIONS:
            raise ValueError(f"{identifier}: estación no permitida")
        used_capabilities.add("recipes")

    entities = _array(_read_json(files, "data/entities.json"), "entities", 128)
    for i, entity in enumerate(entities):
        identifier = namespaced_id(entity.get("id"), mod_id, f"entities[{i}].id")
        if identifier in seen_ids:
            raise ValueError(f"definición duplicada: {identifier}")
        seen_ids.add(identifier)
        _resource(entity.get("model"), files, f"{identifier}.model", {".mfmodel"})
        _resource(entity.get("texture"), files, f"{identifier}.texture", {".rgba", ".png", ".webp"})
        _number(entity.get("width", 0.8), f"{identifier}.width", 0.1, 16.0)
        _number(entity.get("height", 1.0), f"{identifier}.height", 0.1, 32.0)
        _number(entity.get("eye_height", 0.7), f"{identifier}.eye_height", 0.1, 32.0)
        _int(entity.get("spawn_count", 1), f"{identifier}.spawn_count", 0, 8)
        if "animations" in entity:
            _resource(entity["animations"], files, f"{identifier}.animations", {".json"})
            used_capabilities.add("animations")
        if "controller" in entity:
            _resource(entity["controller"], files, f"{identifier}.controller", {".json"})
            used_capabilities.add("animations")
        used_capabilities.update({"entities", "models", "textures"})

    biomes = _array(_read_json(files, "data/biomes.json"), "biomes", 64)
    for i, biome in enumerate(biomes):
        identifier = namespaced_id(biome.get("id"), mod_id, f"biomes[{i}].id")
        if identifier in seen_ids:
            raise ValueError(f"definición duplicada: {identifier}")
        seen_ids.add(identifier)
        for climate in ("temperature", "humidity"):
            values = biome.get(climate, [-1.0, 1.0] if climate == "temperature" else [0.0, 1.0])
            if not isinstance(values, list) or len(values) != 2:
                raise ValueError(f"{identifier}.{climate}: se esperaba [mínimo,máximo]")
            low = _number(values[0], f"{identifier}.{climate}[0]", -1.0, 1.0)
            high = _number(values[1], f"{identifier}.{climate}[1]", -1.0, 1.0)
            if low > high:
                raise ValueError(f"{identifier}.{climate}: rango invertido")
        for field in ("surface", "filler"):
            if field in biome:
                namespaced_reference(biome[field], mod_id, f"{identifier}.{field}")
        used_capabilities.add("biomes")

    structures = _array(_read_json(files, "data/structures.json"), "structures", 64)
    for i, structure in enumerate(structures):
        identifier = namespaced_id(structure.get("id"), mod_id, f"structures[{i}].id")
        if identifier in seen_ids:
            raise ValueError(f"definición duplicada: {identifier}")
        seen_ids.add(identifier)
        blocks_data = _array(structure.get("blocks"), f"{identifier}.blocks", 4096)
        if not blocks_data:
            raise ValueError(f"{identifier}: estructura vacía")
        for j, block in enumerate(blocks_data):
            position = block.get("pos")
            if not isinstance(position, list) or len(position) != 3:
                raise ValueError(f"{identifier}.blocks[{j}]: pos inválida")
            _int(position[0], f"{identifier}.blocks[{j}].x", -7, 7)
            _int(position[1], f"{identifier}.blocks[{j}].y", -64, 64)
            _int(position[2], f"{identifier}.blocks[{j}].z", -7, 7)
            namespaced_reference(block.get("block"), mod_id, f"{identifier}.blocks[{j}].block")
        used_capabilities.add("structures")

    ui = _read_json(files, "data/ui.json")
    if ui:
        if not isinstance(ui, dict):
            raise ValueError("ui.json debe ser un objeto")
        screens = _array(ui.get("screens", []), "ui.screens", 16)
        for i, screen in enumerate(screens):
            _resource(screen.get("document"), files, f"ui.screens[{i}].document", {".rml"})
            if "style" in screen:
                _resource(screen["style"], files, f"ui.screens[{i}].style", {".rcss"})
            actions = screen.get("actions", [])
            if not isinstance(actions, list) or any(action not in SAFE_UI_ACTIONS for action in actions):
                raise ValueError("UI: acción no permitida")
        if screens:
            used_capabilities.add("ui")

    sounds = _read_json(files, "data/sounds.json")
    if sounds:
        if not isinstance(sounds, dict) or len(sounds) > 128:
            raise ValueError("sounds.json inválido")
        for event, config in sounds.items():
            if not isinstance(event, str) or not re.fullmatch(r"[a-z0-9_.-]{1,64}", event):
                raise ValueError("evento de sonido inválido")
            if isinstance(config, str):
                path = config
                volume = 1.0
            elif isinstance(config, dict):
                path = config.get("file")
                volume = _number(config.get("volume", 1.0), f"sounds.{event}.volume", 0.0, 2.0)
            else:
                raise ValueError(f"sounds.{event} inválido")
            _resource(path, files, f"sounds.{event}.file", {".wav", ".ogg"})
        used_capabilities.add("sounds")

    for name in files:
        if name.startswith("assets/"):
            suffix = Path(name).suffix.lower()
            if suffix == ".rgba" and len(files[name]) != 1024:
                raise ValueError(f"{name}: las texturas RGBA de bloque deben ser 16x16 (1024 bytes)")
            if suffix == ".mfmodel":
                used_capabilities.add("models")
            elif suffix in {".png", ".webp"}:
                used_capabilities.add("textures")
            elif suffix in {".wav", ".ogg"}:
                used_capabilities.add("sounds")
            elif suffix in {".rml", ".rcss"}:
                used_capabilities.add("ui")
            elif suffix == ".json":
                used_capabilities.add("animations")

    return used_capabilities


def _read_archive(path: Path) -> tuple[dict[str, bytes], dict[str, Any]]:
    if path.stat().st_size > MAX_ARCHIVE:
        raise ValueError("el ZIP supera 20 MiB")
    try:
        with zipfile.ZipFile(path, "r") as archive:
            infos = archive.infolist()
            if len(infos) > MAX_ENTRIES:
                raise ValueError("demasiadas entradas en el ZIP")
            files: dict[str, bytes] = {}
            total = 0
            for info in infos:
                if info.is_dir():
                    continue
                if not _safe_path(info.filename):
                    raise ValueError(f"ruta o tipo de archivo no permitido: {info.filename}")
                if info.filename in files:
                    raise ValueError(f"entrada duplicada: {info.filename}")
                if info.file_size < 0 or info.compress_size < 0:
                    raise ValueError("tamaño de entrada inválido")
                if info.file_size > MAX_EXPANDED or (info.compress_size == 0 and info.file_size > 0):
                    raise ValueError("tamaño o compresión inválidos")
                if info.file_size > max_file_size(info.filename):
                    raise ValueError(f"{info.filename}: supera su límite de tamaño")
                if info.file_size > max(1, info.compress_size) * 200:
                    raise ValueError(f"{info.filename}: ratio de compresión sospechoso")
                total += info.file_size
                if total > MAX_EXPANDED:
                    raise ValueError("el contenido expandido supera 32 MiB")
                data = archive.read(info)
                if len(data) != info.file_size:
                    raise ValueError("tamaño de entrada inconsistente")
                files[info.filename] = data
    except zipfile.BadZipFile as error:
        raise ValueError("el archivo no es un ZIP válido") from error
    manifest_bytes = files.pop("manifest.json", None)
    if manifest_bytes is None or len(manifest_bytes) > MAX_MANIFEST:
        raise ValueError("manifest.json ausente o demasiado grande")
    try:
        manifest = json.loads(manifest_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError("manifest.json no es JSON UTF-8 válido") from error
    return files, manifest


def max_file_size(path: str) -> int:
    suffix = Path(path).suffix.lower()
    if suffix == ".mfmodel":
        return MAX_MODEL
    if suffix in {".png", ".webp", ".rgba"}:
        return MAX_IMAGE
    if suffix in {".wav", ".ogg"}:
        return MAX_SOUND
    if suffix == ".json" or suffix in {".rml", ".rcss"} or path.endswith(".mfmod"):
        return MAX_JSON
    return MAX_EXPANDED


def validate_package(path: Path, engine_version: str = ENGINE_VERSION) -> dict[str, Any]:
    engine = version_tuple(engine_version)
    files, manifest = _read_archive(path)
    required = {"format", "id", "name", "version", "engine_min", "engine_max",
                "entry", "description", "capabilities", "dependencies", "files"}
    if not isinstance(manifest, dict) or set(manifest) != required:
        raise ValueError("manifest.json contiene campos ausentes o no permitidos")
    if manifest["format"] != 3 or manifest["entry"] != "data/gameplay.mfmod":
        raise ValueError("formato o punto de entrada MFMod3 no compatible")
    mod_id = manifest["id"]
    if not isinstance(mod_id, str) or not ID_RE.fullmatch(mod_id):
        raise ValueError("id del mod inválido")
    if not isinstance(manifest["name"], str) or not manifest["name"].strip() or len(manifest["name"]) > 96:
        raise ValueError("name inválido")
    if not isinstance(manifest["description"], str) or len(manifest["description"]) > 512:
        raise ValueError("description inválida")
    version = version_tuple(manifest["version"])
    minimum = version_tuple(manifest["engine_min"])
    maximum = version_tuple(manifest["engine_max"])
    if minimum > maximum or not minimum <= engine <= maximum:
        raise ValueError("versión de Minefade fuera del rango admitido")

    gameplay = files.get("data/gameplay.mfmod")
    if not gameplay:
        raise ValueError("falta data/gameplay.mfmod")
    if len(gameplay) > 32768:
        raise ValueError("data/gameplay.mfmod supera 32 KiB")
    with __import__("tempfile").TemporaryDirectory(prefix="mfmod3-") as temp:
        gameplay_path = Path(temp) / "gameplay.mfmod"
        gameplay_path.write_bytes(gameplay)
        from tools.validate_mfmod import parse_mod
        gameplay_values = parse_mod(gameplay_path)
    if gameplay_values["id"] != mod_id or gameplay_values["name"] != manifest["name"]:
        raise ValueError("id/name de gameplay no coincide con manifest.json")
    if gameplay_values["enabled"] != "false":
        raise ValueError("los paquetes nuevos deben instalarse desactivados")

    file_index = manifest["files"]
    if not isinstance(file_index, dict) or set(file_index) != set(files):
        raise ValueError("manifest.files no coincide con el contenido real")
    for name, data in files.items():
        record = file_index[name]
        if not isinstance(record, dict) or set(record) != {"size", "sha256"}:
            raise ValueError(f"manifest.files[{name}] inválido")
        if record["size"] != len(data) or record["sha256"] != hashlib.sha256(data).hexdigest():
            raise ValueError(f"hash o tamaño no coincide: {name}")

    dependencies = manifest["dependencies"]
    if not isinstance(dependencies, dict) or len(dependencies) > 64:
        raise ValueError("dependencies inválidas")
    for dependency, required_version in dependencies.items():
        if not ID_RE.fullmatch(dependency) or not isinstance(required_version, str) or len(required_version) > 64:
            raise ValueError("dependencia inválida")

    actual_capabilities = validate_definitions(files, mod_id)
    declared = manifest["capabilities"]
    if not isinstance(declared, list) or len(set(declared)) != len(declared) or any(x not in CAPABILITIES for x in declared):
        raise ValueError("capabilities inválidas")
    if not actual_capabilities.issubset(set(declared)):
        raise ValueError("manifest.capabilities no declara todo el contenido del paquete")
    return {
        "id": mod_id, "name": manifest["name"], "version": manifest["version"],
        "engine_min": manifest["engine_min"], "engine_max": manifest["engine_max"],
        "capabilities": sorted(declared), "files": len(files),
        "expanded_bytes": sum(len(data) for data in files.values()),
    }


def create_package(root: Path, output: Path, engine_min: str, engine_max: str,
                   version: str = "1.0.0", description: str = "") -> dict[str, Any]:
    version_tuple(version)
    minimum, maximum = version_tuple(engine_min), version_tuple(engine_max)
    if minimum > maximum:
        raise ValueError("engine_min supera engine_max")
    gameplay_file = root / "data" / "gameplay.mfmod"
    if not gameplay_file.is_file():
        raise ValueError("falta data/gameplay.mfmod")
    mod_id = ""
    name = ""
    with __import__("tempfile").TemporaryDirectory(prefix="mfmod3-src-") as temp:
        copied = Path(temp) / "gameplay.mfmod"
        copied.write_bytes(gameplay_file.read_bytes())
        from tools.validate_mfmod import parse_mod
        values = parse_mod(copied)
    mod_id, name = values["id"], values["name"]
    if values["enabled"] != "false":
        raise ValueError("desactiva enabled antes de crear el paquete")

    files: dict[str, bytes] = {}
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise ValueError("no se permiten enlaces simbólicos")
        if not path.is_file():
            continue
        name_in_package = path.relative_to(root).as_posix()
        if name_in_package == "manifest.json":
            continue
        if not _safe_path(name_in_package):
            raise ValueError(f"ruta o tipo de archivo no permitido: {name_in_package}")
        data = path.read_bytes()
        if len(data) > max_file_size(name_in_package):
            raise ValueError(f"{name_in_package}: supera su límite de tamaño")
        files[name_in_package] = data
        if len(files) > MAX_ENTRIES - 1:
            raise ValueError("demasiadas entradas en el paquete")
    if "data/gameplay.mfmod" not in files:
        raise ValueError("falta data/gameplay.mfmod")
    used = validate_definitions(files, mod_id)
    # Gameplay settings are always the entry point, even when all content modules are absent.
    manifest = {
        "format": 3, "id": mod_id, "name": name, "version": version,
        "engine_min": engine_min, "engine_max": engine_max,
        "entry": "data/gameplay.mfmod", "description": description[:512],
        "capabilities": sorted(used), "dependencies": {},
        "files": {key: {"size": len(value), "sha256": hashlib.sha256(value).hexdigest()}
                  for key, value in sorted(files.items())},
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        archive.writestr("manifest.json", json.dumps(manifest, ensure_ascii=False, sort_keys=True, separators=(",", ":")))
        for key, value in sorted(files.items()):
            archive.writestr(key, value)
    return validate_package(output)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    create = sub.add_parser("create")
    create.add_argument("root", type=Path)
    create.add_argument("output", type=Path)
    create.add_argument("--engine-min", default=ENGINE_VERSION)
    create.add_argument("--engine-max", default="2.3.99")
    create.add_argument("--version", default="1.0.0")
    create.add_argument("--description", default="")
    validate = sub.add_parser("validate")
    validate.add_argument("package", type=Path)
    validate.add_argument("--engine-version", default=ENGINE_VERSION)
    args = parser.parse_args()
    try:
        if args.command == "create":
            result = create_package(args.root, args.output, args.engine_min,
                                    args.engine_max, args.version, args.description)
        else:
            result = validate_package(args.package, args.engine_version)
        print(json.dumps(result, ensure_ascii=False, sort_keys=True))
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"mfmod3: error: {error}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
