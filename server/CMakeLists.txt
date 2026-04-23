add_executable(rtl433_webui
  main.cpp
  server.cpp
  rtl433_manager.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/../embedded_files.cpp
)

target_include_directories(rtl433_webui PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${asio_SOURCE_DIR}/asio/include
  ${crow_SOURCE_DIR}/include
)

target_link_libraries(rtl433_webui PRIVATE
  Crow::Crow
  nlohmann_json::nlohmann_json
  pthread
)

target_compile_definitions(rtl433_webui PRIVATE
  ASIO_STANDALONE
)

install(TARGETS rtl433_webui RUNTIME DESTINATION bin)
