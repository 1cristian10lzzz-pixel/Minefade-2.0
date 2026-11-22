#!/usr/bin/env python3
"""Validate Minefade's data-only .mfmod v1 format and gameplay extensions."""
from __future__ import annotations
import argparse
import math
import re
from pathlib import Path

MAX_BYTES, MAX_LINES = 32768, 128
ALLOWED = {
    "id", "name", "version", "description", "icon", "enabled",
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
    if values.get("icon", "cube") not in {"cube", "tool", "creature", "world", "magic", "decor"}:
        raise ValueError("icon inválido")

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
