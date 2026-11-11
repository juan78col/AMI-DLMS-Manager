
# AMI-DLMS-Manager

**Idiomas / Languages:** Español | [English](#english)

Plataforma abierta para automatizar la gestión de sistemas de medición avanzada. El servicio conecta medidores y concentradores (DCU), identifica dispositivos y ejecuta operaciones DLMS/COSEM desde una configuración centralizada.

Ayuda a reducir tareas manuales y ajustes repetidos: los perfiles DLMS reúnen los parámetros de comunicación por tipo de equipo, mientras que los plugins permiten incorporar mecanismos de identificación de distintos fabricantes. Requiere una configuración inicial de base de datos, perfiles, credenciales y dispositivos; el objetivo es evitar configuraciones especiales dispersas por cada flujo, no eliminar la puesta en marcha.

Implementada en C++20 con Boost.Asio, Gurux DLMS y PostgreSQL. Está pensada para crecer hacia distintos equipos mediante perfiles y adaptadores de fabricante.

![CI](https://github.com/juan78col/AMI-DLMS-Manager/actions/workflows/build.yml/badge.svg)

> **Enfoque actual:** medidores directos y DCU Inhemeter. El código contiene además una ruta inicial para DCU ZStar/IDIS por Wrapper, pero esa ruta es experimental y no debe considerarse interoperable o validada en campo. La inclusión de una ruta en el código no equivale a una prueba con equipo real.

## Estado y alcance funcional

| Ruta | Puerto TCP local | Transporte / flujo | Estado documentado |
|---|---:|---|---|
| Medidor directo | 4059 | DLMS/COSEM según el perfil; HDLC o Wrapper | Enfoque actual; validar con perfiles, credenciales y modelos concretos. |
| DCU Inhemeter | 8877 | Trama externa Inhemeter, desempaquetado y diálogo DLMS con el DCU o medidores detrás de él | Enfoque actual; requiere sus plugins/configuración externos cuando se usan heartbeats. |
| DCU ZStar/IDIS | 8880 | DLMS Wrapper separado del framing Inhemeter; perfil y secretos propios del DCU | Experimental: conexión y asociación necesitan validación con equipo y base reales. |

Los puertos indicados son los listeners configurados por la aplicación; deben permitirse en el firewall y coincidir con la configuración de los equipos. El soporte real depende del modelo, firmware, perfil DLMS, esquema de base de datos y plugin disponibles.

### Flujo general

1. El servidor acepta una conexión en el listener correspondiente y crea una sesión.
2. Si el primer tráfico es un heartbeat, el cargador busca un plugin compatible por tipo de dispositivo y bytes de identificación. El plugin extrae el identificador y puede producir un ACK.
3. Se resuelven datos del dispositivo y el perfil DLMS desde PostgreSQL. Los perfiles generales se cargan en caché al iniciar; se pueden recargar con `SIGHUP`.
4. La sesión inicializa el cliente Gurux. HDLC comienza con SNRM/UA y Wrapper inicia con AARQ/AARE; la máquina de estados procesa las respuestas y los comandos.
5. En Inhemeter, el adaptador de trama exterior decodifica/encapsula el contenido DLMS. Esta capa de transporte es distinta de la conversación DLMS.

Los perfiles parametrizan direcciones, interfaz, autenticación, seguridad, suite, conformance, PDU y parámetros HDLC. No se debe asumir que dispositivos de un mismo fabricante comparten todos esos valores.

## Credenciales y seguridad

- El proceso requiere `AMI_MASTER_KEY_HEX`: una clave maestra de 32 bytes expresada en exactamente 64 caracteres hexadecimales. Se usa con AES-256-GCM para cifrar/descifrar secretos; no guardes la clave en Git, logs, imágenes ni archivos de configuración públicos.
- PostgreSQL requiere `PG_HOST`, `PG_PORT`, `PG_DBNAME`, `PG_USER`, `PG_PASSWORD` y `PG_APPLICATION_NAME`.
- Los secretos de una entidad se asocian a su identidad mediante AAD. Para la ruta de perfil que aplica `ApplyProfileToParser`, el formato es `ami-secret-v1|<serial>|<campo>`; los campos usados son `systemTitle`, `cipherKey`, `authKey` y `dedicateKey`.
- Las credenciales de asociación servidor–DCU son distintas de las claves de los medidores que el DCU administra. ZStar debe resolver secretos propios del DCU; no reutilizar claves de sus medidores para autenticar el canal al concentrador.
- Protege la base, los respaldos y los canales de administración. Rotar la clave maestra requiere un procedimiento de recifrado coordinado; perderla impide recuperar los secretos existentes.

La base de datos debe contar con las tablas, funciones y perfiles requeridos por la configuración de dispositivos y credenciales. Prepara y aplica el esquema correspondiente antes de iniciar el servicio; la compilación no crea ni modifica objetos de PostgreSQL.

## Sistema de plugins

El plugin es un adaptador de identificación de heartbeat, no una implementación de DLMS ni una garantía automática de compatibilidad con el fabricante. El contrato C++20 está en `include/plugin_api.h`: cada plugin declara un nombre, los tipos de dispositivo que admite (`0` DCU, `1` medidor directo), sus firmas (`magic_bytes`) y cómo interpretar el heartbeat. Puede devolver un ID de dispositivo y un payload de ACK.

Al inicio, `PluginLoader` busca bibliotecas dinámicas en `./plugins` (`.dll` en Windows, `.so` en Linux), carga los símbolos `create_plugin`/`destroy_plugin` y ordena las firmas para probar primero las más específicas. La carpeta se crea/resuelve respecto al directorio de trabajo del proceso. Un plugin nuevo debe compilarse con una ABI y toolchain compatibles con el ejecutable y desplegarse en ese directorio.

El ejecutable principal no compila ni instala automáticamente las bibliotecas de plugins. Despliega las bibliotecas compatibles (`.dll` en Windows, `.so` en Linux) dentro de `./plugins`, relativa al directorio de trabajo del proceso. El empaquetado de cada instalación debe compilar los plugins para la ABI y plataforma del ejecutable y colocarlos en esa ruta.

Para añadir soporte de fabricante de forma mantenible:

1. Implementa un plugin de identificación para el heartbeat y prueba firmas parciales, fragmentadas y ambiguas.
2. Si el fabricante encapsula DLMS, implementa/adapta la capa de framing y extracción/encapsulado, sin alterar el estado DLMS común si no es necesario.
3. Reutiliza el perfil DLMS estándar cuando la asociación sea realmente compatible; parametriza direcciones, interfaz, autenticación, suite y llaves por equipo.
4. Añade pruebas con capturas anonimizadas y, antes de declarar soporte, pruebas de asociación, comandos, errores y reconexión con el modelo/firmware correspondiente.

## Requisitos y compilación

Requisitos detectados por el CMake del proyecto:

- CMake 3.24 o superior.
- Compilador C++20 y generador compatible (Ninja o Visual Studio con soporte CMake).
- Boost, OpenSSL, PostgreSQL client libraries y Threads.
- Gurux DLMS C++ (headers y biblioteca `gurux_dlms_cpp`) si `USE_GURUX` está habilitado (por defecto).

Ejemplo con Ninja:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

En Visual Studio se puede abrir la carpeta como proyecto CMake y configurar las rutas de dependencias según la instalación. La detección de Gurux usa `GURUX_INCLUDE_DIR` y `GURUX_LIBRARY`; configura esas variables si la instalación no está en rutas estándar.

La compilación del ejecutable no aplica migraciones de PostgreSQL, no crea perfiles ni genera/instala plugins. Configura esos componentes antes de iniciar el servicio. El proceso carga la clave maestra, carga plugins desde `./plugins`, inicializa el pool PostgreSQL y carga los perfiles DLMS. En Linux atiende señales: `SIGHUP` recarga perfiles y `SIGINT`/`SIGTERM` detienen el servicio.

## Qué falta para ampliar el soporte DCU

La base de DLMS/COSEM puede reutilizarse cuando un DCU de otro fabricante implemente las mismas asociaciones y servicios estándar. Lo que suele variar y debe confirmarse con documentación/trazas del equipo es el transporte, framing exterior, heartbeat/identificación, direccionamiento, seguridad y operaciones de administración del concentrador.

Para ZStar/IDIS, el código actual tiene listener Wrapper independiente, resolución separada del perfil/secretos del DCU y una ruta de asociación con bootstrap/Invocation Counter para el caso seguro. Sin embargo, no está validado extremo a extremo con un DCU y requiere que la base de datos tenga configuradas las funciones y los campos correspondientes a los secretos propios del concentrador. Tampoco se ha portado aquí el flujo antiguo `WriteListMeters()` que registra la lista de medidores y sus claves dentro del objeto DLMS del DCU. Por ello, ZStar no forma parte del alcance actualmente verificado hasta completar las pruebas y ese flujo, si el producto lo requiere.

Próximas mejoras recomendadas:

- Proveer de forma versionada y segura el esquema/migraciones requeridos para perfiles, dispositivos y secretos.
- Automatizar la compilación, empaquetado y despliegue de plugins compatibles por plataforma.
- Añadir pruebas automatizadas de parsing de heartbeat/framing y pruebas de integración de las máquinas DLMS con respuestas simuladas.
- Validar equipos y firmwares reales: asociación, seguridad/HLS, lectura, perfiles, comandos, timeouts, fragmentación de tramas y reconexión.
- Implementar adaptadores por fabricante solo en las diferencias de transporte/operación, manteniendo el core DLMS compartido donde corresponda.
- Completar y validar el registro de medidores en DCU (por ejemplo, portar `WriteListMeters()` de forma tipada y con pruebas), sin mezclar las claves del DCU con las de los medidores.

## Licencia y dependencias

El código propio se ofrece bajo MIT según `LICENSE`. Esa licencia no cambia la licencia de las dependencias ni concede derechos sobre código, marcas, protocolos o documentación de terceros. Gurux DLMS C++ se identifica en este proyecto como GPLv2 y se instala externamente; revisa `DEPENDENCIES.md`, `THIRD_PARTY_NOTICES.md` y `GURUX_INSTALL.md`, y analiza las obligaciones aplicables antes de distribuir un binario enlazado con Gurux. Las versiones/licencias deben verificarse para cada distribución.

## Contribuir

Abre un issue para describir el equipo, firmware, puerto/protocolo, perfil y comportamiento observado. Antes de adjuntar trazas, elimina seriales, direcciones IP, tokens y cualquier secreto. Las contribuciones deberían incluir pruebas reproducibles y separar cambios genéricos DLMS de particularidades de fabricante.

---

# English

An open platform for automating the management of advanced metering systems. The service connects meters and data concentrator units (DCUs), identifies devices, and runs DLMS/COSEM operations from a centralized configuration.

It is designed to reduce manual work and repeated setup: DLMS profiles group communication parameters by device type, while plugins provide a way to add identification methods for different vendors. Initial setup of the database, profiles, credentials, and devices is still required; the goal is to avoid scattered, one-off configuration across communication flows, not to eliminate commissioning.

Built with C++20, Boost.Asio, Gurux DLMS, and PostgreSQL, the platform is intended to grow across device types through profiles and vendor adapters.

> **Current focus:** direct meters and Inhemeter DCUs. The code also contains an initial ZStar/IDIS DCU path over Wrapper, but that path is experimental and must not be considered interoperable or field-validated. Code presence does not constitute successful testing with real equipment.

## Status and functional scope

| Path | Local TCP port | Transport / flow | Documented status |
|---|---:|---|---|
| Direct meter | 4059 | DLMS/COSEM according to the profile; HDLC or Wrapper | Current focus; validate with the specific profiles, credentials, and models in use. |
| Inhemeter DCU | 8877 | Inhemeter outer framing, decapsulation, and DLMS dialogue with the DCU or meters behind it | Current focus; external plugins/configuration are required when heartbeats are used. |
| ZStar/IDIS DCU | 8880 | DLMS Wrapper separate from Inhemeter framing; DCU-specific profile and secrets | Experimental: connection and association still require validation against real equipment and a real database. |

These are the listener ports configured by the application. Permit them through the firewall and configure the devices accordingly. Actual support depends on the model, firmware, DLMS profile, database schema, and available plugin.

### High-level flow

1. The server accepts a connection on the corresponding listener and creates a session.
2. If the first payload is a heartbeat, the loader looks for a plugin matching the device type and identification bytes. The plugin extracts the identifier and may produce an ACK.
3. Device data and the DLMS profile are resolved from PostgreSQL. General profiles are cached at startup and can be reloaded with `SIGHUP`.
4. The session initializes the Gurux client. HDLC starts with SNRM/UA; Wrapper starts with AARQ/AARE. A state machine processes responses and commands.
5. For Inhemeter, the outer-frame adapter decodes/encodes the DLMS payload. This transport layer is separate from the DLMS conversation.

Profiles configure addressing, interface, authentication, security, suite, conformance, PDU size, and HDLC parameters. Do not assume devices from the same vendor share all settings.

## Credentials and security

- The process requires `AMI_MASTER_KEY_HEX`: a 32-byte master key represented by exactly 64 hexadecimal characters. It is used with AES-256-GCM to encrypt/decrypt secrets. Never commit it or place it in logs, images, or public configuration files.
- PostgreSQL requires `PG_HOST`, `PG_PORT`, `PG_DBNAME`, `PG_USER`, `PG_PASSWORD`, and `PG_APPLICATION_NAME`.
- Entity secrets are bound to their identity using AAD. For the profile path in `ApplyProfileToParser`, the format is `ami-secret-v1|<serial>|<field>`; the fields used are `systemTitle`, `cipherKey`, `authKey`, and `dedicateKey`.
- Server-to-DCU association credentials are distinct from the credentials of meters managed by that DCU. ZStar must resolve its own DCU secrets; do not reuse meter keys to authenticate the server-to-concentrator channel.
- Protect the database, backups, and administration channels. Master-key rotation requires a coordinated re-encryption procedure; losing the key makes existing secrets unrecoverable.

The database must have the tables, functions, and profiles required by device and credential configuration. Prepare and apply the corresponding schema before starting the service; building the executable does not create or modify PostgreSQL objects.

## Plugin system

A plugin is a heartbeat-identification adapter, not a DLMS implementation or an automatic guarantee of vendor compatibility. The C++20 contract is in `include/plugin_api.h`: each plugin declares a name, supported device types (`0` DCU, `1` direct meter), signatures (`magic_bytes`), and heartbeat parsing logic. It can return a device ID and an ACK payload.

At startup, `PluginLoader` searches for dynamic libraries in `./plugins` (`.dll` on Windows, `.so` on Linux), loads the `create_plugin`/`destroy_plugin` symbols, and checks more specific signatures first. The directory is resolved relative to the process working directory. A plugin must be built with an ABI and toolchain compatible with the executable and deployed in that directory.

The main executable does not automatically build or install plugin libraries. Deploy compatible libraries (`.dll` on Windows, `.so` on Linux) in `./plugins`, relative to the process working directory. Each installation package should build plugins for the executable's platform and ABI and place them in that location.

To add vendor support in a maintainable way:

1. Implement a heartbeat-identification plugin and test partial, fragmented, and ambiguous signatures.
2. If the vendor encapsulates DLMS, implement/adapt its framing and extraction/encapsulation layer without changing the shared DLMS state machine unless necessary.
3. Reuse the standard DLMS profile only when the association is genuinely compatible; configure addressing, interface, authentication, suite, and keys per device.
4. Add tests using anonymized captures and, before claiming support, test association, commands, error handling, and reconnection with the relevant model/firmware.

## Requirements and build

Requirements identified by the project CMake configuration:

- CMake 3.24 or newer.
- A C++20 compiler and a supported generator (Ninja or Visual Studio with CMake support).
- Boost, OpenSSL, PostgreSQL client libraries, and Threads.
- Gurux DLMS C++ headers and `gurux_dlms_cpp` library when `USE_GURUX` is enabled (the default).

Ninja example:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

With Visual Studio, open the folder as a CMake project and configure dependency paths as needed. Gurux discovery uses `GURUX_INCLUDE_DIR` and `GURUX_LIBRARY`; set these variables if the installation is not in a standard location.

Building the executable does not apply PostgreSQL migrations, create profiles, or build/install plugins. Provision these components before starting the service. The process loads the master key and plugins from `./plugins`, initializes the PostgreSQL pool, and loads DLMS profiles. On Linux, `SIGHUP` reloads profiles and `SIGINT`/`SIGTERM` stop the service.

## Extending DCU support

The shared DLMS/COSEM layer may be reusable when another vendor's DCU implements the same associations and standard services. Transport, outer framing, heartbeat/identification, addressing, security, and concentrator-management operations often differ and must be confirmed using device documentation and traces.

For ZStar/IDIS, the current code has an independent Wrapper listener, separate DCU profile/secret resolution, and an association path with bootstrap/Invocation Counter for secure profiles. However, it has not been validated end-to-end with a DCU and requires the database to provide the corresponding functions and fields for concentrator-specific secrets. The legacy `WriteListMeters()` flow, which registers the list of meters and their keys inside the DCU's DLMS object, has not been ported here. ZStar is therefore outside the currently verified scope until testing and that flow (if required by the product) are complete.

Recommended next steps:

- Provide versioned and secure database schemas/migrations for profiles, devices, and secrets.
- Automate the build, packaging, and deployment of platform-compatible plugins.
- Add automated heartbeat/framing parser tests and integration tests for DLMS state machines using simulated responses.
- Validate real equipment and firmware: association, security/HLS, reads, profiles, commands, timeouts, frame fragmentation, and reconnection.
- Implement vendor adapters only for transport/operation differences, reusing the shared DLMS core where appropriate.
- Complete and validate DCU meter registration (for example, port `WriteListMeters()` with typed structures and tests) without mixing DCU and meter credentials.

## License and dependencies

Original project code is offered under MIT as stated in `LICENSE`. That license does not change dependency licenses or grant rights to third-party code, trademarks, protocols, or documentation. This project identifies Gurux DLMS C++ as GPLv2 and installs it externally. Review `DEPENDENCIES.md`, `THIRD_PARTY_NOTICES.md`, and `GURUX_INSTALL.md`, and assess applicable obligations before distributing a binary linked with Gurux. Verify dependency versions and licenses for every release.

## Contributing

Open an issue describing the device, firmware, port/protocol, profile, and observed behavior. Before attaching traces, remove serial numbers, IP addresses, tokens, and all secrets. Contributions should include reproducible tests and keep generic DLMS changes separate from vendor-specific behavior.

