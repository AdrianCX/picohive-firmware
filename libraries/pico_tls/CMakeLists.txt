add_library(pico_tls INTERFACE)

# The root CAs come from 'src/config/tls_domains.txt' if present or 'os/config/tls_domains.txt'
# roots are fetched when the list changes and written into the build, see os/util/make_root_certificates.py
if (EXISTS "${CMAKE_SOURCE_DIR}/src/config/tls_domains.txt")
  set(tls_domains_file "${CMAKE_SOURCE_DIR}/src/config/tls_domains.txt")
else()
  set(tls_domains_file "${CMAKE_CURRENT_SOURCE_DIR}/../../config/tls_domains.txt")
endif()

set(PICO_TLS_ROOTS_CPP "${CMAKE_BINARY_DIR}/generated/root_certificates_der.cpp")

execute_process(
  COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/../../util/make_root_certificates.py"
          --domains "${tls_domains_file}" --out "${PICO_TLS_ROOTS_CPP}"
  RESULT_VARIABLE PICO_TLS_ROOTS_RESULT)

if (NOT PICO_TLS_ROOTS_RESULT EQUAL 0)
  message(FATAL_ERROR "Could not build the TLS root certificates from ${tls_domains_file}")
endif()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${tls_domains_file}")

if (DEFINED ENV{SOURCE_DATE_EPOCH})
  set(PICO_TLS_BUILD_EPOCH "$ENV{SOURCE_DATE_EPOCH}")
else()
  string(TIMESTAMP PICO_TLS_BUILD_EPOCH "%s" UTC)
endif()

math(EXPR PICO_TLS_DAY_BEFORE "${PICO_TLS_BUILD_EPOCH} - 86400")
set(PICO_TLS_SAVED_EPOCH "$ENV{SOURCE_DATE_EPOCH}")
set(ENV{SOURCE_DATE_EPOCH} "${PICO_TLS_DAY_BEFORE}")
string(TIMESTAMP PICO_TLS_EXPIRED_BEFORE "%Y%m%d" UTC)
if (PICO_TLS_SAVED_EPOCH STREQUAL "")
  unset(ENV{SOURCE_DATE_EPOCH})
else()
  set(ENV{SOURCE_DATE_EPOCH} "${PICO_TLS_SAVED_EPOCH}")
endif()

file(WRITE "${CMAKE_BINARY_DIR}/generated/pico_tls_build_date.h.in" "#pragma once\n#define PICO_TLS_EXPIRED_BEFORE ${PICO_TLS_EXPIRED_BEFORE}\n")
configure_file("${CMAKE_BINARY_DIR}/generated/pico_tls_build_date.h.in" "${CMAKE_BINARY_DIR}/generated/pico_tls_build_date.h" COPYONLY)

target_sources(pico_tls INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls_wrapper.c
  ${CMAKE_CURRENT_SOURCE_DIR}/root_certificates.cpp
  ${PICO_TLS_ROOTS_CPP}
  ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM}/tls_listener.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM}/listener.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM}/session.cpp
  )

if (PICO_OS_PLATFORM STREQUAL "linux")
  target_sources(pico_tls INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM}/tls_support.cpp)
  target_link_libraries(pico_tls INTERFACE pico_mbedtls pico_logger pico_simple_util)
else()
  target_sources(pico_tls INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM}/tls_handshake_gate.cpp)
  target_link_libraries(pico_tls INTERFACE pico_lwip_mbedtls pico_mbedtls pico_logger pico_simple_util)
endif()

target_include_directories(pico_tls INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM} ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
