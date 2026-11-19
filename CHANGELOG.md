# Registro de cambios — Minefade 2.0

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
