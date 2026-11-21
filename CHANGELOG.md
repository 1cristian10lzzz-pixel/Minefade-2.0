# Registro de cambios — Minefade 2.0

## Versión candidata 2.4.0 — compatibilidad LAN con mods
- Cambia el protocolo LAN a v3 y negocia una huella SHA-256 de todos los mods activos en NSD y TCP.
- Rechaza a los clientes con paquetes/ajustes distintos antes de iniciar el mundo; incluye los mods v1 y los paquetes v2.
- Mantiene la protección por manifiesto de cada mundo y los IDs dinámicos cargados de forma determinista.
- Android versionCode 27 / versionName 2.4.0; pendiente de compilación CI y pruebas en dos teléfonos reales.

## Versión candidata 2.3.0 — MFMod v2
- Añade paquetes `.mfmod` ZIP con bloques y objetos personalizados, texturas PNG, recetas, biomas, estructuras y criaturas voxel definidas por datos.
- Permite sustituir eventos de audio existentes con archivos OGG/WAV validados.
- Fija la compatibilidad de las partidas a la huella SHA-256 del contenido instalado y conserva la carga de mods v1.
- La compatibilidad LAN con mods se incorpora en 2.4.0; en 2.3.0 se bloqueaba hasta contar con negociación de hashes.
- Versión Android candidata: versionCode 26 / versionName 2.3.0. Requiere superar la compilación y pruebas CI antes de distribuirse.

## Actualización 2.2 — sistema de mods de datos ampliado
- Se incorporan propiedades compatibles para velocidad agachada, velocidad pasiva/hostil, radios de huida y alerta, radio de aparición y límite de entidades.
- Se añade aparición periódica y limitada de fauna pasiva a una distancia configurable del jugador; permanece desactivada en mods antiguos que no indiquen intervalo.
- Se ajustan radio/vida de objetos soltados, cantidad de drops y velocidad de minería en supervivencia.
- Se sincronizan las validaciones Java, C++ y Python y se prueban rangos y valores inválidos.
- Se añade el mod de ejemplo Minefade Aventura Plus en examples/; empieza desactivado y no se instala desde assets.
- Se conserva la compatibilidad de mods de datos antiguos. Estas opciones requieren actualizar el APK a 2.2.0.
- Android versionCode 25 / versionName 2.2.0; la compilación sigue pendiente de CI y pruebas físicas.

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
