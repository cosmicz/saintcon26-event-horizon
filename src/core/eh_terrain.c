/* Scene 3: heightfield flythrough by column ray marching ("voxel space"
 * style): march depth slices front to back, project each column's height,
 * and draw only the part not already occluded. Fog blends distant terrain
 * into the horizon colour while the sun rises through the scene. Heightmap
 * and colour map are tileable value noise generated once on entry.
 *
 * Columns are independent, so parts split the frame by columns (sky
 * included) and each part keeps its own occlusion horizon on the stack.
 * Terrain is drawn first and the sky fills only what remains of each
 * column inside the round display: the sky's per-pixel glow (a float
 * divide) is the costliest per-pixel work in the scene. The circle is
 * symmetric, so the row spans double as column spans. The
 * map covers the same 128x128 world-unit tile as v1 at EH_TERRAIN_SIZE / 128
 * samples per unit, with the extra resolution spent on a finer noise
 * octave. */
#include <math.h>

#include "eh/eh_scenes.h"
#include "eh/eh_tables.h"
#include "eh_math.h"

#define N EH_TERRAIN_SIZE
#define MASK (EH_TERRAIN_SIZE - 1)
#define MAP_SCALE (EH_TERRAIN_SIZE / 128.0f) /* map samples per world unit */
#define FAR_Z 140.0f
#define HEIGHT_SCALE 0.7f
#define ALTITUDE 120.0f  /* minimum eye height */
#define CLEARANCE 24.0f  /* eye height above the highest nearby ground */
#define NEAR_RADIUS 12   /* world units around the eye that must stay below it */
#define PROJ 240.0f
#define HORIZON_Y 124.0f
#define SPEED 18.0f /* world units per second */
#define FADE_IN_MS 1000u
#define COLS (EH_FB_W / EH_PARTS)

static float lattice(int x, int y, int period, uint32_t seed)
{
    x &= period - 1;
    y &= period - 1;
    /* Unsigned products: the hash relies on wraparound, which is undefined
     * for signed int. */
    return eh_hash01(((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u) ^ seed);
}

/* Tileable value noise at a lattice period dividing N. */
static float value_noise(int x, int y, int cell, uint32_t seed)
{
    const int period = N / cell;
    const int gx = x / cell, gy = y / cell;
    const float fx = eh_smoothstep((float)(x % cell) / cell);
    const float fy = eh_smoothstep((float)(y % cell) / cell);
    const float a = lattice(gx, gy, period, seed), b = lattice(gx + 1, gy, period, seed);
    const float c = lattice(gx, gy + 1, period, seed), d = lattice(gx + 1, gy + 1, period, seed);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

static eh_rgb lerp_rgb(eh_rgb a, eh_rgb b, float w)
{
    eh_rgb o = {(uint8_t)(a.r + (b.r - a.r) * w), (uint8_t)(a.g + (b.g - a.g) * w),
                (uint8_t)(a.b + (b.b - a.b) * w)};
    return o;
}

void eh_terrain_init(eh_terrain_ws *ws)
{
    /* Octaves from half the map down to 4 samples: at 256 samples that is
     * one octave finer (2 world units) than v1's 128-sample map. */
    float amp_sum = 0.0f;
    /* First pass quantises raw noise into the colour map, which is free
     * until the second pass; this avoids a separate float buffer. */
    uint16_t lo = 0xFFFF, hi = 0;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float h = 0.0f, amp = 1.0f;
            amp_sum = 0.0f;
            for (int cell = N / 2, o = 0; cell >= 4; cell /= 2, o++, amp *= 0.5f) {
                h += amp * value_noise(x, y, cell, 0x9E3779B9u * (uint32_t)(o + 1));
                amp_sum += amp;
            }
            const uint16_t q = (uint16_t)(h / amp_sum * 65535.0f);
            ws->color[y * N + x] = q;
            lo = q < lo ? q : lo;
            hi = q > hi ? q : hi;
        }
    for (int i = 0; i < N * N; i++) {
        /* Square the normalised height: broad valleys, sharper peaks. */
        const float n = (float)(ws->color[i] - lo) / (float)(hi - lo);
        ws->height[i] = (uint8_t)(n * n * 255.0f);
    }

    const eh_rgb water = {20, 60, 120}, sand = {190, 170, 110}, grass = {50, 120, 45},
                 rock = {110, 100, 95}, snow = {240, 240, 250};
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            const int h = ws->height[y * N + x];
            eh_rgb c;
            if (h < 18)
                c = water;
            else if (h < 30)
                c = lerp_rgb(sand, grass, (h - 18) / 12.0f);
            else if (h < 120)
                c = lerp_rgb(grass, rock, (h - 30) / 90.0f);
            else if (h < 180)
                c = lerp_rgb(rock, snow, (h - 120) / 60.0f);
            else
                c = snow;
            /* Light from +x: slope shading from the neighbouring samples, so
             * finer maps resolve finer facets; scaled to v1's strength. */
            const int dh = ws->height[y * N + ((x - 1) & MASK)] - ws->height[y * N + ((x + 1) & MASK)];
            const float slope = eh_clampf(1.0f + dh * 0.025f * MAP_SCALE, 0.45f, 1.35f);
            /* Per-sample grain (rock, grass, snow crust): the map's own
             * resolution sets how fine it is. */
            const float grain =
                1.0f + 0.16f * (eh_hash01(((uint32_t)y << 16) ^ (uint32_t)x ^ 0xA511E9B3u) - 0.5f);
            const float s = slope * grain;
            ws->color[y * N + x] = eh_rgb565_pack((unsigned)eh_clampf(c.r * s, 0, 255),
                                                  (unsigned)eh_clampf(c.g * s, 0, 255),
                                                  (unsigned)eh_clampf(c.b * s, 0, 255));
        }
}

float eh_terrain_sun_y(uint32_t local_ms, uint32_t duration_ms)
{
    return 250.0f - 190.0f * eh_smoothstep((float)local_ms / duration_ms);
}

static void path_at(float t, float *x, float *y)
{
    *x = 40.0f + t * SPEED * cosf(0.2f);
    *y = 70.0f + t * SPEED * sinf(0.2f);
}

/* Bilinear ground height in eye units at world (x, y): continuous in the
 * position, unlike the nearest-sample lookup the renderer uses. */
static float ground(const eh_terrain_ws *ws, float x, float y)
{
    const float mx = x * MAP_SCALE, my = y * MAP_SCALE;
    const float fx = floorf(mx), fy = floorf(my);
    const int ix = (int)fx, iy = (int)fy;
    const float ax = mx - fx, ay = my - fy;
    const float h00 = ws->height[(iy & MASK) * N + (ix & MASK)];
    const float h10 = ws->height[(iy & MASK) * N + ((ix + 1) & MASK)];
    const float h01 = ws->height[((iy + 1) & MASK) * N + (ix & MASK)];
    const float h11 = ws->height[((iy + 1) & MASK) * N + ((ix + 1) & MASK)];
    const float top = h00 + (h10 - h00) * ax, bottom = h01 + (h11 - h01) * ax;
    return (top + (bottom - top) * ay) * HEIGHT_SCALE;
}

/* Highest ground within NEAR_RADIUS of the eye at time t (seconds). */
static float near_peak(const eh_terrain_ws *ws, float t)
{
    float x, y, peak = 0.0f;
    path_at(t, &x, &y);
    for (int dy = -NEAR_RADIUS; dy <= NEAR_RADIUS; dy += 3)
        for (int dx = -NEAR_RADIUS; dx <= NEAR_RADIUS; dx += 3)
            if (dx * dx + dy * dy <= NEAR_RADIUS * NEAR_RADIUS) {
                const float g = ground(ws, x + dx, y + dy);
                peak = g > peak ? g : peak;
            }
    return peak;
}

/* v1 flew at a fixed 120 and passed through peaks up to ~178, filling the
 * frame with snow colour. The eye now rides a cone envelope of the nearby
 * peaks sampled on a fixed time grid s_j:
 *
 *   eye(t) = max(ALTITUDE, CLEARANCE + max_j (peak(s_j) - CLIMB * |s_j - t|))
 *
 * Each term is CLIMB-Lipschitz in t, so the eye never moves faster than
 * CLIMB units/s, and it reaches each peak's height by the time the peak is
 * underneath. Only samples within WINDOW_S of t are evaluated; a sample at
 * the window edge contributes at most 178.5 - CLIMB * WINDOW_S + CLEARANCE
 * = 112.5 < ALTITUDE, so entering the window never moves the eye. It
 * depends only on local_ms, so seeking still matches playback. */
#define CLIMB 30.0f   /* eye units per second */
#define WINDOW_S 3.0f
#define STEP_S 0.25f

float eh_terrain_altitude(const eh_terrain_ws *ws, uint32_t local_ms)
{
    const float t = local_ms / 1000.0f;
    const int j0 = (int)floorf((t - WINDOW_S) / STEP_S), j1 = (int)ceilf((t + WINDOW_S) / STEP_S);
    float envelope = 0.0f;
    for (int j = j0; j <= j1; j++) {
        const float s = j * STEP_S;
        const float e = near_peak(ws, s) - CLIMB * fabsf(s - t);
        envelope = e > envelope ? e : envelope;
    }
    const float eye = envelope + CLEARANCE;
    return eye > ALTITUDE ? eye : ALTITUDE;
}

void eh_terrain_prepare(eh_terrain_ws *ws, uint32_t local_ms, uint32_t duration_ms,
                        const eh_camera *cam)
{
    const float t = local_ms / 1000.0f;
    const float dawn = eh_smoothstep((float)local_ms / duration_ms);
    ws->horizon = HORIZON_Y + cam->pitch * 180.0f;
    ws->sun_y = eh_terrain_sun_y(local_ms, duration_ms) + (ws->horizon - HORIZON_Y);
    ws->sun_x = EH_FB_W / 2.0f - cam->yaw * 220.0f;

    /* Sky: night violet to dawn blue above, warm horizon band. */
    ws->top = lerp_rgb((eh_rgb){20, 10, 50}, (eh_rgb){60, 110, 190}, dawn);
    ws->low = lerp_rgb((eh_rgb){120, 50, 80}, (eh_rgb){250, 170, 110}, dawn);
    ws->fog = lerp_rgb(ws->low, (eh_rgb){255, 220, 180}, 0.35f * dawn);

    const float heading = 0.3f * sinf(t * 0.15f) + cam->yaw;
    ws->fx = cosf(heading); /* forward */
    ws->fy = sinf(heading);
    ws->rx = -ws->fy; /* right */
    ws->ry = ws->fx;
    path_at(t, &ws->px, &ws->py);
    ws->altitude = eh_terrain_altitude(ws, local_ms);
    ws->fade_white = local_ms < FADE_IN_MS ? 256 - local_ms * 256 / FADE_IN_MS : 0;
}

/* Sky for rows [y0, y1) of column x: night-to-dawn gradient with the sun
 * and its glow. */
static void sky_column(const eh_terrain_ws *ws, const eh_rgb *rowc, eh_rgb565 *fb, int x, int y0,
                       int y1)
{
    const float dx = x + 0.5f - ws->sun_x;
    for (int y = y0; y < y1; y++) {
        const float dy = y + 0.5f - ws->sun_y;
        const float d2 = dx * dx + dy * dy;
        eh_rgb c = rowc[y];
        if (d2 < 576.0f)
            c = (eh_rgb){255, 245, 200};
        else
            c = lerp_rgb(c, (eh_rgb){255, 200, 120}, eh_clampf(1200.0f / d2, 0.0f, 1.0f) * 0.8f);
        fb[y * EH_FB_W + x] = eh_rgb565_pack(c.r, c.g, c.b);
    }
}

void eh_terrain_render_part(eh_terrain_ws *ws, eh_rgb565 *fb, int part)
{
    const int x0 = part * COLS, x1 = x0 + COLS;
    const eh_rgb565 fog565 = eh_rgb565_pack(ws->fog.r, ws->fog.g, ws->fog.b);
    /* Occlusion horizon per column: rows at and below ybuf are drawn. It
     * starts at the bottom of the column's visible span; ytop is the top. */
    int ybuf[COLS], ytop[COLS];
    for (int i = 0; i < COLS; i++) {
        ybuf[i] = eh_span_x1[x0 + i];
        ytop[i] = eh_span_x0[x0 + i];
    }
    /* Half v1's slice spacing: twice the rows need twice the depth samples
     * to avoid stair-stepped ridgelines. */
    float z = 1.0f, dz = 0.2f;
    while (z < FAR_Z) {
        /* World position of this slice at column x0, and per-column step. */
        const float sx = ws->rx * 2.0f * z / EH_FB_W, sy = ws->ry * 2.0f * z / EH_FB_W;
        const float lx = ws->px + (ws->fx - ws->rx) * z + sx * x0;
        const float ly = ws->py + (ws->fy - ws->ry) * z + sy * x0;
        const float inv = PROJ / z;
        const unsigned fogw = (unsigned)(eh_clampf(powf(z / FAR_Z, 1.3f), 0.0f, 1.0f) * 256.0f);
        const unsigned keep = 256u - fogw;
        const unsigned fr = (fog565 >> 11) * fogw, fg = ((fog565 >> 5) & 0x3Fu) * fogw,
                       fb5 = (fog565 & 0x1Fu) * fogw;
        float mx = lx * MAP_SCALE, my = ly * MAP_SCALE;
        const float smx = sx * MAP_SCALE, smy = sy * MAP_SCALE;
        for (int i = 0; i < COLS; i++, mx += smx, my += smy) {
            const int m = ((int)floorf(my) & MASK) * N + ((int)floorf(mx) & MASK);
            int top_y = (int)((ws->altitude - ws->height[m] * HEIGHT_SCALE) * inv + ws->horizon);
            if (top_y < ytop[i])
                top_y = ytop[i];
            if (top_y >= ybuf[i])
                continue;
            const eh_rgb565 c = ws->color[m];
            const unsigned r = ((c >> 11) * keep + fr) >> 8;
            const unsigned g = (((c >> 5) & 0x3Fu) * keep + fg) >> 8;
            const unsigned b = ((c & 0x1Fu) * keep + fb5) >> 8;
            const eh_rgb565 shaded = (eh_rgb565)((r << 11) | (g << 5) | b);
            for (int y = top_y; y < ybuf[i]; y++)
                fb[y * EH_FB_W + x0 + i] = shaded;
            ybuf[i] = top_y;
        }
        z += dz;
        dz += 0.006f;
    }

    eh_rgb rowc[EH_FB_H];
    for (int y = 0; y < EH_FB_H; y++) {
        const float w = eh_clampf((y - (ws->horizon - 180.0f)) / 180.0f, 0.0f, 1.0f);
        rowc[y] = lerp_rgb(ws->top, ws->low, w * w);
    }
    uint32_t sky_px = 0;
    for (int i = 0; i < COLS; i++) {
        sky_column(ws, rowc, fb, x0 + i, ytop[i], ybuf[i]);
        sky_px += (uint32_t)(ybuf[i] - ytop[i]);
    }
    ws->sky_px[part] = sky_px;

    eh_fade_rect(fb, 0xFFFF, ws->fade_white, x0, x1, 0, EH_FB_H);
}

void eh_terrain_render(eh_terrain_ws *ws, eh_rgb565 *fb, uint32_t local_ms,
                       uint32_t duration_ms, const eh_camera *cam)
{
    eh_terrain_prepare(ws, local_ms, duration_ms, cam);
    for (int part = 0; part < EH_PARTS; part++)
        eh_terrain_render_part(ws, fb, part);
}
