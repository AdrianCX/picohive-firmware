add_library(pico_commands INTERFACE)

target_sources(pico_commands INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/ai_tools.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/capture_command_context.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/command_dispatcher.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/command_table.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/command_mqtt_manager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/command_request_handler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/mcp_server.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/pending_clears.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ws_frame_reader.cpp
)

target_include_directories(pico_commands INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(pico_commands INTERFACE pico_http pico_tls pico_simple_mqtt pico_simple_ota pico_simple_config pico_fs pico_logger pico_simple_util pico_mjson)

set(PICO_COMMANDS_PATH ${CMAKE_CURRENT_SOURCE_DIR} CACHE INTERNAL "pico_commands location")

set(PICO_BUILTIN_COMMANDS ${CMAKE_CURRENT_SOURCE_DIR}/commands CACHE INTERNAL "pico_commands built-in commands")

function(pico_commands_add TARGET)
    if(NOT ARGN)
        message(FATAL_ERROR "pico_commands_add: no commands directory given for ${TARGET}")
    endif()

    foreach(GIVEN_DIR IN LISTS ARGN)
        get_filename_component(COMMANDS_DIR "${GIVEN_DIR}" ABSOLUTE)

        if(NOT IS_DIRECTORY ${COMMANDS_DIR})
            message(FATAL_ERROR "pico_commands_add: no such directory: ${COMMANDS_DIR}")
        endif()

        file(GLOB COMMAND_SOURCES CONFIGURE_DEPENDS ${COMMANDS_DIR}/*.cpp)

        if(NOT COMMAND_SOURCES)
            message(WARNING "pico_commands_add: ${COMMANDS_DIR} has no commands")
        endif()

        target_sources(${TARGET} PRIVATE ${COMMAND_SOURCES})
        target_include_directories(${TARGET} PRIVATE ${COMMANDS_DIR})
    endforeach()

    get_target_property(ALREADY_ADDED ${TARGET} PICO_COMMANDS_SCRIPT_ADDED)

    if(NOT ALREADY_ADDED)
        target_link_libraries(${TARGET} pico_commands)
        target_link_options(${TARGET} PRIVATE "-Wl,-T,${PICO_COMMANDS_PATH}/${PICO_OS_PLATFORM}/commands.ld")
        set_target_properties(${TARGET} PROPERTIES PICO_COMMANDS_SCRIPT_ADDED TRUE)
    endif()
endfunction()
