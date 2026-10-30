#include "test_util.h"

#include <math.h>
#include <string.h>

double eh_test_diff_fraction(const eh_rgb565 *a, const eh_rgb565 *b)
{
    int visible = 0, differ = 0;
    for (int y = 0; y < EH_FB_H; y++)
        for (int x = 0; x < EH_FB_W; x++) {
            if (!eh_test_inside(x, y))
                continue;
            visible++;
            differ += a[y * EH_FB_W + x] != b[y * EH_FB_W + x];
        }
    return visible ? (double)differ / visible : 0.0;
}

double eh_test_mean_lum_ring(const eh_rgb565 *fb, double r0, double r1)
{
    double sum = 0.0;
    int n = 0;
    for (int y = 0; y < EH_FB_H; y++)
        for (int x = 0; x < EH_FB_W; x++) {
            double r = hypot(x + 0.5 - EH_FB_W / 2.0, y + 0.5 - EH_FB_H / 2.0);
            if (!eh_test_inside(x, y) || r < r0 || r >= r1)
                continue;
            sum += eh_test_lum(fb[y * EH_FB_W + x]);
            n++;
        }
    return n ? sum / n : 0.0;
}

int eh_test_distinct_colors(const eh_rgb565 *fb)
{
    static unsigned char seen[65536];
    memset(seen, 0, sizeof seen);
    int n = 0;
    for (int y = 0; y < EH_FB_H; y++)
        for (int x = 0; x < EH_FB_W; x++) {
            if (!eh_test_inside(x, y))
                continue;
            eh_rgb565 c = fb[y * EH_FB_W + x];
            if (!seen[c]) {
                seen[c] = 1;
                n++;
            }
        }
    return n;
}
