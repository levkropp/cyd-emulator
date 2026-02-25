cmake_minimum_required(VERSION 3.14)
project(cyd-emulator C)

# flexe Xtensa emulator (always built)
add_subdirectory(flexe)

# Find SDL2
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

# Emulator sources
set(EMU_SOURCES
    src/emu_main.c
    src/emu_display.c
    src/emu_touch.c
    src/emu_sdcard.c
    src/emu_crc32.c
    src/emu_json.c
    src/emu_freertos.c
    src/emu_timer.c
    src/emu_nvs.c
    src/emu_system.c
    src/emu_gpio.c
    src/emu_flexe.c
    src/emu_control.c
    src/font.c
)

add_executable(cyd-emulator ${EMU_SOURCES})

target_include_directories(cyd-emulator PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(cyd-emulator PRIVATE
    ${SDL2_LIBRARIES}
    pthread
    xtensa-emu-lib
)

target_compile_options(cyd-emulator PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    ${SDL2_CFLAGS_OTHER}
)

# Need _GNU_SOURCE for ftruncate, fseeko, etc.
target_compile_definitions(cyd-emulator PRIVATE
    _GNU_SOURCE
    EMU_USE_FLEXE
    EMU_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    EMU_BUILD_DIR="${CMAKE_BINARY_DIR}"
)
