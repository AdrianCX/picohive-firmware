#
# Linux mocks used for valgrind testing/gdb and trying out web features
# Mostly generated, do not use in production
# 
set(PICO_OS_BOARD_ONLY
  pico_audio_events pico_arducam_mega pico_noexcept
  pico_stdlib pico_multicore pico_cyw43_arch_lwip_poll
  pico_cyw43_arch_none pico_lwip pico_lwip_mbedtls pico_rand pico_unique_id pico_time)

# No point in using the same 16KB stack as on pico
if (NOT DEFINED PICO_OS_LINUX_STACK_SIZE)
    set(PICO_OS_LINUX_STACK_SIZE 262144)
endif()

function(pico_os_set_stack_size TARGET BYTES)
    target_compile_definitions(${TARGET} PRIVATE PICO_STACK_SIZE=${BYTES})
endfunction()

# printf is fine on a PC; there is no USB or UART console.
function(pico_os_check_no_stdio TARGET)
endfunction()

function(pico_enable_stdio_usb TARGET ENABLED)
endfunction()

function(pico_enable_stdio_uart TARGET ENABLED)
endfunction()

# An executable linking a board only library or hardware_* part stops the configure.
function(pico_os_check_executables DIRECTORY)
    get_property(targets DIRECTORY "${DIRECTORY}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        get_target_property(type ${target} TYPE)
        if (NOT type STREQUAL "EXECUTABLE")
            continue()
        endif()
        get_target_property(libraries ${target} LINK_LIBRARIES)
        foreach(library IN LISTS libraries)
            if ((library IN_LIST PICO_OS_BOARD_ONLY) OR ((library MATCHES "^hardware_") AND NOT TARGET ${library}))
                message(FATAL_ERROR "${target} links ${library}, which only exists on the board; "
                                    "linux builds take the shared libraries only (docs/linux.md)")
            endif()
        endforeach()
    endforeach()
endfunction()

macro(pico_os_pre_project)
    if (NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    endif()
endmacro()

macro(pico_os_init)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 20)

    set(CMAKE_C_FLAGS_DEBUG "-O0 -g3")
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3")
    set(CMAKE_C_FLAGS_RELEASE "-O2 -g")
    set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g")

    if (NOT DEFINED PICO_OS_WERROR)
        set(PICO_OS_WERROR ON)
    endif()

    # The board's warnings. No exceptions and no RTTI, as on the board.
    add_compile_options(
      -Wall
      -Wno-psabi
      -Wno-unused-result
      -Wno-format
      -Wno-unused-function
      -Wno-maybe-uninitialized
      $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
      $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    )

    if (PICO_OS_WERROR)
        add_compile_options(-Werror)
    endif()

    if (PICO_OS_SANITIZE)
        add_compile_options(-fsanitize=${PICO_OS_SANITIZE} -fno-omit-frame-pointer)
        add_link_options(-fsanitize=${PICO_OS_SANITIZE})
    endif()

    # A build id, logged at start and carried in OTA packages; -rdynamic so crash reports on stderr name the functions.
    add_link_options(-Wl,--build-id=sha1 -rdynamic)

    # The board's mbedtls configuration, see config/linux_mbedtls_config.h.
    add_compile_definitions(
      PICO_OS_LINUX=1
      PICO_OS_LINUX_STACK_SIZE=${PICO_OS_LINUX_STACK_SIZE}
      PICO_BOARD="${PICO_BOARD}"
      MBEDTLS_CONFIG_FILE="linux_mbedtls_config.h"
    )

    cmake_language(DEFER CALL pico_os_check_executables "${CMAKE_CURRENT_SOURCE_DIR}")

    include("${PICO_OS_PATH}/signing/signing.cmake")
    pico_os_signing_init()

    pico_os_version_header()

    add_subdirectory("${PICO_OS_PATH}/config"                    "${CMAKE_BINARY_DIR}/os/config")
    add_subdirectory("${PICO_OS_PATH}/platform/linux/mbedtls"      "${CMAKE_BINARY_DIR}/os/mbedtls")
    add_subdirectory("${PICO_OS_PATH}/libraries"                 "${CMAKE_BINARY_DIR}/os/libraries")
endmacro()
