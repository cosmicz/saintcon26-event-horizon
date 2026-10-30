/* SAINTCON 2026 badge pin map, as plain C so host tests can include it.
 *
 * Source: redactd's hardware notes in saintcon26-gif-player (commit
 * 9726dad5). The button map was independently confirmed by static analysis
 * of the stock firmware's button-mask routine. The display pins and RIGHT
 * have since worked on hardware; the other keys were not checked one by one,
 * and the LED colour order is still assumed.
 *
 * EH_BADGE_EEPROM_* are listed only so audits and asserts can name them:
 * nothing we build may configure them. */
#ifndef EH_BADGE_PINS_H
#define EH_BADGE_PINS_H

/* Buttons: active low. */
#define EH_BADGE_BTN_B_PIN      16
#define EH_BADGE_BTN_A_PIN      17
#define EH_BADGE_BTN_RIGHT_PIN  19
#define EH_BADGE_BTN_CENTRE_PIN 20
#define EH_BADGE_BTN_DOWN_PIN   21
#define EH_BADGE_BTN_LEFT_PIN   22
#define EH_BADGE_BTN_UP_PIN     23

/* ST77916 360x360 panel on four-bit QSPI. */
#define EH_BADGE_LCD_BACKLIGHT_PIN 7
#define EH_BADGE_LCD_RESET_PIN     9
#define EH_BADGE_LCD_SCLK_PIN      10
#define EH_BADGE_LCD_D0_PIN        11 /* D0..D3 = GPIO11..14 */
#define EH_BADGE_LCD_CS_PIN        15

/* Nine WS2812 LEDs. */
#define EH_BADGE_RGB_LED_PIN 24

/* Stock's 1 KiB EEPROM (I2C0, 0x50-0x53). Never configured by our images. */
#define EH_BADGE_EEPROM_SDA_PIN 4
#define EH_BADGE_EEPROM_SCL_PIN 5

#endif
