cmake_minimum_required(VERSION 3.24)

project(AMI_DLMS_Manager VERSION 1.0.0 LANGUAGES CXX)

if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
      "$<IF:$<CONFIG:Debug>,EditAndContinue,ProgramDatabase>")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(USE_GURUX     "Use GuruxDLMS library" ON)
option(ENABLE_LTO    "Enable LTO"            OFF)
option(ENABLE_NATIVE "Enable -march=native"  OFF)

# ---- Dependencias (portables) ----
find_package(Boost      CONFIG REQUIRED)
find_package(OpenSSL    REQUIRED COMPONENTS Crypto)
find_package(PostgreSQL REQUIRED)
find_package(Threads    REQUIRED)

if(USE_GURUX)
  find_path(GURUX_INCLUDE_DIR NAMES GXDLMSSecureClient.h GXDLMS.h
    HINTS /usr/local/include/guruxdlms /usr/include)
  find_library(GURUX_LIBRARY NAMES gurux_dlms_cpp
    HINTS /usr/local/lib /usr/lib /usr/lib64)
  if(NOT GURUX_LIBRARY)
    message(FATAL_ERROR "No se encontró gurux_dlms_cpp")
  endif()
endif()

# ---- Target ----
add_executable(AMI_DLMS_Manager
  src/main.cpp
  src/Server.cpp
  src/Session.cpp
  src/funcs.cpp
  src/Qgdw.cpp
  src/device_connector.cpp
  src/DlmsState.cpp
  src/CommandRegistry.cpp
  src/SecretManager.cpp
  src/dlms_profile_service.cpp
  src/writelog_adapter.cpp
  src/logger_init.cpp
  src/plugin_loader.cpp
 )

target_include_directories(AMI_DLMS_Manager PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_definitions(AMI_DLMS_Manager PRIVATE
  SPDLOG_USE_STD_FORMAT
  $<$<CONFIG:Debug>:DEBUG>
)

if(USE_GURUX)
  target_include_directories(AMI_DLMS_Manager PRIVATE ${GURUX_INCLUDE_DIR})
  target_link_libraries(AMI_DLMS_Manager PRIVATE ${GURUX_LIBRARY})
endif()

target_link_libraries(AMI_DLMS_Manager PRIVATE
  Boost::headers
  OpenSSL::Crypto
  PostgreSQL::PostgreSQL
  Threads::Threads
)

# ---- Warnings por compilador ----
if(MSVC)
  target_compile_options(AMI_DLMS_Manager PRIVATE /W4 /permissive-)
else()
  target_compile_options(AMI_DLMS_Manager PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow)
endif()

# ---- Optimizaciones opcionales ----
if(ENABLE_NATIVE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(AMI_DLMS_Manager PRIVATE -march=native)
endif()

if(ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _ipo_ok LANGUAGES CXX)
  if(_ipo_ok)
    set_property(TARGET AMI_DLMS_Manager
                 PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
endif()