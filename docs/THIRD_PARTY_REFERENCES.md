# Referencias técnicas y licencias

Este documento distingue entre código que está en Minefade y proyectos estudiados como referencia. La mención de un proyecto no significa que su código esté integrado.

## Distant Horizons

- Repositorio principal: https://gitlab.com/distant-horizons-team/distant-horizons
- Core indicado por el submódulo: https://gitlab.com/distant-horizons-team/distant-horizons-core
- Wiki de desarrolladores: https://gitlab.com/distant-horizons-team/distant-horizons/-/wikis/home

El ZIP entregado por el usuario incluye el repositorio principal y su archivo `.gitmodules`, que apunta al submódulo Core; no incluye los archivos del submódulo. No se ha compilado ni copiado Distant Horizons Core dentro de Minefade.

Uso previsto de la investigación: LOD progresivo, columnas de terreno, regiones jerárquicas, separación entre terreno cercano y lejano, persistencia de datos visuales derivados y prioridades de trabajo. La implementación de Minefade debe ser limpia e independiente, compatible con C++20/OpenGL ES 3 y con los datos reales de su mundo voxel.

## Sodium

- Repositorio: https://github.com/CaffeineMC/sodium
- Licencia de referencia documentada en la guía adjunta: PolyForm Shield 1.0.0.

La guía técnica advierte la condición Noncompete de PolyForm Shield. Por ello, este repositorio usa ideas generales de arquitectura de rendering (snapshots, colas priorizadas, cancelación, presupuesto de uploads y gestión de buffers) y no incorpora código, clases ni assets de Sodium.

## Chunky

- Repositorio: https://github.com/pop4959/Chunky
- Licencia de referencia documentada en la guía adjunta: GPL-3.0.

Minefade puede implementar un planificador independiente de pregeneración tomando como requisitos funcionales pausa, reanudación, cancelación, orden de región y persistencia de tareas. No copiar código de Chunky sin asumir y cumplir las obligaciones GPL aplicables.

## Dependencias declaradas en CMake

- FreeType: tag `VER-2-13-3`.
- RmlUi: tag `6.3`.

CMake descarga estas dependencias durante la configuración. Antes de distribuir binarios, conservar los textos de licencia/avisos de cada dependencia y revisar sus condiciones actuales y las de dependencias transitivas.

## Regla de incorporación futura

Antes de reutilizar código externo: fijar URL, rama/tag y commit exactos; revisar LICENSE y cabeceras por archivo; documentar modificaciones; comprobar compatibilidad de licencias; incluir los avisos y publicar el código fuente exigido. Este documento no es asesoría legal.
