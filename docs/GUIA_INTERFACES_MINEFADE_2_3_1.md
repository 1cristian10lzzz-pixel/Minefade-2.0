# Guía visual de interfaces — Minefade 2.3.1

Esta guía aplica el lenguaje visual de las referencias entregadas para Minefade 2.0. No cambia IDs de inventario, mundos ni acciones de juego.

## Lenguaje visual común
- Paneles azul pizarra y azul noche; bordes claros fríos y acentos cian.
- Verde lima reservado para acciones principales y selección activa.
- Esquinas moderadamente redondeadas; bordes finos, contraste alto y superficies compactas.
- Iconos de objetos en pixel art nítido, sin estirar la textura.
- Evitar sombras pesadas detrás de la HUD y la barra rápida.

## 1. Juego / HUD
- Minimapa y pausa en la zona superior.
- El centro de la pantalla queda libre para apuntar y observar el mundo.
- La información de armadura, vida, hambre, nivel y experiencia va en una fila por encima de la barra rápida.
- La barra rápida permanece centrada, con nueve casillas, contadores en la esquina inferior y selección claramente delimitada.
- Los controles táctiles conservan sus áreas y callbacks; el rediseño no debe interceptar la cámara ni el movimiento.

## 2. Inventario
- Panel principal azul pizarra ocupando el área útil, sin cubrir los márgenes de seguridad del dispositivo.
- Zona de jugador/equipamiento y cuadrícula de objetos separadas visualmente.
- Cuatro espacios de armadura agrupados junto al personaje; los 36 espacios de inventario siguen usando sus IDs existentes.
- En horizontal, columnas paralelas; en vertical, secciones apiladas y desplazables.
- Los números de cantidad y durabilidad se conservan y mantienen legibilidad.

## 3. Fabricación
- Mesa visual con cuadrícula 2×2, flecha y espacio de resultado en la parte superior.
- Recetas disponibles en una lista o cuadrícula desplazable debajo.
- La mesa visual no sustituye el sistema de fabricación: cada botón de receta debe seguir invocando la acción real y validar ingredientes/resultado antes de consumir materiales.

## 4. Menús, mundos y secciones
- Menú principal con marca Minefade, accesos compactos y arte de portada.
- Tarjetas de mundos con nombre, modo, semilla y acción de entrada.
- Avatar, ajustes, multijugador y pausa reutilizan el mismo lenguaje de paneles, botones, títulos y bordes.
- Los listados largos deben desplazarse dentro de su propio panel; no se recortan los últimos controles.

## 5. Comprobaciones de aceptación
- Probar 360, 390 y 430 dp, tableta, rotación y pantalla con recorte/notch.
- Abrir/cerrar inventario y fabricación; probar todas las recetas y arrastre/selección de espacios.
- Confirmar que la barra conserva nueve IDs y la selección real del inventario.
- Entrar a un mundo nuevo y a uno guardado; revisar terreno al aparecer y después de girar/cambiar de aplicación.
- Forzar pausa/reanudación y recreación de EGL; comprobar que el jugador, mundo, inventario y HUD se conservan.
- Validar primera y tercera persona, controles multitáctiles, guardado/carga y FPS en un dispositivo Android real.

## Estado de validación
Las pruebas automatizadas y la compilación de CI se usan como puertas de regresión. No demuestran por sí solas el aspecto final de la GPU, el tacto ni la estabilidad térmica en todos los teléfonos; estas comprobaciones físicas siguen siendo obligatorias.
