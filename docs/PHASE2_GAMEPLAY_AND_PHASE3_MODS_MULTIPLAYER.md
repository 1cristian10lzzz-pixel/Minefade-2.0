# Minefade 2.0 — fases 2 y 3: mundo, supervivencia, mods y multijugador

## Objetivo y regla de compatibilidad

Este documento registra la implementación de las fases 2 y 3 sobre la línea 2.2.0 y la base de estabilidad de la fase 1.

La prioridad es conservar los mundos, los identificadores históricos de bloques, las ABI del núcleo Rust y los paquetes MFMod v1. Los sistemas nuevos se añaden alrededor de esos contratos. No se migra automáticamente ni se sobrescribe un mundo cuando su conjunto de mods no coincide.

## Fase 2 — mundo y jugabilidad

### Ya implementado en esta revisión

- **Vida y hambre de supervivencia:** el motor mantiene vida y comida en un rango de 0 a 20. El modo creativo no agota comida ni recibe daño por el ciclo de supervivencia.
- **Gasto de comida:** el movimiento consume agotamiento; el agotamiento se convierte de forma limitada en puntos de hambre.
- **Regeneración:** con la barra de comida alta, la vida regenera por intervalos y consume comida.
- **Inanición:** con hambre a cero, se aplica daño periódico hasta que el jugador coma o muera.
- **Daño por caída:** se detecta una nueva toma de suelo y se calcula daño a partir de la velocidad vertical previa al aterrizaje. No se calcula daño cuando el jugador simplemente sigue en el suelo.
- **Daño de enemigos cercanos:** las entidades hostiles existentes (kinds 4 y 5) hacen daño por contacto con enfriamiento, evitando una pérdida de vida por cada frame.
- **Combate cuerpo a cuerpo:** botón de ataque, alcance limitado y cono frontal; solo se dañan las entidades hostiles. Puños, espada, hacha y pico usan daños distintos. Los enemigos llevan salud en tiempo de ejecución y desaparecen al llegar a cero.
- **Comer desde el inventario:** el botón COMER consume una unidad seleccionada de zanahorias o patatas y restaura comida. No consume el objeto si la acción no es válida o la barra ya está llena.
- **HUD de supervivencia:** muestra vida y comida junto con las métricas de rendimiento.
- **Reaparición:** al llegar a cero de vida se intenta resolver una posición segura, se restaura una cantidad inicial de vida y comida y se conserva el inventario.
- **Guardado de supervivencia sin migrar el mundo:** los valores de vida y comida se escriben en el sidecar `.survival` con magic y versión, validación de tamaño/valores y sustitución temporal. Los archivos MFVOX001/002/003 mantienen su estructura histórica.

### Sistemas que ya existían y se conservan

- Modos supervivencia, creativo y aventura.
- Inventario de 36 espacios, barra rápida de 9 y 4 huecos de armadura.
- Recetas actuales, creación de herramientas y equipamiento, cofre inicial y objetos recogibles.
- Fauna inicial, entidades pasivas/hostiles básicas, comportamiento de huida y persecución.
- Biomas, ríos, cuevas, vegetación, agua de nivel estático y ruinas deterministas actuales.
- Minería, colocación de bloques, comandos, personalización del avatar, vista en primera/tercera persona y shaders de parámetros existentes.

### Pendiente para cerrar la fase 2 al nivel de un juego completo

1. **Alimentos completos:** cocinar, carne, pan, pescado, cultivos con estados de crecimiento completos, pociones y efectos temporales. En esta revisión solo se habilitan zanahorias y patatas que ya existen en el catálogo.
2. **Durabilidad real:** desgaste persistente por herramienta y armadura, barras de durabilidad, rotura y reparación. El inventario actual codifica identificador/cantidad en cada slot y aún no almacena durabilidad individual.
3. **Loot y economía de entidades:** tablas de drops por entidad, experiencia, objetos de cadáver, domesticación y reproducción. El ataque implementado retira el enemigo al derrotarlo, pero todavía no crea una tabla de botín.
4. **Combate completo:** ataques enemigos animados, proyectiles, retroceso, armaduras con absorción específica por material, invulnerabilidad temporal y estados alterados.
5. **IA ampliada:** navegación, búsqueda de ruta con obstáculos, memoria de objetivos, grupos, horarios, reproducción, monturas y animaciones completas para cada especie.
6. **Mundo:** nuevas estructuras y mazmorras conectadas a biomas, aldeas, minas, templos, dimensiones y puntos de interés más extensos. Las ruinas actuales no equivalen a un generador de estructuras completo.
7. **Clima y líquidos:** lluvia visible con partículas, acumulación/evaporación si procede, nieve, tormentas, ciclos de clima y simulación de flujo de fluidos. El agua actual es bloque fluido de nivel estático, no una simulación dinámica.
8. **Pruebas de jugabilidad:** equilibrio de daño/hambre, guardado/carga, muerte/reaparición, combate, inventario táctil, FPS y temperatura en móviles reales.

## Fase 3 — mods y multijugador

### Formato MFMod v2 incorporado

Se añade el contenedor ZIP de datos `.mfmod2`, manteniendo el formato de texto `.mfmod` v1.

Manifiesto v2 requerido:

- `format`: 2.
- `id`: identificador estable en minúsculas.
- `name`: nombre visible.
- `version`: versión semántica del paquete.
- `engine_min` y `engine_max`: rango semántico de compatibilidad.
- `entry`: actualmente debe ser `data/gameplay.mfmod`.
- `sha256`: SHA-256 del archivo de ajustes.

Entradas permitidas del ZIP:

- `manifest.json`
- `data/` (directorio opcional)
- `data/gameplay.mfmod`

No se admiten rutas absolutas, traversal, separadores de Windows, entradas repetidas, archivos desconocidos, scripts, DEX/JAR, bibliotecas nativas ni recursos arbitrarios. Se limitan entradas y tamaño expandido. La importación verifica UTF-8, JSON, versión del motor, SHA-256, ID/nombre y el parser MFMod v1. El paquete se escribe en una carpeta temporal y se mueve con rename; se instala desactivado.

El motor nativo conserva los archivos v1 planos y también descubre únicamente el payload `gameplay.mfmod` dentro de una carpeta de paquete que tenga `manifest.json`. El parser de ajustes continúa aplicando la whitelist y los rangos existentes.

### Herramientas y compatibilidad

- `tools/mfmod2.py pack`: empaqueta un archivo MFMod v1 desactivado en el nuevo contenedor.
- `tools/mfmod2.py validate`: verifica ZIP, rutas, campos, rango de motor y hash.
- El administrador de mods lista el payload instalado del paquete y permite activar/desactivar su archivo de ajustes.
- Al activar/desactivar un payload MFMod v2 se vuelve a calcular el SHA-256 del manifiesto.
- Cada mundo guarda un sidecar `.mods` con la huella SHA-256 del conjunto de mods activos. En una discrepancia se avisa y se conserva la huella anterior; no se reescribe silenciosamente.

### LAN — protocolo 3

- La publicación NSD anuncia el protocolo y la huella SHA-256 de los mods activos.
- El descubrimiento comprueba la versión del protocolo y lee la huella del anuncio.
- HELLO incluye protocolo, huella de contenido y nombre.
- El host rechaza clientes que no tengan exactamente la misma huella.
- WELCOME devuelve la huella del host y el cliente la vuelve a comprobar.
- La búsqueda, el ingreso y el hosting usan la misma huella calculada por ModManager.

Esto evita iniciar una sesión con distintas versiones de mods de configuración. No convierte todavía la LAN en un servidor autoritativo completo.

### Pendiente para cerrar la fase 3 al nivel de un sistema de mods/multijugador completo

1. **Registries dinámicos:** bloques, materiales, objetos, herramientas, armaduras, recetas, loot, entidades, modelos, atributos, biomas, estructuras, sonidos y partículas. El payload actual solo aplica los parámetros permitidos por MFMod v1.
2. **Recursos visuales/sonoros:** texturas, atlas, modelos MAES, animaciones, audio y UI por namespace, con manifiesto de recursos y carga diferida segura.
3. **Dependencias y conflictos:** resolución de dependencias, versiones, incompatibilidades, orden de carga, namespaces y permisos/capacidades por mod.
4. **Eventos y scripting:** bus de eventos versionado y acotado. Si se incorpora scripting, deberá aislar Lua con IO/OS/package/debug, red, JNI y acceso directo a archivos bloqueados; límite de instrucciones y memoria por mod. Nunca cargar código nativo o DEX/JAR desde mods.
5. **Creador dentro del juego:** editor de configuración y recursos, plantillas, previsualización, validador, información de permisos, dependencias y exportador .mfmod2. La revisión actual aporta el formato y las herramientas CLI; no un editor completo.
6. **Mundo autoritativo LAN:** sincronización inicial de bloques/chunks, seed y reglas del mundo, entidades, salud, inventario, equipo, drops, clima, hora y cambios incrementales; autoridad del host, límites de paquetes, validación y reconciliación.
7. **Persistencia LAN:** guardado del mundo del anfitrión, recuperación tras desconexión, reconexión, control de versiones y tratamiento de desconexiones durante escritura.
8. **Pruebas entre dispositivos:** al menos dos teléfonos en la misma Wi-Fi, mods iguales/distintos, rechazo de paquetes incompatibles, latencia, pérdida de señal y cierre/reinicio. Esta revisión no ha sido probada físicamente entre dos Android.

## Criterios de aceptación para la compilación de las fases 2 y 3

- Python: todas las pruebas antiguas y nuevas de supervivencia, v2, integridad del ZIP y protocolo.
- Rust y C++: regresiones de mundo, inventario, combate, serialización y compatibilidad histórica.
- Android: compilación ARM64 y ARMv7, verificación de la firma y SHA-256 del APK.
- Prueba manual real: abrir mundo antiguo, abrir mundo nuevo, comer, perder vida, morir/reaparecer, guardar/cargar, importar un v1 y un v2, activar/desactivar mods, comprobar huella y rechazar sesión LAN con mods distintos.
- La compilación CI no sustituye las pruebas de pantalla, controles, GPU ni dos teléfonos reales.

## Regla de alcance

La base de las fases 2 y 3 queda ampliada con supervivencia, combate inicial, paquetes de datos v2 y negociación LAN compatible por huella. No se debe marcar como terminado el juego completo, el editor de mods, los registries de contenido, el simulador de clima/fluidos ni el multijugador con sincronización integral hasta implementar y probar los apartados pendientes de este documento.
