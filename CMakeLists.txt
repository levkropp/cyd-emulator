cmake_minimum_required(VERSION 3.10)
project(cyd-emulator C)

# App source directory (ESP32 firmware sources)
# Priority: 1) explicit -DAPP_SOURCE_DIR  2) ../survival  3) built-in demo
if(NOT DEFINED APP_SOURCE_DIR)
    if(EXISTS "${CMAKE_SOURCE_DIR}/../survival/esp32/main/main.c")
        set(APP_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../survival/esp32/main"
            CACHE PATH "Path to ESP32 app sources")
    else()
        set(APP_SOURCE_DIR "${CMAKE_SOURCE_DIR}/demos/freertos"
            CACHE PATH "Path to ESP32 app sources (built-in demo)")
    endif()
endif()

# Resolve relative paths (so demos/freertos works from build dir)
get_filename_component(APP_SOURCE_DIR "${APP_SOURCE_DIR}" ABSOLUTE
                       BASE_DIR "${CMAKE_SOURCE_DIR}")

# Verify app source dir exists
if(NOT EXISTS "${APP_SOURCE_DIR}/main.c")
    message(FATAL_ERROR
        "APP_SOURCE_DIR=${APP_SOURCE_DIR} does not contain main.c.\n"
        "Pass -DAPP_SOURCE_DIR=<path-to-esp32/main>\n"
        "Built-in demos: demos/nolibrary, demos/freertos")
endif()

# Detect standalone mode: full firmware has gpt.c + external image decoders
if(EXISTS "${APP_SOURCE_DIR}/gpt.c" AND
   EXISTS "${CMAKE_SOURCE_DIR}/../survival/sped/sped.c")
    set(EMU_STANDALONE OFF)
else()
    set(EMU_STANDALONE ON)
endif()

# Common demo headers (display.h, touch.h, etc.) for standalone builds
set(DEMO_COMMON_DIR "${CMAKE_SOURCE_DIR}/demos/common")

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
    src/emu_freertos.c
)

if(EMU_STANDALONE)
    # Standalone/demo: minimal app sources
    message(STATUS "Building standalone: ${APP_SOURCE_DIR}")
    set(APP_SOURCES
        ${APP_SOURCE_DIR}/main.c
        ${DEMO_COMMON_DIR}/font.c
    )
else()
    # Full firmware: all app sources from external project
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
        ${CMAKE_SOURCE_DIR}/../survival/sped/sped.c
        ${CMAKE_SOURCE_DIR}/../survival/femtojpeg/femtojpeg.c
    )
endif()

add_executable(cyd-emulator ${EMU_SOURCES} ${APP_SOURCES})

# Include paths: emulator shims first, then app headers
target_include_directories(cyd-emulator PRIVATE
    ${CMAKE_SOURCE_DIR}/include      # ESP-IDF shim headers
    ${APP_SOURCE_DIR}                # App-specific headers
    ${SDL2_INCLUDE_DIRS}
)

if(EMU_STANDALONE)
    # Demo builds also need the common demo headers
    target_include_directories(cyd-emulator PRIVATE
        ${DEMO_COMMON_DIR}
    )
else()
    target_include_directories(cyd-emulator PRIVATE
        ${CMAKE_SOURCE_DIR}/../survival/sped
        ${CMAKE_SOURCE_DIR}/../survival/femtojpeg
    )
endif()

# Link libraries
target_link_libraries(cyd-emulator PRIVATE
    ${SDL2_LIBRARIES}
    pthread
)

if(NOT EMU_STANDALONE)
    target_link_libraries(cyd-emulator PRIVATE miniz)
endif()

target_compile_options(cyd-emulator PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    ${SDL2_CFLAGS_OTHER}
)

# Need _GNU_SOURCE for ftruncate, fseeko, etc.
target_compile_definitions(cyd-emulator PRIVATE _GNU_SOURCE)

if(EMU_STANDALONE)
    target_compile_definitions(cyd-emulator PRIVATE EMU_STANDALONE)
endif()
