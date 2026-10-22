add_library(pico_common INTERFACE)


target_sources(pico_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM}/common_functions.cpp)

target_include_directories(pico_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM} ${CMAKE_CURRENT_SOURCE_DIR} ${PICO_OS_VERSION_INCLUDE_DIR})

if (PICO_OS_PLATFORM STREQUAL "linux")
  # Everything an app needs to come up, serve its page, take OTA and run commands.
  target_link_libraries(pico_common INTERFACE
    pico_config
    pico_fs
    pico_simple_util
    pico_simple_config
    pico_commands
    pico_logger
    )
else()
  target_sources(pico_common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM}/wifi_settings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM}/dhcpserver/dhcpserver.c
    ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM}/dnsserver/dnsserver.c
  )

  # Everything an app needs to come up on Wi-Fi, serve its page, take OTA and run commands.
  target_link_libraries(pico_common INTERFACE
    pico_cyw43_arch_lwip_poll
    pico_lwip_mbedtls
    pico_mbedtls
    pico_stdlib
    pico_config
    pico_noexcept
    pico_fs
    pico_simple_util
    pico_simple_config
    pico_commands
    pico_logger
    )
endif()
