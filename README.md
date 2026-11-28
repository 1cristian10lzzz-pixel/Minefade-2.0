# Minefade 2.0

Juego voxel nativo para Android. Este es el repositorio principal de desarrollo.

> **Estado:** fuente y recursos importados; el APK se compila ahora en GitHub Actions. Ninguna compilación se considera Release estable hasta superar las pruebas en Android real.

## Descargar el APK

Cada cambio enviado a `main` inicia una compilación remota. Para descargarla desde el teléfono:

1. Abre [Actions](https://github.com/1cristian10lzzz-pixel/Minefade-2.0/actions).
2. Selecciona la ejecución más reciente **Minefade Android APK**.
3. Descarga el artefacto `Minefade-2.0-APK-...` al final de la página.

Los tags `v*` generan un borrador de prerelease con el APK y su SHA-256. Se publica como estable solo después de probar instalación, mundos y controles en un teléfono.

## Arquitectura

- Android / Java: actividad, UI, controles, mundos, audio, mods/shaders y descubrimiento LAN NSD.
- C++20: motor voxel, jugador, meshing y backend OpenGL ES 3.
- Rust: núcleo de simulación mediante ABI C.
- RmlUi 6.3 y FreeType 2.13.3.
- Chunks, greedy meshing, culling por frustum, transparencias y LOD de distancia inicial.

## Actualización técnica 2.2.4 — recuperación de fluidos LOD2
- El LOD lejano usa alturas separadas de terreno y fluidos y vuelve a construir las superficies de agua/lava.
- Se añade una prueba nativa para un bloque de agua en LOD2.
- La firma persistente de distribución sigue siendo necesaria para una actualización in-place.

## Actualización técnica 2.2.4 — creación, LAN y avatar
- Creación de mundo organizada en pestañas General, Avanzado y Trucos, conectadas al nombre, modo, semilla, cofre y comandos que el juego persiste.
- Multijugador local: módulos Crear/Unirse junto a Salir y buscador que filtra sesiones NSD descubiertas por nombre/anfitrión.
- Editor de avatar con acciones Aleatorio y Restablecer, miniaturas más legibles y el mismo modelo 3D interactivo.
- La búsqueda es solo para partidas LAN descubiertas; no hay aún un backend de servidores públicos.

## Actualización técnica 2.2.3 — renderer voxel y carga de terreno

- La oclusión de bloques ahora respeta formas parciales: una losa, escalera o valla ya no oculta una cara completa del cubo vecino.
- La malla de cada chunk del camino móvil se compila en segundo plano a partir de una copia inmutable; solo se publica si la revisión del terreno no ha cambiado.
- Las nuevas mallas GPU se cargan en buffers temporales y se intercambian juntas tras validar las capas opaca y transparente. Un fallo de subida mantiene la versión visible anterior.
- Se conserva la vegetación en el LOD cercano y se corrige el caso de chunks que contienen solo geometría transparente.
- Se incorporan pruebas de regresión para oclusión parcial y meshing asíncrono.

**Versión Android:** versionCode 27 / versionName 2.2.3. La compilación CI y las pruebas físicas siguen siendo obligatorias antes de declarar una Release estable.

## Actualización técnica 2.2.2 — interfaz, controles y fabricación
- Se corrige el error Android de descubrimiento NSD leyendo los atributos TXT desde sus bytes y validando sus valores.
- La fabricación ahora es transaccional: ingredientes y resultado se confirman juntos, sin pérdidas por fallos parciales.
- Se amplía el recetario con objetos de supervivencia, construcción y mecanismos.
- Se compacta la barra rápida, se mejora el contraste y se amplían los paneles táctiles de inventario y fabricación.
- Se corrige el texto para mostrar las 36 casillas reales y se refleja el botón de ataque para el perfil zurdo.

## Actualización técnica 2.2 — sistema MFMod ampliado

- Se amplía el formato de mods de datos para controlar movimiento agachado, velocidad y radios de IA de criaturas, aparición periódica limitada de fauna, límites de población, minería y objetos soltados.
- El importador Android, el motor C++ y el validador Python comparten claves y límites; los paquetes antiguos siguen siendo válidos.
- Se incorpora el ejemplo Minefade Aventura Plus en examples/; no se instala ni se activa automáticamente.
- Perfil mejorado: `examples/Minefade_Aventura_Plus_Extendido_2_0.mfmod`, con más fauna y aparición ambiental limitada, ajustes de IA, movimiento, minería y objetos. El perfil se importa manualmente y comienza desactivado.
- Las nuevas opciones necesitan el APK 2.2.0; un APK 2.1.0 no las reconoce.

## Actualización técnica 2.1

- Subida de mallas GPU limitada y priorizada por cercanía a la cámara.
- Restablecimiento del estado OpenGL de superficie antes de limpiar cada frame.
- Diagnóstico del HUD con chunks pendientes y recursos GPU residentes/pendientes.
- GitHub Actions valida los pull requests y no crea commits automáticos de recursos.

**Versión Android:** versionCode 28 / versionName 2.2.4. Es una compilación de validación hasta probarla en dispositivos Android reales.

## Sistemas presentes

Menú Android, HUD táctil, personalización del avatar, inventario, mundos separados, modos supervivencia/creativo/aventura, comandos, fauna, mods de datos, shaders y prototipo LAN NSD/TCP.

## Compatibilidad de mundos

El cargador reconoce `MFVOX001`, `MFVOX002` y `MFVOX003`; la escritura actual produce `MFVOX003`. No cambiar formatos ni IDs de bloque sin migración versionada y pruebas.

## Estado del renderer

La caché por chunk y buffers GPU incrementales siguen siendo la base. La compilación de un chunk del camino móvil ahora usa snapshots y un trabajador en segundo plano, con validación de revisión y publicación segura; el upload GPU conserva la malla anterior hasta tener listas ambas capas. El LOD lejano sigue siendo una aproximación por altura, no una implementación de Distant Horizons. Véase [el plan técnico](docs/RENDERER_UPGRADE_PLAN_2_0.md).

## Organización

- `app/` — app Android, C++, recursos.
- `rust-core/` — núcleo Rust.
- `tests/` — pruebas.
- `tools/` — herramientas de catálogo/mods/shaders.
- `docs/` — arquitectura y validación.

## Historial y seguridad

- La base se importó desde el commit `562761841115903523126e5f8bd4db001f2fc061` del repositorio anterior.
- El repositorio anterior permanece intacto.
- No se almacenan contraseñas ni claves privadas de firma en Git.
