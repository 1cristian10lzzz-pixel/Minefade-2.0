# Estado técnico verificado — Minefade 2.0

Fecha de auditoría: 19 de noviembre de 2026.

## Identidad de la base

- Repositorio de origen: `1cristian10lzzz-pixel/MINEFADE-`.
- Rama de origen: `minefade-2.3-bedrock-controls-gameplay`.
- Commit de origen: `562761841115903523126e5f8bd4db001f2fc061`.
- Paquete Android actual: `com.minefade.nativeengine`.
- `versionCode = 23`, `versionName = "2.0.3"` en `app/build.gradle.kts`.
- `minSdk = 24`, `targetSdk = 36`; ABI `arm64-v8a` y `armeabi-v7a`.
- C++20, OpenGL ES 3.0, Rust mediante ABI C, RmlUi 6.3 y FreeType 2.13.3.

La etiqueta de versión de Gradle se conserva en esta importación. No se ha incrementado ni se ha declarado una nueva versión distribuible porque todavía no se ha compilado ni probado un APK desde el repositorio de destino.

## Qué existe en el código

### Mundo y chunks

`VoxelWorld` mantiene chunks de 16×16 con altura de mundo de 96 bloques. Hay cola de generación, deduplicación mediante conjunto de coordenadas y hasta dos trabajos `std::async` para generación. `RequestGenerateAround` reconstruye la lista de chunks faltantes para el radio solicitado; limpia la cola de solicitudes anteriores antes de rellenarla con la nueva región. Si no puede crear workers, puede generar chunks de forma síncrona en `PumpChunkGeneration`.

Los cambios de bloques invalidan la malla del chunk afectado y de sus vecinos. La caché de mallas es por chunk y lleva una revisión para que el renderer no vuelva a subir los chunks sin cambios.

### Mesh y render GPU

`BuildMesh` selecciona chunks por distancia a la cámara y puede limitar los chunks que reconstruye por llamada. Usa hasta dos workers CPU con `std::async`, pero espera (`get`) a que terminen antes de salir del método. Los workers leen los chunks seleccionados a través de referencias al estado del mundo; no se crea todavía un snapshot inmutable independiente ni una cola persistente de trabajos de meshing.

El camino normal pide presupuesto de una malla por llamada, pero la tarea no queda desacoplada del método que la programa. Los resultados se escriben en la caché antes de retornar.

`WorldMeshRenderer::Sync` conserva recursos VAO/VBO por chunk y actualiza una malla si cambia su revisión. La subida usa `glBufferData`. En la implementación actual, la llamada recorre todos los chunks y sube todos los que hayan cambiado; no hay un presupuesto explícito de bytes o milisegundos de upload por frame. Estas llamadas se hacen desde la ruta gráfica de Engine.

Hay pases separados de geometría opaca y transparente; los transparentes se ordenan aproximadamente por distancia de chunk. El descarte de terreno usa AABB/frustum.

### LOD lejano y Distant Horizons

El LOD de Minefade se elige por distancia de chunk: nivel completo cerca, un nivel simplificado intermedio y un nivel lejano de altura/material. El nivel más lejano calcula una altura por columna y emite superficie superior más laterales donde baja la altura vecina. Es una aproximación de terreno distante: no conserva cuevas, túneles, voladizos ni geometría subterránea en esa representación.

La rama declara que esta ruta está inspirada en ideas de LOD de Distant Horizons, pero no contiene el Core de Distant Horizons integrado. El ZIP proporcionado contiene el repositorio principal y su referencia a un submódulo, pero no el contenido de ese submódulo.

### Guardados

`Save` escribe el formato `MFVOX003`. `Load` reconoce `MFVOX001`, `MFVOX002` y `MFVOX003` y realiza remapeos de IDs de bloque/inventario para el formato antiguo. El cargador valida conteos, datos de jugador y bloques desconocidos. La nueva arquitectura del renderer no debe cambiar la estructura del guardado del mundo: las mallas y el LOD son datos derivados, no la fuente de verdad.

### Android / EGL

`MinefadeSurfaceView` usa `GLSurfaceView`, solicita contexto OpenGL ES 3 y serializa llamadas JNI de entrada/renderizado en el hilo gráfico. Activa la preservación del contexto EGL durante pausa cuando el sistema lo permite. El ciclo real puede recrear Surface/contexto; la restauración completa de recursos GPU debe comprobarse con pruebas en dispositivo.

### Otros subsistemas presentes

El código incluye pantallas Android, controles táctiles, editor de avatar, HUD/Inventario RmlUi con fallback Android, gestión de mundos, audio, comandos, catálogo de bloques, shaders/mods de datos y prototipo LAN NSD/TCP.

## Riesgos y pendientes confirmados por inspección

1. El meshing no dispone todavía de snapshots inmutables ni de una cola de trabajos que sobreviva entre frames; los workers finalizan dentro de la llamada a `BuildMesh`.
2. La subida GPU no tiene límite adaptativo por frame; sincronizar muchos chunks modificados puede producir picos.
3. El LOD lejano por altura pierde estructuras subterráneas y voladizos.
4. La generación alrededor del jugador limpia y reconstruye la cola; se debe medir si esto produce trabajo repetido bajo movimiento rápido.
5. La reconstrucción de recursos al perderse el contexto EGL requiere validación física.
6. El HUD usa una ruta gráfica propia; la franja negra reportada debe investigarse midiendo Surface, insets, viewport, scissor y framebuffer, no corrigiendo la cámara a ciegas.
7. El prototipo LAN necesita pruebas con dos dispositivos Android y validación de sincronización/estados desconectados.
8. No hay medidas de FPS, latencia de chunks o consumo térmico válidas en dispositivos reales asociadas a esta revisión.

## Cambios implementados en la actualización 2.1

- `WorldMeshRenderer::Sync` recibe la posición de cámara y un máximo de chunks por pasada; ordena los meshes sucios por distancia y transfiere primero los cercanos.
- Presupuesto por calidad: 1 chunk en calidad baja, 2 en media y 3 en alta. El renderer conserva contadores de subidas realizadas y pendientes.
- El motor sincroniza GPU cada intervalo de meshing incluso cuando la cola CPU ya está limpia; así drena el trabajo aplazado por el presupuesto anterior.
- Se valida que el tamaño del buffer cabe en `GLsizeiptr` antes de llamar a `glBufferData`, incluido ARMv7.
- Cada frame restaura viewport, scissor, máscara de color, máscara de profundidad y depth function antes del clear.
- El HUD incluye chunks pendientes, chunks GPU residentes y subidas pendientes.
- CI valida pull requests y no publica commits automáticos de recursos desde el job de compilación. El permiso de prerelease se encuentra en un job separado.

Esta etapa **no** implementa todavía snapshots inmutables, una cola persistente de meshing ni límites de bytes/milisegundos dentro de la transferencia de un único chunk. El upload continúa ocurriendo en el hilo EGL; hay que medirlo en Android real antes de ajustar los presupuestos.

## Validación pendiente de esta importación

- Compilar con SDK/NDK/CMake/JDK/Rust indicados en `docs/ANDROID_BUILD_AND_RELEASE.md`.
- Ejecutar los tests C++ y Python.
- Confirmar que el APK se instala en dispositivos ARM64 y ARMv7 que se quieran soportar.
- Abrir, guardar, cerrar y volver a abrir un mundo de cada formato disponible.
- Validar primera persona, tercera persona, avatar, inventario, controles, transparencia, HUD, pausa y recreación de Surface/EGL.
- Probar LAN con dos dispositivos.

Hasta que se ejecuten estas pruebas, esta revisión es una **base de código importada**, no un lanzamiento terminado.
