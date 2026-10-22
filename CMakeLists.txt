cmake_minimum_required(VERSION 3.12)

include(${CMAKE_CURRENT_LIST_DIR}/pico_os.cmake)

pico_os_pre_project()

project(pico_playground C CXX ASM)

pico_os_init()
