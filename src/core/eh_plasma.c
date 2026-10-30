/* Fixed-point plasma: four sine fields summed and mapped through a cyclic
 * palette.
 *
 * Spatial frequencies match v1's 180-px frame, now sampled at every native
 * pixel. Per call the x and diagonal fields are tabulated once (360 + 720
 * bytes of stack); per row the y field is one lookup; per pixel the cost is three
 * table reads, an incremental squared distance for the moving radial field,
 * one sine lookup and one palette lookup. Phase rates use shifts so the frame
 * is a pure integer function of t_ms, identical on any host or core. */
#include "eh/eh_plasma.h"

#include "eh/eh_tables.h"

void eh_plasma_render(eh_rgb565 *fb, uint32_t t_ms, unsigned palette, int y0, int y1,
                      int clip_circle)
{
    const eh_rgb565 *pal = eh_palettes[palette % EH_PALETTE_COUNT];
    const unsigned p1 = (t_ms * 3u) >> 5, p2 = (t_ms * 5u) >> 6, p3 = (t_ms * 7u) >> 7,
                   p4 = (t_ms * 3u) >> 7, shift = (t_ms >> 4) & 255u;

    uint8_t xt[EH_FB_W], dg[EH_FB_W + EH_FB_H];
    for (int x = 0; x < EH_FB_W; x++)
        xt[x] = eh_sin8[((unsigned)x + p1) & 255u];
    for (int s = 0; s < EH_FB_W + EH_FB_H; s++)
        dg[s] = eh_sin8[(((unsigned)s * 3u) / 4u + p3) & 255u];

    /* Radial field centre wanders by up to ~85 px around the middle. */
    const int cx = EH_FB_W / 2 + ((int)eh_sin8[p4 & 255u] - 128) * 2 / 3;
    const int cy = EH_FB_H / 2 + ((int)eh_sin8[(p4 + 64u) & 255u] - 128) * 2 / 3;

    if (y0 < 0)
        y0 = 0;
    if (y1 > EH_FB_H)
        y1 = EH_FB_H;
    for (int y = y0; y < y1; y++) {
        const int x0 = clip_circle ? eh_span_x0[y] : 0;
        const int x1 = clip_circle ? eh_span_x1[y] : EH_FB_W;
        const unsigned yt = eh_sin8[(((unsigned)y * 3u) / 2u + p2) & 255u];
        const int dy = y - cy;
        int dx = x0 - cx;
        int d2 = dx * dx + dy * dy;
        eh_rgb565 *row = fb + y * EH_FB_W;
        for (int x = x0; x < x1; x++) {
            unsigned v = xt[x] + yt + dg[x + y] + eh_sin8[((unsigned)(d2 >> 7) - p1 * 2u) & 255u];
            row[x] = pal[((v >> 2) + shift) & 255u];
            d2 += 2 * dx + 1;
            dx++;
        }
    }
}
