# Compilación Android y política de lanzamientos

## Compilación remota automática

No necesitas computadora ni Android Studio para generar el APK. El repositorio usa GitHub Actions para ejecutar la compilación en servidores de GitHub.

El flujo está en `.github/workflows/android-apk.yml` y se ejecuta:
- En cada pull request dirigido a `main` (pruebas y APK de validación antes de integrar).
- Al recibir cambios en `main`.
- Al crear un tag que empiece por `v`.
- Cuando se inicia manualmente desde la pestaña Actions.

El job de compilación usa permisos de solo lectura. El job separado que crea borradores de prerelease recibe permiso de escritura únicamente en tags `v*`. Los generadores se ejecutan en modo de verificación: si producen diferencias, CI falla y pide actualizar los recursos en el mismo commit; no modifica `main` con commits bot fuera de la revisión probada.

El runner configura JDK 17, Android SDK Platform 36, Build Tools 36.0.0, NDK 28.2.13676358, CMake 3.22.1, Rust stable con targets Android y Gradle 9.6.0. Ejecuta primero las pruebas Python y Rust/C++. Los PR y pushes normales producen un APK debug de validación; los tags `v*` producen un APK release firmado con la clave persistente, para `arm64-v8a` y `armeabi-v7a`.

Cada compilación correcta publica un artefacto APK descargable en el resumen de ejecución de Actions, con un SHA-256. El artefacto se conserva 90 días.

## Cómo descargarlo desde Android

1. Abre el repositorio Minefade 2.0 en GitHub.
2. Entra en la pestaña **Actions**.
3. Abre la ejecución más reciente llamada **Minefade Android APK**.
4. Baja hasta **Artifacts**.
5. Descarga `Minefade-2.0-APK-...` y abre el APK desde el gestor de descargas.

Si la ejecución está fallida, el APK no se publica. Abre los logs de la ejecución para ver el primer error real.

## Requisitos declarados por el proyecto

- JDK 17.
- Android SDK Platform 36 y Android Build Tools 36.0.0.
- Android NDK `28.2.13676358`.
- CMake 3.22.1.
- Gradle 9.6.0.
- Rust stable + Cargo.
- Targets Rust Android `aarch64-linux-android` y `armv7-linux-androideabi`.
- Conexión a GitHub durante CMake para descargar FreeType 2.13.3 y RmlUi 6.3 (FetchContent).

## Versionado y firma

El módulo conserva `applicationId = com.minefade.nativeengine`, `versionCode = 30` y `versionName = 2.3.1`. No reducir el versionCode ni cambiar el identificador sin una migración probada.

Los APK de PR y validación normal son debug y no se deben usar como actualizaciones de producción. Los tags `v*` generan `app-release.apk`, firmado con la clave privada persistente del proyecto y con `debuggable=false`.

Para activar la firma, añade estos cuatro secretos en GitHub: **Settings > Secrets and variables > Actions > New repository secret**:
- `MINEFADE_RELEASE_KEYSTORE_BASE64`
- `MINEFADE_RELEASE_STORE_PASSWORD`
- `MINEFADE_RELEASE_KEY_ALIAS`
- `MINEFADE_RELEASE_KEY_PASSWORD`

El almacén PKCS#12 y los valores correspondientes se entregan al propietario del proyecto por separado. Nunca los subas a Git ni los muestres en logs. No regeneres la clave para próximas versiones: todas deben conservar el mismo certificado y `applicationId = com.minefade.nativeengine`. Si pierdes el almacén privado, no podrás firmar una actualización compatible sobre las instalaciones existentes.

Los tags `v*` crean un borrador de prerelease firmado. Antes de declarar estable, prueba instalarlo encima de la versión anterior, abrir/guardar mundos y validar el juego en Android físico.

## Criterios antes de una Release estable

1. Las pruebas Python, Rust y C++ deben pasar.
2. El APK debe compilar para las ABI publicadas y verificarse con `apksigner`.
3. Debe instalarse y abrirse en Android real.
4. Debe probarse la apertura, guardado y reapertura de un mundo existente.
5. Deben comprobarse avatar, inventario, HUD, controles táctiles y tercera persona.
6. Deben probarse agua/transparencia, shaders, carga de chunks, pausa y recreación de Surface/EGL.
7. El multijugador LAN debe probarse entre dos dispositivos.
8. Registrar dispositivos, GPU, logs y métricas; no inventar cifras de rendimiento.
9. La descripción del Release debe incluir cambios, fallos corregidos, pruebas, ABI, versionCode/versionName y limitaciones.

## Construcción manual

Si alguien dispone de Android SDK/NDK, JDK, Rust y Gradle 9.6.0, puede ejecutar desde la raíz:

```bash
gradle --no-daemon :app:assembleDebug
```

Salida esperada: `app/build/outputs/apk/debug/app-debug.apk`.
