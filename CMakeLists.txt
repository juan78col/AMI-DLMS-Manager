# CMakeList.txt: proyecto de CMake para DlmsServer2, incluya el origen y defina
# la lógica específica del proyecto aquí.
#
cmake_minimum_required (VERSION 3.8)

# Habilite Recarga activa para los compiladores de MSVC si se admiten.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("AMI_DLMS_Manager")
option(USE_GURUX "Use GuruxDLMS library" ON)
set(CMAKE_CXX_STANDARD 20)

set(POSTGRESQL_INCLUDE_DIR "/usr/pgsql-15/include")
set(POSTGRESQL_LIBRARY_DIR "/usr/pgsql-15/lib")
set(GURUX_INCLUDE_DIR "/usr/local/include")
set(GURUX_LIBRARY_DIR "/usr/local/lib")
set(STANDARD_DIRECTORIES "/usr/include" "/usr/local/include")

include_directories(${POSTGRESQL_INCLUDE_DIR})
link_directories(${POSTGRESQL_LIBRARY_DIR})
if(USE_GURUX)
  include_directories(${GURUX_INCLUDE_DIR})
  link_directories(${GURUX_LIBRARY_DIR})
endif()
include_directories(${STANDARD_DIRECTORIES})

#para ubuntu
set(POSTGRESQL_INCLUDE_DIR2 "/usr/include/postgresql")
set(POSTGRESQL_LIBRARY_DIR2 "/usr/lib/postgresql")
include_directories(${POSTGRESQL_INCLUDE_DIR2})
link_directories(${POSTGRESQL_LIBRARY_DIR2})


# Buscar Boost (sin componentes específicos)
find_package(Boost REQUIRED)

# Incluir headers estándar y Boost
include_directories(
  ${Boost_INCLUDE_DIRS}
  /usr/include
)
include_directories("/opt/rh/gcc-toolset-14/root/usr/include")
include_directories("/usr/include")

# Vincular bibliotecas estándar y Boost
link_directories(
  /usr/lib64
)


# Agregue un origen al ejecutable de este proyecto.
#file(GLOB_RECURSE SOURCES "*.cpp")
# add_executable (DlmsServer2 main.cpp)
#add_executable(DlmsServer2 ${SOURCES})
add_executable(AMI_DLMS_Manager
  main.cpp
  Server.cpp
  Session.cpp
  funciones.cpp
  Qgdw.cpp
  device_connector.cpp)
# Definir la macro _DEBUG_ solo en modo Debug
target_compile_definitions(AMI_DLMS_Manager PRIVATE
  $<$<CONFIG:Debug>:_DEBUG_>
)
target_compile_definitions(AMI_DLMS_Manager PRIVATE
  $<$<CONFIG:Debug>:_DEBUG_>
)
# Link common libraries. GuruxDLMS is optional and controlled by USE_GURUX.
target_link_libraries(AMI_DLMS_Manager pthread)
target_link_libraries(AMI_DLMS_Manager pq)
#message(STATUS "USE_GURUX=${USE_GURUX}")
if(USE_GURUX)
  target_link_libraries(AMI_DLMS_Manager gurux_dlms_cpp)
endif()
# Vincular Boost (si usas componentes específicos, agrégalos aquí)
target_link_libraries(AMI_DLMS_Manager ${Boost_LIBRARIES})

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET AMI_DLMS_Manager PROPERTY CXX_STANDARD 20)
  set_property(TARGET AMI_DLMS_Manager PROPERTY CXX_STANDARD_REQUIRED ON)
  set_property(TARGET AMI_DLMS_Manager PROPERTY CXX_EXTENSIONS OFF)  # Esto evita gnu++20
endif()

# Flags de optimización para producción
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -flto -DNDEBUG -Wall -Wextra")
endif()

# TODO: Agregue pruebas y destinos de instalación si es necesario.
# Buscar todos los .cpp en el proyecto opcion auto
#file(GLOB_RECURSE ALL_SOURCES "*.cpp")

# Filtrar archivos válidos (excluye los generados por CMake)
#set(PROJECT_SOURCES "")
#foreach(src ${ALL_SOURCES})
#  if(NOT src MATCHES "CMakeFiles/")
#    list(APPEND PROJECT_SOURCES ${src})
#  endif()
#endforeach()

# Crear el ejecutable con los archivos filtrados
#add_executable(DlmsServer2 ${PROJECT_SOURCES})