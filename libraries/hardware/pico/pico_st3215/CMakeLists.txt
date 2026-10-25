add_library(pico_st3215 INTERFACE)

target_sources(pico_st3215 INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/st3215.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/st3215_bus.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/st3215_protocol.cpp
)

target_link_libraries(pico_st3215 INTERFACE pico_stdlib pico_logger pico_simple_util hardware_uart)
target_include_directories(pico_st3215 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
