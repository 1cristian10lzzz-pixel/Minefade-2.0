# MFMod v2 — paquetes de contenido de Minefade

## Objetivo

MFMod v2 amplía el formato v1 (archivo de configuración de texto) con paquetes ZIP de datos y recursos. No carga bibliotecas nativas, APKs, JAR/DEX ni scripts ejecutables. El formato mantiene compatibilidad con mods v1.

## Estructura del archivo

Un archivo `.mfmod` v2 es un ZIP con esta estructura:

```text
mi_mod.mfmod
├── manifest.json
└── textures/
    ├── mineral_top.png
    ├── mineral_side.png
    └── mineral_bottom.png
```

El importador rechaza rutas absolutas, `..`, barras invertidas, archivos duplicados y cualquier carga que no sea `manifest.json` o una textura PNG dentro de `textures/`.

## Ejemplo de manifest.json

```json
{
  "format": 2,
  "id": "ruby_pack",
  "name": "Ruby Pack",
  "version": "1.0.0",
  "description": "Añade mena de rubí y rubíes",
  "blocks": [
    {
      "id": "ruby_ore",
      "name": "Mena de rubí",
      "hardness": 3.0,
      "sound": "stone",
      "textures": {
        "top": "textures/mineral_top.png",
        "side": "textures/mineral_side.png",
        "bottom": "textures/mineral_bottom.png"
      },
      "shape": "cube",
      "tool": "pickaxe",
      "stack": 64,
      "light": 0,
      "transparent": false
    }
  ],
  "items": [
    {
      "id": "ruby",
      "name": "Rubí",
      "texture": "textures/ruby.png",
      "stack": 64
    }
  ],
  "recipes": [
    {
      "id": "polished_ruby",
      "name": "Pulir rubí",
      "requiresTable": true,
      "output": { "id": "ruby_pack:ruby", "count": 1 },
      "ingredients": [
        { "id": "ruby_pack:ruby_ore", "count": 1 }
      ]
    }
  ],
  "biomes": [
    {
      "id": "crystal_forest",
      "name": "Bosque de cristal",
      "temperature": { "min": -0.2, "max": 0.7 },
      "humidity": { "min": 0.2, "max": 1.0 },
      "continental": { "min": -0.1, "max": 0.9 },
      "topBlock": "ruby_pack:ruby_ore",
      "fillerBlock": "minecraft:dirt",
      "priority": 10,
      "surfaceOffset": 1
    }
  ]
}
```

## Identificadores

- Los IDs locales usan minúsculas, números, punto, guion y guion bajo.
- Los recursos del propio paquete usan `<id-del-mod>:<id-local>`, por ejemplo `ruby_pack:ruby_ore`.
- Para recetas pueden usarse IDs del juego base sin namespace o recursos de otro mod con namespace.
- Los IDs duplicados dentro del paquete se rechazan. Los conflictos entre paquetes se resuelven por orden estable de carga: el primer ID ya registrado gana y el duplicado no se instala en el registro del motor.

## Límites de importación

| Límite | Máximo |
|---|---:|
| ZIP comprimido | 16 MB |
| Contenido descomprimido total | 32 MB |
| Tamaño de cada archivo | 4 MB |
| Entradas ZIP | 256 |
| Textura PNG | 128 × 128 px |
| Bloques + objetos por mod | 128 |
| Recetas por mod | 128 |
| Biomas por mod | 32 |
| Ingredientes por receta | 5 |
| Salida de receta | 1–64 |
| Ingrediente | 1–64 |

Se admiten sonidos de bloque de la lista integrada: `stone`, `wood`, `gravel`, `sand`, `grass`, `glass` y `metal`. Las formas de bloque admitidas son las formas ya implementadas por el motor: cubo, losa, escalera, planta cruzada, panel fino, cristal, valla, puerta, trampilla, fluido y andamio.

## Qué aplica actualmente el motor

- Bloques personalizados con nombre, dureza, resistencia, forma, herramienta preferida, nivel de luz y tres caras de textura.
- Objetos personalizados con textura y límite de pila. En esta primera integración se representan como recursos de inventario y no como armas/armaduras con comportamiento propio.
- Recetas personalizadas visibles en la mesa de fabricación, con verificación y consumo de ingredientes.
- Biomas definidos por rangos de temperatura, humedad y continentalidad; permiten escoger un bloque de superficie, uno de relleno, prioridad y pequeño ajuste de altura.
- Atlas RGBA extendido compartido por el mundo y los iconos del inventario.
- Mods v1 y sus ajustes de jugabilidad continúan importándose.

El mundo guarda un manifiesto lateral con la lista de mods de contenido y sus versiones. El motor bloquea una carga incompatible para evitar interpretar IDs guardados como bloques distintos. El multijugador con mods de contenido permanece bloqueado hasta que exista una negociación de hashes y versiones entre anfitrión y clientes.

## Lo que todavía no ofrece MFMod v2

El soporte de estructuras, entidades/modelos/IA, audio personalizado, pantallas/UI y logros personalizados aún no forma parte de la API ejecutable descrita arriba. No se deben anunciar como funciones disponibles. La carga de código nativo o scripts de terceros queda fuera del formato por seguridad; un futuro sistema de eventos deberá usar una API de capacidades limitada y un sandbox con presupuesto de ejecución.
