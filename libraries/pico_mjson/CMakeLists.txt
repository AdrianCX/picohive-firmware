add_library(pico_mjson INTERFACE)

target_sources(pico_mjson INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/pico_mjson.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/mjson/mjson.c
)

target_link_libraries(pico_mjson INTERFACE pico_logger)

target_include_directories(pico_mjson INTERFACE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/mjson)

target_compile_definitions(pico_mjson INTERFACE
  MJSON_ENABLE_RPC=0
  MJSON_ENABLE_BASE64=0
  MJSON_ENABLE_MERGE=0
  MJSON_ENABLE_PRETTY=0
)
