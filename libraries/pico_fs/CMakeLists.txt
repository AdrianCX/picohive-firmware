add_library(pico_fs INTERFACE)

target_include_directories(pico_fs INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM} ${CMAKE_CURRENT_SOURCE_DIR})

if (PICO_OS_PLATFORM STREQUAL "linux")
  target_sources(pico_fs INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM}/os_fs_posix.c)
  target_link_libraries(pico_fs INTERFACE pico_simple_util pico_logger)
else()
  target_sources(pico_fs INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM}/os_fs_littlefs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM}/littlefs/lfs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM}/littlefs/lfs_util.c)
  target_include_directories(pico_fs INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/${PICO_OS_PLATFORM}/littlefs)
  target_link_libraries(pico_fs INTERFACE hardware_flash hardware_sync pico_config)
endif()

