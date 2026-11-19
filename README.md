# Minefade 2.0

Juego voxel nativo para Android. Este es el repositorio principal de desarrollo.

> **Estado:** fuente y recursos importados; el APK se compila ahora en GitHub Actions. Ninguna compilación se considera Release estable hasta superar las pruebas en Android real.

## Descargar el APK

Cada cambio enviado a `main` inicia una compilación remota. Para descargarla desde el teléfono:

1. Abre [Actions](https://github.com/1cristian10lzzz-pixel/Minefade-2.0/actions).
2. Selecciona la ejecución más reciente **Minefade Android APK**.
3. Descarga el artefacto `Minefade-2.0-APK-...` al final de la página.

Los tags `v*` generan un borrador de prerelease con el APK y su SHA-256. Se publica como estable solo después de probar instalación, mundos y controles en un teléfono.

## Arquitectura

- Android / Java: actividad, UI, controles, mundos, audio, mods/shaders y descubrimiento LAN NSD.
- C++20: motor voxel, jugador, meshing y backend OpenGL ES 3.
- Rust: núcleo de simulación mediante ABI C.
- RmlUi 6.3 y FreeType 2.13.3.
- Chunks, greedy meshing, culling por frustum, transparencias y LOD de distancia inicial.

## Sistemas presentes

Menú Android, HUD táctil, personalización del avatar, inventario, mundos separados, modos supervivencia/creativo/aventura, comandos, fauna, mods de datos, shaders y prototipo LAN NSD/TCP.

## Compatibilidad de mundos

El cargador reconoce `MFVOX001`, `MFVOX002` y `MFVOX003`; la escritura actual produce `MFVOX003`. No cambiar formatos ni IDs de bloque sin migración versionada y pruebas.

## Estado del renderer

La caché por chunk y buffers GPU incrementales ya existen. Faltan snapshots inmutables, cola de meshing persistente, cancelación por revisión y presupuesto adaptativo de uploads. El LOD lejano actual es una aproximación por altura, no el renderer completo de Distant Horizons. Véase [el plan técnico](docs/RENDERER_UPGRADE_PLAN_2_0.md).

## Organización

- `app/` — app Android, C++, recursos.
- `rust-core/` — núcleo Rust.
- `tests/` — pruebas.
- `tools/` — herramientas de catálogo/mods/shaders.
- `docs/` — arquitectura y validación.

## Historial y seguridad

- La base se importó desde el commit `562761841115903523126e5f8bd4db001f2fc061` del repositorio anterior.
- El repositorio anterior permanece intacto.
- No se almacenan contraseñas ni claves privadas de firma en Git.
