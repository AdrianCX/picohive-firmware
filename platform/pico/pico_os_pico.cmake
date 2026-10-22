#
# Re-order stacks around so we can have a larger stack without issues in linker scripts
# - https://petewarden.com/2024/01/16/understanding-the-raspberry-pi-picos-memory-layout/
#
function(pico_os_set_stack_size TARGET BYTES)
    if (NOT BYTES GREATER 0)
        message(FATAL_ERROR "pico_os_set_stack_size(${TARGET}): ${BYTES} is not a stack size")
    endif()
    math(EXPR misaligned "${BYTES} % 32")
    if (misaligned)
        # crt0 aligns the .stack section to 32 bytes.
        message(FATAL_ERROR "pico_os_set_stack_size(${TARGET}): ${BYTES} is not a multiple of 32")
    endif()

    set(chip "${PICO_CHIP}")
    if (NOT chip)
        string(REGEX MATCH "^rp[0-9]+" chip "${PICO_PLATFORM}")
    endif()
    set(locations "${PICO_SDK_PATH}/src/${chip}/pico_platform/script_include/default_locations.ld")
    if (NOT EXISTS "${locations}")
        message(FATAL_ERROR "pico_os_set_stack_size(${TARGET}): no ${locations}; "
                            "the SDK memory layout has moved, this needs updating")
    endif()
    file(READ "${locations}" defaults)

    # RAM_LENGTH_DEFAULT = 512k; -> RAM_LENGTH = 0x80000
    foreach(name RAM_ORIGIN SCRATCH_X_LENGTH SCRATCH_Y_ORIGIN SCRATCH_Y_LENGTH)
        if (NOT defaults MATCHES "${name}_DEFAULT[ \t]*=[ \t]*([0-9a-fA-Fxk]+)[ \t]*;")
            message(FATAL_ERROR "pico_os_set_stack_size(${TARGET}): no ${name}_DEFAULT in ${locations}")
        endif()
        string(REGEX REPLACE "^([0-9]+)k$" "\\1*1024" value "${CMAKE_MATCH_1}")
        math(EXPR ${name} "${value}" OUTPUT_FORMAT HEXADECIMAL)
    endforeach()

    math(EXPR sram_end "${SCRATCH_Y_ORIGIN} + ${SCRATCH_Y_LENGTH}" OUTPUT_FORMAT HEXADECIMAL)
    math(EXPR scratch_y "${sram_end} - ${BYTES}" OUTPUT_FORMAT HEXADECIMAL)
    math(EXPR scratch_x "${scratch_y} - ${SCRATCH_X_LENGTH}" OUTPUT_FORMAT HEXADECIMAL)
    math(EXPR ram_length "${scratch_x} - ${RAM_ORIGIN}" OUTPUT_FORMAT HEXADECIMAL)
    if (NOT ram_length GREATER 0)
        message(FATAL_ERROR "pico_os_set_stack_size(${TARGET}): a ${BYTES} byte stack leaves no RAM")
    endif()

    # PICO_STACK_SIZE reserves the stack inside the region; the region is sized
    # to match, so an overflowing stack is a link error rather than a surprise.
    target_compile_definitions(${TARGET} PRIVATE PICO_STACK_SIZE=${BYTES})
    pico_set_linker_script_var(${TARGET} RAM_LENGTH ${ram_length})
    pico_set_linker_script_var(${TARGET} SCRATCH_X_ORIGIN ${scratch_x})
    pico_set_linker_script_var(${TARGET} SCRATCH_Y_ORIGIN ${scratch_y})
    pico_set_linker_script_var(${TARGET} SCRATCH_Y_LENGTH ${BYTES})

    math(EXPR ram_kb "${ram_length} / 1024")
    math(EXPR stack_kb "${BYTES} / 1024")
    message(STATUS "${TARGET}: ${stack_kb}K stack at ${scratch_y}-${sram_end}, "
                   "RAM ${RAM_ORIGIN}+${ram_kb}K, SCRATCH_X at ${scratch_x}")
endfunction()

# 
# Saves memory/flash, fail the build if printf stdio got linked in, Use trace() instead.
function(pico_os_check_no_stdio TARGET)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DNM=${CMAKE_NM}
            -DELF=$<TARGET_FILE:${TARGET}>
            -P "${PICO_OS_PATH}/check_no_stdio.cmake"
        VERBATIM)
endfunction()

function(pico_os_check_executables DIRECTORY)
    get_property(targets DIRECTORY "${DIRECTORY}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        get_target_property(type ${target} TYPE)
        if (type STREQUAL "EXECUTABLE")
            pico_os_check_no_stdio(${target})
        endif()
    endforeach()
endfunction()

macro(pico_os_pre_project)
    set(PICO_SDK_PATH "${PICO_OS_PATH}/submodules/pico-sdk")
    set(PICO_MBEDTLS_PATH "${PICO_OS_PATH}/submodules/pico-sdk/lib/mbedtls")

    include("${PICO_OS_PATH}/submodules/pico_sdk_import.cmake")
endmacro()

macro(pico_os_init)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 20)

    pico_sdk_init()

    add_compile_options(-Os -g3
      -Wall
      -Wno-psabi
      -Wno-unused-result
      -Wno-format
      -Wno-unused-function
      -Wno-maybe-uninitialized
      -Werror
    )

    add_link_options(-Wl,--build-id=sha1)
    add_link_options("-Wl,-T,${PICO_OS_PATH}/build_id.ld")

    add_compile_definitions(
      PICO_OS_PICO=1
      PICO_PANIC_FUNCTION=fail
      PICO_PANIC_FUNCTION_DOES_NOT_RETURN=1
      PICO_CYW43_LOGGING_ENABLED=0
    )

    cmake_language(DEFER CALL pico_os_check_executables "${CMAKE_CURRENT_SOURCE_DIR}")

    include("${PICO_OS_PATH}/signing/signing.cmake")
    pico_os_signing_init()

    pico_os_version_header()

    add_subdirectory("${PICO_OS_PATH}/config"     "${CMAKE_BINARY_DIR}/os/config")
    add_subdirectory("${PICO_OS_PATH}/submodules" "${CMAKE_BINARY_DIR}/os/submodules")
    add_subdirectory("${PICO_OS_PATH}/libraries"  "${CMAKE_BINARY_DIR}/os/libraries")
endmacro()
