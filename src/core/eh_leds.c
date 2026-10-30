#include "eh/eh_leds.h"

#include "eh/eh_tables.h"

void eh_leds_from_frame(const eh_rgb565 *fb, eh_rgb out[EH_LED_COUNT], uint8_t max_brightness)
{
    for (int k = 0; k < EH_LED_COUNT; k++) {
        unsigned r = 0, g = 0, b = 0;
        for (int s = 0; s < EH_LED_SAMPLES; s++) {
            eh_rgb p = eh_rgb565_unpack(fb[eh_led_samples[k][s]]);
            r += p.r;
            g += p.g;
            b += p.b;
        }
        r = (r + EH_LED_SAMPLES / 2) / EH_LED_SAMPLES;
        g = (g + EH_LED_SAMPLES / 2) / EH_LED_SAMPLES;
        b = (b + EH_LED_SAMPLES / 2) / EH_LED_SAMPLES;
        out[k].r = (uint8_t)((r * max_brightness + 127u) / 255u);
        out[k].g = (uint8_t)((g * max_brightness + 127u) / 255u);
        out[k].b = (uint8_t)((b * max_brightness + 127u) / 255u);
    }
}
