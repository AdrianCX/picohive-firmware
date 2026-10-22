add_library(pico_http INTERFACE)
target_sources(pico_http INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/http_session.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/http_header.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/http_header_helper.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/websocket_handler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/http_request.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/http_client.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/http_chunk_decoder.cpp
  )

target_include_directories(pico_http INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(pico_http INTERFACE pico_tls pico_logger pico_simple_util)
