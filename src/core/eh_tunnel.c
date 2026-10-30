/* Scene 2: classic table-driven tunnel. Angle and inverse-distance tables are
 * built once on scene entry; per pixel the cost is two table reads, a twist
 * term, one texel and a depth shade. The tunnel centre follows a gentle path
 * plus the D-pad camera. The scene enters from white (the torus dive) and
 * ends flying into the light.
 *
 * The tables cover one quadrant and are mirrored: a native-resolution
 * full-plane table with camera margin would be 4x larger. Angles carry 1024
 * steps per turn, so the angular quantisation stays near one pixel at the
 * rim of the 360-px panel. */
#include <math.h>

#include "eh/eh_scenes.h"
#include "eh/eh_tables.h"
#include "eh_math.h"

#define DEPTH_K 6400.0f
#define FADE_IN_MS 800u
#define GLOW_MS 3000u
#define WHITE_MS 1200u
#define T EH_TUNNEL_TEX_SIZE
#define Q EH_TUNNEL_Q

/* XOR panels with bevelled seams: a bright seam with a shadow line beside
 * it, four panels around the tunnel and four per texture repeat in depth.
 * One palette cycle per texture, as in v1, at the texture's resolution. */
static void build_tex(eh_tunnel_ws *ws, unsigned palette)
{
    const eh_rgb565 *pal = eh_palettes[palette % EH_PALETTE_COUNT];
    const int seam = T / 4, scale = 256 / T;
    for (int y = 0; y < T; y++)
        for (int x = 0; x < T; x++) {
            eh_rgb565 c = pal[((x ^ y) * scale) & 255];
            const int sx = x % seam, sy = y % seam;
            if (sx == 0 || sy == 0)
                c |= 0x8410u;
            else if (sx == 1 || sy == 1)
                c = (eh_rgb565)((c >> 1) & 0x7BEFu);
            ws->tex[y * T + x] = c;
        }
    ws->tex_palette = palette;
}

void eh_tunnel_init(eh_tunnel_ws *ws, unsigned palette)
{
    for (int y = 0; y < Q; y++)
        for (int x = 0; x < Q; x++) {
            const float dx = x + 0.5f, dy = y + 0.5f;
            const float r = sqrtf(dx * dx + dy * dy);
            const int i = y * Q + x;
            ws->qang[i] = (uint8_t)(int)floorf(atan2f(dy, dx) * (1024.0f / EH_TAU));
            const float d = DEPTH_K / r;
            ws->depth[i] = (uint8_t)(d > 255.0f ? 255 : (int)d);
        }
    build_tex(ws, palette);
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void eh_tunnel_prepare(eh_tunnel_ws *ws, uint32_t local_ms, uint32_t duration_ms,
                       const eh_camera *cam)
{
    if (ws->tex_palette != cam->palette)
        build_tex(ws, cam->palette);
    const float t = (float)local_ms;
    const int m = EH_TUNNEL_MARGIN;
    /* Yaw is an angle that wraps at +-pi while LEFT/RIGHT is held; sinf keeps
     * the centre continuous across the wrap (a linear term jumped 80 px). */
    ws->cx = EH_FB_W / 2 + clampi((int)lroundf(sinf(t * 0.0011f) * 20.0f + sinf(cam->yaw) * 30.0f), -m, m);
    ws->cy = EH_FB_H / 2 + clampi((int)lroundf(cosf(t * 0.0007f) * 16.0f - cam->pitch * 50.0f), -m, m);
    ws->roll = (local_ms * 3u) >> 4;
    ws->advance = (local_ms * 15u) >> 6;
    /* Twist: signed depth-proportional angle offset that swings over time. */
    ws->twist = (int)eh_sin8[(local_ms >> 5) & 255u] - 128;
    /* Light at the end of the tunnel: deep pixels blend toward white. */
    ws->glow = local_ms + GLOW_MS > duration_ms ? (local_ms + GLOW_MS - duration_ms) * 256u / GLOW_MS : 0;
    unsigned fade = 0;
    if (local_ms < FADE_IN_MS)
        fade = 256 - local_ms * 256 / FADE_IN_MS;
    if (local_ms + WHITE_MS > duration_ms)
        fade = (local_ms + WHITE_MS - duration_ms) * 256 / WHITE_MS;
    ws->fade_white = fade;
}

/* Pixel centres mirror about the centre lines: offset -1 maps to 0. */
static inline int fold(int d) { return d < 0 ? -d - 1 : d; }

/* Quadrant unfold of a table angle a for offsets (dx, dy), in 1/1024 turn. */
static inline unsigned unfold(unsigned a, int dx, int dy)
{
    return dx >= 0 ? (dy >= 0 ? a : 1023u - a) : (dy >= 0 ? 511u - a : 512u + a);
}

unsigned eh_tunnel_angle(const eh_tunnel_ws *ws, int x, int y)
{
    const int dx = x - ws->cx, dy = y - ws->cy;
    return unfold(ws->qang[fold(dy) * Q + fold(dx)], dx, dy);
}

void eh_tunnel_render_part(const eh_tunnel_ws *ws, eh_rgb565 *fb, int part)
{
    const int y0 = part * EH_PART_ROWS, y1 = y0 + EH_PART_ROWS;
    for (int y = y0; y < y1; y++) {
        const int dy = y - ws->cy;
        const int ay = fold(dy);
        const uint8_t *qang = ws->qang + ay * Q, *dep = ws->depth + ay * Q;
        eh_rgb565 *row = fb + y * EH_FB_W;
        for (int x = eh_span_x0[y]; x < eh_span_x1[y]; x++) {
            const int dx = x - ws->cx;
            const int ax = fold(dx);
            const unsigned d = dep[ax], ang = unfold(qang[ax], dx, dy);
            const unsigned u =
                ((ang + ws->roll + (unsigned)(((int)d * ws->twist) >> 5)) & 1023u) * T >> 10;
            const unsigned v = ((d + ws->advance) * T >> 7) & (T - 1);
            const unsigned c = ws->tex[v * T + u];
            /* Near walls bright, far end dark; glow lifts the far end. Depth
             * is in half v1 units, so (d + d^2/255) / 2 tracks v1's 255 - d
             * at the walls and still reaches black at the centre. */
            const unsigned shade = 255u - d * (255u + d) / 510u;
            unsigned r = ((c >> 11) * shade) >> 8, g = (((c >> 5) & 0x3Fu) * shade) >> 8,
                     b = ((c & 0x1Fu) * shade) >> 8;
            if (ws->glow) {
                const unsigned w = (d * ws->glow) >> 8;
                r += ((31u - r) * w) >> 8;
                g += ((63u - g) * w) >> 8;
                b += ((31u - b) * w) >> 8;
            }
            row[x] = (eh_rgb565)((r << 11) | (g << 5) | b);
        }
    }
    eh_fade_rect(fb, 0xFFFF, ws->fade_white, 0, EH_FB_W, y0, y1);
}

void eh_tunnel_render(eh_tunnel_ws *ws, eh_rgb565 *fb, uint32_t local_ms, uint32_t duration_ms,
                      const eh_camera *cam)
{
    eh_tunnel_prepare(ws, local_ms, duration_ms, cam);
    for (int part = 0; part < EH_PARTS; part++)
        eh_tunnel_render_part(ws, fb, part);
}
