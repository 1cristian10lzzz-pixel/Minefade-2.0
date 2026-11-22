# Plan técnico del renderer — Minefade 2.0

## Objetivo

Mejorar el renderizado de terreno, carga de chunks y estabilidad en Android sin reemplazar el motor completo, sin tocar innecesariamente la simulación y sin modificar el formato de guardado. Mantener chunks cercanos con geometría voxel completa y usar LOD solo como representación visual derivada para zonas lejanas.

La implementación debe ser propia para C++20 y OpenGL ES 3; las referencias externas sirven para estudiar arquitectura y algoritmos, no para copiar código sujeto a licencias incompatibles.

## Arquitectura destino

Separar cuatro etapas que hoy están parcialmente acopladas:

1. **World / generación:** autoridad del estado de bloques y generación de chunks.
2. **Chunk meshing CPU:** snapshots inmutables de bloques, luz/materiales y vecinos; workers producen vértices e índices CPU.
3. **Coordinador de render:** valida revisión, distancia, prioridad, cancelación y consumo de memoria; nunca hace llamadas GLES.
4. **Backend GPU Android:** solo el hilo con contexto EGL actual crea, actualiza y elimina VAO/VBO/texturas.

Contrato conceptual:

```
Cambio de bloque / llegada de chunk
        ↓
Marcar secciones afectadas como dirty + revision++
        ↓
Capturar ChunkMeshSnapshot inmutable (chunk + halo de vecinos)
        ↓
Cola priorizada, acotada y deduplicada
        ↓
Worker CPU: greedy meshing / caras / UV / AO / materiales
        ↓
ChunkMeshResult { coordenadas, revision, capas, bounds, bytes }
        ↓
Validar que la revisión no quedó obsoleta
        ↓
Cola de upload GPU con presupuesto adaptativo por frame
        ↓
Hilo EGL/GLES3: actualizar buffers y publicar el nuevo mesh
        ↓
Culling de frustum + pases opaco / cutout / transparente / HUD
```

## Fase A — Diagnóstico y pruebas base

- Registrar `GL_VENDOR`, `GL_RENDERER`, `GL_VERSION`, extensiones y tamaño real de framebuffer.
- Instrumentar p50/p95/p99 de frame, generación, meshing, upload, bytes GPU y cantidad/edad de trabajos pendientes.
- Añadir pruebas para chunk vacío, caras internas, costuras entre vecinos, límites de radio de render, LOD y transparencia.
- Añadir una prueba de recreación EGL/Surface y restauración de buffers.
- Conservar y probar `MFVOX001/002/003` antes de cambiar arquitectura.

Criterio de salida: baseline reproducible y sin alteración de guardados.

## Fase B — Snapshot inmutable

- Definir `ChunkMeshSnapshot` como datos CPU compactos y sin punteros a objetos mutables del mundo.
- Incluir halo de vecinos suficiente para decidir caras en límites de chunk.
- Capturar la revisión del chunk y revisiones vecinas usadas.
- Los workers no acceden a `chunks_`, cachés ni contenedores mutables del mundo.
- Mantener al hilo de simulación como único escritor del mundo, salvo que se implemente sincronización transaccional explícita.
- Descartar resultado si su revisión ya no coincide; nunca reemplazar una malla más nueva con una antigua.

Criterio de salida: tests bajo edición rápida de bloques y movimiento entre chunks sin carreras ni meshes obsoletos publicados.

## Fase C — Scheduler de meshing

- Introducir una cola de capacidad limitada y deduplicación por clave de chunk/sección.
- Reemplazar una entrada pendiente por la revisión más reciente en vez de acumular duplicados.
- Cancelar tareas que ya no son útiles; la cancelación no debe publicar resultados tardíos.
- Mantener número fijo y configurable de workers; evitar un `std::async` ilimitado por frame.
- Priorizar: P0 terreno que evita agujeros frente al jugador; P1 chunks visibles y dirección de movimiento; P2 chunks cercanos para límites/colisión; P3 fuera de pantalla cercano; P4 LOD visible lejano; P5 pregeneración; P6 mantenimiento de disco.
- Mantener un fallback seguro de visibilidad: si el culling está viejo o no disponible, no esconder terreno necesario.

Criterio de salida: cola acotada, sin crecimiento ilimitado, sin trabajos duplicados repetidos y con prioridad reproducible.

## Fase D — Presupuesto de uploads GPU

- Crear una cola CPU→GPU de resultados validados.
- Solo consumirla desde el hilo que tiene el contexto EGL actual.
- Limitar trabajo por frame con un presupuesto inicial conservador en tiempo y bytes; calibrar con telemetría, no copiar constantes de PC.
- Permitir que los chunks P0/P1 adelanten uploads de baja prioridad.
- Reutilizar buffers cuando su capacidad sea suficiente; separar memoria temporal CPU y memoria persistente GPU.
- Evitar liberar buffers aún usados por órdenes GPU; controlar memoria residente y reservada por dispositivo.
- En pausa/recreación de Surface, invalidar IDs GPU antiguos y reconstruirlos desde datos CPU/cache reconstruible.

Criterio de salida: no bloquear el hilo de render por generación/meshing; no subir todos los meshes cambiados en un único frame si exceden el presupuesto.

## Fase E — LOD inspirado en Distant Horizons

### Separación de capas

- Los chunks cercanos siguen siendo fuente de verdad y usan geometría voxel completa.
- El terreno distante se representa en un sistema visual derivado independiente.
- Coordinar depth buffer, distancia de transición, solapamiento y niebla para evitar huecos/z-fighting.
- Empezar con columnas de altura/material, después regiones jerárquicas; evaluar quadtree/selección por error de pantalla según el presupuesto real de Android.

### Datos LOD

- Guardar muestras por columna: altura, material superior, material de superficie, indicador de fluido y datos mínimos de transición.
- No borrar ni sustituir datos originales de bloques.
- Persistir LOD solo como caché derivada versionada; se puede reconstruir desde chunks.
- Diseñar transiciones entre niveles y límites de chunk para no mostrar costuras.
- El modelo de altura no representa cuevas ni voladizos. Para zonas donde esos elementos deban verse, usar una representación por capas/segmentos o mantener un nivel de detalle mayor.

### Advertencias de fuente

El ZIP proporcionado de Distant Horizons contiene el repositorio principal y la referencia a `distant-horizons-core` como submódulo, pero no incluye el contenido de Core. Por ello, no se ha importado ni compilado su renderer. La implementación Minefade debe ser independiente y adaptada a GLES3. No reutilizar fragmentos, assets o clases de terceros sin fijar commit, licencia y avisos.

Criterio de salida: LOD lejano coherente, sin huecos visibles en bordes, transiciones ni cambio de cámara; chunks cercanos idénticos en contenido.

## Fase F — Pregeneración voluntaria de mundo

Sistema separado de streaming normal y renderer:

- Planificar región circular/cuadrada y orden espiral/radial.
- Procesar chunks existentes como completados; no regenerarlos.
- Cola propia con prioridad inferior a la exploración normal.
- Progreso, velocidad y estimación de tiempo como métricas, no como garantía.
- Pausa, reanudación y cancelación.
- Guardar estado de tarea de forma transaccional.
- Cancelar nunca elimina chunks ya generados.
- Respetar límites de RAM, disco, temperatura y consumo energético.

Criterio de salida: pausa/reanudación tras reinicio, cancelación sin pérdida y streaming normal siempre prioritario.

## Fase G — Calidad visual y estabilidad

- Pases explícitos para opaco, cutout, agua/transparencia y HUD.
- Definir estados GLES por pase y restaurarlos; no heredar accidentalmente blend/scissor/viewport.
- Investigar la franja negra con medidas de Surface, insets, `glViewport`, scissor, FBO, attachments, orientación y pausa/reanudación.
- Investigar pantalla blanca registrando shader compile/link logs, atlas, framebuffer y primer chunk publicado; usar fallback de textura/visibilidad sin esconder errores.
- Mantener UI independiente del renderer de terreno.
- Optimizar agua/sombras solo con pruebas en GPUs reales; no afirmar shadow maps si solo hay sombras de contacto.

## Métricas obligatorias

- Frame time p50/p95/p99 y FPS observado.
- Tiempo de generación, snapshot, meshing y upload.
- Chunks por segundo.
- Tamaño y edad de cada cola, tareas canceladas y duplicadas.
- CPU RAM temporal y GPU RAM usada/reservada.
- Bytes subidos y tiempo GPU/CPU por frame.
- Tiempo de carga/guardado y consumo térmico.
- Estado de superficie/contexto EGL.

No publicar porcentajes de mejora ni FPS hasta medirlos en teléfonos reales de gama baja, media y alta, con modelo de GPU, resolución y versión GLES registrados.

## Prohibiciones de regresión

- No modificar IDs de bloque sin migración.
- No eliminar ni reinterpretar `MFVOX001/002/003`.
- No hacer que los workers llamen GLES.
- No cambiar recursos GPU fuera del hilo EGL.
- No sustituir el mundo voxel por una altura aproximada.
- No compartir una sola cola sin prioridades entre streaming, render, pregeneración y guardado.
- No introducir GitHub Actions.
- No publicar un APK como Release terminado hasta pasar los criterios de aceptación.

---
## Ejecución de la Fase 1 — revisión de estabilidad y renderer

**Estado de esta rama:** implementación parcial verificable por CI; todavía no equivale a aceptación en un teléfono físico ni a completar todas las fases A–G del diseño.

### Cambios que sí están implementados

1. **Diagnóstico GLES al iniciar**
   - Registra fabricante, renderer, versión OpenGL ES, versión GLSL, lista (truncada de forma segura) de extensiones, límites de textura/renderbuffer/viewport, framebuffer enlazado y estado de completitud.
   - Una superficie con ancho/alto no válido durante una recreación se ignora; no se sustituye por un viewport 1×1.
   - Antes de limpiar y dibujar el mundo se enlaza el framebuffer de ventana (0), se restaura viewport completo, scissor, máscaras de color/profundidad y función de profundidad.
   - En resize se vuelve a enlazar el framebuffer de ventana y se registra un estado incompleto.

2. **Snapshots de bloques para meshing**
   - Se copian solo los chunks con malla pendiente y su halo cardinal de vecinos, no todos los chunks cargados en cada pasada.
   - Las consultas de bloques vecinos se resuelven desde ese conjunto de snapshots, no consultando los contenedores mutables de VoxelWorld desde el worker.
   - Cada chunk lleva una revisión de ejecución. Un cambio real de bloque la incrementa; escribir el mismo bloque no invalida innecesariamente la malla.
   - Antes de publicar una malla se compara la revisión viva con la revisión capturada. Si no coincide, se descarta el resultado y se vuelve a marcar el chunk como dirty.
   - La revisión y las métricas son datos de ejecución: no se escriben al archivo del mundo.

3. **Presupuesto de transferencia GPU**
   - Se conserva la prioridad por distancia y el límite de 1/2/3 chunks por frame según la calidad.
   - Además se pasa un presupuesto inicial por calidad de 256 KiB / 768 KiB / 2 MiB y una ventana objetivo de 1,5 / 2,5 / 4 ms.
   - Se comprueba tiempo y bytes entre chunks. Un primer chunk puede superar el presupuesto de bytes/tiempo para evitar que un chunk grande quede bloqueado para siempre; una única llamada GLES no se interrumpe a mitad de transferencia.
   - Los VBO crecen geométricamente, se reutiliza la capacidad disponible y se actualizan con glBufferSubData; se evita reasignar almacenamiento completo para cada cambio de malla.
   - El renderer conserva y restaura las vinculaciones VAO y GL_ARRAY_BUFFER alrededor de la subida.

4. **Métricas del motor**
   - Mantiene una ventana móvil de 120 fotogramas para calcular p50/p95/p99 del tiempo de frame.
   - Expone en diagnóstico el tiempo de meshing, el tiempo empleado por el hilo de render en recoger trabajos de generación completados, tiempo de sincronización GPU, chunks, memoria CPU estimada de bloques/mallas, capacidad VBO GPU, bytes subidos y pendientes.
   - El dato generation_pump_ms mide el coste del bombeo/recogida en el hilo de render; no debe confundirse con la duración interna total de cada worker de generación.

5. **Compatibilidad de guardados**
   - No cambia la estructura serializada de MFVOX003.
   - No se serializan revisiones ni contadores de memoria.
   - Se conservan los cargadores históricos de MFVOX001 y MFVOX002; las pruebas de mundo/guardado deben seguir ejecutándose en CI.

### Qué no se declara terminado todavía

- **Scheduler persistente de meshing:** BuildMesh sigue creando workers y esperando a que terminen antes de retornar. Hay snapshots seguros y un presupuesto de mallas por llamada, pero aún falta una cola persistente acotada/deduplicada con cancelación real, revisión y antigüedad por trabajo.
- **Presupuesto duro por transferencia:** el límite de tiempo/bytes se aplica entre chunks; la primera subida puede rebasarlo y una transferencia GLES individual no se puede pausar en mitad de glBufferSubData.
- **Recuperación EGL comprobada:** se reconstruyen recursos en la ruta de inicialización, pero falta una prueba instrumentada en dispositivo/emulador que fuerce pérdida y recreación de contexto y compare el resultado.
- **Memoria de mundo:** se mide RAM de bloques y caché, pero no se expulsa mundo visitado. No hacerlo evita perder construcciones: una expulsión segura necesita almacenamiento regional transaccional y recuperación antes de liberar el chunk.
- **LOD distante:** la representación LOD2 sigue siendo altura/material por columna y no conserva cuevas, túneles, voladizos ni capas subterráneas.
- **Franja negra/pantalla blanca:** se agregaron comprobaciones y diagnóstico del framebuffer, pero el resultado visual también depende de Surface, insets y GPU reales; no se puede declarar corregido solo por compilar.
- **Pruebas Android físicas:** falta abrir, entrar a un mundo, guardar, volver a abrir, girar/recrear Surface, comprobar primera/tercera persona, HUD y controles en dispositivos ARM reales de diferentes GPUs.
- **Telemetría restante:** faltan edad/profundidad de colas, chunks por segundo medidos, latencias de guardado/carga y temperatura del dispositivo.

### Orden de aceptación restante

1. CI limpia con pruebas Python, Rust, C++ y compilación Android para ARM64/ARMv7.
2. Verificar firma, paquete, versión, ABIs y checksum del APK exacto generado por el último commit.
3. Instalar y ejecutar en Android real; probar mundo nuevo y mundos guardados históricos, pausa/reanudación y recreación EGL.
4. Medir p50/p95/p99, colas, memoria y estabilidad térmica en al menos un teléfono de gama baja, uno medio y uno alto.
5. Solo después, cerrar los pendientes de scheduler persistente y almacenamiento regional antes de etiquetar esta fase como aceptada al 100 %.

**Regla de integridad:** una compilación correcta demuestra que el proyecto compila y pasa sus pruebas automatizadas; no demuestra por sí sola que el juego se abra correctamente en todos los teléfonos ni que la franja negra esté resuelta.
