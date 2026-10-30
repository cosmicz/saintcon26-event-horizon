#include "eh_present.h"

#include <math.h>
#include <stdio.h>

/* Unlit LED housing colour, so LED positions stay visible when dark. */
#define HOUSING 36

void eh_present_led_center(int led, int *x, int *y)
{
    const double a = -M_PI / 2 + 2 * M_PI * led / EH_LED_COUNT;
    *x = (int)lround(EH_PRESENT_W / 2.0 + EH_PRESENT_LED_RING * cos(a));
    *y = (int)lround(EH_PRESENT_H / 2.0 + EH_PRESENT_LED_RING * sin(a));
}

static void put(uint8_t *rgb, int x, int y, unsigned r, unsigned g, unsigned b)
{
    if (x < 0 || x >= EH_PRESENT_W || y < 0 || y >= EH_PRESENT_H)
        return;
    uint8_t *p = &rgb[(y * EH_PRESENT_W + x) * 3];
    p[0] = (uint8_t)r;
    p[1] = (uint8_t)g;
    p[2] = (uint8_t)b;
}

static unsigned gain(unsigned v, uint8_t led_max)
{
    if (led_max == 0)
        return 0;
    const unsigned g = v * 255u / led_max;
    return g > 255u ? 255u : g;
}

void eh_present(const eh_rgb565 *fb, const eh_rgb leds[EH_LED_COUNT], uint8_t led_max,
                uint8_t *rgb)
{
    for (int i = 0; i < EH_PRESENT_W * EH_PRESENT_H; i++) {
        rgb[i * 3 + 0] = EH_PRESENT_BEZEL_R;
        rgb[i * 3 + 1] = EH_PRESENT_BEZEL_G;
        rgb[i * 3 + 2] = EH_PRESENT_BEZEL_B;
    }

    /* Round panel, 1:1 with the native framebuffer; pixels whose centre is
     * outside radius 180 are not glass and keep the bezel colour. */
    for (int py = 0; py < EH_LCD_H; py++) {
        const int dy = 2 * py + 1 - EH_LCD_H;
        for (int px = 0; px < EH_LCD_W; px++) {
            const int dx = 2 * px + 1 - EH_LCD_W;
            if (dx * dx + dy * dy > EH_LCD_W * EH_LCD_W)
                continue;
            const eh_rgb c = eh_rgb565_unpack(fb[py * EH_FB_W + px]);
            put(rgb, EH_PRESENT_LCD_X + px, EH_PRESENT_LCD_Y + py, c.r, c.g, c.b);
        }
    }

    for (int k = 0; k < EH_LED_COUNT; k++) {
        int cx, cy;
        eh_present_led_center(k, &cx, &cy);
        const int lit = leds[k].r || leds[k].g || leds[k].b;
        const unsigned r = lit ? gain(leds[k].r, led_max) : HOUSING;
        const unsigned g = lit ? gain(leds[k].g, led_max) : HOUSING;
        const unsigned b = lit ? gain(leds[k].b, led_max) : HOUSING;
        const int rad = EH_PRESENT_LED_DOT;
        for (int y = -rad; y <= rad; y++)
            for (int x = -rad; x <= rad; x++)
                if (x * x + y * y <= rad * rad)
                    put(rgb, cx + x, cy + y, r, g, b);
    }
}

int eh_ppm_write(const char *path, const uint8_t *rgb, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    const size_t n = (size_t)w * (size_t)h * 3;
    int ok = fprintf(f, "P6\n%d %d\n255\n", w, h) > 0 && fwrite(rgb, 1, n, f) == n;
    ok = (fclose(f) == 0) && ok;
    return ok ? 0 : -1;
}
