cmake_minimum_required(VERSION 3.10)
project(cyd-emulator C)

# App source directory (ESP32 firmware sources)
if(NOT DEFINED APP_SOURCE_DIR)
    set(APP_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../survival/esp32/main"
        CACHE PATH "Path to ESP32 app sources")
endif()

# Verify app source dir exists
if(NOT EXISTS "${APP_SOURCE_DIR}/main.c")
    message(FATAL_ERROR
        "APP_SOURCE_DIR=${APP_SOURCE_DIR} does not contain main.c.\n"
        "Pass -DAPP_SOURCE_DIR=<path-to-esp32/main>")
endif()

# Find SDL2
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

# Emulator sources
set(EMU_SOURCES
    src/emu_main.c
    src/emu_display.c
    src/emu_touch.c
    src/emu_sdcard.c
    src/emu_payload.c
    src/emu_crc32.c
    src/emu_json.c
)

# App sources (reused verbatim from ESP32 firmware)
set(APP_SOURCES
    ${APP_SOURCE_DIR}/main.c
    ${APP_SOURCE_DIR}/gpt.c
    ${APP_SOURCE_DIR}/fat32.c
    ${APP_SOURCE_DIR}/exfat.c
    ${APP_SOURCE_DIR}/flasher.c
    ${APP_SOURCE_DIR}/ui.c
    ${APP_SOURCE_DIR}/font.c
    ${APP_SOURCE_DIR}/settings.c
    ${APP_SOURCE_DIR}/app.c
    ${APP_SOURCE_DIR}/apps.c
    ${APP_SOURCE_DIR}/app_flasher.c
    ${APP_SOURCE_DIR}/app_settings.c
    ${APP_SOURCE_DIR}/app_files.c
)

add_executable(cyd-emulator ${EMU_SOURCES} ${APP_SOURCES})

# Include paths: emulator shims first, then app headers
target_include_directories(cyd-emulator PRIVATE
    ${CMAKE_SOURCE_DIR}/include      # ESP-IDF shim headers
    ${APP_SOURCE_DIR}                # App headers (display.h, etc.)
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(cyd-emulator PRIVATE
    ${SDL2_LIBRARIES}
    miniz
    pthread
)

target_compile_options(cyd-emulator PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    ${SDL2_CFLAGS_OTHER}
)

# Need _GNU_SOURCE for ftruncate, fseeko, etc.
target_compile_definitions(cyd-emulator PRIVATE _GNU_SOURCE)
