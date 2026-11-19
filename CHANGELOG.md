# Registro de cambios — Minefade 2.0

## Actualización 2.1 — estabilidad del renderer y del repositorio
- Se limita la cantidad de chunks cuyo mesh se transfiere a GPU por pasada (1/2/3 según calidad), priorizando los más cercanos.
- Se conserva el trabajo de upload pendiente entre frames aunque no haya nuevos meshes CPU que reconstruir.
- Se valida el tamaño de los buffers antes de convertirlo a GLsizeiptr en Android de 32 bits.
- Se restaura viewport, scissor y máscaras de color/profundidad antes de limpiar el frame.
- El HUD de diagnóstico informa chunks pendientes, chunks GPU residentes y subidas de malla pendientes.
- CI valida pull requests, verifica los recursos generados sin commits automáticos y limita el permiso de escritura al borrador de prerelease.
- Android versionCode 24 / versionName 2.1.0; la estabilidad final sigue pendiente de pruebas físicas.

## Base inicial e importación
- Se importó la base de código nativa más completa, con app Android, C++20, Rust, OpenGL ES, RmlUi, catálogo, atlas, sonidos, pruebas y herramientas.
- Se restauró el atlas RGBA original (576×486; SHA de Git coincidente con el archivo fuente).
- Se añadieron documentos de arquitectura, compatibilidad de guardados y renovación del renderer.

## Compilación Android remota
- Se añadió GitHub Actions para probar y compilar APK debug con arm64-v8a y armeabi-v7a en cada actualización de `main`.
- Cada compilación correcta publica un artefacto de 90 días con suma SHA-256.
- Los tags `v*` preparan borradores de prerelease con APK adjunto.
- No se declara una Release estable sin pruebas Android físicas y firma persistente.

## Validación pendiente
- Primera ejecución correcta del pipeline Android.
- Instalación y prueba de mundos en Android.
- Firma de distribución persistente para actualizaciones in-place.
- Renovación del pipeline de meshing/LOD y medición real en dispositivos.
