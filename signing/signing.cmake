# signing.cmake - OTA upgrades only accept firmware encrypted and signed
#
# Keys are kept in numbered versions, each one a whole set in the application keys/versions/<n>/
#
#   PICO_OS_KEY_VERSION        what running devices currently have and accept.
#   PICO_OS_NEXT_KEY_VERSION   what this build embeds and devices will accept after upgrade
#
# At first they're equal and all builds are packaged and accept the same key.
# When we want key rotation:
# - we create a new key and increment PICO_OS_NEXT_KEY_VERSION
# - we update all devices in the field
# - we bump PICO_OS_KEY_VERSION to the same value.
# - (all devices now have new key)
#
# Warning: Missing keys are created, never replaced, so if you forget to place keys in next,
#          new ones will be created for you and you may flash with those by mistake.
#
set(PICO_OS_KEY_DIR "${CMAKE_SOURCE_DIR}/keys" CACHE PATH "Where the numbered key versions live")

# Writes a nul terminated PEM into a header, as mbedtls wants it.
function(pico_os_write_key_header path name variable)
    file(READ "${path}" pem)
    string(STRIP "${pem}" pem)
    string(REPLACE "\n" "\\n\"\n    \"" pem "${pem}")

    file(CONFIGURE OUTPUT "${PICO_OS_SIGNING_INCLUDE_DIR}/${name}" CONTENT
"// Generated from ${path} - do not edit.
#pragma once

// PEM, nul terminated as required by mbedtls
static const char ${variable}[] =
    \"${pem}\\n\";
" @ONLY)

    # Reconfigure when the key is replaced, so the new one gets embedded.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
endfunction()

# Path of a key version, made if it is not there yet.
function(pico_os_key_version_dir version output)
    set(dir "${PICO_OS_KEY_DIR}/versions/${version}")

    if ((NOT EXISTS "${PICO_OS_KEY_DIR}") AND (NOT PICO_OS_KEY_DIR STREQUAL "${CMAKE_SOURCE_DIR}/keys"))
        message(FATAL_ERROR
            "PICO_OS_KEY_DIR is ${PICO_OS_KEY_DIR}, which does not exist. If the keys moved, "
            "delete ${CMAKE_BINARY_DIR}/CMakeCache.txt so the new location is picked up, or set "
            "PICO_OS_KEY_DIR in the application's CMakeLists.txt before pico_os_init().")
    endif()

    if (NOT EXISTS "${dir}/ota_signing_key.pem")
        message(WARNING "No key version ${version} in ${PICO_OS_KEY_DIR}/versions, generating one. "
                        "Devices running firmware built with different keys will refuse these upgrades: "
                        "if you have that version's keys, put them there and rebuild.")
        execute_process(COMMAND "${PICO_OS_PATH}/signing/create_keys.sh" "${version}" "${PICO_OS_KEY_DIR}"
                        RESULT_VARIABLE rc)
        if (NOT rc EQUAL 0)
            message(FATAL_ERROR "Could not create key version ${version} (is openssl installed?)")
        endif()
    endif()

    set(${output} "${dir}" PARENT_SCOPE)
endfunction()

function(pico_os_signing_init)
    set(include_dir "${CMAKE_BINARY_DIR}/os/signing")
    set(PICO_OS_SIGNING_INCLUDE_DIR "${include_dir}" CACHE INTERNAL "Where the key headers are generated")

    if (NOT DEFINED PICO_OS_KEY_VERSION)
        set(PICO_OS_KEY_VERSION 0)
    endif()

    if ((NOT DEFINED PICO_OS_NEXT_KEY_VERSION) OR (PICO_OS_NEXT_KEY_VERSION STREQUAL ""))
        set(PICO_OS_NEXT_KEY_VERSION "${PICO_OS_KEY_VERSION}")
    endif()

    foreach(version "${PICO_OS_KEY_VERSION}" "${PICO_OS_NEXT_KEY_VERSION}")
        if (NOT version MATCHES "^[0-9]+$")
            message(FATAL_ERROR "PICO_OS_KEY_VERSION and PICO_OS_NEXT_KEY_VERSION are version numbers; got '${version}'")
        endif()
    endforeach()

    pico_os_key_version_dir("${PICO_OS_KEY_VERSION}" current)
    pico_os_key_version_dir("${PICO_OS_NEXT_KEY_VERSION}" next)

    set(signing_public "${next}/ota_signing_key.pub.pem")
    set(encryption_private "${next}/ota_encryption_key.pem")
    set(signing_private "${current}/ota_signing_key.pem")
    set(encryption_public "${current}/ota_encryption_key.pub.pem")

    foreach(needed "${signing_public}" "${encryption_private}")
        if (NOT EXISTS "${needed}")
            message(FATAL_ERROR "No ${needed}. Every build embeds these two; restore key version "
                                "${PICO_OS_NEXT_KEY_VERSION} or run signing/create_keys.sh ${PICO_OS_NEXT_KEY_VERSION}.")
        endif()
    endforeach()

    execute_process(COMMAND openssl pkey -in "${encryption_private}" -noout -text
                    OUTPUT_VARIABLE encryption_key_text ERROR_QUIET RESULT_VARIABLE rc)
    if ((NOT rc EQUAL 0) OR (NOT encryption_key_text MATCHES "prime256v1"))
        message(FATAL_ERROR
            "${encryption_private} is not a P-256 key. Encrypted upgrades use ECDH: "
            "run signing/create_keys.sh ${PICO_OS_NEXT_KEY_VERSION} --force to replace that version. "
            "Devices running a build made with the old key then need flashing over USB.")
    endif()

    pico_os_write_key_header("${signing_public}" ota_signing_key.h OTA_SIGNING_PUBLIC_KEY)
    pico_os_write_key_header("${encryption_private}" ota_encryption_key.h OTA_ENCRYPTION_PRIVATE_KEY)

    if (EXISTS "${signing_private}")
        set(PICO_OS_SIGNING_PRIVATE_KEY "${signing_private}" CACHE INTERNAL "Key .uf2.encrypted files are signed with")
        set(PICO_OS_ENCRYPTION_PUBLIC_KEY "${encryption_public}" CACHE INTERNAL "Key .uf2.encrypted files are made with")

        if (PICO_OS_KEY_VERSION STREQUAL PICO_OS_NEXT_KEY_VERSION)
            message(STATUS "OTA keys: version ${PICO_OS_KEY_VERSION}")
        else()
            message(STATUS "OTA keys: packaged for version ${PICO_OS_KEY_VERSION}, embedding version ${PICO_OS_NEXT_KEY_VERSION}. "
                           "Install this everywhere, then set PICO_OS_KEY_VERSION to ${PICO_OS_NEXT_KEY_VERSION}.")
        endif()
    else()
        unset(PICO_OS_SIGNING_PRIVATE_KEY CACHE)
        unset(PICO_OS_ENCRYPTION_PUBLIC_KEY CACHE)
        message(STATUS "OTA keys: embedding version ${PICO_OS_NEXT_KEY_VERSION}, no ${signing_private} "
                       "so no .uf2.encrypted")
    endif()
endfunction()

# Writes <name>.uf2.encrypted beside the <name>.uf2 pico_add_uf2_output() makes.
function(pico_os_add_signed_uf2_output TARGET)
    if (PICO_NO_UF2 OR PICO_NO_PICOTOOL OR NOT TARGET picotool OR NOT PICO_OS_SIGNING_PRIVATE_KEY)
        return()
    endif()

    # Same output location as pico_add_uf2_output().
    get_target_property(archive_directory ${TARGET} ARCHIVE_OUTPUT_DIRECTORY)
    if (archive_directory)
        get_filename_component(output_path "${archive_directory}" REALPATH BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
        set(output_path "${output_path}/")
    else()
        set(output_path "")
    endif()
    pico_get_output_name(${TARGET} output_name)

    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND "${PICO_OS_PATH}/signing/encrypt_my_binary.py"
            "${output_path}${output_name}.uf2"
            "${output_path}${output_name}.uf2.encrypted"
            --board "${PICO_BOARD}"
            --elf "$<TARGET_FILE:${TARGET}>"
            --key "${PICO_OS_ENCRYPTION_PUBLIC_KEY}"
            --signing-key "${PICO_OS_SIGNING_PRIVATE_KEY}"
        VERBATIM
        BYPRODUCTS
            "${output_path}${output_name}.uf2.encrypted")
endfunction()

function(pico_os_add_signed_linux_output TARGET)
    if (NOT PICO_OS_SIGNING_PRIVATE_KEY)
        return()
    endif()

    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND "${CMAKE_OBJCOPY}" --strip-debug "$<TARGET_FILE:${TARGET}>" "$<TARGET_FILE:${TARGET}>.stripped"
        COMMAND truncate --size=%16 "$<TARGET_FILE:${TARGET}>.stripped"
        COMMAND python3 "${PICO_OS_PATH}/signing/encrypt_my_binary.py"
            "$<TARGET_FILE:${TARGET}>.stripped"
            "$<TARGET_FILE:${TARGET}>.encrypted"
            --board "${PICO_BOARD}"
            --elf "$<TARGET_FILE:${TARGET}>"
            --key "${PICO_OS_ENCRYPTION_PUBLIC_KEY}"
            --signing-key "${PICO_OS_SIGNING_PRIVATE_KEY}"
        VERBATIM)
endfunction()

# Writes <name>.debug beside the ELF: the symbols and line tables only, for crash symbolization on the device page instead of the full ELF.
# Code and data sections are kept as empty headers (addresses only)
# The build-id note stays so the page can match it to the firmware;
# macros (-g3), variable locations and unwind tables are left out, since addr2line-style lookups don't use them
# The rest is zlib-compressed. 
option(PICO_OS_DEBUG_FILE "Write <name>.debug (symbols only) beside the ELF" ON)

function(pico_os_add_debug_output TARGET)
    if (NOT PICO_OS_DEBUG_FILE)
        return()
    endif()

    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND "${CMAKE_OBJCOPY}" --only-keep-debug
            --remove-section=.debug_macro
            --remove-section=.debug_loc
            --remove-section=.debug_loclists
            --remove-section=.debug_frame
            --compress-debug-sections=zlib
            "$<TARGET_FILE:${TARGET}>"
            "$<TARGET_FILE_DIR:${TARGET}>/$<TARGET_FILE_BASE_NAME:${TARGET}>.debug"
        VERBATIM)
endfunction()

function(pico_add_extra_outputs TARGET)
    if (PICO_OS_PLATFORM STREQUAL "linux")
        pico_os_add_signed_linux_output(${TARGET})
    else()
        _pico_add_extra_outputs(${TARGET})
        pico_os_add_signed_uf2_output(${TARGET})
        pico_os_add_debug_output(${TARGET})
    endif()
endfunction()
