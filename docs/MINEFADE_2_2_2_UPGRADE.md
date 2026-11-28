# Minefade 2.2.2 — interfaz, controles, inventario y fabricación

## Base de trabajo
Esta actualización parte de la rama de desarrollo más reciente de Minefade 2.0. Conserva la arquitectura Android + C++20 + Rust + OpenGL ES 3 + RmlUi, los identificadores del catálogo y la compatibilidad de guardados MFVOX.

El catálogo de investigación adjunto explica que Mojang no publica el código completo del cliente y motor Java/Bedrock. Los repositorios públicos contienen muestras, bibliotecas, esquemas, documentación y datos estructurados; no equivalen al motor íntegro. Minefade usa esos materiales como referencias de diseño y datos, sin redistribuir código propietario.

## 1. Error Android NSD
Android NSD entrega atributos TXT como `Map<String, byte[]>`. La ruta de resolución comprueba valores ausentes, limita cada atributo a 128 bytes y decodifica UTF-8 antes de validar la versión del protocolo y el fingerprint de contenido. Esto corrige el error `byte[] cannot be converted to String` detectado en Actions #159 sin debilitar la compatibilidad entre mods.

## 2. Fabricación transaccional
Las recetas se ejecutan sobre una copia temporal de `Inventory`. Los ingredientes se consumen en esa copia y después se inserta el resultado. La copia solo reemplaza al inventario vivo cuando todo termina correctamente. Así no se pierden materiales ante fallos parciales y el espacio liberado por los ingredientes puede alojar el resultado.

Se amplía el recetario con antorchas, cofres, hornos, puertas, escaleras, vallas, losas, trampillas, señales, raíles, placas de presión, botones y palancas. Se conservan las recetas existentes de tablones, palos, mesa, minerales, herramientas y armaduras.

## 3. HUD y controles táctiles
- Barra rápida más compacta y centrada para pantallas móviles.
- Mejor contraste en estadísticas, minimapa, joystick y casillas.
- Paneles de inventario y fabricación con mayor área útil y botones de receta más cómodos.
- Texto corregido para indicar las 36 casillas del inventario real, separadas de las 9 posiciones de la barra rápida.
- El botón de ataque también se refleja al seleccionar el esquema zurdo.
- Se conservan los modos de joystick/D-pad, sensibilidad, zona muerta, salto automático, inversión del eje vertical y ajustes de mano ya presentes.

## 4. Compatibilidad
- No se cambian los formatos MFVOX ni los IDs de bloques/objetos.
- No se altera el ABI Rust/C ni el formato MFMod.
- Android pasa a `versionCode 26` y `versionName 2.2.2`.

## 5. Referencias públicas para investigación
- Mojang Bedrock Samples: https://github.com/Mojang/bedrock-samples
- UI de Bedrock Samples: https://github.com/Mojang/bedrock-samples/tree/main/resource_pack/ui
- Behavior Packs y recetas: https://github.com/Mojang/bedrock-samples/tree/main/behavior_pack
- Datos de bloques, objetos y recetas versionadas: https://github.com/PrismarineJS/minecraft-data
- Brigadier: https://github.com/Mojang/brigadier
- DataFixerUpper: https://github.com/Mojang/DataFixerUpper

Cada repositorio mantiene su propia licencia; se debe revisar antes de incorporar código o recursos.

## 6. Pruebas necesarias antes de declarar estable
CI debe pasar todas las pruebas Python, Rust y C++ y compilar el APK para arm64-v8a y armeabi-v7a. Después debe probarse en teléfonos reales: instalación limpia, arranque, apertura/guardado/reapertura de mundos, inventario, recetas, controles con varios dedos, pausa, recreación de Surface/EGL y descubrimiento LAN entre dos dispositivos de la misma Wi-Fi. El APK debug del runner no garantiza actualización sobre instalaciones firmadas con otra clave.
