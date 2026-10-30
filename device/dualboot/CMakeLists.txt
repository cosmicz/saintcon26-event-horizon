# RP2350 images for the dual-boot layout. Host build only: this produces
# files; flashing is a separate step. See device/dualboot/README.org for the
# layout and build commands.
cmake_minimum_required(VERSION 3.20)

set(EH_REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(PICO_PLATFORM rp2350-arm-s)
set(PICO_BOARD saintcon26_badge)
set(PICO_BOARD_HEADER_DIRS ${EH_REPO_ROOT}/device/board)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(eh_dualboot C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_compile_options(-Wall -Wextra -Werror)

# Both images drive the panel, so both need the private stock backup, from
# which the panel init header is generated into this build directory only
# (it stays private): -DEH_STOCK_DUMP=/path/to/stock-fullflash.bin.
set(EH_STOCK_DUMP "" CACHE FILEPATH "Private 16 MiB stock flash backup (for the panel init table)")
if(EH_STOCK_DUMP)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(EH_PANEL_INIT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated-private)
  set(EH_PANEL_INIT_H ${EH_PANEL_INIT_DIR}/eh_st77916_stock_init.h)
  add_custom_command(OUTPUT ${EH_PANEL_INIT_H}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${EH_PANEL_INIT_DIR}
    COMMAND Python3::Interpreter -I ${EH_REPO_ROOT}/device/tools/extract_panel_init.py
            ${EH_STOCK_DUMP} ${CMAKE_OBJDUMP} ${EH_PANEL_INIT_H}
    DEPENDS ${EH_REPO_ROOT}/device/tools/extract_panel_init.py ${EH_STOCK_DUMP}
    VERBATIM)

  # Slot 0: the menu, with partitions.json embedded in its block loop.
  # picotool partition create is run directly (not pico_embed_pt_in_binary) to
  # pass --singleton: with it, the bootrom does not also scan flash slot 1
  # (datasheet 5.1.15), which our menu image overlays.
  add_executable(eh_menu
    ${EH_REPO_ROOT}/device/menu/main.c
    ${EH_REPO_ROOT}/device/menu/eh_boot_select.c
    ${EH_REPO_ROOT}/device/menu/eh_menu_draw.c
    ${EH_REPO_ROOT}/device/demo/eh_lcd.c
    ${EH_PANEL_INIT_H})
  target_include_directories(eh_menu PRIVATE
    ${EH_REPO_ROOT}/include ${EH_REPO_ROOT}/device/menu ${EH_REPO_ROOT}/device/board
    ${EH_REPO_ROOT}/device/demo ${EH_PANEL_INIT_DIR})
  target_compile_definitions(eh_menu PRIVATE EH_LCD_RELEASE)
  pico_generate_pio_header(eh_menu ${EH_REPO_ROOT}/device/demo/eh_lcd_qspi.pio)
  pico_generate_pio_header(eh_menu ${EH_REPO_ROOT}/device/demo/eh_ws2812.pio)
  target_link_libraries(eh_menu pico_runtime pico_time pico_bootrom hardware_gpio hardware_pio
    hardware_dma hardware_pwm hardware_clocks hardware_resets hardware_sync)
  pico_enable_stdio_uart(eh_menu 0)
  pico_enable_stdio_usb(eh_menu 0)
  pico_set_program_name(eh_menu "eh-dualboot-menu")
  pico_init_picotool()
  set(EH_PARTITIONS_JSON ${CMAKE_CURRENT_LIST_DIR}/partitions.json)
  set_property(TARGET eh_menu APPEND PROPERTY LINK_DEPENDS ${EH_PARTITIONS_JSON})
  add_custom_command(TARGET eh_menu POST_BUILD
    COMMAND picotool partition create --quiet ${EH_PARTITIONS_JSON}
            $<TARGET_FILE:eh_menu> $<TARGET_FILE:eh_menu> --singleton
    VERBATIM)
  pico_add_bin_output(eh_menu)
  pico_add_dis_output(eh_menu)
  pico_add_map_output(eh_menu)

  # Slot 1 (partition 1): EVENT HORIZON.
  file(GLOB EH_CORE_SOURCES ${EH_REPO_ROOT}/src/core/*.c)
  add_executable(eh_demo
    ${EH_REPO_ROOT}/device/demo/main.c
    ${EH_REPO_ROOT}/device/demo/eh_lcd.c
    ${EH_PANEL_INIT_H}
    ${EH_CORE_SOURCES})
  target_include_directories(eh_demo PRIVATE
    ${EH_REPO_ROOT}/include ${EH_REPO_ROOT}/src/core ${EH_REPO_ROOT}/device/board
    ${EH_REPO_ROOT}/device/demo ${EH_PANEL_INIT_DIR})
  # Per-function stack frames (*.su) for the 2 KiB-per-core stack check.
  target_compile_options(eh_demo PRIVATE -fstack-usage)
  # Each core's stack is its own 4 KiB scratch bank; reserve all of it so the
  # link fails if anything else is placed there. Deepest frame: terrain part,
  # 1,624 B.
  target_compile_definitions(eh_demo PRIVATE PICO_STACK_SIZE=0x1000 PICO_CORE1_STACK_SIZE=0x1000)
  pico_generate_pio_header(eh_demo ${EH_REPO_ROOT}/device/demo/eh_lcd_qspi.pio)
  pico_generate_pio_header(eh_demo ${EH_REPO_ROOT}/device/demo/eh_ws2812.pio)
  target_link_libraries(eh_demo pico_runtime pico_multicore pico_time hardware_gpio hardware_pio
    hardware_dma hardware_pwm hardware_clocks hardware_sync)
  pico_enable_stdio_uart(eh_demo 0)
  pico_enable_stdio_usb(eh_demo 0)
  pico_set_program_name(eh_demo "event-horizon")
  pico_add_bin_output(eh_demo)
  pico_add_dis_output(eh_demo)
  pico_add_map_output(eh_demo)
else()
  message(STATUS "EH_STOCK_DUMP not set: eh_menu and eh_demo (need the private panel init) not built")
endif()
