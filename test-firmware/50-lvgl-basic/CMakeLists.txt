# ESP-IDF CMake project for LVGL basic test
cmake_minimum_required(VERSION 3.16)

# LVGL comes from component registry via main/idf_component.yml
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lvgl-basic-test)
