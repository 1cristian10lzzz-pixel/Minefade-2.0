# Compilación Android y política de lanzamientos

## Requisitos declarados por el proyecto

La configuración actual de Gradle/CMake declara:

- JDK 17.
- Android SDK Platform 36 y Android Build Tools 36.0.0.
- Android NDK `28.2.13676358`.
- CMake 3.22.1.
- Gradle Wrapper 9.6.0.
- Rust stable + Cargo.
- Targets Rust Android `aarch64-linux-android` y `armv7-linux-androideabi`.
- Conexión a GitHub durante CMake para descargar FreeType 2.13.3 y RmlUi 6.3 (FetchContent).

Comprobar las versiones instaladas con el equipo de build antes de compilar. El proyecto no incluye el SDK ni el NDK.

## Comando de compilación

Desde la raíz del repositorio:

```bash
chmod +x gradlew
./gradlew --no-daemon :app:assembleDebug
```

Salida esperada:

```
app/build/outputs/apk/debug/app-debug.apk
```

Este path es la salida esperada de Gradle; no significa que ya exista un APK en este repositorio.

## Validaciones previas a una Release

1. Ejecutar los tests nativos de mundo (`tests/run-world-tests.sh`) y todas las pruebas Python de `tests/`.
2. Compilar las ABI declaradas y revisar logs de Gradle/CMake/Rust.
3. Instalar el APK desde cero y actualizar desde la versión previa cuando la firma lo permita.
4. Abrir un mundo existente, construir/romper bloques, guardar, cerrar y volver a abrir.
5. Comprobar avatar, inventario, HUD, controles táctiles y tercera persona.
6. Comprobar agua/transparencia, shaders, carga de chunks y frustum.
7. Pausar/reanudar, bloquear/desbloquear pantalla y forzar recreación de Surface/EGL.
8. Probar LAN en dos Android reales y revisar desconexión/reconexión.
9. Medir rendimiento/temperatura en teléfonos representativos; registrar dispositivos y GPU.
10. Revisar permisos, assets y avisos de licencias de dependencias.

## Versionado y firma

El módulo conserva actualmente `applicationId = com.minefade.nativeengine`, `versionCode = 23` y `versionName = 2.0.3`. No cambiar el identificador ni reducir el versionCode de una actualización sin una decisión explícita sobre instalación y migración. Una firma de debug no garantiza que un APK sea instalable como actualización de otro firmado con una clave distinta.

No se ha encontrado en esta copia una clave de firma de producción. No incluir claves privadas, contraseñas ni keystores en Git.

## Política de GitHub Releases

- No crear una Release para una compilación no probada.
- Subir solo APK generado a partir del commit que acompaña la Release.
- Añadir en la descripción: cambios, fallos corregidos, pruebas realizadas, ABI, versionCode/versionName y limitaciones conocidas.
- Mantener `CHANGELOG.md` actualizado.
- No usar GitHub Actions. La compilación y publicación deben hacerse manualmente.
- No borrar versiones ya publicadas ni mundos de usuarios.
