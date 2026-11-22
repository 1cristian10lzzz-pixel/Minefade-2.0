#!/usr/bin/env python3
"""Create and validate Minefade MFMod v2 data-only ZIP packages."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import tempfile
import zipfile
from pathlib import Path

from tools.validate_mfmod import parse_mod

ALLOWED_ENTRIES = {"manifest.json", "data/", "data/gameplay.mfmod"}
ALLOWED_MANIFEST = {
    "format", "id", "name", "version", "engine_min", "engine_max", "entry", "sha256"
}
MAX_MANIFEST_BYTES = 8192
MAX_GAMEPLAY_BYTES = 32768
MAX_EXPANDED_BYTES = 40960
MAX_ENTRIES = 3


def version_tuple(value: str) -> tuple[int, int, int]:
    if not re.fullmatch(r"[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}", value):
        raise ValueError(f"versión semántica inválida: {value}")
    return tuple(int(part) for part in value.split("."))


def _safe_entry(name: str, is_dir: bool) -> bool:
    if not name or name.startswith("/") or "\\" in name or ":" in name:
        return False
    if any(part in {"", ".", ".."} for part in name.rstrip("/").split("/")):
        return False
    return name in ALLOWED_ENTRIES and (is_dir == name.endswith("/"))


def validate_package(path: Path, engine_version: str = "2.2.0") -> dict[str, object]:
    engine = version_tuple(engine_version)
    try:
        with zipfile.ZipFile(path, "r") as archive:
            infos = archive.infolist()
            if len(infos) > MAX_ENTRIES:
                raise ValueError("demasiadas entradas")
            seen: set[str] = set()
            total = 0
            for info in infos:
                if not _safe_entry(info.filename, info.is_dir()):
                    raise ValueError(f"ruta o entrada no permitida: {info.filename}")
                if info.filename in seen:
                    raise ValueError(f"entrada duplicada: {info.filename}")
                seen.add(info.filename)
                if info.file_size < 0:
                    raise ValueError("tamaño de entrada inválido")
                total += info.file_size
                limit = MAX_MANIFEST_BYTES if info.filename == "manifest.json" else MAX_GAMEPLAY_BYTES
                if info.is_dir():
                    limit = 0
                if info.file_size > limit or total > MAX_EXPANDED_BYTES:
                    raise ValueError("el paquete supera el límite de tamaño")
            if not {"manifest.json", "data/gameplay.mfmod"}.issubset(seen):
                raise ValueError("faltan manifest.json o data/gameplay.mfmod")
            manifest_bytes = archive.read("manifest.json")
            gameplay_bytes = archive.read("data/gameplay.mfmod")
    except zipfile.BadZipFile as error:
        raise ValueError("el archivo no es un ZIP válido") from error

    try:
        manifest = json.loads(manifest_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError("manifest.json no es JSON UTF-8 válido") from error
    if not isinstance(manifest, dict) or set(manifest) != ALLOWED_MANIFEST:
        raise ValueError("el manifiesto contiene campos ausentes o no soportados")
    if manifest.get("format") != 2 or manifest.get("entry") != "data/gameplay.mfmod":
        raise ValueError("formato o archivo de entrada no compatible")

    mod_id = manifest.get("id")
    name = manifest.get("name")
    package_version = manifest.get("version")
    if not isinstance(mod_id, str) or not re.fullmatch(r"[a-z0-9][a-z0-9_.-]{1,63}", mod_id):
        raise ValueError("id inválido")
    if not isinstance(name, str) or not name.strip() or len(name) > 96:
        raise ValueError("name inválido")
    package = version_tuple(str(package_version))
    minimum = version_tuple(str(manifest.get("engine_min")))
    maximum = version_tuple(str(manifest.get("engine_max")))
    if minimum > maximum or not minimum <= engine <= maximum:
        raise ValueError("versión de Minefade fuera del rango admitido")
    expected = manifest.get("sha256")
    actual = hashlib.sha256(gameplay_bytes).hexdigest()
    if not isinstance(expected, str) or not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise ValueError("sha256 inválido")
    if expected != actual:
        raise ValueError("sha256 de data/gameplay.mfmod no coincide")

    with tempfile.TemporaryDirectory(prefix="mfmod2-") as directory:
        payload = Path(directory) / "gameplay.mfmod"
        payload.write_bytes(gameplay_bytes)
        values = parse_mod(payload)
    if values["id"] != mod_id or values["name"] != name:
        raise ValueError("id/name del manifiesto no coincide con MFMod v1")
    if values["enabled"] != "false":
        raise ValueError("los mods nuevos deben instalarse desactivados")
    return {
        "id": mod_id,
        "name": name,
        "version": package_version,
        "engine_min": manifest["engine_min"],
        "engine_max": manifest["engine_max"],
        "sha256": actual,
    }


def create_package(
    gameplay_path: Path,
    output_path: Path,
    package_version: str,
    engine_min: str,
    engine_max: str,
) -> dict[str, object]:
    values = parse_mod(gameplay_path)
    if values["enabled"] != "false":
        raise ValueError("desactiva enabled en el .mfmod antes de empaquetarlo")
    version_tuple(package_version)
    if version_tuple(engine_min) > version_tuple(engine_max):
        raise ValueError("engine_min no puede ser mayor que engine_max")
    gameplay_bytes = gameplay_path.read_bytes()
    if len(gameplay_bytes) > MAX_GAMEPLAY_BYTES:
        raise ValueError("el .mfmod supera 32 KB")
    manifest = {
        "format": 2,
        "id": values["id"],
        "name": values["name"],
        "version": package_version,
        "engine_min": engine_min,
        "engine_max": engine_max,
        "entry": "data/gameplay.mfmod",
        "sha256": hashlib.sha256(gameplay_bytes).hexdigest(),
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("manifest.json", json.dumps(manifest, ensure_ascii=False, separators=(",", ":")))
        archive.writestr("data/", b"")
        archive.writestr("data/gameplay.mfmod", gameplay_bytes)
    return validate_package(output_path, engine_max)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    validate = sub.add_parser("validate")
    validate.add_argument("package", type=Path)
    validate.add_argument("--engine-version", default="2.2.0")
    pack = sub.add_parser("pack")
    pack.add_argument("gameplay", type=Path)
    pack.add_argument("output", type=Path)
    pack.add_argument("--version", default="1.0.0")
    pack.add_argument("--engine-min", default="2.2.0")
    pack.add_argument("--engine-max", default="2.2.99")
    args = parser.parse_args()
    try:
        if args.command == "validate":
            result = validate_package(args.package, args.engine_version)
        else:
            result = create_package(args.gameplay, args.output, args.version,
                                    args.engine_min, args.engine_max)
        print("OK MFMod v2: " + json.dumps(result, ensure_ascii=False))
        return 0
    except (OSError, ValueError) as error:
        print(f"ERROR MFMod v2: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
