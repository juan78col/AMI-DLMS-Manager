# Instalación de GuruxDLMS (guía básica)

Estas instrucciones son orientativas; consulta la documentación oficial de Gurux para versiones y pasos exactos.

Linux (ejemplo - Ubuntu/Debian):

1. Instala dependencias básicas:

```bash
sudo apt update
sudo apt install build-essential cmake git libssl-dev
```

2. Clona el repositorio de Gurux y compila (ruta y comandos pueden variar según la versión):

```bash
git clone https://github.com/gurux/gurux.dlms.cpp.git
cd gurux.dlms.cpp
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
sudo cmake --install .
```

3. Verifica que los headers estén en /usr/local/include y las librerías en /usr/local/lib. Ajusta CMakeLists.txt del proyecto si usas rutas distintas.

Windows (resumen):

- Instala Visual Studio con soporte C++ y CMake.
- Clona y compila GuruxDLMS con CMake desde la línea de comandos o el GUI de CMake.
- Copia los headers y las librerías a una ruta conocida y añade las rutas a CMakeLists.txt o configura variables de entorno.

Notas
- GuruxDLMS está bajo GPLv2. Si enlazas estáticamente, tu binario final podría requerir cumplir GPLv2.
- Para evitar problemas de licencia, no incluyas el código de GuruxDLMS en este repositorio.

Instalación de dependencias comunes
----------------------------------

Ubuntu / Debian (Boost, libpq):

```bash
sudo apt update
sudo apt install -y build-essential cmake git libssl-dev libboost-all-dev libpq-dev
```

Notas:
- libpq-dev proporciona las cabeceras y la librería cliente de PostgreSQL (libpq).
- libboost-all-dev instala Boost (incluye Asio). Si prefieres paquetes más pequeños, instala solo los componentes que necesites.

Windows (vcpkg recomendada):

1. Instala vcpkg (si no lo tienes):

```powershell
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
```

2. Instala paquetes necesarios (ejemplo):

```powershell
.\vcpkg install boost-asio:x64-windows
.\vcpkg install boost:x64-windows
.\vcpkg install libpq:x64-windows
```

3. Integra vcpkg con CMake/Visual Studio (opcional):

```powershell
.\vcpkg integrate install
```

MacOS (Homebrew):

```bash
brew update
brew install boost postgresql openssl
```

Consejos para CMake
-------------------
- Si instalas librerías con vcpkg, pasa la toolchain a CMake: -DCMAKE_TOOLCHAIN_FILE=path\to\vcpkg\scripts\buildsystems\vcpkg.cmake
- Ajusta las variables en CMakeLists.txt (POSTGRESQL_INCLUDE_DIR, GURUX_INCLUDE_DIR, etc.) si las librerías se instalan en rutas no estándar.

