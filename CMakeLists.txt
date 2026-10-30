cmake_minimum_required(VERSION 3.20)
project(event_horizon LANGUAGES C)

# Host-only build: portable renderer core, headless CLI, tests and an optional
# SDL2 preview. Nothing here targets, enumerates or writes to a device.

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(EH_WERROR "Treat warnings as errors" ON)
option(EH_PREVIEW "Build the SDL2 preview window if SDL2 is found" ON)

# Quality knobs from include/eh/eh_config.h, e.g. -DEH_DEFINES="EH_TERRAIN_SHIFT=7".
set(EH_DEFINES "" CACHE STRING "Extra compile definitions (semicolon-separated)")
if(EH_DEFINES)
  add_compile_definitions(${EH_DEFINES})
endif()

option(EH_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer (abort on first error)" OFF)

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow)
option(EH_TSAN "Build with ThreadSanitizer (host thread pool)" OFF)
if(EH_SANITIZE AND EH_TSAN)
  message(FATAL_ERROR "EH_SANITIZE and EH_TSAN are mutually exclusive")
endif()
if(EH_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()
if(EH_TSAN)
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
endif()
if(EH_WERROR)
  add_compile_options(-Werror)
endif()

string(TOUPPER "${CMAKE_BUILD_TYPE}" EH_BUILD_TYPE_UPPER)
set(EH_BUILD_INFO
    "${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION} | ${CMAKE_BUILD_TYPE} | ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_${EH_BUILD_TYPE_UPPER}} | ${CMAKE_SYSTEM_PROCESSOR}")

# Portable core: no I/O, no allocation, only <stdint.h>/<string.h>/<math.h>.
add_library(eh_core STATIC
  src/core/eh_tables.c
  src/core/eh_plasma.c
  src/core/eh_raster.c
  src/core/eh_camera.c
  src/core/eh_leds.c
  src/core/eh_fade.c
  src/core/eh_torus.c
  src/core/eh_tunnel.c
  src/core/eh_terrain.c
  src/core/eh_particles.c
  src/core/eh_demo.c)
target_include_directories(eh_core PUBLIC include)
find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
  target_link_libraries(eh_core PUBLIC ${MATH_LIBRARY})
endif()

# Dual-boot menu selection rules and screen: portable C shared with the
# RP2350 menu image (device/dualboot), built here so the host suite tests them.
add_library(eh_boot_select STATIC device/menu/eh_boot_select.c device/menu/eh_menu_draw.c)
target_include_directories(eh_boot_select PUBLIC device/menu device/board)
if(MATH_LIBRARY)
  target_link_libraries(eh_boot_select PUBLIC ${MATH_LIBRARY})
endif()

# Host adapters.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
add_library(eh_host STATIC src/host/eh_present.c src/host/eh_pool.c)
target_include_directories(eh_host PUBLIC src/host)
target_link_libraries(eh_host PUBLIC eh_core Threads::Threads)

add_executable(eh_cli src/host/eh_cli.c)
target_link_libraries(eh_cli PRIVATE eh_host)
target_compile_definitions(eh_cli PRIVATE "EH_BUILD_INFO=\"${EH_BUILD_INFO}\"")

if(EH_PREVIEW)
  find_package(SDL2 CONFIG)
  if(SDL2_FOUND)
    add_executable(eh_preview src/host/eh_preview.c)
    target_link_libraries(eh_preview PRIVATE eh_host SDL2::SDL2)
  else()
    message(STATUS "SDL2 not found: eh_preview not built (headless CLI still available)")
  endif()
endif()

# Tests: one ctest entry per registered test function in tests/test_main.c.
enable_testing()
add_executable(eh_tests
  tests/test_main.c
  tests/test_util.c
  tests/test_tables.c
  tests/test_plasma.c
  tests/test_raster.c
  tests/test_camera.c
  tests/test_leds.c
  tests/test_present.c
  tests/test_scenes.c
  tests/test_demo.c
  tests/test_boot_select.c
  tests/test_menu.c
  tests/test_qspi_pack.c)
target_include_directories(eh_tests PRIVATE tests device/demo)
target_link_libraries(eh_tests PRIVATE eh_host eh_boot_select)

# file(READ) rather than file(STRINGS): the registry's line-continuation
# backslashes would otherwise escape CMake's list separators.
file(READ tests/test_main.c EH_TEST_MAIN)
string(REGEX MATCHALL "X\\(test_[a-z0-9_]+\\)" EH_TEST_ENTRIES "${EH_TEST_MAIN}")
foreach(entry IN LISTS EH_TEST_ENTRIES)
  string(REGEX MATCH "test_[a-z0-9_]+" name "${entry}")
  add_test(NAME ${name} COMMAND eh_tests ${name})
endforeach()

# Dual-boot packer and EEPROM-safety audits (device/tools), on synthetic inputs,
# plus the source pin audit of the real device sources.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME dualboot_pack_tests
    COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/device/tools/test_dualboot_pack.py)
  add_test(NAME extract_panel_init_tests
    COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/device/tools/test_extract_panel_init.py)
  add_test(NAME image_audit_tests
    COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/device/tools/test_image_audit.py)
  add_test(NAME pin_audit_tests
    COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/device/tools/test_pin_audit.py)
  # The release exporter stays out of the public snapshot, and so does its test.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_release_export.py)
    add_test(NAME release_export_tests
      COMMAND Python3::Interpreter -I -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_release_export.py)
  endif()
  add_test(NAME pin_audit_sources
    COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/device/tools/pin_audit.py)
endif()

# Smoke tests of the executables themselves (short runs, temp output only).
add_test(NAME cli_frames_smoke
  COMMAND eh_cli frames --out ${CMAKE_CURRENT_BINARY_DIR}/smoke-frames --at 7000,20000,34000,50000)
add_test(NAME cli_bench_smoke
  COMMAND eh_cli bench plasma --frames 30 --warmup 5)
add_test(NAME cli_memory_smoke COMMAND eh_cli memory)
if(Python3_FOUND AND CMAKE_BUILD_TYPE STREQUAL "Release")
  # The menu's committed torus tile must be what the generator makes from
  # today's renderer (tools/gen_menu_art.py). Release only: the tile was
  # generated from a Release build, and Debug/sanitizer builds round floats
  # differently, so their frame is not bit-identical.
  add_test(NAME menu_art_frame COMMAND eh_cli frames
    --out ${CMAKE_CURRENT_BINARY_DIR}/menu-art --at 7000)
  set_tests_properties(menu_art_frame PROPERTIES FIXTURES_SETUP menu_art_frame)
  add_test(NAME menu_art_generate COMMAND Python3::Interpreter -I
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_menu_art.py
    ${CMAKE_CURRENT_BINARY_DIR}/menu-art/frame_07000.ppm ${CMAKE_CURRENT_BINARY_DIR}/menu-art/eh_menu_art.h)
  set_tests_properties(menu_art_generate PROPERTIES
    FIXTURES_REQUIRED menu_art_frame FIXTURES_SETUP menu_art_generated)
  add_test(NAME menu_art_current COMMAND ${CMAKE_COMMAND} -E compare_files
    ${CMAKE_CURRENT_SOURCE_DIR}/device/menu/eh_menu_art.h ${CMAKE_CURRENT_BINARY_DIR}/menu-art/eh_menu_art.h)
  set_tests_properties(menu_art_current PROPERTIES FIXTURES_REQUIRED menu_art_generated)
endif()
if(TARGET eh_preview)
  # Ten fixed 33 ms steps end at t = 297 ms; the stateless renderer must give
  # the window exactly the frame the headless CLI writes for that time.
  add_test(NAME preview_smoke COMMAND eh_preview --frames 10
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/preview-smoke.ppm)
  set_tests_properties(preview_smoke PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy"
    FIXTURES_SETUP preview_shot)
  add_test(NAME cli_frame_297 COMMAND eh_cli frames
    --out ${CMAKE_CURRENT_BINARY_DIR}/preview-compare --at 297)
  set_tests_properties(cli_frame_297 PROPERTIES FIXTURES_SETUP cli_shot)
  add_test(NAME preview_matches_cli COMMAND ${CMAKE_COMMAND} -E compare_files
    ${CMAKE_CURRENT_BINARY_DIR}/preview-smoke.ppm
    ${CMAKE_CURRENT_BINARY_DIR}/preview-compare/frame_00297.ppm)
  set_tests_properties(preview_matches_cli PROPERTIES FIXTURES_REQUIRED "preview_shot;cli_shot")
  # Direct seek in the window versus playback from zero in the CLI, mid-tunnel:
  # differing prior framebuffer histories must not reach the display.
  add_test(NAME preview_seek_20000 COMMAND eh_preview --start 20000 --frames 1
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/preview-seek-20000.ppm)
  set_tests_properties(preview_seek_20000 PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy"
    FIXTURES_SETUP preview_seek_shot)
  add_test(NAME cli_frame_20000 COMMAND eh_cli frames
    --out ${CMAKE_CURRENT_BINARY_DIR}/preview-compare --at 20000)
  set_tests_properties(cli_frame_20000 PROPERTIES FIXTURES_SETUP cli_seek_shot)
  add_test(NAME preview_seek_matches_cli_playback COMMAND ${CMAKE_COMMAND} -E compare_files
    ${CMAKE_CURRENT_BINARY_DIR}/preview-seek-20000.ppm
    ${CMAKE_CURRENT_BINARY_DIR}/preview-compare/frame_20000.ppm)
  set_tests_properties(preview_seek_matches_cli_playback PROPERTIES
    FIXTURES_REQUIRED "preview_seek_shot;cli_seek_shot")
endif()
