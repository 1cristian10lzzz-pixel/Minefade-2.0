# Minefade 2.3.0 — auditoría de alcance y validación

**Base:** rama `feature/minefade-2.3.0-full-overhaul`  
**Versión candidata Android:** 2.3.0 · versionCode 29  
**Estado:** candidata de desarrollo. Requiere CI y pruebas físicas antes de llamarse estable.

## 1. Correcciones incluidas en esta revisión

### HUD y controles Android
- La barra rápida calcula el ancho de sus nueve espacios usando la resolución útil y la densidad del dispositivo. El estilo base se mantiene y el tamaño final se adapta al ancho real.
- El D-pad ya no captura el rectángulo completo de 132 dp: cada dirección solo responde dentro de su botón visible. Los huecos dejan de bloquear la cámara.
- Agacharse está en el centro del D-pad. Su ruta de entrada es independiente del puntero virtual de RmlUi y puede coexistir con otro dedo que use la cámara o una dirección.
- Los botones del modo de respaldo Android mantienen la propiedad del dedo que los pulsó; levantar otro dedo no libera por error el botón activo.
- El ajuste zurdo reposiciona en vivo el D-pad, salto y ataque, además de cambiar las zonas de entrada.
- El botón Android de chat solo se muestra cuando falla el HUD RmlUi; evita superponer dos botones de chat cuando el HUD nativo funciona.
- Los recursos de fallback mantienen controles básicos, quickbar e inventario disponibles cuando RmlUi no puede inicializarse.

### Renderizado y transparencias
- El pase transparente ordena triángulos por distancia al centro de cada triángulo, no solo por centro de chunk. Esto mejora el orden de superficies de agua/lava que se cruzan dentro del mismo chunk.
- Se limita el buffer temporal de ordenación a 75.000 triángulos; si se supera el límite o falla la subida, el motor conserva el camino alternativo de ordenación por chunk.
- El VAO/VBO temporal se reutiliza, su memoria se incluye en la métrica de VRAM y se libera con el renderer.
- Se conserva el presupuesto de subida GPU por frame y la malla visible anterior en caso de fallo de una transferencia.

**Límite:** esto no implementa todavía mapas de sombras dinámicas completos, lluvia real, simulación física de fluidos, render por materiales avanzado ni LOD vertical multicapas. El agua existente sigue dependiendo del shader y del sistema de fluidos actual.

### Progresión y logros
- Se incorporan XP, niveles y umbral creciente por nivel. El HUD muestra vida, comida, nivel y XP.
- Se conceden puntos por romper/colocar bloques, recoger objetos, fabricar, derrotar enemigos y saltar.
- Los contadores de logros de bloques, objetos, saltos y primera entrada de mundo se actualizan desde el motor.
- Se guarda la progresión en el archivo lateral `.progression` con magic/version y límites de validación. No se cambia MFVOX001/002/003.
- La pantalla de logros presenta el nivel/XP y tarjetas de dos columnas legibles en Android.

**Límite:** esta primera progresión no equivale todavía a un sistema completo de misiones, habilidades desbloqueables, árboles de talentos ni recompensas por temporada. Es la base persistente para añadirlos sin falsos contadores.

## 2. Sistemas que se conservan de la base 2.2.4

- Fabricación transaccional: los ingredientes y el resultado se aplican juntos.
- Protección de parcelas y permisos serializados fuera del formato de mundo.
- Inventario y quickbar existentes, slots de durabilidad y armadura.
- Guardado de supervivencia y formatos históricos MFVOX.
- Chat local/LAN y descubrimiento Android NSD.
- MFMod y perfiles MFShader de datos validados.

## 3. Límites actuales de mods y shaders

El formato de mod activo en esta rama permite paquetes de configuración con claves aprobadas y límites de tamaño. No debe confundirse con un API universal de contenido. Todavía falta integrar en esta rama, con sus dependencias y pruebas, el registro extensible de bloques/objetos, texturas, recetas, biomas, estructuras, entidades y sonidos que está desarrollado en una rama separada.

Los perfiles MFShader v1 ajustan parámetros del shader integrado: ondas de agua, animación de lava y viento de hojas. No cargan GLSL arbitrario ni reemplazan programas completos. Es intencional por seguridad y compatibilidad GLES; una futura versión necesita manifiesto, permisos, validación de uniforms/atributos, compilación por GPU y fallback cuando un perfil falla.

No se debe anunciar scripting nativo o carga de bibliotecas descargadas. Los mods deben seguir siendo datos verificados, sin DEX/JAR ni `.so` externos.

## 4. Trabajo pendiente de prioridad alta

1. Resolver e integrar de forma comprobable el registro de contenido MFMod v2 y el sistema de recursos; no mezclar ramas divergentes a ciegas.
2. Mejorar el atlas/registro de ítems para que herramientas, armas, comida y materiales tengan iconos y comportamientos propios, no solo una entrada visual.
3. Terminar el render de entidades para todos los tipos, no únicamente el modelo MAES de oveja; validar rigs, orientaciones y animaciones en primera/tercera persona.
4. Implementar sombras proyectadas con mapas de sombras según capacidad GLES, lluvia/partículas, reflejos/refracción y fluidos con estados; mantener perfil de bajo consumo.
5. Sustituir el LOD superficial por LOD de volumen/capas para conservar cuevas, túneles y voladizos a distancia.
6. Ampliar supervivencia: recetas/estaciones, fundición y combustible, comida/efectos, equipo y durabilidad, combate/loot, misiones y desbloqueos.
7. Completar sincronización LAN autoritativa de inventarios, contenedores, entidades y contenido, y probar en dos teléfonos. La huella de mods por sí sola solo comprueba igualdad del contenido; no cifra el tráfico ni evita trampas.
8. Ejecutar pruebas físicas en varias GPUs, resoluciones, rotación, pérdida/recreación de EGL, multitáctil y guardado/reapertura de mundos.

## 5. Criterios de aceptación para esta candidata

- Todos los tests Python pasan.
- Las pruebas nativas C++/Rust pasan.
- Gradle compila APK para arm64-v8a y armeabi-v7a.
- `apksigner verify` valida el APK y los metadatos identifican 2.3.0 / versionCode 29.
- La APK instala y abre; un mundo MFVOX anterior se conserva y puede guardarse/reabrirse.
- En Android físico se validan D-pad + cámara + salto + agachado simultáneos, inventario, chat y cambio de mano.
- En GPU física se revisan agua, transparencias cruzadas, bandas negras, pantalla blanca, memoria y estabilidad al recrear EGL.
- LAN requiere dos dispositivos y pruebas reales de conexión/edición de bloques.

Pasar CI demuestra compilación y pruebas automatizadas; no demuestra por sí solo que el juego funcione al 100% en todos los teléfonos.
