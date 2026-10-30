# Toolchain smoke build for the RP2350 (Arm Secure, Cortex-M33). Proves the
# host can produce an RP2350 image. Nothing here talks to a device: no stdio,
# no USB, no I2C. See device/README.org for the toolchain and how to build.
cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED PICO_PLATFORM)
  set(PICO_PLATFORM rp2350-arm-s)
endif()
if(NOT DEFINED PICO_BOARD)
  set(PICO_BOARD pico2)
endif()
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(eh_smoke C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(eh_smoke main.c)
target_link_libraries(eh_smoke pico_stdlib)
pico_enable_stdio_uart(eh_smoke 0)
pico_enable_stdio_usb(eh_smoke 0)
pico_add_extra_outputs(eh_smoke)
