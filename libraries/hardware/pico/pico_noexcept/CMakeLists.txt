add_library(pico_noexcept INTERFACE)

target_sources(pico_noexcept INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/noexcept.cpp
)
