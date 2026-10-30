/* Edge-function rasterizer: each row visits only the run its edge functions
 * allow (solved from per-edge reciprocal slopes, with a pixel of slack),
 * and steps depth and texture coordinates incrementally. Shared edges may
 * be covered by both neighbours (no top-left rule); with opaque,
 * depth-tested triangles that only costs a redundant write. */
#include "eh/eh_raster.h"

#include <math.h>

void eh_raster_clear_depth(uint16_t *zb, int rows)
{
    for (int i = 0; i < rows * EH_FB_W; i++)
        zb[i] = 0xFFFF;
}

static float edge(const eh_vtx *a, const eh_vtx *b, float px, float py)
{
    return (b->x - a->x) * (py - a->y) - (b->y - a->y) * (px - a->x);
}

static float minf3(float a, float b, float c) { return fminf(a, fminf(b, c)); }
static float maxf3(float a, float b, float c) { return fmaxf(a, fmaxf(b, c)); }

int eh_raster_tri(const eh_raster_target *t, const eh_vtx *a, const eh_vtx *b,
                  const eh_vtx *c)
{
    const float area = edge(a, b, c->x, c->y);
    if (!(area > 0.0f))
        return 0;

    int minx = (int)floorf(minf3(a->x, b->x, c->x));
    int maxx = (int)ceilf(maxf3(a->x, b->x, c->x));
    int miny = (int)floorf(minf3(a->y, b->y, c->y));
    int maxy = (int)ceilf(maxf3(a->y, b->y, c->y));
    if (minx < 0)
        minx = 0;
    if (maxx > EH_FB_W - 1)
        maxx = EH_FB_W - 1;
    if (miny < t->y0)
        miny = t->y0;
    if (maxy > t->y1 - 1)
        maxy = t->y1 - 1;
    if (minx > maxx || miny > maxy)
        return 0;

    const float inv = 1.0f / area;
    /* Edge functions w0 (opposite a), w1 (opposite b), w2 (opposite c) at
     * pixel centres, with their per-pixel x / per-row y increments. */
    const float dx[3] = {-(c->y - b->y), -(a->y - c->y), -(b->y - a->y)};
    const float dy[3] = {c->x - b->x, a->x - c->x, b->x - a->x};
    const float px0 = minx + 0.5f, py0 = miny + 0.5f;
    float row[3] = {edge(b, c, px0, py0), edge(c, a, px0, py0), edge(a, b, px0, py0)};
    /* Depth and texture coordinates are affine in screen space: step them
     * instead of re-deriving barycentrics per pixel. */
    const float zx = (dx[0] * a->z + dx[1] * b->z + dx[2] * c->z) * inv;
    const float zy = (dy[0] * a->z + dy[1] * b->z + dy[2] * c->z) * inv;
    const float ux = (dx[0] * a->u + dx[1] * b->u + dx[2] * c->u) * inv;
    const float uy = (dy[0] * a->u + dy[1] * b->u + dy[2] * c->u) * inv;
    const float vx = (dx[0] * a->v + dx[1] * b->v + dx[2] * c->v) * inv;
    const float vy = (dy[0] * a->v + dy[1] * b->v + dy[2] * c->v) * inv;
    float zrow = (row[0] * a->z + row[1] * b->z + row[2] * c->z) * inv;
    float urow = (row[0] * a->u + row[1] * b->u + row[2] * c->u) * inv;
    float vrow = (row[0] * a->v + row[1] * b->v + row[2] * c->v) * inv;
    /* Reciprocal x slopes bound each row's covered run, so only about one
     * rejected pixel per edge is visited instead of the whole bounding box. */
    float rdx[3];
    for (int e = 0; e < 3; e++)
        rdx[e] = dx[e] != 0.0f ? 1.0f / dx[e] : 0.0f;

    const int size = 1 << t->tex_shift, mask = size - 1;
    int written = 0, depth_failed = 0;
    for (int y = miny; y <= maxy; y++) {
        float lo = 0.0f, hi = (float)(maxx - minx);
        int empty = 0;
        for (int e = 0; e < 3; e++) {
            /* w_e(i) = row[e] + dx[e] * i, i = x - minx */
            if (dx[e] > 0.0f) {
                const float i = -row[e] * rdx[e];
                lo = i > lo ? i : lo;
            } else if (dx[e] < 0.0f) {
                const float i = -row[e] * rdx[e];
                hi = i < hi ? i : hi;
            } else if (row[e] < 0.0f) {
                empty = 1;
            }
        }
        if (!empty && lo <= hi + 1.0f) {
            /* floor/ceil leave up to a pixel of slack each side, far more
             * than the bound's rounding error; the exact test decides. */
            int i0 = (int)floorf(lo), i1 = (int)ceilf(hi);
            i0 = i0 < 0 ? 0 : i0;
            i1 = i1 > maxx - minx ? maxx - minx : i1;
            if (t->stats && i1 >= i0) {
                t->stats->rows++;
                t->stats->tested += (uint32_t)(i1 - i0 + 1);
            }
            const float fi = (float)i0;
            float w0 = row[0] + dx[0] * fi, w1 = row[1] + dx[1] * fi, w2 = row[2] + dx[2] * fi;
            float z = zrow + zx * fi, u = urow + ux * fi, v = vrow + vx * fi;
            uint16_t *zp = &t->zb[(y - t->y0) * EH_FB_W + minx + i0];
            eh_rgb565 *fp = &t->fb[y * EH_FB_W + minx + i0];
            for (int i = i0; i <= i1; i++, zp++, fp++, w0 += dx[0], w1 += dx[1], w2 += dx[2],
                     z += zx, u += ux, v += vx) {
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                    continue;
                const float zc = z < 0.0f ? 0.0f : z > 1.0f ? 1.0f : z;
                const uint16_t zq = (uint16_t)(zc * 65534.0f);
                if (zq >= *zp) {
                    depth_failed++;
                    continue;
                }
                *zp = zq;
                const unsigned tu = (unsigned)((int)(u * size) & mask);
                const unsigned tv = (unsigned)((int)(v * size) & mask);
                *fp = t->tex[(tv << t->tex_shift) | tu];
                written++;
            }
        }
        for (int e = 0; e < 3; e++)
            row[e] += dy[e];
        zrow += zy;
        urow += uy;
        vrow += vy;
    }
    if (t->stats)
        t->stats->depth_failed += (uint32_t)depth_failed;
    return written;
}

void eh_raster_line(const eh_raster_target *t, int x0, int y0, int x1, int y1,
                    eh_rgb565 color)
{
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    const int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < EH_FB_W && y0 >= t->y0 && y0 < t->y1)
            t->fb[y0 * EH_FB_W + x0] = color;
        if (x0 == x1 && y0 == y1)
            break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}
