cmake_minimum_required(VERSION 3.12)

include(${CMAKE_CURRENT_LIST_DIR}/os/pico_os.cmake)

pico_os_pre_project()

project(@PROJECT_NAME@ C CXX ASM)

# OTA key versions in keys/versions/<n>, see os/docs/ota.md.
set(PICO_OS_KEY_VERSION 0)
set(PICO_OS_NEXT_KEY_VERSION 0)

pico_os_init()

set(APP_SOURCES
  src/main.cpp
  )

set(APP_LIBRARIES
  pico_common
  # pico_telegram
  # pico_calendar_commands
  )

add_executable(@PROJECT_NAME@ ${APP_SOURCES})

# The web page, from src/web/page.yaml (see os/web/README.md).
pico_os_web_page(@PROJECT_NAME@ ${CMAKE_CURRENT_SOURCE_DIR}/src/web/page.yaml)

pico_os_set_stack_size(@PROJECT_NAME@ 16384)

target_compile_definitions(@PROJECT_NAME@ PRIVATE
  PICO_STDIO_STACK_BUFFER_SIZE=1024
  PICO_MALLOC_PANIC=1
  )

target_include_directories(@PROJECT_NAME@ PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/config
  )

target_link_libraries(@PROJECT_NAME@ ${APP_LIBRARIES})

pico_commands_add(@PROJECT_NAME@
  ${CMAKE_CURRENT_SOURCE_DIR}/src/commands
  ${PICO_BUILTIN_COMMANDS}
  ${PICO_SIMPLE_CONFIG_COMMANDS}
  # ${PICO_TELEGRAM_COMMANDS}
  # ${PICO_CALENDAR_COMMANDS}
  )

pico_add_extra_outputs(@PROJECT_NAME@)

pico_enable_stdio_usb(@PROJECT_NAME@ 0)
pico_enable_stdio_uart(@PROJECT_NAME@ 0)
