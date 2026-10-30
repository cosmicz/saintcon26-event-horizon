/* Shared display, colour and input contract for the EVENT HORIZON core.
 *
 * The core renders into a caller-owned framebuffer at the panel's native
 * 360x360 RGB565 (259,200 bytes, ~49% of RP2350 SRAM). v1 rendered 180x180
 * and pixel-doubled; native resolution is the largest single RAM block and
 * the most visible quality gain. Only the round area is glass; the adapter
 * applies the circular mask. */
#ifndef EH_TYPES_H
#define EH_TYPES_H

#include <stdint.h>

#define EH_FB_W 360
#define EH_FB_H 360
#define EH_FB_PIXELS (EH_FB_W * EH_FB_H)

#define EH_LCD_W 360
#define EH_LCD_H 360

/* Nine WS2812 LEDs per the community hardware notes; physical placement on
 * the badge is unverified, so adapters treat it as a display choice. */
#define EH_LED_COUNT 9

typedef uint16_t eh_rgb565;

typedef struct {
    uint8_t r, g, b;
} eh_rgb;

static inline eh_rgb565 eh_rgb565_pack(unsigned r, unsigned g, unsigned b)
{
    return (eh_rgb565)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

/* Expand with bit replication so 0x1F maps to 0xFF and 0 to 0. */
static inline eh_rgb eh_rgb565_unpack(eh_rgb565 c)
{
    unsigned r5 = (c >> 11) & 0x1Fu, g6 = (c >> 5) & 0x3Fu, b5 = c & 0x1Fu;
    eh_rgb out = {(uint8_t)((r5 << 3) | (r5 >> 2)), (uint8_t)((g6 << 2) | (g6 >> 4)),
                  (uint8_t)((b5 << 3) | (b5 >> 2))};
    return out;
}

/* Button bitmask. Five-way D-pad plus A/B as reported by community notes;
 * down/A/B were not hardware-confirmed there, so mapping stays provisional. */
enum eh_button {
    EH_BTN_UP = 1u << 0,
    EH_BTN_DOWN = 1u << 1,
    EH_BTN_LEFT = 1u << 2,
    EH_BTN_RIGHT = 1u << 3,
    EH_BTN_CENTER = 1u << 4,
    EH_BTN_A = 1u << 5,
    EH_BTN_B = 1u << 6,
};

#endif
