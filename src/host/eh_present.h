/* Host presentation adapter: composes the native 360x360 framebuffer onto a
 * simulated round panel (1:1, circular mask) with
 * the nine LEDs drawn around the bezel. The SDL preview and the headless
 * frame dump both use this, so a dumped PPM is exactly what the window shows. */
#ifndef EH_HOST_PRESENT_H
#define EH_HOST_PRESENT_H

#include <stdint.h>

#include "eh/eh_types.h"

#define EH_PRESENT_W 440
#define EH_PRESENT_H 440
#define EH_PRESENT_LCD_X ((EH_PRESENT_W - EH_LCD_W) / 2)
#define EH_PRESENT_LCD_Y ((EH_PRESENT_H - EH_LCD_H) / 2)
#define EH_PRESENT_LED_RING 202 /* LED centre distance from canvas centre, px */
#define EH_PRESENT_LED_DOT 10   /* LED dot radius, px */

/* Background outside the panel (bezel). */
#define EH_PRESENT_BEZEL_R 20
#define EH_PRESENT_BEZEL_G 20
#define EH_PRESENT_BEZEL_B 24

/* rgb is EH_PRESENT_W * EH_PRESENT_H * 3 bytes, row-major RGB888. LED values
 * are brightness-capped by the model; led_max is that cap, and the preview
 * scales LEDs back up by 255/led_max so hue stays visible on screen. */
void eh_present(const eh_rgb565 *fb, const eh_rgb leds[EH_LED_COUNT], uint8_t led_max,
                uint8_t *rgb);

void eh_present_led_center(int led, int *x, int *y);

/* Binary PPM (P6). Returns 0 on success. */
int eh_ppm_write(const char *path, const uint8_t *rgb, int w, int h);

#endif
