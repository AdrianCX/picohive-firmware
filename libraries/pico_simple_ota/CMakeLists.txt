add_library(pico_simple_ota INTERFACE)
target_sources(pico_simple_ota INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM}/upgrade_helper.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ota_container.cpp
)

target_link_libraries(pico_simple_ota INTERFACE pico_tls pico_logger pico_simple_util)
target_compile_definitions(pico_simple_ota INTERFACE PICO_OS_BOARD_NAME="${PICO_BOARD}")
target_include_directories(pico_simple_ota INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM} ${CMAKE_CURRENT_SOURCE_DIR} ${PICO_OS_SIGNING_INCLUDE_DIR})
