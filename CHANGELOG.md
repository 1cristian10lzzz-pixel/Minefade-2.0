# Registro de cambios — Minefade 2.0

Este archivo registra cambios realmente incorporados al repositorio. No representa por sí solo un Release binario.

## Base inicial del repositorio

- Se importó la base de código seleccionada de la rama `minefade-2.3-bedrock-controls-gameplay`, commit `562761841115903523126e5f8bd4db001f2fc061`.
- Se conservó la aplicación Android, motor C++20, núcleo Rust, backend OpenGL ES, UI RmlUi, catálogo de bloques, recursos de audio, gestión de mundos, avatar, controles, mods, shaders y prototipo LAN.
- Se conservaron pruebas nativas y scripts de validación/generación.
- Se dejaron fuera ramas/paquetes temporales, parches históricos y workflows de GitHub Actions.
- Se añadieron documentación actualizada de arquitectura, construcción, estado y plan de renderer.

### Validación pendiente
- Compilar APK Android desde esta copia.
- Ejecutar pruebas nativas y pruebas Python en el entorno de build.
- Probar instalación, arranque, apertura y guardado de mundos existentes en Android físico.
- Comprobar renderer, pérdida/recreación de EGL, agua/transparencia, HUD y controles.
- Probar descubrimiento y sesión LAN entre dos dispositivos.

**No hay APK ni Release publicado como resultado de esta importación.**
