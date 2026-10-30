/* Frame inspection helpers shared by the tests. */
#ifndef EH_TEST_UTIL_H
#define EH_TEST_UTIL_H

#include "eh/eh_types.h"

/* Panel pixel (px, py) of the 360x360 display is glass iff its centre lies
 * within radius 180; this is the presentation mask. */
static inline int eh_test_panel_inside(int px, int py)
{
    int dx = 2 * px + 1 - EH_LCD_W, dy = 2 * py + 1 - EH_LCD_H;
    return dx * dx + dy * dy <= EH_LCD_W * EH_LCD_W;
}

/* The framebuffer is native resolution: pixel (x, y) is visible iff the
 * panel pixel it is presented on is glass. */
static inline int eh_test_inside(int x, int y) { return eh_test_panel_inside(x, y); }

static inline unsigned eh_test_lum(eh_rgb565 c)
{
    eh_rgb p = eh_rgb565_unpack(c);
    return (p.r * 77u + p.g * 150u + p.b * 29u) >> 8;
}

/* Fraction of visible (in-circle) pixels that differ between frames. */
double eh_test_diff_fraction(const eh_rgb565 *a, const eh_rgb565 *b);
/* Mean luminance of visible pixels whose centre lies in [r0, r1) from centre. */
double eh_test_mean_lum_ring(const eh_rgb565 *fb, double r0, double r1);
/* Number of distinct colours among visible pixels. */
int eh_test_distinct_colors(const eh_rgb565 *fb);

#endif
