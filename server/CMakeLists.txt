# Collect SDR libraries propagated by rtl_433's CMakeLists
get_target_property(R433_LINK_LIBS r_433 LINK_LIBRARIES)

add_executable(rtl433_webui
  main.cpp
  server.cpp
  rtl433_manager.cpp
  rtl433_glue.c
  ${CMAKE_CURRENT_BINARY_DIR}/../embedded_files.cpp
)

target_include_directories(rtl433_webui PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${asio_SOURCE_DIR}/asio/include
  ${crow_SOURCE_DIR}/include
  # rtl_433 public and internal headers
  ${CMAKE_SOURCE_DIR}/rtl_433/include
  ${CMAKE_SOURCE_DIR}/rtl_433/src
)

target_compile_definitions(rtl433_webui PRIVATE ASIO_STANDALONE _GNU_SOURCE)

target_link_libraries(rtl433_webui PRIVATE
  r_433
  Crow::Crow
  nlohmann_json::nlohmann_json
  pthread
  m
)

install(TARGETS rtl433_webui RUNTIME DESTINATION bin)
