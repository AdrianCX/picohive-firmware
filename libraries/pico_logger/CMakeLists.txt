add_library(pico_logger INTERFACE)

target_sources(pico_logger INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/pico_logger.h
  ${CMAKE_CURRENT_SOURCE_DIR}/pico_nologger.h
  ${CMAKE_CURRENT_SOURCE_DIR}/log_encryption.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/log_encryption.h
  ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM}/pico_logger.c
)

if (PICO_OS_PLATFORM STREQUAL "linux")
  target_link_libraries(pico_logger INTERFACE pico_fs pico_simple_util pico_mbedtls ${CMAKE_DL_LIBS})
else()
  target_sources(pico_logger INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM}/fault_vectors.S
    ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM}/cm_backtrace/cm_backtrace.c
  )
  target_link_libraries(pico_logger INTERFACE pico_lwip pico_simple_util pico_mbedtls pico_rand pico_fs)
  target_include_directories(pico_logger INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM}/cm_backtrace)
endif()

target_include_directories(pico_logger INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM} ${CMAKE_CURRENT_SOURCE_DIR})
