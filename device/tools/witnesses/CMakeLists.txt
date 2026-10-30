# NEGATIVE WITNESSES for device/tools/image_audit.py: never flash these. Each
# contains exactly one forbidden pattern, and the audit must fail it on that
# check alone (device/dualboot/check.sh verifies the failing check names).
cmake_minimum_required(VERSION 3.20)
set(EH_REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(PICO_PLATFORM rp2350-arm-s)
set(PICO_BOARD saintcon26_badge)
set(PICO_BOARD_HEADER_DIRS ${EH_REPO_ROOT}/device/board)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(eh_witnesses C CXX ASM)
pico_sdk_init()

foreach(w rom i2c gpio45)
  add_executable(witness_${w} witness_${w}.c)
  target_link_libraries(witness_${w} pico_runtime pico_time hardware_gpio)
  pico_enable_stdio_uart(witness_${w} 0)
  pico_enable_stdio_usb(witness_${w} 0)
  pico_add_bin_output(witness_${w})
endforeach()
target_link_libraries(witness_rom hardware_flash)
target_link_libraries(witness_i2c hardware_i2c)
