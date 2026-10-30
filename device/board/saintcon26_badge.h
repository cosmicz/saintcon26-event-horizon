/* Pico SDK board header for the SAINTCON 2026 badge (RP2350A, 16 MiB QSPI
 * flash). Selected with -DPICO_BOARD=saintcon26_badge and
 * PICO_BOARD_HEADER_DIRS pointing here.
 *
 * Deliberately omitted, unlike the SDK's pico2.h: PICO_DEFAULT_I2C* (GPIO4/5
 * is stock's EEPROM bus), PICO_SMPS_MODE_PIN (GPIO23 is D-pad up here),
 * PICO_VBUS_PIN (GPIO24 drives the RGB LEDs), PICO_DEFAULT_LED_PIN and the
 * default SPI pins. Nothing in the SDK should reach for those pins on our
 * behalf. */
#ifndef _BOARDS_SAINTCON26_BADGE_H
#define _BOARDS_SAINTCON26_BADGE_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

#define SAINTCON26_BADGE
#define PICO_RP2350A 1

#include "eh_badge_pins.h"

/* Dock UART0 (GPIO0 TX / GPIO1 RX). stdio stays disabled in our images. */
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif

#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

/* No A2-silicon workarounds. With A2 support on, picotool's UF2 output adds
 * an absolute-family block in the last flash page (device/README.org);
 * the badge silicon is reported as A4 (not verified by us). */
pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 0)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 0
#endif

#endif
