# Notas de importación — base Minefade 2.0

- Fuente: `1cristian10lzzz-pixel/MINEFADE-`, rama `minefade-2.3-bedrock-controls-gameplay`, commit `562761841115903523126e5f8bd4db001f2fc061`.
- El proyecto Gradle de `MinefadeNativeEngine/` se colocó directamente en la raíz para que `./gradlew :app:assembleDebug` se ejecute desde el directorio principal.
- Se importaron el motor C++/Rust, la app Android, catálogo JSON, sonidos, tests, scripts y documentación técnica vigente.
- Se dejaron fuera los workflows de GitHub Actions, los parches temporales, fragmentos de revisión, notas de releases antiguas y auditorías/roadmaps históricos de versiones anteriores. No se importó el historial de ramas ni se modificó el repositorio fuente.
- El conector GitHub no pudo transportar el binario RGBA original directamente. Se reconstruyó sin pérdida a partir de `docs/block_atlas_preview.png`: PNG 576×486, RGBA 8 bits, 1,119,744 bytes. El blob reconstruido tiene el mismo SHA-1 de Git que el archivo original: `ba3e6c50403290f10a2bf06bc443fd73c55a5a43`. Por tanto, el contenido binario coincide exactamente con el recurso original.
- El atlas está en `app/src/main/assets/textures/block_atlas.rgba` y la vista previa sigue en `docs/block_atlas_preview.png`.
- No se ejecutó Gradle/CMake/Cargo, no se generó APK y no se creó GitHub Release en esta importación.

La siguiente tarea es configurar/verificar toolchain Android, ejecutar la suite de pruebas y compilar el primer APK desde este repositorio.
