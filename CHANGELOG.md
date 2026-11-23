# Registro de cambios — Minefade 2.0

## Actualización 2.2.3 — renderizado voxel y fluidez de chunks
- Se corrige la oclusión de caras: solo un cubo opaco completo puede ocultar una cara entera; losas, escaleras, vallas y otras formas parciales dejan visibles las partes no cubiertas.
- La construcción de la malla de un chunk se traslada a una tarea de fondo con snapshots de solo lectura; el resultado se publica únicamente si la revisión del chunk sigue vigente.
- La generación de terreno usa un trabajador concurrente para reducir la competencia por CPU con la malla en móviles.
- Las plantas pequeñas se conservan en el anillo LOD cercano.
- La subida GPU usa buffers de staging y reemplaza juntas las capas opaca/transparente solo cuando ambas subidas se completan; si falla, se mantiene el chunk visible anterior.
- Se dibuja también el mundo cuando el único contenido cargado es transparente.
- Se añaden pruebas nativas para losas junto a cubos y para la ruta asíncrona de meshing.
- Android versionCode 27 / versionName 2.2.3. Sigue pendiente probar el APK en dispositivos físicos y medir FPS/jank reales.

## Actualización 2.2.2 — interfaz, controles, fabricación y LAN
- Se corrige la compilación Android del descubrimiento NSD: Android entrega atributos del servicio como `byte[]`; Minefade los decodifica como UTF-8 con límite de tamaño y validación.
- La fabricación se ejecuta sobre una copia temporal del inventario. Ingredientes y resultado se confirman juntos; si algo falla, el inventario original no cambia.
- Se aprovecha el espacio liberado al consumir ingredientes para guardar el objeto producido.
- Se amplía el recetario con antorchas, cofres, hornos, puertas, escaleras, vallas, losas, trampillas, señales, raíles, placas, botones y palancas.
- Se compacta la barra rápida, mejora el contraste del HUD y crece el área útil de inventario/fabricación.
- Se corrige el texto del inventario para indicar sus 36 espacios reales y se refleja el botón de ataque en el modo zurdo.
- Android versionCode 26 / versionName 2.2.2. La estabilidad aún requiere CI y pruebas físicas.

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
