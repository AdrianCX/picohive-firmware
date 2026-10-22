#
# Linux mocks used for valgrind testing/gdb and trying out web features
# Mostly generated, do not use in production
# 
# Use pico-sdk mbedtls with the board configuration (config/linux_mbedtls_config.h)
#
set(PICO_OS_MBEDTLS_PATH "${PICO_OS_PATH}/submodules/pico-sdk/lib/mbedtls")

file(GLOB PICO_OS_MBEDTLS_SOURCES "${PICO_OS_MBEDTLS_PATH}/library/*.c")
if (NOT PICO_OS_MBEDTLS_SOURCES)
    message(FATAL_ERROR "No mbedtls in ${PICO_OS_MBEDTLS_PATH}: git submodule update --init --recursive")
endif()

add_library(pico_os_mbedtls STATIC ${PICO_OS_MBEDTLS_SOURCES})

target_include_directories(pico_os_mbedtls PUBLIC
  ${PICO_OS_MBEDTLS_PATH}/include
  ${PICO_OS_PATH}/config
  ${PICO_OS_PATH}/libraries/pico_logger
)

if (EXISTS "${CMAKE_SOURCE_DIR}/src/config")
  target_include_directories(pico_os_mbedtls PUBLIC ${CMAKE_SOURCE_DIR}/src/config)
endif()

# Third party code: its warnings are not ours.
target_compile_options(pico_os_mbedtls PRIVATE -w)

add_library(pico_mbedtls INTERFACE)
target_link_libraries(pico_mbedtls INTERFACE pico_os_mbedtls)
