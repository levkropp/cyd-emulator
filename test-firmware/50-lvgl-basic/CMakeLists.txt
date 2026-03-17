# ESP-IDF CMake project for LVGL basic test
cmake_minimum_required(VERSION 3.16)

# Include LVGL component
set(EXTRA_COMPONENT_DIRS components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lvgl-basic-test)
