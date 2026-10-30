#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eh_present.h"
#include "eh_test.h"

static eh_rgb565 fb[EH_FB_PIXELS];
static uint8_t canvas[EH_PRESENT_W * EH_PRESENT_H * 3];

static const uint8_t *at(int x, int y) { return &canvas[(y * EH_PRESENT_W + x) * 3]; }

static int is_rgb(const uint8_t *p, int r, int g, int b) { return p[0] == r && p[1] == g && p[2] == b; }

void test_present_circle_mask_and_mapping(void)
{
    eh_rgb leds[EH_LED_COUNT] = {{0, 0, 0}};
    memset(fb, 0, sizeof fb);
    fb[50 * EH_FB_W + 100] = 0xF800u;
    for (int i = 0; i < EH_FB_PIXELS; i++)
        if (i != 50 * EH_FB_W + 100)
            fb[i] = 0xFFFFu;
    eh_present(fb, leds, 64, canvas);

    int lx = EH_PRESENT_LCD_X, ly = EH_PRESENT_LCD_Y;
    /* Panel centre shows the framebuffer; corners of the panel square are
     * outside the round glass and show bezel. */
    EH_CHECK(is_rgb(at(lx + 180, ly + 180), 255, 255, 255));
    EH_CHECK(is_rgb(at(lx + 2, ly + 2), EH_PRESENT_BEZEL_R, EH_PRESENT_BEZEL_G, EH_PRESENT_BEZEL_B));
    EH_CHECK(is_rgb(at(lx + 357, ly + 357), EH_PRESENT_BEZEL_R, EH_PRESENT_BEZEL_G,
                    EH_PRESENT_BEZEL_B));
    /* 1:1: fb (100, 50) is exactly panel (100, 50), and its neighbours are not red. */
    EH_CHECK(is_rgb(at(lx + 100, ly + 50), 255, 0, 0));
    EH_CHECK(is_rgb(at(lx + 101, ly + 50), 255, 255, 255));
    EH_CHECK(is_rgb(at(lx + 99, ly + 50), 255, 255, 255));
    EH_CHECK(is_rgb(at(lx + 100, ly + 51), 255, 255, 255));
    EH_CHECK(is_rgb(at(lx + 100, ly + 49), 255, 255, 255));
}

void test_present_leds_drawn_with_gain(void)
{
    eh_rgb leds[EH_LED_COUNT];
    for (int k = 0; k < EH_LED_COUNT; k++)
        leds[k] = (eh_rgb){0, 0, 0};
    leds[0] = (eh_rgb){64, 0, 0};
    leds[4] = (eh_rgb){0, 32, 0};
    memset(fb, 0, sizeof fb);
    eh_present(fb, leds, 64, canvas);

    int x, y;
    eh_present_led_center(0, &x, &y);
    EH_CHECK(y < EH_PRESENT_LCD_Y && x == EH_PRESENT_W / 2);
    EH_CHECK(is_rgb(at(x, y), 255, 0, 0));
    eh_present_led_center(4, &x, &y);
    EH_CHECK(y > EH_PRESENT_H / 2);
    EH_CHECK_MSG(at(x, y)[1] >= 126 && at(x, y)[1] <= 128, "g=%d", at(x, y)[1]);
    eh_present_led_center(1, &x, &y);
    EH_CHECK(x > EH_PRESENT_W / 2 && y < EH_PRESENT_H / 2); /* clockwise from top */
}

void test_present_ppm_write(void)
{
    memset(canvas, 7, sizeof canvas);
    char path[] = "/tmp/eh_test_ppm_XXXXXX";
    int fd = mkstemp(path);
    EH_CHECK(fd >= 0);
    if (fd < 0)
        return;
    EH_CHECK(eh_ppm_write(path, canvas, EH_PRESENT_W, EH_PRESENT_H) == 0);
    FILE *f = fopen(path, "rb");
    char header[32] = {0};
    size_t n = fread(header, 1, 15, f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    remove(path);
    EH_CHECK(n == 15 && memcmp(header, "P6\n440 440\n255\n", 15) == 0);
    EH_CHECK(size == 15 + EH_PRESENT_W * EH_PRESENT_H * 3);
}
