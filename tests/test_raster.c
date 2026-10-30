#include <math.h>
#include <string.h>

#include "eh/eh_raster.h"
#include "eh_test.h"

static eh_rgb565 fb[EH_FB_PIXELS];
static uint16_t zb[EH_FB_PIXELS];
#define TEX_SHIFT 6
#define TEX_SIZE (1 << TEX_SHIFT)
static eh_rgb565 tex_red[TEX_SIZE * TEX_SIZE], tex_blue[TEX_SIZE * TEX_SIZE],
    tex_split[TEX_SIZE * TEX_SIZE];

#define RED 0xF800u
#define BLUE 0x001Fu

static void setup(void)
{
    for (int i = 0; i < TEX_SIZE * TEX_SIZE; i++) {
        tex_red[i] = RED;
        tex_blue[i] = BLUE;
        tex_split[i] = (i % TEX_SIZE) < TEX_SIZE / 2 ? RED : BLUE;
    }
    memset(fb, 0, sizeof fb);
    eh_raster_clear_depth(zb, EH_FB_H);
}

static eh_rgb565 px(int x, int y) { return fb[y * EH_FB_W + x]; }

void test_raster_coverage_and_culling(void)
{
    setup();
    eh_raster_target t = {fb, zb, tex_red, TEX_SHIFT, 0, EH_FB_H, NULL};
    eh_vtx a = {10, 10, 0.5f, 0, 0}, b = {50, 10, 0.5f, 0, 0}, c = {10, 50, 0.5f, 0, 0};
    const int written = eh_raster_tri(&t, &a, &b, &c);
    int red = 0;
    for (int i = 0; i < EH_FB_PIXELS; i++)
        red += fb[i] == RED;
    EH_CHECK_MSG(written == red && red > 700 && red < 900, "returned %d, drew %d", written, red);
    EH_CHECK(px(20, 20) == RED);
    EH_CHECK(px(11, 48) == RED);
    EH_CHECK(px(45, 45) == 0);
    EH_CHECK(px(60, 20) == 0);
    EH_CHECK(zb[20 * EH_FB_W + 20] < 0xFFFF);

    setup();
    eh_raster_tri(&t, &a, &c, &b); /* reversed winding: back-facing */
    int drawn = 0;
    for (int i = 0; i < EH_FB_PIXELS; i++)
        drawn += fb[i] != 0;
    EH_CHECK_MSG(drawn == 0, "back face drew %d pixels", drawn);
}

void test_raster_depth_order_independent(void)
{
    eh_vtx a = {0, 0, 0.2f, 0, 0}, b = {100, 0, 0.2f, 0, 0}, c = {0, 100, 0.2f, 0, 0};
    eh_vtx fa = {0, 0, 0.8f, 0, 0}, fbv = {100, 0, 0.8f, 0, 0}, fc = {0, 100, 0.8f, 0, 0};

    setup();
    eh_raster_target near_t = {fb, zb, tex_red, TEX_SHIFT, 0, EH_FB_H, NULL};
    eh_raster_target far_t = {fb, zb, tex_blue, TEX_SHIFT, 0, EH_FB_H, NULL};
    eh_raster_tri(&near_t, &a, &b, &c);
    eh_raster_tri(&far_t, &fa, &fbv, &fc);
    EH_CHECK(px(30, 30) == RED);

    setup();
    eh_raster_tri(&far_t, &fa, &fbv, &fc);
    eh_raster_tri(&near_t, &a, &b, &c);
    EH_CHECK(px(30, 30) == RED);
}

void test_raster_band_and_texture(void)
{
    setup();
    eh_raster_target band = {fb, zb, tex_red, TEX_SHIFT, 0, 30, NULL};
    eh_vtx a = {10, 10, 0.5f, 0, 0}, b = {80, 10, 0.5f, 0, 0}, c = {10, 80, 0.5f, 0, 0};
    eh_raster_tri(&band, &a, &b, &c);
    EH_CHECK(px(20, 20) == RED);
    EH_CHECK(px(20, 40) == 0);

    /* The depth buffer is band-relative: a band starting at row 40 uses
     * zb[0..] for row 40, and leaves depth beyond its rows untouched. */
    setup();
    eh_raster_clear_depth(zb, 20);
    zb[20 * EH_FB_W] = 0x1234;
    eh_raster_target mid = {fb, zb, tex_red, TEX_SHIFT, 40, 60, NULL};
    eh_raster_tri(&mid, &a, &b, &c);
    EH_CHECK(px(20, 45) == RED);
    EH_CHECK(px(20, 39) == 0 && px(20, 60) == 0);
    EH_CHECK(zb[(45 - 40) * EH_FB_W + 20] < 0xFFFF);
    EH_CHECK(zb[20 * EH_FB_W] == 0x1234);

    setup();
    eh_raster_target t = {fb, zb, tex_split, TEX_SHIFT, 0, EH_FB_H, NULL};
    eh_vtx q0 = {0, 0, 0.5f, 0.0f, 0.0f}, q1 = {160, 0, 0.5f, 0.999f, 0.0f},
           q2 = {0, 160, 0.5f, 0.0f, 0.999f};
    eh_raster_tri(&t, &q0, &q1, &q2);
    EH_CHECK(px(10, 5) == RED);
    EH_CHECK(px(140, 5) == BLUE);
}

void test_raster_line(void)
{
    setup();
    eh_raster_target t = {fb, zb, tex_red, TEX_SHIFT, 0, EH_FB_H, NULL};
    eh_raster_line(&t, 5, 5, 25, 15, BLUE);
    EH_CHECK(px(5, 5) == BLUE);
    EH_CHECK(px(25, 15) == BLUE);
    EH_CHECK(px(15, 10) == BLUE);
    EH_CHECK(px(15, 14) == 0);
    /* Off-screen endpoints are clipped rather than written out of bounds. */
    eh_raster_line(&t, -50, 100, 500, 100, RED);
    EH_CHECK(px(0, 100) == RED && px(EH_FB_W - 1, 100) == RED);
}

static float rnd(uint32_t *seed, float lo, float hi)
{
    *seed = *seed * 1664525u + 1013904223u;
    return lo + (hi - lo) * ((*seed >> 8) / 16777216.0f);
}

void test_raster_matches_reference_coverage(void)
{
    /* The row-run solver must visit every pixel the plain edge test would
     * accept. Random triangles (thin, large, partly off-screen) are checked
     * against a brute-force pixel-centre test; pixels within a hair of an
     * edge are numerically ambiguous and skipped. */
    uint32_t seed = 12345u;
    int missed = 0, extra = 0, checked = 0;
    for (int n = 0; n < 300; n++) {
        setup();
        eh_raster_target t = {fb, zb, tex_red, TEX_SHIFT, 0, EH_FB_H, NULL};
        eh_vtx v[3];
        const float span = n % 3 == 0 ? 600.0f : n % 3 == 1 ? 80.0f : 12.0f;
        const float cx = rnd(&seed, -40.0f, 400.0f), cy = rnd(&seed, -40.0f, 400.0f);
        for (int k = 0; k < 3; k++) {
            const float x = cx + rnd(&seed, -span, span);
            const float y = cy + rnd(&seed, -span * 0.2f, span * 0.2f) * (k == 2 ? 5 : 1);
            v[k] = (eh_vtx){x, y, 0.5f, 0.0f, 0.0f};
        }
        const float area = (v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[1].y - v[0].y) * (v[2].x - v[0].x);
        if (area < 0) {
            const eh_vtx tmp = v[1];
            v[1] = v[2];
            v[2] = tmp;
        }
        eh_raster_tri(&t, &v[0], &v[1], &v[2]);
        const double eps = 1e-3 * fabs((double)area);
        for (int y = 0; y < EH_FB_H; y++)
            for (int x = 0; x < EH_FB_W; x++) {
                const double px = x + 0.5, py = y + 0.5;
                double w[3];
                for (int e = 0; e < 3; e++) {
                    const eh_vtx *p = &v[(e + 1) % 3], *q = &v[(e + 2) % 3];
                    w[e] = ((double)q->x - p->x) * (py - p->y) - ((double)q->y - p->y) * (px - p->x);
                }
                const double m = fmin(w[0], fmin(w[1], w[2]));
                if (fabs(m) < eps)
                    continue;
                checked++;
                const int drawn = fb[y * EH_FB_W + x] == RED;
                missed += m > 0 && !drawn;
                extra += m < 0 && drawn;
            }
    }
    EH_CHECK_MSG(missed == 0 && extra == 0, "missed %d, extra %d of %d pixels", missed, extra, checked);
}
