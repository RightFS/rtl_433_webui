cmake_minimum_required(VERSION 3.16)
project(rtl433_webui VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Crow web framework
FetchContent_Declare(
  Crow
  GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
  GIT_TAG v1.2.0
)
FetchContent_MakeAvailable(Crow)

# nlohmann/json
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# Standalone Asio (required by Crow)
FetchContent_Declare(
  asio
  GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
  GIT_TAG asio-1-30-2
)
FetchContent_MakeAvailable(asio)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Frontend build
find_program(NPM_EXECUTABLE npm)
set(FRONTEND_DIR ${CMAKE_SOURCE_DIR}/frontend)
set(FRONTEND_DIST ${FRONTEND_DIR}/dist)

if(NPM_EXECUTABLE)
  add_custom_command(
    OUTPUT ${FRONTEND_DIST}/index.html
    COMMAND ${NPM_EXECUTABLE} install
    COMMAND ${NPM_EXECUTABLE} run build
    WORKING_DIRECTORY ${FRONTEND_DIR}
    COMMENT "Building frontend..."
    VERBATIM
  )

  add_custom_target(frontend_build ALL
    DEPENDS ${FRONTEND_DIST}/index.html
  )

  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/embedded_files.cpp
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/server/embed_generator.py
            ${FRONTEND_DIST} ${CMAKE_CURRENT_BINARY_DIR}/embedded_files.cpp
    DEPENDS frontend_build
    COMMENT "Embedding frontend files..."
    VERBATIM
  )
else()
  message(WARNING "npm not found - using placeholder embedded files")
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/embedded_files.cpp
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/server/embed_generator.py
            ${FRONTEND_DIST} ${CMAKE_CURRENT_BINARY_DIR}/embedded_files.cpp
    COMMENT "Generating embedded files..."
    VERBATIM
  )
endif()

add_subdirectory(server)
