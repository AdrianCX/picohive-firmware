add_library(pico_simple_mqtt INTERFACE)

target_sources(pico_simple_mqtt INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/mqtt_handler.cpp
)

target_include_directories(pico_simple_mqtt INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(pico_simple_mqtt INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(pico_simple_mqtt INTERFACE pico_tls pico_logger pico_simple_util)
