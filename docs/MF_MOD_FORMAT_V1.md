# Minefade Data Mod — formato MFMod v1 ampliado

Un archivo .mfmod es texto UTF-8 con una propiedad clave=valor por línea. Las líneas que empiezan por # son comentarios. No es ZIP y no ejecuta código: el motor solo acepta claves de su lista permitida.

## Metadatos

| Clave | Valores |
|---|---|
| id | Identificador único: minúsculas, números, _, . o - |
| name | Nombre visible (1–96 caracteres) |
| version | Versión del formato: 1 o 1.0 |
| enabled | true o false |
| description | Texto opcional, hasta 512 caracteres |\n| icon | Icono opcional: `cube`, `tool`, `creature`, `world`, `magic` o `decor` |

## Herramientas de autoría

El menú MODS permite crear un paquete desde el botón **+**, asignar nombre,
descripción y categoría visual, e importar paquetes existentes. También ofrece
una plantilla de fauna que se crea solo cuando el jugador la solicita. Los
paquetes nuevos y la plantilla comienzan desactivados; no se instala contenido
demo silenciosamente. El campo `icon` es metadato visual y no altera la lógica.

## Ajustes admitidos

Los paquetes antiguos que solo usan los campos originales siguen siendo compatibles. El campo opcional `icon` solo afecta a la tarjeta visual del gestor. Las nuevas claves extienden el formato de datos v1 sin cambiar la importación.

| Clave | Tipo | Rango | Efecto |
|---|---:|---:|---|
| spawn.sheep, spawn.cow, spawn.bird, spawn.pig | entero | 0–24 | Criaturas adicionales al iniciar el mundo; máximo combinado de 24 por tipo |
| spawn.rabbit | entero | 0–24 | Conejos adicionales; máximo combinado de 16 |
| spawn.zombie, spawn.skeleton | entero | 0–24 | Enemigos adicionales; máximo combinado de 8 por tipo |
| player.speed | decimal | 0.5–2.0 | Velocidad general del jugador |
| player.jump | decimal | 0.75–1.5 | Fuerza del salto |
| player.crouch_speed | decimal | 0.25–1.0 | Velocidad relativa al caminar agachado |
| entity.speed | decimal | 0.5–2.0 | Multiplicador general de criaturas |
| entity.passive_speed | decimal | 0.5–2.0 | Velocidad adicional de criaturas pasivas |
| entity.hostile_speed | decimal | 0.5–2.0 | Velocidad adicional de enemigos |
| entity.flee_radius | decimal | 2–16 | Distancia a la que las criaturas pasivas huyen |
| entity.alert_radius | decimal | 4–32 | Distancia a la que los enemigos detectan y persiguen |
| entity.spawn_radius | decimal | 1–8 | Separación radial de criaturas añadidas al aparecer |
| world.day_speed | decimal | 0.25–4.0 | Ritmo del ciclo día/noche |
| world.entity_cap | entero | 8–128 | Límite de entidades en el sistema |
| world.spawn_interval | decimal (segundos) | 30–900 | Intervalo de aparición de fauna pasiva; desactivado si se omite |
| world.spawn_distance | decimal | 8–48 | Distancia desde el jugador a la que aparece fauna nueva |
| world.passive_cap | entero | 1–64 | Límite de criaturas pasivas para la aparición ambiental |
| drops.pickup_radius | decimal | 0.8–4.0 | Radio de recogida de objetos del suelo |
| drops.lifetime | decimal (segundos) | 30–900 | Tiempo antes de eliminar un objeto abandonado |
| drops.quantity | entero | 1–4 | Cantidad que suelta un bloque al romperlo |
| mining.speed | decimal | 0.5–3.0 | Multiplicador de velocidad de minería en supervivencia |

## Reglas y seguridad

- Máximo 32 KB y 128 líneas.
- Claves únicas; no se aceptan claves desconocidas.
- Los números deben ser finitos y estar dentro de sus rangos.
- Los campos obligatorios son id, name, version y enabled.
- El importador guarda el paquete en el espacio privado de Minefade y lo deja desactivado hasta que el jugador lo active.
- Los multiplicadores se combinan multiplicativamente y quedan limitados; los conteos se agregan con topes. Para world.entity_cap se aplica el límite más restrictivo.
- El motor Android, el lector nativo C++ y tools/validate_mfmod.py deben mantener la misma lista de claves y límites.

## Límites actuales

Esta API controla sistemas ya existentes: jugador, criaturas, IA de huida/persecución y aparición periódica limitada de fauna, minería y objetos soltados. No añade por sí sola modelos o especies nuevas, bloques nuevos, texturas personalizadas, recetas, biomas, estructuras, lluvia, salud/combate ni monturas. Tampoco ejecuta Lua, JavaScript, Java, JNI, C++ ni bibliotecas externas. Esas funciones requieren sistemas de motor adicionales; no deben anunciarse como compatibles con un .mfmod de esta versión.
