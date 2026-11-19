# Notas de importación — base Minefade 2.0

- Fuente: `1cristian10lzzz-pixel/MINEFADE-`, rama `minefade-2.3-bedrock-controls-gameplay`, commit `562761841115903523126e5f8bd4db001f2fc061`.
- El proyecto Gradle de `MinefadeNativeEngine/` se colocó directamente en la raíz para que `./gradlew :app:assembleDebug` se ejecute desde el directorio principal.
- Se importaron el motor C++/Rust, la app Android, catálogo JSON, sonidos, tests, scripts y documentación técnica vigente.
- Se dejaron fuera los workflows de GitHub Actions, los parches temporales, fragmentos de revisión, notas de releases antiguas y auditorías/roadmaps históricos de versiones anteriores. No se incluyó historial de ramas ni se modificó el repositorio fuente.
- Se conservó `docs/block_atlas_preview.png`, pero el conector no pudo recuperar el binario `app/src/main/assets/textures/block_atlas.rgba` (el archivo fuente mide más de 1 MB y la respuesta devolvió contenido vacío). No se insertó un archivo vacío. La app contiene una rutina de atlas de emergencia para cuando el recurso no está disponible; hasta restaurar el RGBA real, la apariencia de bloques usará ese fallback.
- No se ejecutó Gradle/CMake/Cargo, no se generó APK y no se creó GitHub Release en esta importación.

La siguiente tarea de contenido debería regenerar/restaurar `block_atlas.rgba` desde los datos/atlas originales y ejecutar la suite de pruebas antes de cualquier APK distribuible.
