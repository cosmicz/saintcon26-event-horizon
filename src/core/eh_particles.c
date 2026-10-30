/* Scene 4: the last terrain frame shatters into particles that rebuild a
 * logo. Particle position is an analytic function of scene time (explode
 * with drag, then a staggered smoothstep to the target), so any frame can be
 * rendered directly without integrating state. The camera rotates the whole
 * cloud about the display centre.
 *
 * Motion streaks come from the same analytic path: each particle draws a
 * fading segment from where it was EH_TRAIL_MS ago to where it is now, so
 * trails cost no history buffer and stay seekable. */
#include <math.h>

#include "eh/eh_scenes.h"
#include "eh/eh_tables.h"
#include "eh_math.h"

#define EXPLODE_DRAG 2.5f
#define REBUILD_START_S 1.0f
#define REBUILD_STAGGER_S 2.0f
#define REBUILD_S 2.5f
#define FADE_OUT_MS 1000u
#define VIEW_DIST 440.0f
#define DISC_RADIUS 176.0f
#define TRAIL_S 0.08f
#define OFFSCREEN INT16_MIN
#define SUB 16 /* segment coordinates are 1/16 px */

/* 5x7 block glyphs, one row per byte, bit 4 = leftmost column. */
static const struct {
    char ch;
    uint8_t rows[7];
} glyphs[] = {
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'C', {0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {' ', {0, 0, 0, 0, 0, 0, 0}},
};

static const uint8_t *glyph(char ch)
{
    for (unsigned i = 0; i < sizeof glyphs / sizeof glyphs[0]; i++)
        if (glyphs[i].ch == ch)
            return glyphs[i].rows;
    return 0;
}

static int emit_text(const char *s, int scale, int top, uint16_t *tx, uint16_t *ty, int n, int max)
{
    int len = 0;
    while (s[len])
        len++;
    const int width = len * 5 * scale + (len - 1) * scale;
    const int left = (EH_FB_W - width) / 2;
    for (int c = 0; c < len; c++) {
        const uint8_t *rows = glyph(s[c]);
        if (!rows)
            continue;
        for (int gy = 0; gy < 7 * scale; gy++)
            for (int gx = 0; gx < 5 * scale; gx++) {
                if (!(rows[gy / scale] & (0x10 >> (gx / scale))))
                    continue;
                if (n < max) {
                    tx[n] = (uint16_t)(left + c * 6 * scale + gx);
                    ty[n] = (uint16_t)(top + gy);
                }
                n++;
            }
    }
    return n;
}

/* End screen (2026-10-29): "EVENT HORIZON" over "SAINTCON 2026", both at
 * scale 4 (308 px wide, rows 140-167 and 192-219), so every corner stays
 * >= 21 px inside the 180 px panel circle. */
int eh_logo_targets(uint16_t *tx, uint16_t *ty, int max)
{
    int n = emit_text("EVENT HORIZON", 4, 140, tx, ty, 0, max);
    n = emit_text("SAINTCON 2026", 4, 192, tx, ty, n, max);
    return n < max ? n : max;
}

void eh_particles_init(eh_particles_ws *ws, const eh_rgb565 *source_fb)
{
    ws->ntargets = eh_logo_targets(ws->tx, ws->ty, EH_LOGO_MAX_TARGETS);
    const float golden = 2.39996323f;
    for (int i = 0; i < EH_PARTICLE_COUNT; i++) {
        /* Vogel spiral: even coverage of the disc the particles shatter from. */
        const float r = DISC_RADIUS * sqrtf((i + 0.5f) / EH_PARTICLE_COUNT);
        const float a = i * golden;
        const float x = EH_FB_W / 2.0f + r * cosf(a), y = EH_FB_H / 2.0f + r * sinf(a);
        ws->sx[i] = (int16_t)lrintf(x * SUB);
        ws->sy[i] = (int16_t)lrintf(y * SUB);
        ws->scol[i] = source_fb[(int)y * EH_FB_W + (int)x];
        /* 7919 is prime and does not divide ntargets, so i * 7919 mod
         * ntargets is a bijection over the first ntargets particles: every
         * target is covered before any repeats. */
        ws->target[i] = (uint16_t)((uint32_t)i * 7919u % (uint32_t)ws->ntargets);
    }
}

static eh_rgb565 lerp565(eh_rgb565 a, eh_rgb565 b, unsigned w)
{
    const unsigned k = 256 - w;
    const unsigned r = ((a >> 11) * k + (b >> 11) * w) >> 8;
    const unsigned g = (((a >> 5) & 0x3Fu) * k + ((b >> 5) & 0x3Fu) * w) >> 8;
    const unsigned bl = ((a & 0x1Fu) * k + (b & 0x1Fu) * w) >> 8;
    return (eh_rgb565)((r << 11) | (g << 5) | bl);
}

void eh_particles_prepare(eh_particles_ws *ws, uint32_t local_ms, uint32_t duration_ms,
                          const eh_camera *cam)
{
    ws->local_ms = local_ms;
    ws->palette = cam->palette;
    ws->cos_yaw = cosf(cam->yaw);
    ws->sin_yaw = sinf(cam->yaw);
    ws->cos_pitch = cosf(cam->pitch);
    ws->sin_pitch = sinf(cam->pitch);
    /* Large fragments at first so the source frame reads as a mosaic that
     * breaks apart, shrinking to single pixels as it scatters. */
    ws->size = local_ms < 250u ? 5 : local_ms < 600u ? 3 : local_ms < 1000u ? 2 : 1;
    ws->fade_black = local_ms + FADE_OUT_MS > duration_ms
                         ? (local_ms + FADE_OUT_MS - duration_ms) * 256 / FADE_OUT_MS
                         : 0;
}

typedef struct {
    float h1, h2, h3;
} particle_seed;

/* Screen position of particle i at scene time tau (seconds), in 1/16 px;
 * returns 0 when it is behind the viewer. *s_out is the rebuild progress. */
static int locate(const eh_particles_ws *ws, int i, const particle_seed *p, float tau,
                  float *sx_out, float *sy_out, float *s_out)
{
    const float c = EH_FB_W / 2.0f;
    const float sx = ws->sx[i] * (1.0f / SUB), sy = ws->sy[i] * (1.0f / SUB);
    const float ox = sx - c, oy = sy - c;
    const float ol = sqrtf(ox * ox + oy * oy) + 1e-3f;
    const float speed = 40.0f + 120.0f * p->h1, swirl = (p->h2 - 0.5f) * 80.0f;
    const float vx = ox / ol * speed - oy / ol * swirl, vy = oy / ol * speed + ox / ol * swirl;
    const float delay = REBUILD_START_S + REBUILD_STAGGER_S * p->h3;
    const float te = tau < delay ? tau : delay;
    const float spread = (1.0f - expf(-EXPLODE_DRAG * te)) / EXPLODE_DRAG;
    float x = sx + vx * spread, y = sy + vy * spread, z = 0.0f, s = 0.0f;
    if (tau > delay) {
        const int k = ws->target[i];
        s = eh_smoothstep((tau - delay) / REBUILD_S);
        x += (ws->tx[k] + 0.5f - x) * s;
        y += (ws->ty[k] + 0.5f - y) * s;
        z = (1.0f - s) * (p->h1 - 0.5f) * 240.0f;
    }
    *s_out = s;

    /* Rotate about the display centre: yaw about the vertical axis, then
     * pitch about the horizontal one; project with mild perspective. */
    const float rx = x - c, ry = y - c;
    const float x1 = rx * ws->cos_yaw + z * ws->sin_yaw, z1 = -rx * ws->sin_yaw + z * ws->cos_yaw;
    const float y2 = ry * ws->cos_pitch - z1 * ws->sin_pitch;
    const float z2 = ry * ws->sin_pitch + z1 * ws->cos_pitch;
    const float k = VIEW_DIST / (VIEW_DIST + z2);
    if (!(k > 0.0f))
        return 0;
    const float limit = 2000.0f;
    *sx_out = eh_clampf(c + x1 * k, -limit, limit) * SUB;
    *sy_out = eh_clampf(c + y2 * k, -limit, limit) * SUB;
    return 1;
}

static void compute_segments(eh_particles_ws *ws, int i0, int i1)
{
    const eh_rgb565 *pal = eh_palettes[ws->palette % EH_PALETTE_COUNT];
    const float tau = ws->local_ms / 1000.0f;
    for (int i = i0; i < i1; i++) {
        const particle_seed p = {eh_hash01((uint32_t)i * 3u + 1u), eh_hash01((uint32_t)i * 3u + 2u),
                                 eh_hash01((uint32_t)i * 3u + 3u)};
        eh_particle_seg *seg = &ws->seg[i];
        float hx, hy, s;
        if (!locate(ws, i, &p, tau, &hx, &hy, &s)) {
            seg->hx = OFFSCREEN;
            continue;
        }
        seg->hx = (int16_t)floorf(hx);
        seg->hy = (int16_t)floorf(hy);
#if EH_PARTICLE_TRAILS
        float tx, ty, ts;
        const float tail_tau = tau > TRAIL_S ? tau - TRAIL_S : 0.0f;
        if (!locate(ws, i, &p, tail_tau, &tx, &ty, &ts)) {
            tx = hx;
            ty = hy;
        }
        seg->tx = (int16_t)floorf(tx);
        seg->ty = (int16_t)floorf(ty);
#endif
        eh_rgb565 col = ws->scol[i];
        if (s > 0.0f) {
            const int k = ws->target[i];
            /* Logo colour: a palette sweep lifted halfway to white. */
            const eh_rgb565 logo =
                lerp565(pal[((ws->tx[k] >> 1) + (ws->local_ms >> 4)) & 255], 0xFFFF, 128);
            col = lerp565(col, logo, (unsigned)(s * 256.0f));
        }
        seg->col = col;
    }
}

/* Draw one segment's pixels that fall in rows [y0, y1): a streak fading in
 * from the tail, then a size x size head. */
static void draw_segment(const eh_particle_seg *seg, unsigned size, eh_rgb565 *fb, int y0, int y1)
{
    const int hx = seg->hx >> 4, hy = seg->hy >> 4;
#if EH_PARTICLE_TRAILS
    const int tx = seg->tx >> 4, ty = seg->ty >> 4;
    const int dx = hx - tx, dy = hy - ty;
    const int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    const int n = adx > ady ? adx : ady;
    if (n > 0 && !((ty < y0 && hy < y0) || (ty >= y1 && hy >= y1))) {
        for (int k = 0; k < n; k++) {
            const int x = tx + dx * k / n, y = ty + dy * k / n;
            if (y >= y0 && y < y1 && x >= 0 && x < EH_FB_W)
                fb[y * EH_FB_W + x] = lerp565(fb[y * EH_FB_W + x], seg->col,
                                              (unsigned)(32 + 160 * (k + 1) / (n + 1)));
        }
    }
#endif
    if (hy + (int)size <= y0 || hy >= y1)
        return;
    for (int y = hy; y < hy + (int)size; y++) {
        if (y < y0 || y >= y1)
            continue;
        for (int x = hx; x < hx + (int)size; x++)
            if (x >= 0 && x < EH_FB_W)
                fb[y * EH_FB_W + x] = seg->col;
    }
}

void eh_particles_render_part(eh_particles_ws *ws, eh_rgb565 *fb, int pass, int part)
{
    const int y0 = part * EH_PART_ROWS, y1 = y0 + EH_PART_ROWS;
    if (pass == 0) {
        for (int y = y0; y < y1; y++) {
            const eh_rgb565 bg = eh_rgb565_pack(4, 4, (unsigned)(18 + y / 16));
            for (int x = 0; x < EH_FB_W; x++)
                fb[y * EH_FB_W + x] = bg;
        }
        compute_segments(ws, part * EH_PARTICLE_COUNT / EH_PARTS,
                         (part + 1) * EH_PARTICLE_COUNT / EH_PARTS);
        return;
    }
    for (int i = 0; i < EH_PARTICLE_COUNT; i++)
        if (ws->seg[i].hx != OFFSCREEN)
            draw_segment(&ws->seg[i], ws->size, fb, y0, y1);
    eh_fade_rect(fb, 0x0000, ws->fade_black, 0, EH_FB_W, y0, y1);
}

void eh_particles_render(eh_particles_ws *ws, eh_rgb565 *fb, uint32_t local_ms,
                         uint32_t duration_ms, const eh_camera *cam)
{
    eh_particles_prepare(ws, local_ms, duration_ms, cam);
    for (int pass = 0; pass < EH_PARTICLES_PASSES; pass++)
        for (int part = 0; part < EH_PARTS; part++)
            eh_particles_render_part(ws, fb, pass, part);
}
