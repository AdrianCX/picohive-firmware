add_library(pico_epaper INTERFACE)

target_sources(pico_epaper INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/epaper_handler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/screen_state_machine.cpp
  )

target_include_directories(pico_epaper INTERFACE ${PICO_OS_PATH}/libraries/hardware/pico/pico_epaper)

target_link_libraries(pico_epaper INTERFACE pico_simple_util pico_logger)
