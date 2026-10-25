add_library(pico_st3215 INTERFACE)

target_sources(pico_st3215 INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/st3215_bus.cpp
  ${PICO_OS_PATH}/libraries/hardware/pico/pico_st3215/st3215.cpp
  ${PICO_OS_PATH}/libraries/hardware/pico/pico_st3215/st3215_protocol.cpp
  )

target_include_directories(pico_st3215 INTERFACE ${PICO_OS_PATH}/libraries/hardware/pico/pico_st3215)

target_link_libraries(pico_st3215 INTERFACE pico_simple_util pico_logger)
