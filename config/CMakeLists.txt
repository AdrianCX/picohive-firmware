add_library(pico_config INTERFACE)
target_include_directories(pico_config INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(pico_config INTERFACE CYW43_CONFIG_FILE=<cyw43_os_config.h>)
