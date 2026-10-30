#include "eh/eh_scenes.h"
#include "eh/eh_tables.h"

void eh_fade_rect(eh_rgb565 *fb, eh_rgb565 color, unsigned amount, int x0, int x1, int y0,
                  int y1)
{
    if (amount == 0)
        return;
    if (amount > 256)
        amount = 256;
    const unsigned keep = 256 - amount;
    const unsigned tr = (color >> 11) * amount, tg = ((color >> 5) & 0x3Fu) * amount,
                   tb = (color & 0x1Fu) * amount;
    for (int y = y0; y < y1; y++) {
        const int lo = x0 > eh_span_x0[y] ? x0 : eh_span_x0[y];
        const int hi = x1 < eh_span_x1[y] ? x1 : eh_span_x1[y];
        for (eh_rgb565 *p = fb + y * EH_FB_W + lo, *end = fb + y * EH_FB_W + hi; p < end; p++) {
            const unsigned c = *p;
            const unsigned r = ((c >> 11) * keep + tr) >> 8;
            const unsigned g = (((c >> 5) & 0x3Fu) * keep + tg) >> 8;
            const unsigned b = ((c & 0x1Fu) * keep + tb) >> 8;
            *p = (eh_rgb565)((r << 11) | (g << 5) | b);
        }
    }
}
