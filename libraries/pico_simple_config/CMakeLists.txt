add_library(pico_simple_config INTERFACE)

target_sources(pico_simple_config INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/app_config.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/config_file_crypt.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/config_store.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/config_json.cpp
)

target_include_directories(pico_simple_config INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(pico_simple_config INTERFACE pico_mjson pico_mbedtls pico_simple_util pico_fs pico_logger)

set(PICO_SIMPLE_CONFIG_COMMANDS ${CMAKE_CURRENT_SOURCE_DIR}/commands CACHE INTERNAL "pico_simple_config commands")
