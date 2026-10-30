#include "eh/eh_leds.h"
#include "eh/eh_tables.h"
#include "eh_test.h"

static eh_rgb565 fb[EH_FB_PIXELS];

void test_leds_solid_and_cap(void)
{
    eh_rgb out[EH_LED_COUNT];
    for (int i = 0; i < EH_FB_PIXELS; i++)
        fb[i] = 0xF800u;
    eh_leds_from_frame(fb, out, 255);
    for (int k = 0; k < EH_LED_COUNT; k++)
        EH_CHECK(out[k].r == 255 && out[k].g == 0 && out[k].b == 0);
    eh_leds_from_frame(fb, out, EH_LED_DEFAULT_MAX);
    for (int k = 0; k < EH_LED_COUNT; k++)
        EH_CHECK_MSG(out[k].r == EH_LED_DEFAULT_MAX, "led %d r=%d", k, out[k].r);

    for (int i = 0; i < EH_FB_PIXELS; i++)
        fb[i] = 0;
    eh_leds_from_frame(fb, out, 255);
    for (int k = 0; k < EH_LED_COUNT; k++)
        EH_CHECK(out[k].r == 0 && out[k].g == 0 && out[k].b == 0);
}

void test_leds_follow_local_colour(void)
{
    /* Left half blue, right half green: each LED takes its side's colour. */
    for (int y = 0; y < EH_FB_H; y++)
        for (int x = 0; x < EH_FB_W; x++)
            fb[y * EH_FB_W + x] = x < EH_FB_W / 2 ? 0x001Fu : 0x07E0u;
    eh_rgb out[EH_LED_COUNT];
    eh_leds_from_frame(fb, out, 255);
    int checked = 0;
    for (int k = 0; k < EH_LED_COUNT; k++) {
        int cx = 0;
        for (int s = 0; s < EH_LED_SAMPLES; s++)
            cx += eh_led_samples[k][s] % EH_FB_W;
        cx /= EH_LED_SAMPLES;
        if (cx < 150) {
            EH_CHECK_MSG(out[k].b > 200 && out[k].g < 50, "led %d", k);
            checked++;
        } else if (cx > 210) {
            EH_CHECK_MSG(out[k].g > 200 && out[k].b < 50, "led %d", k);
            checked++;
        }
    }
    EH_CHECK(checked >= 6);
}
