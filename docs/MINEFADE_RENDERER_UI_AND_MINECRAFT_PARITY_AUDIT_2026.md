# Auditoría general de Minefade 2.0 — renderer, interfaces, controles y catálogo Minecraft (24-11-2026)

## Alcance y base de trabajo

La auditoría se inició sobre la versión 2.2.4, versionCode 28, commit `094f6437dcd6f79327a30dfb90417460d206cbb2` del repositorio Minefade 2.0. Esa revisión tiene una ejecución CI #172 satisfactoria para los tests y la compilación de validación Android. No equivale a una certificación visual en un teléfono físico.

Las correcciones de controles, RmlUi y subida de texturas que se describen como “incorporadas en esta revisión” están en la rama `feature/phase1-atlas-ui-recovery`, PR #8. La rama sigue separada de `main`; no se ha fusionado.

## 1. Resultado ejecutivo de la auditoría

Se confirma que Minefade no necesita reemplazar todo el motor para empezar a corregir sus fallos. La arquitectura actual ya tiene un renderer C++20/OpenGL ES 3.0, mallas por chunk, culling, snapshots de bloques, revisiones para descartar mallas obsoletas, presupuestos de subida GPU, LOD de distancia, HUD RmlUi y un catálogo generado con atlas propio.

Los problemas concretos identificados en esta revisión fueron:

| Prioridad | Hallazgo | Estado |
|---|---|---|
| P0 | Android podía cancelar un único dedo y dejar otro puntero activo. | Corregido en SurfaceView y en los dos flujos Android de respaldo. |
| P0 | RmlUi convertía todos los dedos en el mismo ratón virtual; un segundo dedo podía soltar un botón del primero. | Corregido: RmlUi reserva el ratón virtual al puntero que inició la interacción. |
| P0 | El HUD tenía dos elementos con `id="chat"` y dos registros nativos para la misma acción. | Corregido: una única instancia y un único registro. |
| P1 | La subida de imágenes de RmlUi no normalizaba/restauraba todos los estados GLES de `GL_UNPACK_*`. | Corregido para alignment, row length, skip pixels/rows y PBO. |
| P1 | Las superficies transparentes se ordenan por distancia entre chunks; no se ordenan triángulos que se cruzan dentro de un mismo chunk. | Pendiente; es una causa realista de artefactos de agua/transparencias. |
| P1 | El LOD lejano es aproximación por altura y no representa correctamente cuevas, voladizos y estructuras verticales. | Pendiente. |
| P1 | El motor aún no tiene scheduler persistente de meshing completamente independiente de los ciclos de construcción actuales ni expulsión regional segura de chunks. | Pendiente. |
| P1 | La aceptación visual de recreación EGL, escala/insets, bandas negras, pantallas blancas, rotación y reanudación requiere dispositivos Android reales. | Pendiente de validación física. |

### Qué está bien encaminado y debe conservarse

- GLES se usa desde el hilo de render y los resultados CPU se validan por revisión antes de publicarse.
- El renderer de terreno ya dispone de presupuestos por número de chunks, bytes y ventana temporal; conserva la malla visible si falla una actualización por capas.
- El HUD envuelve su renderizado en un guardado/restauración del estado GLES de frame. La auditoría no encontró base para afirmar que RmlUi deja sin restaurar todo el estado global de la escena.
- Android puede recrear el contexto EGL después de pausa/reanudación; la app reconstruye el motor y sus recursos desde el ciclo de creación. Aun así, `setPreserveEGLContextOnPause(true)` es solo una preferencia del framework, no una garantía.
- Los guardados MFVOX001/002/003 y los IDs existentes de bloques deben mantenerse sin reinterpretaciones.

## 2. Correcciones implementadas en la rama

### 2.1 Multitáctil Android

- En `MinefadeSurfaceView.onTouchEvent`, `ACTION_CANCEL` recopila todos los IDs y coordenadas de los punteros y libera cada uno en el hilo GL.
- En los listeners Android de respaldo para joystick y toque del mundo, `ACTION_CANCEL` recorre todos los punteros de igual forma.
- RmlUi ahora recibe también el identificador del puntero Android. El dedo que pulsa un botón adquiere el único ratón virtual; un dedo diferente no puede enviar un movimiento, pulsación o liberación del ratón RmlUi. Los otros dedos continúan disponibles para mover la cámara o usar el pad.
- La captura RmlUi se libera al recibir UP/CANCEL del propietario y cuando se destruye el runtime.

### 2.2 Identidad de controles del HUD

- Se eliminó la segunda aparición de `id="chat"`.
- Se eliminó el segundo `AttachAction("chat", "chat")`.
- Se añadió una regresión que verifica IDs estáticos únicos, que cada botón estático tiene acción nativa y que la barra rápida dinámica sigue registrando sus nueve slots.

### 2.3 Estado de carga de texturas GLES

En `RmlUiOpenGLES::CreateTexture` la carga desde memoria CPU ahora:
- guarda el texture unit activo, textura 2D, alineación, row length, píxeles y filas a saltar y binding de PBO;
- desvincula el PBO cuando la fuente es un puntero de CPU;
- normaliza row length y skips a cero, y alignment a 1 para el RGBA de la interfaz;
- restaura el estado previo en la salida normal y en el fallo al crear la textura.

La especificación de GLES permite que row length y skip rows/pixels cambien cómo se interpreta el puntero fuente; heredar valores de otro subsistema puede desplazar filas o leer desde una posición distinta.

### 2.4 Pruebas añadidas

`tests/test_renderer_ui_touch_integrity.py` comprueba los IDs/acciones del HUD, cancelación de todos los dedos, propiedad del ratón virtual RmlUi y aislamiento del estado pixel-unpack. Son pruebas de código y contratos; no simulan los eventos en un Android real ni reemplazan una prueba de uso táctil.

### 2.5 Creación, apertura y aislamiento de mundos

- Se reemplazó el selector desplegable de modo por tarjetas visibles de Supervivencia, Creativo y Aventura, con pestañas Juego/Mundo/Trucos, resumen dinámico, semilla aleatoria y validación explícita. Se mantienen únicamente opciones conectadas al motor: nombre, modo, semilla, cofre inicial y comandos; no se exponen dificultad ni tipos de terreno que aún no tienen implementación.
- Se detectó una contaminación de guardados al cambiar de partida: `MainActivity` pausaba y guardaba el mundo anterior, pero después asignaba la ruta del nuevo mundo antes de que `Engine::Initialise` llamara a `Shutdown()`. Como `Shutdown()` guardaba automáticamente cuando `ready_` seguía activo, podía escribir el mundo anterior dentro del archivo del mundo nuevo.
- Corrección: `Engine::Shutdown(bool saveWorld = true)` permite liberar UI/GPU sin guardar; `Initialise` usa `Shutdown(false)`. La ruta anterior ya se persiste mediante `pauseNativeEngine()` en el hilo GL antes del cambio. Los cierres por error conservan el guardado por defecto.
- Se corrigió además la migración de una partida antigua `default.mfworld`: ahora copia a un archivo temporal, sincroniza los bytes, publica el nuevo guardado, escribe `world.json` y solo entonces elimina el archivo original. Si falla la migración, conserva la fuente.
- `tests/test_world_creation_flow.py` verifica la selección de opciones, el recorrido del path al motor, la carga de MFVOX001/002/003, la migración antigua y la prevención de guardado cruzado.

Estas pruebas son de código y CI. La apertura visual, el render del primer chunk, el guardado/reapertura tras cambiar entre dos mundos y la recuperación del contexto EGL aún deben confirmarse en Android físico.

## 3. Auditoría de renderizado y plan técnico

### P0 — estabilidad de imagen y superficie

1. Añadir un log de arranque por sesión con vendor, renderer, versión GLES/GLSL, extensiones relevantes, tamaño físico de Surface, tamaño lógico del HUD, insets, viewport, framebuffer activo, FBO completo/ incompleto y dimensiones reales del atlas.
2. Añadir una captura de diagnóstico que se pueda activar desde ajustes: un frame con malla opaca, transparencia, entidades, atlas y HUD activables por separado. Así se puede distinguir entre generación del mundo, shaders, textura, framebuffer y RmlUi.
3. Probar el primer frame con mundo vacío y con primer chunk; evitar ocultar una textura o shader fallido bajo un color blanco. En caso de fallo, mostrar un mensaje de motor con el shader/FBO/asset que no pudo inicializarse.
4. Comprobar que resize con ancho/alto cero se ignora y que cada tamaño válido vuelve a enlazar el framebuffer de ventana, actualiza viewport y contexto RmlUi.
5. En Android físico: bloquear/desbloquear, ir a segundo plano, volver, abrir/cerrar teclado, navegar entre menús y rotar o cambiar tamaño de ventana cuando el sistema lo permita.

### P1 — corrección del terreno y transparencias

1. Separar explícitamente pases opaco, cutout/plantas, fluido/transparente, entidades, partículas y HUD, declarando blend/depth/cull/color mask por pase.
2. Sustituir la ordenación únicamente por centro de chunk por una política medida: primero ordenación de superficies/triángulos transparentes en un presupuesto acotado y, si el coste no es aceptable en GPU móvil, una ruta alternativa por tipos de fluido/capas.
3. Evaluar OIT solo como opción experimental por GPU. No copiarla a ciegas: incluso Minecraft Java describe OIT como aproximación que elimina parte de los problemas de ordenación, pero introduce artefactos menores y más coste que su método anterior.
4. Añadir escenas de regresión con agua detrás/delante de vidrio, agua y hojas en límites de chunk, dos planos transparentes que se intersectan y cámara dentro/fuera del agua.
5. Mantener una ruta de render sin efectos para GPU de gama baja. Un shader más complejo no debe bloquear la entrada al mundo.

### P1 — mallas, memoria y carga de chunks

- Completar un scheduler con cola acotada, deduplicación, prioridad por distancia/dirección, cancelación y revisión; ningún worker puede usar OpenGL ni acceder a estructuras mutables sin snapshot.
- No lanzar un future nuevo e ilimitado por frame.
- Añadir presupuesto total por frame en tiempo y bytes, y métricas del primer chunk que sobrepasa el presupuesto.
- Introducir límites de RAM CPU/GPU y expulsión segura por región solo después de asegurar persistencia y reconstrucción desde el guardado.
- Evolucionar LOD por altura a una representación multicapas donde se necesite conservar cuevas, puentes, árboles y voladizos; transición con profundidad y niebla para no crear costuras.
- Probar con cámara quieta, carrera entre chunks, destrucción rápida de bloques, teletransporte, vuelta a zonas anteriores y mundo grande.

### P2 — iluminación, agua y efectos

- Las sombras de contacto actuales no son shadow maps. Primero hay que corregir la consistencia de luz, normales, niebla y blending; después introducir sombras proyectadas por calidad con un fallback sin sombras.
- El agua actual tiene ondas de shader, pero no la simulación completa de nivel, caudal, dirección, cascada, propagación y evaporación. No confundir una animación de superficie con un sistema de fluidos.
- La lluvia necesita sistema de clima, partículas deterministas, gotas, sonido por material, visibilidad y límites por distancia; la nieve y tormenta deben poder desactivarse.
- Probar bandas negras y blanco en el mundo mediante medidas de Surface/insets/FBO/viewport y registros, no por conjetura.

## 4. Auditoría de interfaces y controles

### HUD de juego

Botones estáticos presentes en la revisión auditada: cofre inicial, chat, inventario, perspectiva, pausa, nueve slots, agachado, salto, ataque, fabricar, comer, cerrar inventario y cerrar fabricación. Las acciones nativas correspondientes se registran; los slots y filas de inventario se generan dinámicamente y tienen listeners separados.

La corrección multitáctil evita que un dedo secundario suelte el control HUD del propietario. Falta comprobar físicamente:
- pulsar salto mientras otro dedo mueve cámara;
- mantener agachado y desplazarse;
- minar mientras se mueve el pad;
- abrir inventario mientras se pulsa otro control;
- alternar perspectiva, pausa, chat, fabricar y cofre sin capturas atascadas;
- usar modo zurdo, resolución baja y densidad alta.

### Menús Android

La base incluye pantalla de inicio, creación/listado de mundos, ajustes, personalización de avatar y módulos de multijugador local Crear/Unirse. El servicio LAN usa NSD/TCP; no equivale a servidores públicos de Internet.

La auditoría de código puede comprobar que listeners, IDs y rutas están conectados; no puede garantizar la legibilidad, el tamaño táctil, scroll, foco del teclado, recortes por insets o que todos los caminos visuales funcionen en cada relación de aspecto. Para aceptar la UI hace falta una matriz física con 16:9, 20:9, pantalla pequeña, tablet y escala de fuente grande.

### Inventario, fabricación y equipo

- El inventario actual usa 36 casillas y la barra rápida 9; equipamiento del jugador son cuatro slots de armadura.
- Las acciones de fabricación y movimiento de slots se deben probar con casillas vacías, stacks parciales, inventario lleno, objeto de una unidad y salida/retorno al mundo.
- La barra de durabilidad es visual y está ligada a herramientas que actualmente pueden minar/atacar. Armaduras no tienen todavía desgaste individual ni reparación de equipo.
- Un catálogo visible no equivale a un objeto funcional: el resultado de cada acción necesita receta, stack, uso, durabilidad, modelo en mano, drop, guardado y compatibilidad multijugador.

## 5. Investigación de paridad de objetos y armaduras Minecraft

### Ediciones y referencia elegida

No hay un único catálogo “Minecraft actual”: Java y Bedrock publican contenido y datos diferentes. Minefade procede de una fuente histórica Bedrock (Minecraft 1.8) y su catálogo está organizado como bloques más objetos sintéticos; por eso la referencia principal elegida para la siguiente ampliación es **Minecraft Bedrock estable 26.60**, usando el paquete oficial de muestras `Mojang/bedrock-samples`, tag `v1.26.60.5`.

El archivo oficial `metadata/vanilladata_modules/mojang-items.json` tiene 1.627 registros de item en esa referencia. Incluye bloques que también se pueden tener como ítems, equipo y objetos que no existen en el catálogo actual. La cifra no debe compararse como si fueran 1.627 herramientas distintas ni importarse directamente como IDs de guardado.

### Qué tiene Minefade hoy

El generador actual combina:
- 342 definiciones de bloques de la fuente base, cuyos IDs históricos deben conservarse;
- 72 objetos sintéticos Minefade (palo, herramientas, armaduras de madera/piedra/cobre/hierro/oro/diamante/esmeralda, lingotes/materiales);
- 414 entradas totales en el catálogo unificado y 1.242 tiles de atlas (tres caras por entrada).

Las texturas son píxeles originales generados por `tools/generate_block_catalog.py`. La fuente de Mojang se utiliza como referencia de nombres, propiedades y comportamiento; no se deben copiar ni redistribuir sus imágenes, sonidos o modelos sin resolver expresamente sus licencias.

### Familias prioritarias ausentes o incompletas

El análisis por identificador Bedrock v1.26.60.5 detecta estos nombres oficiales sin una coincidencia exacta en las claves del registro Minefade. Es una lista de brechas de nomenclatura/registro; algunos materiales equivalentes sí existen bajo claves Minefade propias y deben mapearse explícitamente, no duplicarse:

**Herramientas, armas y equipo especial (27 claves):**
`bow`, `brush`, `copper_spear`, `crossbow`, `diamond_spear`, `fishing_rod`, `flint_and_steel`, `golden_axe`, `golden_hoe`, `golden_pickaxe`, `golden_shovel`, `golden_spear`, `golden_sword`, `iron_spear`, `mace`, `netherite_axe`, `netherite_hoe`, `netherite_pickaxe`, `netherite_shovel`, `netherite_spear`, `netherite_sword`, `shears`, `shield`, `spyglass`, `stone_spear`, `trident`, `wooden_spear`.

**Armaduras, monturas y equipo de criatura (30 claves):**
`chainmail_boots`, `chainmail_chestplate`, `chainmail_helmet`, `chainmail_leggings`, `copper_horse_armor`, `copper_nautilus_armor`, `diamond_horse_armor`, `diamond_nautilus_armor`, `elytra`, `golden_boots`, `golden_chestplate`, `golden_helmet`, `golden_horse_armor`, `golden_leggings`, `golden_nautilus_armor`, `iron_horse_armor`, `iron_nautilus_armor`, `leather_boots`, `leather_chestplate`, `leather_helmet`, `leather_horse_armor`, `leather_leggings`, `netherite_boots`, `netherite_chestplate`, `netherite_helmet`, `netherite_horse_armor`, `netherite_leggings`, `netherite_nautilus_armor`, `turtle_helmet`, `wolf_armor`.

**Consumibles y materiales:** la paridad de comida incluye alimentos crudos/cocinados de res, cerdo, pollo, cordero, conejo y pescado; pan, manzana, patata horneada, galleta, sopas/guisos, manzana dorada, zanahoria dorada, bayas, miel, alimentos venenosos y otros ingredientes. También hay que completar pociones, cubos y variantes de proyectil/uso.

**Objetos y bloques recientes de Bedrock 26.60:** lanzas de las siete familias (madera, piedra, cobre, hierro, oro, diamante y netherite), bola de hielo, cristal de hielo, icículo y huevo generador de Frostbite. Parte del contenido de frío está señalado como experimental en el changelog: debe respetar sus flags y no aparecer por defecto como si fuera contenido estable universal.

- **Armadura del jugador:** cuatro slots actuales no bastan para elytra ni equipo de montura/compañero. Hace falta ampliar el modelo de equipo antes de habilitar esos objetos.
- **Armas de distancia:** arco, ballesta y tridente necesitan proyectiles, munición, física, animación, cooldown, daño, drops y guardado; añadir solo el icono no representa la función.
- **Lanzas y maza:** requieren alcance, carga/velocidad cuando corresponda, cooldown, durabilidad y balance por material.
- **Herramientas especiales:** tijeras, pedernal y acero, caña de pescar, pincel y catalejo necesitan acciones y estado funcional propio.
- **Uso y consumibles:** cada comida, poción, cubo y proyectil necesita receta/loot, consumo/efecto, animación/sonido y guardado.

### Incorporaciones recientes que requieren datos específicos

La referencia Bedrock 26.60 contiene las familias de lanzas y contenido de frío experimental de la actualización: bola de hielo, cristal de hielo, icículos y Frostbite. Esos elementos no deben presentarse como completamente disponibles en todos los mundos sin reproducir sus condiciones de experimental/feature flags y mecánicas.

Java 26.3 incluye, por separado, bosque moteado, álamos y set de madera, hongos de estante, arbustos rojos, campamentos abandonados, mapas de explorador, cojines, camas de paja y variantes de escaleras/losas de lana y concreto. Se registran como diferencias Java, no se mezclan dentro del objetivo Bedrock sin decidir paridad cruzada.

### Regla de IDs y migración

- Nunca copiar `raw_id` de Mojang al enum `BlockId`: el metadata oficial contiene valores negativos y colisiones de espacio con los IDs propios.
- Los 414 IDs actuales son estables. Cualquier contenido nuevo se añade después del último ID o al registro de ítems separado.
- La solución final es dividir bloques colocables e ítems no colocables en un `ItemRegistry` con namespace/ID estable, conservar un mapeo de compatibilidad con los IDs antiguos y versionar los sidecars/save data.
- Para cada objeto nuevo se necesita icono original, nombre localizado, stack máximo, categoría, receta/drop, uso, durabilidad, modelo en mano, slot de equipo si corresponde, efecto, guardado y sincronización LAN.
- Elytra, armaduras de caballo/lobo/nautilus y objetos de montura no se deben meter a la fuerza en los cuatro slots del jugador: requieren ranuras y sistemas de montura/compañero separados.
- No crear objetos visuales sin resolver cómo se usan y guardan.

## 6. Plan de implementación recomendado

### Fase A — estabilidad visual y entrada (prioridad máxima)
- Cerrar CI de la corrección multitáctil, IDs HUD y unpack GLES.
- Pruebas físicas de entrada, EGL y escalado.
- Captura de diagnóstico para superficie/FBO/atlas/shaders.

### Fase B — renderer y calidad
- Tests de escena para opacos/cutout/transparencia.
- Ordenación de transparencias revisada y un toggle experimental si se estudia OIT.
- Scheduler persistente, límites CPU/GPU, chunk eviction y LOD multicapas.
- Fases de iluminación/agua/lluvia con fallback móvil.

### Fase C — catálogo y comportamiento
- Registro separado de bloques e ítems, namespace y IDs nuevos.
- Completar herramientas, armaduras y armas base; sistema de uso y desgaste, recetas/loot y reparación.
- Añadir comida, herramientas especiales, pociones/proyectiles y objetos recientes por grupos.
- Luego entidades, vehículos, monturas, equipo de caballo/lobo/nautilus y efectos avanzados.

### Fase D — validación completa de interfaces
- Android instrumentation/UI tests donde sean posibles y matriz manual con dispositivos.
- Ruta de comprobación de cada botón/acción, cada panel y cada estado de apertura/cierre.
- Partidas nuevas y existentes, guardado/carga, reanudación del proceso y pérdida del contexto EGL.
- LAN con dos teléfonos y contenido idéntico/diferente.

## 7. Criterios de aceptación obligatorios

1. Python, Rust, C++ y Android ARM64/ARMv7 CI verdes para el mismo commit.
2. Firma y SHA-256 comprobados; Release firmada con keystore persistente para actualización in-place.
3. Prueba en teléfonos reales de gama baja/media/alta con GPU, GLES, resolución y Android anotados.
4. Cero pantallas blancas/negras reproducibles, shader/FBO/atlas sin errores, agua/transparencia estable y guardados intactos.
5. Todos los botones y entradas del HUD probados con uno o más dedos, gestos cancelados, cambios de foco y paneles abiertos.
6. Cada objeto añadido puede obtenerse, almacenarse, seleccionarse, usarse, romperse/repararse cuando proceda, verse en mano e inventario y recuperarse tras guardar/cargar.
7. La sesión LAN valida la misma versión del protocolo, catálogo y mods; sincroniza los estados implementados.

**No declarar terminado el motor, todas las interfaces ni la paridad Minecraft hasta satisfacer estos criterios.** La CI en verde es necesaria, pero no demuestra por sí sola que cada GPU Android, cada control o cada pantalla funcione físicamente.

## Fuentes de investigación

- Minecraft Bedrock 26.60 changelog (Mojang): https://www.minecraft.net/en-us/article/minecraft-bedrock-edition-26-60-changelog
- Mojang Bedrock Samples, stable tag v1.26.60.5: https://github.com/Mojang/bedrock-samples/tree/v1.26.60.5
- Mojang item metadata: https://github.com/Mojang/bedrock-samples/blob/v1.26.60.5/metadata/vanilladata_modules/mojang-items.json
- Minecraft Java 26.3 release notes: https://www.minecraft.net/en-us/article/minecraft-java-edition-26-3
- Android GLSurfaceView.Renderer — EGL context loss/recreation: https://developer.android.com/reference/android/opengl/GLSurfaceView.Renderer
- Android GLSurfaceView — pause/resume and context preservation: https://developer.android.com/reference/android/opengl/GLSurfaceView
- OpenGL ES 3 `glPixelStorei` unpack state: https://docs.gl/es3/glPixelStorei
- Khronos OpenGL ES 3.0 header constants: https://github.com/KhronosGroup/OpenGL-Registry/blob/main/api/GLES3/gl3.h
