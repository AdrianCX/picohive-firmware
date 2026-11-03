add_library(pico_epaper INTERFACE)

target_sources(pico_epaper INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/screen_state_machine_color.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/screen_state_machine_rb.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/screen_state_machine.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/DEV_Config.c
  ${CMAKE_CURRENT_SOURCE_DIR}/epaper_handler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/font16.c
  )

target_include_directories(pico_epaper INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
