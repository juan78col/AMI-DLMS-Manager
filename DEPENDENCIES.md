# Dependencias y licencias

Este proyecto requiere varias dependencias externas. A continuación se listan las dependencias conocidas, su licencia y notas sobre distribución.

- GuruxDLMS (gurux_dlms_cpp)
  - Licencia: GNU General Public License v2.0 (GPLv2)
  - Nota: GPLv2 es copyleft fuerte. No incluya el código ni los binarios de GuruxDLMS en este repositorio si pretende mantener licencia MIT; el usuario final debe instalar GuruxDLMS por su cuenta. Si enlaza estáticamente con GuruxDLMS, su binario final puede estar sujeto a GPLv2.

- Boost (Asio y otras partes)
  - Licencia: Boost Software License (permisiva)
  - Nota: Compatible con MIT; puede redistribuirse.

- PostgreSQL client (libpq / libpqxx)
  - Licencia: PostgreSQL License / BSD-style (permisiva)
  - Nota: Compatible con MIT; puede redistribuirse.

Recomendaciones de distribución
- Mantener la licencia MIT para el código propio del repositorio.
- No incluir en el repositorio código o binarios de GuruxDLMS (GPLv2). Documente claramente en DEPENDENCIES.md cómo instalar GuruxDLMS externamente.
- Añada un archivo THIRD_PARTY_NOTICES o NOTICE con referencias a las licencias y enlaces oficiales.

Instrucciones de instalación (ejemplo)

Linux (ejemplo para Ubuntu/RHEL):

1. Instalar Boost y herramientas de compilación:

```bash
sudo apt install build-essential cmake ninja-build libboost-all-dev libpq-dev
```

2. Instalar GuruxDLMS (instalar desde paquete oficial o compilar desde su repo):

   - Descargar y compilar GuruxDLMS según sus instrucciones oficiales.
   - Asegurarse de que los headers y la librería estén en /usr/local/include y /usr/local/lib o ajustar CMakeLists.txt.

Windows:

- Instalar Boost y librerías PostgreSQL (vía vcpkg o instaladores oficiales).
- Instalar GuruxDLMS siguiendo su guía para Windows.
