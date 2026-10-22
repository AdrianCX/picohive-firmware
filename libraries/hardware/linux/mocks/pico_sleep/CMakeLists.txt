add_library(pico_sleep INTERFACE)

target_sources(pico_sleep INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/pico_sleep.c
  )

# The board library's header: pico_sleep.h.
target_include_directories(pico_sleep INTERFACE ${PICO_OS_PATH}/libraries/hardware/pico/pico_sleep)

target_link_libraries(pico_sleep INTERFACE pico_logger)
