# web.cmake - packs a web page into the firmware, served straight out of flash.
#
#   pico_os_web_resource(${CMAKE_CURRENT_SOURCE_DIR}/src rc/index.html COMPILED_RESOURCE)
#   add_executable(my_app ... ${COMPILED_RESOURCE})
#
# The page is minified (inline CSS and JS, whitespace and comments in the HTML), gzipped and written as a C
# header next to it, BASE_DIR/INPUT_FILE.gz.hex.h, in the source tree because the
# sources #include it by its relative path; gitignore *.html.gz.hex.h.
#
# The header holds the same names xxd -i gave for INPUT_FILE.gz relative to
# BASE_DIR - rc/index.html becomes __rc_index_html_gz and __rc_index_html_gz_len -
# with the array const, so it stays in flash instead of being copied to RAM.
#
# Minifying needs minify-html, which the build container installs.
function(pico_os_web_resource BASE_DIR INPUT_FILE OUTPUT_VAR)
    set(OUTPUT_FILE "${BASE_DIR}/${INPUT_FILE}.gz.hex.h")
    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND python3 ${PICO_OS_PATH}/web/pack_web_page.py ${BASE_DIR} ${INPUT_FILE} ${OUTPUT_FILE}
        COMMENT "Packing ${INPUT_FILE} into ${INPUT_FILE}.gz.hex.h"
        DEPENDS ${BASE_DIR}/${INPUT_FILE} ${PICO_OS_PATH}/web/pack_web_page.py)
    set(${OUTPUT_VAR} ${OUTPUT_FILE} PARENT_SCOPE)
endfunction()

# pico_os_web_page - builds the target's page from a page.yaml (see web/README.md)
# and packs it the same way, after add_executable():
#
#   add_executable(my_app ...)
#   pico_os_web_page(my_app ${CMAKE_CURRENT_SOURCE_DIR}/src/web/page.yaml)
#
# Both the page and its header go in the build tree, under web/<target>/rc/,
# which goes on the target's include path: the sources keep their
# #include "rc/index.html.gz.hex.h" and the __rc_index_html_gz names.
#
# Rebuilt when page.yaml, anything next to it, or anything in os/web changes.
# A feature turned on without its library linked (telegram without
# pico_telegram) is a build warning.
#
# Needs PyYAML (python3-yaml), which the build container installs.
function(pico_os_web_page TARGET PAGE_YAML)
    # A header pico_os_web_resource() left in the source tree is found first by
    # #include "rc/index.html.gz.hex.h" (the including file's own folder comes
    # first), and the firmware would quietly keep serving the old page.
    file(GLOB_RECURSE STALE "${CMAKE_SOURCE_DIR}/src/*.html.gz.hex.h")
    if (STALE)
        message(FATAL_ERROR "pico_os_web_page: ${STALE} is left from pico_os_web_resource() and would be "
                            "included instead of the page built from ${PAGE_YAML}: delete it")
    endif()

    set(OUT_DIR "${CMAKE_BINARY_DIR}/web/${TARGET}")
    set(HTML "${OUT_DIR}/rc/index.html")
    set(HEADER "${OUT_DIR}/rc/index.html.gz.hex.h")
    get_filename_component(PAGE_DIR "${PAGE_YAML}" DIRECTORY)

    file(GLOB_RECURSE WEB_SOURCES CONFIGURE_DEPENDS
        "${PAGE_DIR}/*"
        "${PICO_OS_PATH}/web/base/*"
        "${PICO_OS_PATH}/web/features/*")

    add_custom_command(
        OUTPUT ${HTML}
        COMMAND python3 ${PICO_OS_PATH}/web/build_web_page.py ${PAGE_YAML} -o ${HTML}
                "--libraries=$<JOIN:$<TARGET_PROPERTY:${TARGET},LINK_LIBRARIES>,$<COMMA>>"
        COMMENT "Building the web page from ${PAGE_YAML}"
        DEPENDS ${WEB_SOURCES} ${PICO_OS_PATH}/web/build_web_page.py
        VERBATIM)
    add_custom_command(
        OUTPUT ${HEADER}
        COMMAND python3 ${PICO_OS_PATH}/web/pack_web_page.py ${OUT_DIR} rc/index.html ${HEADER}
        COMMENT "Packing the web page into rc/index.html.gz.hex.h"
        DEPENDS ${HTML} ${PICO_OS_PATH}/web/pack_web_page.py
        VERBATIM)

    target_sources(${TARGET} PRIVATE ${HEADER})
    target_include_directories(${TARGET} PRIVATE ${OUT_DIR})
endfunction()
