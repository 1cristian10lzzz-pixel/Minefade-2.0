# Registro de cambios — Minefade 2.0

Este archivo registra cambios realmente incorporados al repositorio. No representa por sí solo un Release binario.

## Base inicial e importación del proyecto

- Se importó la base de código seleccionada de la rama `minefade-2.3-bedrock-controls-gameplay`, commit `562761841115903523126e5f8bd4db001f2fc061`.
- Se conservaron la aplicación Android, motor C++20, núcleo Rust, backend OpenGL ES, UI RmlUi, catálogo de bloques, recursos de audio, gestión de mundos, avatar, controles, mods, shaders y prototipo LAN.
- Se conservaron pruebas nativas y scripts de validación/generación.
- Se dejaron fuera ramas/paquetes temporales, parches históricos y workflows de GitHub Actions.
- Se añadió documentación de arquitectura, compilación, estado, referencias de licencias y plan de renovación del renderer.

## Restauración del atlas de bloques

- Se recuperó el atlas RGBA original a partir de la vista previa PNG incluida en el repositorio.
- Dimensiones reconstruidas: 576×486; 1,119,744 bytes.
- SHA-1 de Git coincide con el blob original: `ba3e6c50403290f10a2bf06bc443fd73c55a5a43`. La reconstrucción es idéntica, no una aproximación.

### Validación pendiente
- Compilar APK Android desde este repositorio.
- Ejecutar tests nativos y Python en el entorno de build.
- Probar instalación, arranque, apertura y guardado de mundos existentes en Android físico.
- Comprobar renderer, pérdida/recreación de EGL, agua/transparencia, HUD y controles.
- Probar descubrimiento y sesión LAN entre dos dispositivos.

**No hay APK ni Release publicado como resultado de estas tareas.**
