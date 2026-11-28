# Registro de cambios — Minefade 2.0

## Mantenimiento MFMod para Minefade 2.4.0
- El generador y validador MFMod v3 usan 2.4.0 como versión actual del motor; los paquetes de prueba declaran compatibilidad 2.4.x.
- El runtime nativo permite omitir módulos de contenido opcionales, pero rechaza módulos JSON presentes que estén vacíos, dañados, sean enlaces simbólicos o no sean archivos regulares.
- Se añaden pruebas Python para el rango de compatibilidad 2.4.0 y una prueba nativa para la carga de paquetes con secciones ausentes/JSON corrupto.
- Esta corrección no cambia el versionCode ni versionName del APK 2.4.0; prepara el sistema MFMod para esa versión.

## Actualización 2.4.0 — inventario jugable
- Los 36 espacios del inventario pueden seleccionarse y transferirse entre sí; el movimiento no queda limitado a intercambiar con la casilla activa.
- Las pilas del mismo objeto se combinan hasta el límite definido en el registro de bloques.
- Se pueden dividir pilas en un espacio vacío y soltar unidades al mundo.
- La durabilidad de las herramientas acompaña al objeto al intercambiar espacios; las herramientas dañadas no se pueden dividir ni soltar todavía, para evitar perder el desgaste.
- El espacio seleccionado queda resaltado y el panel incorpora acciones directas para dividir y soltar.
- Se añaden pruebas nativas para fusión, división, extracción, límites y conservación de durabilidad, además de una prueba de integración de interfaz.
- Android versionCode 31 / versionName 2.4.0. Esta entrega requiere CI y validación táctil en Android físico.


## Candidata 2.3.1 — recuperación gráfica y rediseño de interfaz
- El chunk inicial se malla sincrónicamente antes de mostrar el mundo para evitar el primer fotograma vacío mientras arranca el trabajador de mallas.
- Se separa la restauración de OpenGL de la carga del mundo: al recrearse EGL se reconstruyen shaders, atlas, buffers, renderer de chunks, entidades y HUD sin reiniciar la sesión en memoria.
- Si la restauración gráfica falla, Android vuelve a abrir la última partida guardada como recuperación.
- La barra rápida usa nueve casillas centradas, mayor tamaño de iconos, selección destacada y contadores legibles.
- El HUD separa armadura, vida, hambre, nivel y barra de experiencia.
- Inventario y fabricación adoptan paneles azul pizarra, espacios más amplios, distribución adaptativa y mesa visual 2×2; las recetas existentes mantienen sus acciones reales.
- Se unifica el acabado de botones, navegación y tarjetas del menú principal, mundos y secciones.
- Se añaden pruebas de regresión para el chunk inicial, la recuperación EGL, el HUD y los accesos de interfaz.
- Android versionCode 30 / versionName 2.3.1. La compilación CI no sustituye la validación visual y táctil en Android físico.

## Candidata 2.3.0 — barra rápida y mano en primera persona

## Candidata 2.3.0 — barra rápida y mano en primera persona
- La barra conserva sus nueve IDs y el estado real de inventario; ancho, slots, iconos y márgenes se recalculan desde el ancho actual de Surface.
- La selección usa una placa clara de alto contraste y el nombre del objeto desaparece tras 1,25 segundos.
- Se añade un renderer independiente para manga, antebrazo, muñeca, palma, pulgar y dedos; reutiliza las paletas del avatar.
- Bloques usan atlas; palo, pico, hacha, pala, espada, azada y armadura tienen siluetas específicas.
- El viewmodel se dibuja en coordenadas de cámara después del transparente y antes del HUD, y se desactiva en tercera persona.
- Se añade balanceo de reposo/caminar, transición de cambio de item y gestos breves ligados a minado, colocación, ataque confirmado y comida consumida.
- Las subidas de sus tres capas son transaccionales y conservan el viewmodel anterior si alguna falla.
- La matriz visual final requiere capturas y pruebas en Android físico; el build/CI no sustituye esa validación.

## Candidata 2.3.0 — controles, renderer y progresión
- La quickbar se ajusta al ancho/densidad de pantalla; se reducen fondos y bordes para evitar tapar el mundo.
- El D-pad responde únicamente dentro de las cuatro direcciones visibles; el centro queda para agacharse y los huecos dejan de secuestrar gestos de cámara.
- Se corrige la propiedad de punteros en el HUD Android fallback, la actualización en vivo del diseño zurdo y el botón de chat superpuesto.
- El pase de transparencia ordena triángulos por distancia y conserva un fallback acotado para GPU/memoria móvil.
- Se añade XP/niveles y contadores de logros conectados a eventos de juego, guardados en un sidecar versionado por mundo.
- La pantalla de logros añade progreso de nivel y tarjetas de dos columnas.
- Android versionCode 29 / versionName 2.3.0. Requiere pruebas CI y validación en dispositivos físicos antes de declararse estable.
- Alcance, limitaciones y criterios de aceptación: docs/MINEFADE_2_3_0_FULL_AUDIT.md.

## Actualización 2.2.4 — creación, LAN y personalización
- La creación de mundo se organiza en General, Avanzado y Trucos, usando los campos reales que persiste WorldManager.
- El multijugador coloca Crear y Unirse junto a la salida y filtra las partidas LAN descubiertas por nombre o anfitrión.
- El avatar añade Aleatorio y Restablecer, miniaturas más grandes y un visor 3D compartido por las seis categorías.
## Actualización 2.2.4 — recuperación de agua y lava lejana
- El LOD2 conserva alturas independientes para terreno y fluidos; agua y lava vuelven a dibujarse en la capa transparente.
- Se incorpora una regresión nativa que verifica que un bloque de agua mantiene su superficie en un chunk distante.
- Android versionCode 28 / versionName 2.2.4. La firma persistente y las pruebas físicas siguen pendientes.

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
