#include <math.h>
#include <stdlib.h>

#include "eh/eh_tables.h"
#include "eh_test.h"
#include "test_util.h"

void test_sin8(void)
{
    for (int i = 0; i < 256; i++) {
        double ref = 127.5 + 127.5 * sin(2.0 * M_PI * i / 256.0);
        EH_CHECK_MSG(fabs(eh_sin8[i] - ref) <= 1.0, "i=%d got=%d ref=%.2f", i, eh_sin8[i], ref);
    }
    EH_CHECK(eh_sin8[64] == 255);
    EH_CHECK(eh_sin8[192] == 0);
}

void test_circle_spans(void)
{
    int mismatches = 0;
    for (int y = 0; y < EH_FB_H; y++) {
        EH_CHECK(eh_span_x0[y] <= eh_span_x1[y]);
        EH_CHECK(eh_span_x1[y] <= EH_FB_W);
        for (int x = 0; x < EH_FB_W; x++) {
            int in_span = x >= eh_span_x0[y] && x < eh_span_x1[y];
            mismatches += in_span != eh_test_inside(x, y);
        }
    }
    EH_CHECK_MSG(mismatches == 0, "%d pixels disagree with the circle definition", mismatches);
    EH_CHECK(eh_span_x0[180] == 0 && eh_span_x1[180] == EH_FB_W);
    EH_CHECK(eh_span_x1[0] - eh_span_x0[0] < 40);
}

void test_spans_cover_every_displayed_pixel(void)
{
    /* Coverage contract between renderers and presentation: every panel
     * pixel the mask shows must read a framebuffer cell renderers write. */
    int uncovered = 0;
    for (int py = 0; py < EH_LCD_H; py++)
        for (int px = 0; px < EH_LCD_W; px++) {
            if (!eh_test_panel_inside(px, py))
                continue;
            const int x = px, y = py; /* presented 1:1 */
            uncovered += !(x >= eh_span_x0[y] && x < eh_span_x1[y]);
        }
    EH_CHECK_MSG(uncovered == 0, "%d displayed panel pixels read unwritten cells", uncovered);
}

void test_palettes_cyclic_and_distinct(void)
{
    for (int p = 0; p < EH_PALETTE_COUNT; p++)
        for (int i = 0; i < 256; i++) {
            eh_rgb a = eh_rgb565_unpack(eh_palettes[p][i]);
            eh_rgb b = eh_rgb565_unpack(eh_palettes[p][(i + 1) & 255]);
            int jump = abs(a.r - b.r) + abs(a.g - b.g) + abs(a.b - b.b);
            EH_CHECK_MSG(jump <= 48, "palette %d step %d jumps by %d", p, i, jump);
        }
    for (int p = 1; p < EH_PALETTE_COUNT; p++) {
        int same = 0;
        for (int i = 0; i < 256; i++)
            same += eh_palettes[0][i] == eh_palettes[p][i];
        EH_CHECK_MSG(same < 32, "palette %d nearly equals palette 0", p);
    }
}

void test_led_samples_on_rim_clockwise(void)
{
    double prev_angle = -1e9;
    for (int k = 0; k < EH_LED_COUNT; k++) {
        double cx = 0, cy = 0;
        for (int s = 0; s < EH_LED_SAMPLES; s++) {
            int idx = eh_led_samples[k][s];
            int x = idx % EH_FB_W, y = idx / EH_FB_W;
            EH_CHECK(idx < EH_FB_PIXELS);
            EH_CHECK(eh_test_inside(x, y));
            double r = hypot(x + 0.5 - 180.0, y + 0.5 - 180.0);
            EH_CHECK_MSG(r > 130.0 && r < 180.0, "led %d sample radius %.1f", k, r);
            cx += x + 0.5 - 180.0;
            cy += y + 0.5 - 180.0;
        }
        /* Screen angle with y down; LED 0 at top (-pi/2) increasing clockwise. */
        double a = atan2(cy, cx);
        if (k == 0)
            EH_CHECK_MSG(fabs(a + M_PI / 2) < 0.1, "led 0 angle %.3f", a);
        if (a < prev_angle - 1e-9)
            a += 2 * M_PI;
        EH_CHECK_MSG(a > prev_angle, "led %d not clockwise of led %d", k, k - 1);
        prev_angle = a;
    }
}
