cmake_minimum_required(VERSION 3.16)
project(rtl433_webui VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ── Third-party: standalone Asio (required by Crow) ───────────────────────
# Must be fetched before Crow so that ASIO_INCLUDE_DIR is available when
# Crow's CMakeLists calls find_package(asio).
FetchContent_Declare(
  asio
  GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
  GIT_TAG asio-1-30-2
)
FetchContent_MakeAvailable(asio)
set(ASIO_INCLUDE_DIR ${asio_SOURCE_DIR}/asio/include CACHE PATH "Asio include directory" FORCE)

# ── Third-party: Crow (HTTP + WebSocket) ───────────────────────────────────
FetchContent_Declare(
  Crow
  GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
  GIT_TAG v1.2.0
)
FetchContent_MakeAvailable(Crow)

# ── Third-party: nlohmann/json ────────────────────────────────────────────
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# ── rtl_433 submodule – build the r_433 static library ────────────────────
# Override cache vars BEFORE add_subdirectory so rtl_433's CMakeLists picks
# them up.  AUTO means "optional – don't FATAL_ERROR if not found".
set(ENABLE_RTLSDR   "AUTO" CACHE STRING "Enable RTL-SDR support" FORCE)
set(ENABLE_SOAPYSDR "AUTO" CACHE STRING "Enable SoapySDR support" FORCE)
# We don't need OpenSSL inside rtl_433 for our use case.
set(ENABLE_OPENSSL  "OFF"  CACHE STRING "Enable OpenSSL" FORCE)

add_subdirectory(rtl_433 rtl_433_build EXCLUDE_FROM_ALL)
# 'r_433' target is now available.  The rtl_433 executable is excluded.

# ── Frontend build ────────────────────────────────────────────────────────
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_program(NPM_EXECUTABLE npm)

set(FRONTEND_DIR  ${CMAKE_SOURCE_DIR}/frontend)
set(FRONTEND_DIST ${FRONTEND_DIR}/dist)

if(NPM_EXECUTABLE)
  add_custom_command(
    OUTPUT  ${FRONTEND_DIST}/index.html
    COMMAND ${NPM_EXECUTABLE} install
    COMMAND ${NPM_EXECUTABLE} run build
    WORKING_DIRECTORY ${FRONTEND_DIR}
    COMMENT "Building frontend..."
    VERBATIM
  )
  add_custom_target(frontend_build ALL DEPENDS ${FRONTEND_DIST}/index.html)
else()
  message(WARNING "npm not found – using pre-built frontend dist if present")
  add_custom_target(frontend_build)
endif()

add_custom_command(
  OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/embedded_files.cpp
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/server/embed_generator.py
          ${FRONTEND_DIST} ${CMAKE_CURRENT_BINARY_DIR}/embedded_files.cpp
  DEPENDS frontend_build
  COMMENT "Embedding frontend files..."
  VERBATIM
)

add_subdirectory(server)
