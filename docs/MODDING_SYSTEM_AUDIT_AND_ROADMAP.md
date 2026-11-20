# Auditoría de Minefade 2.0: mods, contenido y brechas pendientes

**Fecha:** 20 de noviembre de 2026  
**Base inspeccionada:** `main`, commit `255355ee587b373ba3c096f3db18ad6eed4b425c`  
**Versión Android declarada en la base:** `2.1.0`, `versionCode 24`  
**Estado:** auditoría estática del código y documentación; no sustituye una prueba física del APK.

## 1. Resumen ejecutivo

Minefade 2.0 ya tiene un gestor de mods de datos, un cargador nativo en C++, validación Android/Python y perfiles de shader. Sin embargo, el sistema actual **no es todavía una API general de mods**: es una lista cerrada de ajustes de jugabilidad. El formato `.mfmod` v1 no admite scripts, bloques, objetos, recetas, entidades, biomas, estructuras ni recursos personalizados.

La principal oportunidad no es añadir indefinidamente más claves a una lista fija. Es separar el sistema en tres capas compatibles:

1. **Contenido declarativo y recursos**: bloques, objetos, recetas, entidades, biomas, estructuras, sonidos, modelos y texturas.
2. **Eventos y comportamiento**: una API estable para reaccionar a acciones del juego.
3. **Scripts seguros y limitados**: una capa opcional sobre la API, nunca carga de bibliotecas nativas descargadas.

El PR #2, «Minefade 2.2: ampliar el sistema MFMod de jugabilidad», ya cubre una tanda adicional de ajustes. No debe confundirse con una API de contenido: amplía la misma lista de parámetros predefinidos.

## 2. Versión más reciente comprobada

- La rama `main` inspeccionada declara Android `2.1.0`, `versionCode 24`.
- En la consulta de esta auditoría no había ninguna GitHub Release publicada en el repositorio 2.0.
- Existe el PR [#2 — Minefade 2.2: ampliar el sistema MFMod de jugabilidad](https://github.com/1cristian10lzzz-pixel/Minefade-2.0/pull/2). Está en estado **draft / abierto**, con rama `feature/mfmod-phase-2-3`, `versionName 2.2.0` y `versionCode 25`.
- En la última consulta, las pruebas Python y Rust/C++ del PR habían terminado correctamente; la tarea de compilar el APK Android seguía en ejecución. Por tanto, 2.2.0 no se considera integrada ni publicación estable.

## 3. Qué permite realmente el sistema de mods en main

### 3.1 Formato `.mfmod` v1

El archivo es texto UTF-8, no un ZIP. Contiene una propiedad `clave=valor` por línea. Exige `id`, `name`, `version` y `enabled`; admite `description` opcional.

Las claves reconocidas en main son:

| Área | Claves disponibles |
|---|---|
| Aparición | `spawn.sheep`, `spawn.cow`, `spawn.bird`, `spawn.pig`, `spawn.rabbit`, `spawn.zombie`, `spawn.skeleton` |
| Jugador | `player.speed`, `player.jump` |
| Entidades | `entity.speed` |
| Mundo | `world.day_speed` |

Límites observados: 32 KiB, 128 líneas, claves únicas, propiedades incluidas en una lista permitida y rangos numéricos acotados. Los paquetes se importan desactivados y se activan desde MODS. Los valores de varios paquetes se combinan con acumulación o multiplicación y límites máximos.

### 3.2 Flujo actual

- Android `ModManager` importa, valida, enumera y activa/desactiva archivos desde el almacenamiento privado de la aplicación.
- JNI entrega al motor la ruta de la carpeta de mods.
- C++ vuelve a validar el archivo y aplica los ajustes durante la inicialización del motor/mundo.
- Python mantiene otro validador para las pruebas y herramientas.
- El motor recorre los archivos por orden de nombre; no existe un gestor de dependencias, prioridades declaradas ni recarga en caliente.

### 3.3 Límites estructurales

- **No hay registro dinámico de bloques** ni IDs con namespace.
- **No hay registro de objetos independientes**, herramientas, armas o armaduras nuevas.
- **No hay recetas aportadas por mods**. Las recetas disponibles están codificadas en `Engine::CraftRecipe`.
- **No hay registro de nuevas entidades/modelos**. Las especies y sus tipos siguen siendo una lista fija del motor.
- **No hay contenido de biomas o estructuras definido por mods**.
- **No hay paquetes de texturas, sonidos, modelos, partículas o UI** asociados al formato `.mfmod`.
- **No hay bus de eventos ni API de scripting** para cambiar el comportamiento del juego.
- **No hay manifiesto por mundo** que fije la lista y las versiones/hash de los mods requeridos por una partida.
- **No hay protocolo de compatibilidad de mods para LAN** que verifique que todos los participantes usan el mismo conjunto de contenido.

### 3.4 Perfiles de shader

`.mfshader` es otro formato de datos, no un cargador de shaders arbitrarios. En v1 solo configura ondas de agua, animación de lava, viento de hojas y fuerza del viento. No reemplaza programas GLSL ni admite materiales/texturas propios.

## 4. Trabajo ya abierto para 2.2

El PR #2 añade ajustes para:

- Velocidad al agacharse y velocidades de criaturas pasivas/hostiles.
- Radios de huida, alerta y aparición.
- Límites de entidades y aparición periódica limitada de fauna pasiva.
- Radio de recogida, vida y cantidad de objetos soltados.
- Velocidad de minería.

Actualiza la validación Java, C++ y Python, agrega pruebas y un ejemplo desactivado por defecto. Es un avance útil de jugabilidad, pero mantiene el diseño de lista cerrada de propiedades. No incorpora bloques, texturas, recetas, nuevas entidades/modelos, biomas, estructuras, lluvia, monturas ni misiones; el propio PR los declara fuera de alcance.

**Recomendación:** dejar que el PR #2 termine su compilación y revisión como una mejora de jugabilidad aislada. No mezclar en ese PR una reescritura del formato ni un nuevo runtime de scripts.

## 5. Comparación con el repositorio antiguo

La importación que dio origen a Minefade 2.0 tomó como fuente el repositorio antiguo, rama `minefade-2.3-bedrock-controls-gameplay`, commit `562761841115903523126e5f8bd4db001f2fc061`. Las notas de importación indican que se copiaron el motor, la app Android, Rust/C++, recursos, pruebas, herramientas y documentación; se excluyeron workflows antiguos, parches temporales y revisiones históricas.

Al comparar las ramas antiguas, `minefade-1.1-integral-upgrade` estaba divergida respecto a la rama 2.3 importada. En esa rama histórica el gestor incluía funciones de autoría que no aparecen en el gestor actual:

- Crear un archivo `.mfmod` desde la propia interfaz.
- Elegir un icono/categoría para el mod.
- Generar un paquete de ejemplo.
- Instalar un paquete base incluido en la aplicación.

En la versión actual el panel MODS permite buscar, importar y activar/desactivar; no muestra una herramienta para crear un mod. La eliminación de paquetes demo automáticos puede ser intencional y conviene conservarla: los mods del usuario deben ser explícitos, no instalarse silenciosamente.

Estas funciones antiguas son una **pérdida de comodidad del gestor**, no una API de contenido más potente. Su recuperación es de prioridad media y no debe confundirse con añadir soporte para nuevas mecánicas o assets.

## 6. Funciones del juego que sí están presentes

Para evitar duplicar trabajo o declarar pérdidas falsas, esta revisión confirmó que la base 2.0 contiene:

- Editor de avatar y personalización.
- Gestor de mundos separados y modos de juego.
- Panel de logros con diez objetivos y contadores de progreso.
- Recetas codificadas para materiales, herramientas y piezas de armadura.
- Catálogo de bloques integrado y atlas de texturas del propio juego.
- Comandos de juego y fauna con tipos predefinidos.
- Prototipo de multijugador LAN mediante descubrimiento NSD y transporte TCP.
- Perfiles de shader de datos, separados del formato de mods.

Que exista una función básica no significa que sea extensible por mods ni que tenga paridad con Minecraft. Por ejemplo, las recetas actuales existen, pero no hay un registro para que un mod añada recetas propias.

## 7. Brechas de contenido/jugabilidad confirmadas o documentadas como pendientes

La documentación técnica actual y la inspección de los puntos de entrada indican estas tareas:

### Jugabilidad y contenido
- Registro extensible para objetos, herramientas, armas y armaduras, con durabilidad, estadísticas, daño, protección y efectos.
- Sistema general de salud, daño, combate, hambre y consumo de alimentos.
- Recetas definidas en datos y ampliables por mods; estaciones de fabricación y reglas de ingredientes completas.
- Entidades añadibles por paquetes, con modelos, animaciones, atributos, drops y comportamiento configurable.
- IA de producción, navegación/pathfinding y sincronización de animaciones/estados de entidades remotas.
- Biomas y estructuras de mundo definibles por datos, con generación determinista por semilla.
- Lluvia y clima; simulación de fluidos con niveles/caudal, además de refracción y transparencias más sólidas.
- Almacenamiento/automatización con cofres y tolvas funcionales y un inventario ampliado.
- Multijugador por Internet; el prototipo LAN aún necesita validación con dos dispositivos y sincronización más completa.

### Gráficos y rendimiento
- Rig esquelético reutilizable para jugador y criaturas; los modelos actuales son piezas voxel articuladas, no un sistema general GLB/GLTF.
- Shadow maps y sombras dinámicas proyectadas; las sombras actuales son de contacto.
- LOD lejano que represente cuevas, túneles y voladizos, no solo alturas por columna.
- Snapshots inmutables para meshing, cola persistente de trabajos, cancelación por revisión y presupuesto GPU por bytes/tiempo por frame.
- Descarga segura de chunks fuera de rango y presupuesto de RAM/GPU.
- Pruebas físicas de pérdida/restauración EGL, pantalla blanca, bandas negras, transparencias, HUD y distintas GPUs.

## 8. Arquitectura recomendada para que los mods puedan cambiar de verdad el juego

### Etapa 1 — Manifiesto y paquete extensible

Crear `.mfmod` v2 como archivo ZIP con una estructura fija:

```text
manifest.json
data/blocks.json
data/items.json
data/recipes.json
data/entities.json
data/biomes.json
data/structures.json
assets/textures/
assets/models/
assets/sounds/
scripts/                 # opcional en una etapa posterior
```

El manifiesto debe declarar:
- ID único con namespace, nombre, versión semántica y versión mínima/máxima de Minefade.
- Dependencias, incompatibilidades y orden/prioridad.
- Recursos y módulos presentes.
- Capacidades solicitadas y permisos.
- Hashes de recursos y datos de compatibilidad.

El importador debe validar rutas, evitar Zip Slip, limitar tamaño comprimido y descomprimido, cantidad de archivos y dimensiones de imagen, y escribir primero en una carpeta temporal antes de instalar atómicamente.

### Etapa 2 — Registros de contenido

Crear registros extensibles, construidos antes de cargar el mundo:

1. Bloques y materiales.
2. Objetos, herramientas, armas y armaduras.
3. Recetas, drops y tablas de botín.
4. Entidades, atributos y modelos.
5. Biomas, estructuras y reglas de generación.
6. Sonidos, partículas y recursos visuales.

Usar identificadores estables como `modid:block_name`. No reutilizar ni cambiar los IDs numéricos actuales sin migración. Los guardados deben almacenar identificadores estables o una tabla de registro versionada; los mundos antiguos MFVOX001/002/003 deben seguir abriendo. Si falta un mod al cargar un mundo, mostrar un aviso y preservar los datos desconocidos en lugar de borrar bloques/objetos silenciosamente.

### Etapa 3 — Bus de eventos y API de comportamiento

Exponer eventos versionados con contexto limitado:

- `on_world_load`, `on_world_tick`
- `before_block_break`, `after_block_break`, `before_block_place`
- `on_item_use`, `on_craft`, `on_player_damage`
- `on_entity_spawn`, `on_entity_death`
- `on_loot_generate`, `on_structure_place`

Cada evento debe declarar si permite cancelar la acción, modificar datos o solo observar. Añadir prioridades, orden determinista, límites por evento y aislamiento de errores. Los callbacks de mods no deben llamar directamente a OpenGL ni modificar el mundo desde workers: deben emitir operaciones verificadas que el motor aplique en el hilo de simulación.

### Etapa 4 — Scripting opcional y seguro

No cargar bibliotecas `.so`, DEX/JAR ni código nativo descargado. Android advierte que el código cargado dinámicamente se ejecuta con los permisos de la aplicación y aumenta la superficie de inyección/manipulación.

Para scripts, evaluar Lua embebido (la documentación oficial lo diseña como lenguaje extensible embebido en una aplicación C) con:
- Bibliotecas peligrosas deshabilitadas (`io`, `os`, `package`, `debug` y acceso directo al sistema).
- Allocator personalizado con límite de memoria por mod.
- Presupuesto de instrucciones/tiempo por callback y límite de callbacks por tick.
- API de host explícita; sin filesystem, red ni JNI general.
- Permisos por capacidad, logs, aislamiento de fallos y botón para desactivar el mod problemático.

La VM debe ser un componente opcional y versionado. No introducir scripting hasta que los registros de contenido y los eventos estén probados. WebAssembly es otra opción de aislamiento, pero requiere medir tamaño, rendimiento y mantenimiento del runtime en Android antes de seleccionarlo.

### Etapa 5 — Mundos y multijugador

Guardar junto a cada mundo un manifiesto de mods activos con ID, versión y hash. El mundo debe indicar si faltan mods o si una versión no es compatible. En LAN, el anfitrión debe anunciar la lista requerida; los clientes comparan hashes y versiones antes de entrar. Primero sincronizar datos de bloques, objetos y entidades de forma autoritativa; después plantear Internet.

### Etapa 6 — Herramientas para creadores

Recuperar el creador de mods desde el gestor y ampliarlo:
- Crear/editar manifiestos y paquetes de datos.
- Icono, nombre, descripción y versión.
- Previsualizar recursos y validar errores antes de importar.
- Duplicar, exportar, actualizar y desinstalar sin borrar partidas.
- Mostrar dependencias, conflictos y permisos.
- Incluir ejemplos pequeños y documentación, sin activar ningún mod por defecto.

## 9. Prioridad propuesta

| Prioridad | Trabajo | Motivo |
|---|---|---|
| P0 | Terminar y validar PR #2 (2.2) | Amplía el control de jugabilidad existente sin reescribir el motor. |
| P1 | Unificar el esquema/validador y versionar el manifiesto | Evita que Java, C++, Python y las herramientas acepten formatos distintos. |
| P1 | Creador de mods y paquetes por mundo | Mejora el flujo de creadores y evita que cada mundo dependa de la carpeta global. |
| P2 | Registros dinámicos de bloques/objetos/recetas | Es la primera ampliación real de contenido jugable. |
| P2 | Texturas, modelos y sonidos externos | Hace visibles los nuevos contenidos y permite resource packs. |
| P3 | Entidades, biomas, estructuras y loot | Añade nuevos sistemas y variedad de mundo. |
| P3 | Bus de eventos estable | Permite cambiar reglas sin añadir una clave por cada caso. |
| P4 | Lua u otra VM aislada | Habilita lógica avanzada; debe llegar después de estabilizar la API. |
| P4 | Sincronización de mods en LAN e Internet | Impide mundos incompatibles y estados divergentes. |

## 10. Criterios para aceptar la ampliación

- Compatibilidad probada con todos los mods v1 válidos y los ejemplos de 2.2.
- IDs existentes del catálogo y guardados antiguos preservados.
- Pruebas de importación corrupta, duplicados, dependencias circulares, rutas maliciosas, tamaños extremos y archivos comprimidos-bomba.
- Pruebas con dos o más mods que modifican el mismo recurso/regla, con orden determinista y conflictos visibles.
- Guardar, cerrar y reabrir mundos con mods; abrir mundos sin el mod y recuperar los datos cuando se vuelva a instalar.
- Pruebas de LAN con hashes iguales y desiguales.
- Pruebas Android arm64-v8a y armeabi-v7a; dispositivos físicos de gama baja/media.
- No anunciar soporte de scripts, shaders arbitrarios o entidades nuevas hasta que haya pruebas específicas para esas funciones.
- No declarar APK estable por pasar CI solamente: instalarlo, jugar, guardar, reabrir y probar el dispositivo físico.

## 11. Archivos de referencia inspeccionados

- `docs/MF_MOD_FORMAT_V1.md`
- `docs/MF_SHADER_FORMAT_V1.md`
- `docs/PROJECT_STATUS_2_0.md`
- `docs/ENGINE_ARCHITECTURE.md`
- `docs/MEGA_UPDATE_2_1.md`
- `docs/RENDERER_UPGRADE_PLAN_2_0.md`
- `app/src/main/java/com/minefade/nativeengine/ModManager.java`
- `app/src/main/java/com/minefade/nativeengine/ShaderPackManager.java`
- `app/src/main/java/com/minefade/nativeengine/MinefadeHomeScreen.java`
- `app/src/main/cpp/engine/Engine.cpp`
- `app/src/main/cpp/world/VoxelWorld.cpp`
- `app/src/main/cpp/game/Player.cpp`
- `tools/validate_mfmod.py`
- `tests/test_mfmod_pack.py`

## Fuentes externas de diseño y seguridad

- Manual oficial de Lua 5.4: https://www.lua.org/manual/5.4/manual.html
- Historial oficial de Lua y versiones publicadas: https://www.lua.org/versions.html
- Eventos como puntos de extensión compatibles (referencia de Fabric): https://docs.fabricmc.net/develop/events/
- Seguridad Android sobre carga dinámica de código: https://developer.android.com/privacy-and-security/risks/dynamic-code-loading
- Lista de seguridad Android: https://developer.android.com/privacy-and-security/security-tips

**Resultado:** el camino recomendado es convertir Minefade de una colección de ajustes MFMod a un sistema de contenido extensible, versionado y seguro. La ampliación de parámetros de 2.2 es útil, pero debe ser una fase intermedia, no el destino del sistema.
