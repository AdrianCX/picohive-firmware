add_library(pico_ical INTERFACE)

target_sources(pico_ical INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/ical_parser.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ical_parser.h
  ${CMAKE_CURRENT_SOURCE_DIR}/calendar_util.cpp
)

target_include_directories(pico_ical INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

