add_library(pico_telegram INTERFACE)

target_sources(pico_telegram INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/telegram_manager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/telegram_updates.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/telegram_commands.cpp
)

target_link_libraries(pico_telegram INTERFACE pico_logger pico_mjson pico_http pico_commands pico_simple_ota pico_simple_config)
target_include_directories(pico_telegram INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

set(PICO_TELEGRAM_COMMANDS ${CMAKE_CURRENT_SOURCE_DIR}/commands CACHE INTERNAL "pico_telegram's commands")
