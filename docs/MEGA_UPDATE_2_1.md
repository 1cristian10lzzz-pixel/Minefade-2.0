# Minefade 2.1 — actualización integral por etapas

## Alcance de esta entrega

Esta rama inicia una mejora general sin reemplazar el motor ni romper los mundos existentes. La primera etapa se concentra en renderizado estable, transferencias GPU y compilación reproducible.

### Implementado en esta rama

- Subidas de mallas limitadas a 1/2/3 chunks por pasada según calidad gráfica.
- Prioridad de carga GPU por distancia a la cámara, con desempate determinista por coordenadas.
- Drenaje de subidas pendientes incluso cuando el meshing CPU no tiene trabajo nuevo.
- Validación de tamaños antes de convertir cantidades de vértices a `GLsizeiptr`.
- Restauración de viewport, scissor y máscaras de buffers antes de limpiar cada frame.
- Contadores de chunks en generación, chunks residentes en GPU y subidas de malla pendientes.
- CI para pull requests, verificación de recursos generados sin commits automáticos y permisos de escritura aislados al borrador de prerelease.
- Versionado Android actualizado a versionCode 24 / versionName 2.1.0.

## Conservación de contenido

- Se mantiene `applicationId = com.minefade.nativeengine`.
- No se cambian los IDs del catálogo de bloques.
- No se modifica la lectura/escritura de `MFVOX001`, `MFVOX002` o `MFVOX003`.
- No se eliminan mundos, recursos, modos, avatar, inventario, comandos, mods, shaders ni funciones LAN.
- No se incorpora ninguna contraseña ni clave privada al repositorio.

## Siguientes etapas técnicas

1. **Streaming y meshing:** snapshots inmutables por chunk, cola de trabajos persistente, revisión/cancelación de resultados obsoletos y prioridad por distancia.
2. **Memoria y LOD:** presupuesto de RAM/GPU, liberación segura de recursos fuera de rango y LOD que represente voladizos/capas/cuevas sin sustituir el mundo real.
3. **Estabilidad visual:** instrumentar shader/framebuffer/atlas, probar restauración EGL y rastrear las franjas negras o pantallas blancas con diagnósticos reproducibles.
4. **Gameplay y controles:** pruebas de minería, colocación, salto, tercera persona, avatar, inventario y HUD en distintas densidades y relaciones de aspecto.
5. **Multijugador LAN:** validación entre dos dispositivos, reconexión, desconexión, orden de ediciones y límites de mensajes.
6. **Calidad y rendimiento:** perfiles de gama baja/media/alta; registrar FPS p50/p95, tiempos de generación/meshing/upload, memoria y temperatura antes de afirmar mejoras cuantitativas.

## Criterio de aceptación

La compilación de CI y las pruebas automatizadas son necesarias, pero no suficientes. No declarar estable ni publicar como versión final hasta instalar el APK en Android real, abrir/guardar/reabrir mundos existentes y probar controles, HUD, transparencias, shaders, suspensión/reanudación y LAN. El APK de CI usa una firma debug temporal y no garantiza actualizaciones in-place entre runners distintos.
