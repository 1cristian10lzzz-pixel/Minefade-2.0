# Minefade 2.0

Juego voxel nativo para Android. Este repositorio es la base principal de desarrollo de Minefade 2.0.

> **Estado:** código fuente importado y documentado. Todavía no hay un APK nuevo verificado ni un Release 2.0 publicado desde este repositorio.

## Base de código

Importada desde el commit `562761841115903523126e5f8bd4db001f2fc061` de la rama `minefade-2.3-bedrock-controls-gameplay` del repositorio anterior. Se eligió por contener el conjunto más completo de código nativo, juego, Android, recursos y pruebas. El repositorio anterior permanece intacto.

El módulo Android está en la raíz (`app/`).

## Arquitectura

- **Android / Java:** actividad, pantalla principal, controles táctiles, gestión de mundos, audio, mods/shaders y descubrimiento LAN mediante NSD.
- **C++20:** motor, mundo voxel, jugador, catálogo de bloques, construcción de mallas y backend OpenGL ES 3.
- **Rust:** núcleo determinista de simulación y animación mediante ABI C.
- **Interfaz:** RmlUi 6.3 con backend OpenGL ES y FreeType 2.13.3.
- **Renderizado:** mallas por chunk, greedy meshing para cubos opacos, descarte por frustum, pases opaco/transparente y LOD de distancia inicial.
- **Contenido:** catálogo modular, definiciones JSON, sonidos y vista previa PNG del atlas.

## Sistemas presentes

Menú Android, HUD táctil, personalización del avatar, inventario/barra rápida, mundos separados, supervivencia/creativo/aventura, comandos, cofre inicial, fauna y animaciones básicas, mods de datos, paquetes de shaders y prototipo de multijugador LAN NSD/TCP.

La copia inicial no incluye el binario RGBA del atlas: el código Android dispone de un atlas de emergencia generado en tiempo de inicio cuando el recurso no está disponible. Se conserva la vista previa PNG para referencia. Hay que restaurar/generar el atlas real antes de considerar completa la revisión visual.

La presencia de código no equivale a validación en un teléfono. LAN, controles, renderizado y guardados requieren pruebas físicas.

## Compilar Android

Consulta [docs/ANDROID_BUILD_AND_RELEASE.md](docs/ANDROID_BUILD_AND_RELEASE.md). El proyecto requiere Android SDK/NDK, CMake, JDK, Gradle Wrapper y Rust/Cargo.

```bash
./gradlew --no-daemon :app:assembleDebug
```

Salida prevista: `app/build/outputs/apk/debug/app-debug.apk`. No se ha ejecutado una compilación desde este repositorio.

## Compatibilidad de mundos

El cargador reconoce `MFVOX001`, `MFVOX002` y `MFVOX003`; la escritura actual produce `MFVOX003`. Cualquier cambio de esquema debe incluir migración versionada y pruebas. No borrar datos al actualizar.

## Renderer: trabajo pendiente

La caché por chunk y la conservación de buffers GPU ya existen. Faltan snapshots inmutables, una cola de meshing persistente, priorización/cancelación por revisión y un presupuesto adaptativo de uploads GPU. El LOD lejano actual es una aproximación por altura, no el renderer completo de Distant Horizons. Véase [docs/RENDERER_UPGRADE_PLAN_2_0.md](docs/RENDERER_UPGRADE_PLAN_2_0.md).

## Versionado y Releases

- El desarrollo vive en `main`.
- Registrar cambios reales y pruebas en `CHANGELOG.md`.
- Crear GitHub Release solo con APK real, instalación comprobada, apertura/guardado de mundos y pruebas Android de las funciones modificadas.
- Cada Release debe indicar cambios, pruebas, ABI, versionCode/versionName y limitaciones.
- No usar GitHub Actions ni automatizaciones de compilación.

## Carpetas

`app/` Android/C++/assets · `rust-core/` Rust · `tests/` pruebas · `tools/` herramientas · `docs/` documentación.
