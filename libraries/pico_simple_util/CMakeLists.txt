add_library(pico_simple_util INTERFACE)

target_sources(pico_simple_util INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/buffer_chain.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/buffer_chain.h
  ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM}/memory_stats.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/memory_stats.h
  ${CMAKE_CURRENT_SOURCE_DIR}/pico_helper.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/pico_helper.h
  ${CMAKE_CURRENT_SOURCE_DIR}/reply_buffer.h
  ${CMAKE_CURRENT_SOURCE_DIR}/shared_buffer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/shared_buffer.h
)

target_include_directories(pico_simple_util INTERFACE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM})


if (PICO_OS_PLATFORM STREQUAL "linux")
  find_package(Threads REQUIRED)
  target_sources(pico_simple_util INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM}/os_platform.cpp ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM}/os_loop.cpp)
  target_link_libraries(pico_simple_util INTERFACE pico_config pico_logger Threads::Threads)
else()
  target_link_libraries(pico_simple_util INTERFACE pico_stdlib pico_rand pico_unique_id hardware_gpio hardware_pwm hardware_clocks pico_config)
endif()
