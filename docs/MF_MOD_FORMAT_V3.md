# MFMod v3 — Minefade 2.4.0

## Compatibilidad

El generador `tools/mfmod3.py` usa Minefade **2.4.0** como versión de motor predeterminada. Un paquete creado para esta versión declara, por defecto, `engine_min: 2.4.0` y `engine_max: 2.4.99`. El importador Android compara ese rango con el `versionName` real de la aplicación.

El paquete es un ZIP con extensión `.mfmod3`. No ejecuta código nativo, Java, DEX, JAR, Lua ni scripts externos.

## Estructura

```text
manifest.json
data/
  gameplay.mfmod
  blocks.json       # opcional
  items.json        # opcional
  recipes.json      # opcional
  entities.json     # opcional
  biomes.json       # opcional
  structures.json   # opcional
  ui.json           # opcional
  sounds.json       # opcional
assets/
  textures/
  models/
  sounds/
  ui/
```

Los recursos referenciados deben existir dentro del mismo paquete. El manifiesto registra el tamaño y SHA-256 de cada archivo. La importación limita el número de entradas, el tamaño comprimido/expandido, las rutas y los tipos de recurso; el paquete se instala desactivado.

## Reglas importantes

- Los IDs de contenido nuevo deben usar el namespace del mod, por ejemplo `aventura:block_basalt`.
- Las referencias a contenido pueden apuntar al propio mod, `minecraft:` o `minefade:`.
- Las texturas RGBA usadas por el atlas de bloques, iconos y texturas de entidades deben ser 16×16 píxeles (1024 bytes).
- Las tablas de drops admiten de uno a cuatro resultados, con cantidad mínima/máxima y probabilidad entre 0 y 1.
- Las recetas admiten hasta nueve ingredientes y estaciones declaradas por el formato.
- Los módulos opcionales pueden omitirse. Si un archivo JSON opcional existe, debe ser JSON válido y tener la estructura esperada; un archivo corrupto no se ignora.
- Las partidas antiguas y los archivos MFMod v1/v2 siguen usando sus rutas de compatibilidad; esta actualización no los convierte automáticamente.

## Crear y validar

Desde la raíz del repositorio:

```bash
python3 tools/mfmod3.py create RUTA_DEL_MOD salida.mfmod3 --engine-min 2.4.0 --engine-max 2.4.99
python3 tools/mfmod3.py validate salida.mfmod3
python3 -m pytest -q tests/test_mfmod3.py
```

El formato v3 permite describir contenido y recursos, pero una capacidad declarada en el manifiesto no debe considerarse activa en el juego hasta que exista integración runtime y pruebas para esa función. En particular, el paquete no habilita scripts arbitrarios ni reemplaza automáticamente las pantallas base.
