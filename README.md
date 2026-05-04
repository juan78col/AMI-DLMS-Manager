
# AMI-DLMS-Manager

Sistema AMI para administración de medidores vía DLMS/COSEM (C++20).

![CI](https://github.com/juan78col/AMI-DLMS-Manager/actions/workflows/build.yml/badge.svg)

Descripción
-----------
Proyecto que implementa la comunicación DLMS/COSEM para redes AMI, permitiendo lectura y gestión remota de medidores. Usa Boost.Asio (corrutinas), la librería Gurux DLMS (dependencia GPLv2; ver DEPENDENCIES.md) y conectores a bases de datos (Postgres). Diseñado para ejecutarse en Linux y Windows e integrarse con concentradores RF inhemeter.
se agregaran ,as opciones como dcu dlms sztar y medidores independendientes sin dcu
Características principales
- Implementación en C++20.
- Uso de corrutinas de Asio para operaciones asíncronas.
- Integración con Gurux DLMS para parsing/serialización DLMS.
- Soporte para almacenamiento en PostgreSQL (conector asíncrono).

Requisitos
- CMake >= 3.8
- Generador Ninja (se recomienda) o Visual Studio con soporte CMake
- Compilador con soporte C++20
- Librerías externas: Boost (Asio), Gurux DLMS (headers y biblioteca), libpq/libpqxx o conector asíncrono equivalente para PostgreSQL

Construcción (línea de comandos)

1. Generar y configurar el build con Ninja:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

2. Alternativamente abrir la carpeta con Visual Studio (CMake) y compilar desde el IDE.

Ejecución
- El ejecutable resultante estará en build/ (o la salida configurada por CMake). Ejecuta el binario con los permisos y parámetros adecuados.

Estructura del repositorio
- src/         - Código fuente principal
- include/     - Cabeceras externas/internas
- CMakeLists.txt
- README.md
- LICENSE
- DEPENDENCIES.md
- THIRD_PARTY_NOTICES.md
- GURUX_INSTALL.md

Contribuciones
- Abre incidencias (issues) para bugs o mejoras.
- Envía pull requests con cambios pequeños y revisions claras.

Licencia
- El código propio en este repositorio se publica bajo la licencia MIT (archivo LICENSE).

Dependencias importantes
- GuruxDLMS: GPLv2 — no está incluido. Debe instalarse externamente por el usuario; ver DEPENDENCIES.md y GURUX_INSTALL.md.
- Boost, libpq/libpqxx: licencias permisivas (ver DEPENDENCIES.md).

Enlaces rápidos
- DEPENDENCIES.md: Detalles de dependencias y licencias.
- THIRD_PARTY_NOTICES.md: Notas de terceros.
- GURUX_INSTALL.md: Guía básica de instalación de GuruxDLMS y dependencias comunes.

Contacto
- Usa el sistema de issues de GitHub para preguntas, incidencias o propuestas de mejora.
