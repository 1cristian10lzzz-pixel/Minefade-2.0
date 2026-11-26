# Auditoría técnica del renderer — Minefade 2.0
Fecha: 26 de noviembre de 2026
Rama revisada: `main` (base `2fd295a78890e6172375528d57963d213270ab01`)
Rama de correcciones: `fix/renderer-stability-2026-11`

## Alcance
Revisión estática del motor Android, C++20, OpenGL ES 3, generación/meshing de chunks, atlas, transparencias, ciclo EGL, pruebas y flujo de compilación. No equivale a una prueba en un teléfono.

## Fallos concretos corregidos en esta rama

1. **Pase 3D bloqueado por la geometría opaca.** `Engine::Render` envolvía los pases de entidades, objetos y transparencias dentro de una condición que exigía `OpaqueVertexCount() > 0`. Si una escena no tenía caras opacas, tampoco se dibujaban sus elementos transparentes ni las entidades. El pase ahora se ejecuta cuando el programa de mundo está disponible; cada subsistema decide si tiene algo que dibujar.
2. **Pérdida de la última malla válida al fallar una subida.** `WorldMeshRenderer::Sync` actualizaba directamente los VAO/VBO activos y, ante un fallo parcial entre el pase opaco y el transparente, anulaba los conteos del chunk. Ahora ambos pases se construyen en recursos temporales y solo se sustituyen los recursos activos cuando los dos uploads terminan bien. Si falla la sustitución, se conserva la malla anterior.

Se añadieron regresiones estáticas para estos dos casos en `tests/test_renderer_invariants.py`.

## Riesgos y mejoras aún pendientes

### Prioridad crítica: streaming y meshing
- El camino normal de `Engine::RebuildWorldMesh` invoca `BuildMesh(..., 1u, false)`: solo se permite reconstruir un chunk por ciclo de 1/30 s. El cálculo de `workerCount` queda en uno, por lo que los dos workers disponibles en el código no se aprovechan durante el juego normal.
- El meshing se ejecuta dentro de la llamada de render y espera a terminar. Esto puede crear picos de frame-time al explorar o modificar bloques.
- Falta la cola persistente de trabajos, snapshots inmutables de bloque + halo de vecinos, cancelación de resultados obsoletos y publicación por revisión que ya describe `RENDERER_UPGRADE_PLAN_2_0.md`. Debe implementarse antes de paralelizar más: los workers no deben leer datos mutables del mundo ni llamar a GLES.
- El radio visible actual usa LOD de altura simplificado. Ese nivel no representa cuevas, voladizos ni varias capas verticales.

### Prioridad alta: GPU y transparencias
- `Sync` limita uploads por cantidad fija (1/2/3 chunks según calidad), no por tiempo/bytes reales; falta telemetría p50/p95/p99 y presupuesto adaptativo.
- Cada revisión de chunk vuelve a especificar almacenamiento con `glBufferData`. No cambiarlo ciegamente por `glBufferSubData`: en Android puede introducir esperas si la GPU todavía lee el buffer. Evaluar orphaning o buffers múltiples con mediciones por GPU.
- Las transparencias se ordenan por distancia al centro del chunk. Es una aproximación; agua/vidrio de chunks que se solapan visualmente pueden mezclarse en orden incorrecto. La mejora futura debe usar lotes/superficies transparentes ordenables, sin romper el presupuesto.
- El atlas estático se carga al iniciar; no debe subirse por frame.

### Prioridad alta: validación visual y dispositivo
- El restablecimiento de viewport, scissor y máscaras antes de limpiar ya está presente. Para diagnosticar la franja negra aún deben registrarse dimensiones reales de Surface/framebuffer, insets, viewport, scissor y orientación en el dispositivo que la reproduce.
- Los shaders registran errores de compilación/enlace y existe un shader alternativo GLES 3.0. La pantalla blanca requiere una captura/logcat del dispositivo: shader, textura atlas, FBO y primer chunk GPU publicado.
- La luz actual no debe confundirse con sombras dinámicas de mapa de profundidad. El ajuste de sombras de contacto no prueba que exista un sistema completo de shadow maps.
- Faltan pruebas de instalación, creación/carga de mundos, pausa/reanudación EGL, controles táctiles y rendimiento en teléfonos Android reales de gama baja/media/alta.

### Motor general
- Separar la simulación de la frecuencia de refresco con timestep fijo y acumulador.
- Añadir métricas de streaming, generación, meshing, upload, RAM CPU/GPU y antigüedad de colas.
- Probar guardado/carga y compatibilidad `MFVOX001/002/003` después de cada cambio del mundo.
- Mantener networking LAN, simulación, UI y render independientes para que un fallo de interfaz no detenga el mundo.

## Métodos de referencia consultados
- Android Developers, *Vertex data management*: organización de datos de vértices para GPUs móviles y tile-based rendering.
- Samsung Developer, *OpenGL ES Usage Recommendations*: sincronización CPU/GPU, orphaning, batch de uploads, fallbacks y pruebas en varios chipsets.
- Khronos OpenGL Wiki, *Buffer Object Streaming*: orphaning y sincronización de recursos de vértices.

## Estado
Los dos fallos del apartado de correcciones están implementados y cubiertos por nuevas pruebas de regresión. La ejecución de CI y la validación física Android deben completarse antes de etiquetar un APK como estable. No se han cambiado formatos de guardado ni IDs de bloques.
