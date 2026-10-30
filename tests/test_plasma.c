#include <string.h>

#include "eh/eh_plasma.h"
#include "eh_test.h"
#include "test_util.h"

#define SENTINEL 0xBEEFu

static eh_rgb565 fa[EH_FB_PIXELS], fb2[EH_FB_PIXELS];

static void fill(eh_rgb565 *fb, eh_rgb565 v)
{
    for (int i = 0; i < EH_FB_PIXELS; i++)
        fb[i] = v;
}

void test_plasma_deterministic_and_animated(void)
{
    eh_plasma_render(fa, 1234, 0, 0, EH_FB_H, 1);
    eh_plasma_render(fb2, 1234, 0, 0, EH_FB_H, 1);
    EH_CHECK(memcmp(fa, fb2, sizeof fa) == 0);

    eh_plasma_render(fb2, 1734, 0, 0, EH_FB_H, 1);
    double moved = eh_test_diff_fraction(fa, fb2);
    EH_CHECK_MSG(moved > 0.5, "only %.2f of pixels changed over 500 ms", moved);
    EH_CHECK_MSG(eh_test_distinct_colors(fa) >= 64, "flat frame: %d colours",
                 eh_test_distinct_colors(fa));
}

void test_plasma_circle_clip(void)
{
    fill(fa, SENTINEL);
    eh_plasma_render(fa, 500, 0, 0, EH_FB_H, 1);
    int outside_written = 0, inside_sentinel = 0, inside = 0;
    for (int y = 0; y < EH_FB_H; y++)
        for (int x = 0; x < EH_FB_W; x++) {
            int s = fa[y * EH_FB_W + x] == SENTINEL;
            if (eh_test_inside(x, y)) {
                inside++;
                inside_sentinel += s;
            } else {
                outside_written += !s;
            }
        }
    EH_CHECK_MSG(outside_written == 0, "%d pixels outside the circle written", outside_written);
    EH_CHECK(inside_sentinel * 100 < inside);

    fill(fb2, SENTINEL);
    eh_plasma_render(fb2, 500, 0, 0, EH_FB_H, 0);
    int left = 0;
    for (int i = 0; i < EH_FB_PIXELS; i++)
        left += fb2[i] == SENTINEL;
    EH_CHECK(left * 100 < EH_FB_PIXELS);
    /* Clipping changes only which pixels are written, never their values. */
    EH_CHECK(eh_test_diff_fraction(fa, fb2) == 0.0);
}

void test_plasma_bands_compose_exactly(void)
{
    fill(fa, 0);
    eh_plasma_render(fa, 777, 2, 0, EH_FB_H, 1);
    fill(fb2, 0);
    eh_plasma_render(fb2, 777, 2, 0, 180, 1);
    eh_plasma_render(fb2, 777, 2, 180, EH_FB_H, 1);
    EH_CHECK(memcmp(fa, fb2, sizeof fa) == 0);

    fill(fb2, 0);
    eh_plasma_render(fb2, 777, 2, 0, 73, 1);
    eh_plasma_render(fb2, 777, 2, 73, 241, 1);
    eh_plasma_render(fb2, 777, 2, 241, EH_FB_H, 1);
    EH_CHECK(memcmp(fa, fb2, sizeof fa) == 0);

    fill(fb2, SENTINEL);
    eh_plasma_render(fb2, 777, 2, 0, 180, 0);
    int below = 0;
    for (int i = 180 * EH_FB_W; i < EH_FB_PIXELS; i++)
        below += fb2[i] != SENTINEL;
    EH_CHECK_MSG(below == 0, "band [0,180) wrote %d pixels below row 180", below);

    fill(fb2, SENTINEL);
    eh_plasma_render(fb2, 777, 2, 180, EH_FB_H, 0);
    int above = 0;
    for (int i = 0; i < 180 * EH_FB_W; i++)
        above += fb2[i] != SENTINEL;
    EH_CHECK_MSG(above == 0, "band [180,360) wrote %d pixels above row 180", above);
}

void test_plasma_palette_select(void)
{
    eh_plasma_render(fa, 900, 0, 0, EH_FB_H, 1);
    eh_plasma_render(fb2, 900, 1, 0, EH_FB_H, 1);
    EH_CHECK(eh_test_diff_fraction(fa, fb2) > 0.9);
}
