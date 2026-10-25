add_library(pico_openai INTERFACE)

target_sources(pico_openai INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/openai_manager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/openai_chat.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/openai_agent.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/openai_sequence.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ai_log.cpp
)

target_link_libraries(pico_openai INTERFACE pico_logger pico_mjson pico_http pico_commands)
target_include_directories(pico_openai INTERFACE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/commands)

set(PICO_OPENAI_COMMANDS ${CMAKE_CURRENT_SOURCE_DIR}/commands CACHE INTERNAL "pico_openai's commands")
