add_library(pico_calendar_commands INTERFACE)

target_sources(pico_calendar_commands INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/calendar_commands.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/calendar_schedule.cpp
)

target_link_libraries(pico_calendar_commands INTERFACE pico_logger pico_http pico_ical pico_commands pico_simple_config pico_mjson pico_simple_util)
target_include_directories(pico_calendar_commands INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

set(PICO_CALENDAR_COMMANDS ${CMAKE_CURRENT_SOURCE_DIR}/commands CACHE INTERNAL "pico_calendar_commands' commands")
