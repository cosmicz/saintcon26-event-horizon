#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "eh/eh_scenes.h"
#include "eh/eh_tables.h"
#include "eh_test.h"
#include "test_util.h"

static eh_rgb565 fa[EH_FB_PIXELS], fb2[EH_FB_PIXELS];
static eh_torus_ws torus;
static eh_tunnel_ws tunnel;
static eh_terrain_ws terrain, terrain2;
static eh_particles_ws particles;

static uint32_t torus_covered(const eh_torus_ws *ws)
{
    uint32_t n = 0;
    for (int p = 0; p < EH_PARTS; p++)
        n += ws->covered[p];
    return n;
}

void test_fade(void)
{
    for (int i = 0; i < EH_FB_PIXELS; i++)
        fa[i] = 0x0000;
    memcpy(fb2, fa, sizeof fa);
    eh_fade_rect(fb2, 0xFFFF, 0, 0, EH_FB_W, 0, EH_FB_H);
    EH_CHECK(memcmp(fa, fb2, sizeof fa) == 0);
    eh_fade_rect(fb2, 0xFFFF, 256, 0, EH_FB_W, 0, EH_FB_H);
    EH_CHECK(fb2[180 * EH_FB_W + 180] == 0xFFFF && fb2[180 * EH_FB_W] == 0xFFFF);
    /* Corners are outside the round panel and never shown: untouched. */
    EH_CHECK(fb2[0] == 0x0000 && fb2[EH_FB_PIXELS - 1] == 0x0000);
    memcpy(fb2, fa, sizeof fa);
    eh_fade_rect(fb2, 0xFFFF, 128, 100, 200, 100, 280);
    unsigned mid = eh_test_lum(fb2[110 * EH_FB_W + 150]);
    EH_CHECK_MSG(mid > 110 && mid < 145, "half fade lum %u", mid);
    /* Only the rectangle changes. */
    EH_CHECK(fb2[290 * EH_FB_W + 150] == 0x0000 && fb2[99 * EH_FB_W + 150] == 0x0000);
    EH_CHECK(fb2[110 * EH_FB_W + 99] == 0x0000 && fb2[110 * EH_FB_W + 200] == 0x0000);
    EH_CHECK(fb2[100 * EH_FB_W + 100] != 0x0000 && fb2[279 * EH_FB_W + 199] != 0x0000);
}

void test_torus_knot_renders_and_responds(void)
{
    eh_camera cam;
    eh_camera_init(&cam);
    eh_torus_init(&torus, 0);
    eh_torus_render(&torus, fa, 6000, 14000, &cam);
    const double cov = (double)torus_covered(&torus) / EH_FB_PIXELS;
    EH_CHECK_MSG(cov > 0.08 && cov < 0.6, "knot covers %.3f of the frame", cov);
    EH_CHECK(eh_test_distinct_colors(fa) > 256);

    cam.yaw = 1.0f;
    eh_torus_render(&torus, fb2, 6000, 14000, &cam);
    /* Same plasma underneath, so every difference is the knot moving. */
    EH_CHECK_MSG(eh_test_diff_fraction(fa, fb2) > 0.05, "camera yaw changed %.3f",
                 eh_test_diff_fraction(fa, fb2));

    cam.yaw = 0.0f;
    cam.wireframe = 1;
    eh_torus_render(&torus, fb2, 6000, 14000, &cam);
    EH_CHECK(eh_test_diff_fraction(fa, fb2) > 0.05);
    EH_CHECK(torus_covered(&torus) == 0);
}

void test_torus_transitions(void)
{
    eh_camera cam;
    eh_camera_init(&cam);
    eh_torus_init(&torus, 0);
    eh_torus_render(&torus, fa, 0, 14000, &cam);
    EH_CHECK_MSG(eh_test_mean_lum_ring(fa, 0, 180) < 30, "start lum %.1f",
                 eh_test_mean_lum_ring(fa, 0, 180));
    eh_torus_render(&torus, fa, 14000 - 30, 14000, &cam);
    EH_CHECK_MSG(eh_test_mean_lum_ring(fa, 0, 180) > 220, "dive-out lum %.1f",
                 eh_test_mean_lum_ring(fa, 0, 180));
}

/* Skipping strips by ring y range must not change any pixel: compare with
 * pass 1 rerun on the same pass-0 output with every strip range unbounded.
 * Covers a mid-scene frame and a dive frame with near-clipped vertices. */
void test_torus_strip_cull_exact(void)
{
    eh_camera cam;
    eh_camera_init(&cam);
    eh_torus_init(&torus, 0);
    const uint32_t times[] = {6000, 13300, 13800};
    for (int k = 0; k < 3; k++) {
        eh_torus_prepare(&torus, times[k], 14000, &cam);
        for (int part = 0; part < EH_PARTS; part++)
            eh_torus_render_part(&torus, fa, 0, part, 0);
        memcpy(fb2, fa, sizeof fa);
        int skipped = 0;
        for (int part = 0; part < EH_PARTS; part++) {
            const int y0 = part * EH_PART_ROWS, y1 = y0 + EH_PART_ROWS;
            for (int u = 0; u < EH_TORUS_NU; u++) {
                const int u1 = (u + 1) % EH_TORUS_NU;
                skipped += (torus.ring_ymax[u] < (y0 - 1) * 8 && torus.ring_ymax[u1] < (y0 - 1) * 8) ||
                           (torus.ring_ymin[u] > (y1 + 1) * 8 && torus.ring_ymin[u1] > (y1 + 1) * 8);
            }
            eh_torus_render_part(&torus, fa, 1, part, 0);
        }
        EH_CHECK_MSG(skipped > EH_TORUS_NU, "t=%u only %d strips skipped", times[k], skipped);
        for (int u = 0; u < EH_TORUS_NU; u++) {
            torus.ring_ymin[u] = INT16_MIN;
            torus.ring_ymax[u] = INT16_MAX;
        }
        for (int part = 0; part < EH_PARTS; part++)
            eh_torus_render_part(&torus, fb2, 1, part, 0);
        EH_CHECK_MSG(memcmp(fa, fb2, sizeof fa) == 0, "t=%u strip cull changed %.5f", times[k],
                     eh_test_diff_fraction(fa, fb2));
    }
}

void test_tunnel_depth_motion_and_camera(void)
{
    eh_camera cam;
    eh_camera_init(&cam);
    eh_tunnel_init(&tunnel, 0);
    eh_tunnel_render(&tunnel, fa, 5000, 12000, &cam);
    EH_CHECK(eh_test_distinct_colors(fa) > 64);
    double centre = eh_test_mean_lum_ring(fa, 0, 16), rim = eh_test_mean_lum_ring(fa, 120, 176);
    EH_CHECK_MSG(centre + 20 < rim, "centre %.1f rim %.1f", centre, rim);

    eh_tunnel_render(&tunnel, fb2, 5100, 12000, &cam);
    EH_CHECK(eh_test_diff_fraction(fa, fb2) > 0.3);

    cam.yaw = 0.8f;
    cam.pitch = 0.4f;
    eh_tunnel_render(&tunnel, fb2, 5000, 12000, &cam);
    EH_CHECK(eh_test_diff_fraction(fa, fb2) > 0.3);

    cam.yaw = cam.pitch = 0.0f;
    eh_tunnel_render(&tunnel, fa, 12000 - 30, 12000, &cam);
    EH_CHECK(eh_test_mean_lum_ring(fa, 0, 180) > 220);
}

void test_tunnel_centre_continuous_across_yaw_wrap(void)
{
    /* Camera yaw wraps from +pi to -pi while LEFT/RIGHT is held; the tunnel
     * centre must not jump (it skipped 80 px on the badge). */
    eh_camera cam;
    eh_camera_init(&cam);
    cam.yaw = (float)M_PI - 1e-3f;
    eh_tunnel_prepare(&tunnel, 5000, 12000, &cam);
    const int before = tunnel.cx;
    cam.yaw = -(float)M_PI + 1e-3f;
    eh_tunnel_prepare(&tunnel, 5000, 12000, &cam);
    EH_CHECK_MSG(abs(tunnel.cx - before) <= 1, "cx %d -> %d across the wrap", before, tunnel.cx);

    /* Yaw still steers: a quarter turn each way moves the centre apart. */
    cam.yaw = 0.5f * (float)M_PI;
    eh_tunnel_prepare(&tunnel, 5000, 12000, &cam);
    const int right = tunnel.cx;
    cam.yaw = -0.5f * (float)M_PI;
    eh_tunnel_prepare(&tunnel, 5000, 12000, &cam);
    EH_CHECK_MSG(right - tunnel.cx >= 40, "cx right %d left %d", right, tunnel.cx);
}

void test_tunnel_angle_matches_atan2(void)
{
    /* The mirrored quadrant table must reproduce the full-plane angle around
     * any camera-shifted centre, including across the axes. */
    eh_camera cam;
    eh_camera_init(&cam);
    eh_tunnel_init(&tunnel, 0);
    const int centres[][2] = {{180, 180}, {140, 220}, {220, 140}, {167, 201}};
    int worst = 0;
    for (size_t c = 0; c < sizeof centres / sizeof centres[0]; c++) {
        tunnel.cx = centres[c][0];
        tunnel.cy = centres[c][1];
        for (int y = 0; y < EH_FB_H; y++)
            for (int x = 0; x < EH_FB_W; x++) {
                if (!eh_test_inside(x, y))
                    continue;
                double ref = atan2(y + 0.5 - tunnel.cy, x + 0.5 - tunnel.cx) * 1024.0 / (2 * M_PI);
                if (ref < 0)
                    ref += 1024.0;
                int diff = abs((int)eh_tunnel_angle(&tunnel, x, y) - (int)floor(ref));
                diff = diff > 512 ? 1024 - diff : diff;
                worst = diff > worst ? diff : worst;
            }
    }
    EH_CHECK_MSG(worst <= 1, "angle off by %d/1024 turn", worst);
}

void test_terrain_heightmap_deterministic_varied(void)
{
    eh_terrain_init(&terrain);
    eh_terrain_init(&terrain2);
    EH_CHECK(memcmp(terrain.height, terrain2.height, sizeof terrain.height) == 0);
    EH_CHECK(memcmp(terrain.color, terrain2.color, sizeof terrain.color) == 0);
    int lo = 255, hi = 0;
    for (int i = 0; i < EH_TERRAIN_SIZE * EH_TERRAIN_SIZE; i++) {
        lo = terrain.height[i] < lo ? terrain.height[i] : lo;
        hi = terrain.height[i] > hi ? terrain.height[i] : hi;
    }
    EH_CHECK_MSG(hi - lo > 100, "height range %d..%d", lo, hi);
}

void test_terrain_sky_sun_and_camera(void)
{
    eh_camera cam;
    eh_camera_init(&cam);
    eh_terrain_init(&terrain);
    EH_CHECK(eh_terrain_sun_y(12000, 16000) < eh_terrain_sun_y(3000, 16000));

    eh_terrain_render(&terrain, fa, 8000, 16000, &cam);
    EH_CHECK(eh_test_distinct_colors(fa) > 128);
    /* Ground fills the bottom of the circle; the top is sky. */
    double sky = 0, ground = 0;
    for (int x = 160; x < 200; x++) {
        sky += eh_test_lum(fa[16 * EH_FB_W + x]);
        ground += eh_test_lum(fa[344 * EH_FB_W + x]);
    }
    EH_CHECK_MSG(sky != ground, "sky and ground rows identical");
    /* Sky rows are a vertical gradient; a ground row carries texture. */
    int textured = 0;
    for (int x = 121; x < 240; x++)
        textured += fa[344 * EH_FB_W + x] != fa[344 * EH_FB_W + x - 1];
    EH_CHECK_MSG(textured > 20, "ground row has %d colour changes", textured);

    eh_terrain_render(&terrain, fb2, 8200, 16000, &cam);
    EH_CHECK_MSG(eh_test_diff_fraction(fa, fb2) > 0.2, "flight moved %.3f",
                 eh_test_diff_fraction(fa, fb2));
    cam.yaw = 0.7f;
    eh_terrain_render(&terrain, fb2, 8000, 16000, &cam);
    EH_CHECK(eh_test_diff_fraction(fa, fb2) > 0.2);
    cam.yaw = 0.0f;
    cam.pitch = 0.5f;
    eh_terrain_render(&terrain, fb2, 8000, 16000, &cam);
    EH_CHECK(eh_test_diff_fraction(fa, fb2) > 0.2);

    cam.pitch = 0.0f;
    eh_terrain_render(&terrain, fa, 0, 16000, &cam);
    EH_CHECK(eh_test_mean_lum_ring(fa, 0, 180) > 220);
}

static double white_fraction(const eh_rgb565 *fb)
{
    int n = 0, white = 0;
    for (int y = 0; y < EH_FB_H; y++)
        for (int x = 0; x < EH_FB_W; x++)
            if (eh_test_inside(x, y)) {
                n++;
                white += eh_test_lum(fb[y * EH_FB_W + x]) > 230;
            }
    return (double)white / n;
}

void test_terrain_camera_clears_peaks(void)
{
    /* v1 flew at a fixed height through snow peaks, whiting out the frame
     * around local 1.5-2.5 s. After the fade-in, no frame may be mostly
     * near-white, and the eye height must move smoothly. */
    eh_camera cam;
    eh_camera_init(&cam);
    eh_terrain_init(&terrain);
    double worst = 0;
    uint32_t worst_ms = 0;
    for (uint32_t t = 1000; t < 16000; t += 100) {
        eh_terrain_render(&terrain, fa, t, 16000, &cam);
        const double w = white_fraction(fa);
        if (w > worst) {
            worst = w;
            worst_ms = t;
        }
    }
    EH_CHECK_MSG(worst < 0.5, "%.2f of the display near-white at local %u ms", worst, worst_ms);

    float max_step = 0;
    float prev = eh_terrain_altitude(&terrain, 0);
    for (uint32_t t = 33; t < 16000; t += 33) {
        const float a = eh_terrain_altitude(&terrain, t);
        max_step = fabsf(a - prev) > max_step ? fabsf(a - prev) : max_step;
        prev = a;
    }
    /* CLIMB is 30 units/s: at most ~1 unit per 33 ms frame. */
    EH_CHECK_MSG(max_step < 1.05f, "eye height jumps %.2f in one 30 fps frame", max_step);
}

void test_logo_targets_inside_display(void)
{
    static uint16_t tx[EH_LOGO_MAX_TARGETS], ty[EH_LOGO_MAX_TARGETS];
    int n = eh_logo_targets(tx, ty, EH_LOGO_MAX_TARGETS);
    EH_CHECK_MSG(n > 4000 && n < EH_LOGO_MAX_TARGETS, "%d targets", n);
    int outside = 0;
    for (int i = 0; i < n; i++)
        outside += !eh_test_inside(tx[i], ty[i]);
    EH_CHECK(outside == 0);
}

/* The end screen reads "EVENT HORIZON" over "SAINTCON 2026"
 * (2026-10-29). Decoded from the target bitmap alone: text bands by row
 * occupancy, glyphs by column gaps, words by wider gaps; glyph shapes for
 * the line starts are written out here independently of the renderer. */
static uint8_t logo_bitmap[EH_FB_H][EH_FB_W];

static int band_glyphs(int y0, int y1, int *words, int *x_first, int *x_last)
{
    int glyphs = 0, gap = 0, in_glyph = 0;
    *words = 0;
    *x_first = -1;
    for (int x = 0; x < EH_FB_W; x++) {
        int lit = 0;
        for (int y = y0; y < y1; y++)
            lit |= logo_bitmap[y][x];
        if (lit) {
            if (!in_glyph) {
                glyphs++;
                if (*x_first < 0 || gap > 12)
                    ++*words;
                if (*x_first < 0)
                    *x_first = x;
            }
            in_glyph = 1;
            gap = 0;
            *x_last = x;
        } else {
            in_glyph = 0;
            gap++;
        }
    }
    return glyphs;
}

/* 5x7 cell pattern of the glyph whose left edge is x0 (scale px per cell). */
static void glyph_rows(int x0, int y0, int scale, uint8_t rows[7])
{
    for (int r = 0; r < 7; r++) {
        rows[r] = 0;
        for (int c = 0; c < 5; c++)
            rows[r] = (uint8_t)(rows[r] << 1 | logo_bitmap[y0 + r * scale + scale / 2][x0 + c * scale + scale / 2]);
    }
}

void test_logo_reads_event_horizon_then_saintcon(void)
{
    static uint16_t tx[EH_LOGO_MAX_TARGETS], ty[EH_LOGO_MAX_TARGETS];
    int n = eh_logo_targets(tx, ty, EH_LOGO_MAX_TARGETS);
    EH_CHECK_MSG(n <= EH_PARTICLE_COUNT, "%d targets for %d particles", n, EH_PARTICLE_COUNT);
    memset(logo_bitmap, 0, sizeof logo_bitmap);
    for (int i = 0; i < n; i++)
        logo_bitmap[ty[i]][tx[i]] = 1;

    int bands[4][2], nb = 0, prev = 0;
    for (int y = 0; y <= EH_FB_H; y++) {
        int lit = 0;
        for (int x = 0; y < EH_FB_H && x < EH_FB_W; x++)
            lit |= logo_bitmap[y][x];
        if (lit && !prev && nb < 4)
            bands[nb][0] = y;
        if (!lit && prev && nb < 4)
            bands[nb++][1] = y;
        prev = lit;
    }
    EH_CHECK_MSG(nb == 2, "%d text bands", nb);
    if (nb != 2)
        return;

    static const uint8_t E[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    static const uint8_t S[7] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
    const uint8_t *first[2] = {E, S};
    for (int b = 0; b < 2; b++) {
        int words, x0, x1;
        int glyphs = band_glyphs(bands[b][0], bands[b][1], &words, &x0, &x1);
        /* EVENT HORIZON and SAINTCON 2026: 12 glyphs in two words each. */
        EH_CHECK_MSG(glyphs == 12 && words == 2, "band %d: %d glyphs, %d words", b, glyphs, words);
        const int scale = (bands[b][1] - bands[b][0]) / 7;
        EH_CHECK_MSG(scale >= 3, "band %d scale %d too small to read", b, scale);
        uint8_t rows[7];
        glyph_rows(x0, bands[b][0], scale, rows);
        EH_CHECK_MSG(memcmp(rows, first[b], 7) == 0, "band %d does not start with %c", b, b ? 'S' : 'E');
        /* Every corner at least 12 px inside the 180 px panel circle. */
        const int ys[2] = {bands[b][0], bands[b][1] - 1}, xs[2] = {x0, x1};
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++) {
                float dx = xs[j] + 0.5f - EH_FB_W / 2.0f, dy = ys[i] + 0.5f - EH_FB_H / 2.0f;
                EH_CHECK_MSG(sqrtf(dx * dx + dy * dy) <= 168.0f, "band %d corner (%d,%d)", b, xs[j], ys[i]);
            }
    }
}

void test_particles_shatter_then_rebuild(void)
{
    eh_camera cam;
    eh_camera_init(&cam);
    for (int i = 0; i < EH_FB_PIXELS; i++)
        fa[i] = 0x07E0u; /* source frame: solid green */
    eh_particles_init(&particles, fa);
    EH_CHECK(particles.ntargets > 4000);
    EH_CHECK_MSG(EH_PARTICLE_COUNT >= particles.ntargets, "%d particles for %d targets",
                 EH_PARTICLE_COUNT, particles.ntargets);

    /* At t = 0 the fragments tile the source disc as a mosaic. */
    eh_particles_render(&particles, fa, 0, 12000, &cam);
    int green = 0;
    for (int i = 0; i < EH_FB_PIXELS; i++)
        green += fa[i] == 0x07E0u;
    EH_CHECK_MSG(green > 80000, "%d source-coloured pixels at t=0", green);

    eh_particles_render(&particles, fa, EH_PARTICLES_SETTLED_MS, 12000, &cam);
    int lit = 0;
    for (int i = 0; i < particles.ntargets; i++)
        lit += eh_test_lum(fa[particles.ty[i] * EH_FB_W + particles.tx[i]]) > 100;
    EH_CHECK_MSG(lit == particles.ntargets, "%d of %d targets lit when settled", lit,
                 particles.ntargets);
    EH_CHECK(eh_test_mean_lum_ring(fa, 0, 180) < 120);

    eh_particles_render(&particles, fb2, 500, 12000, &cam);
    int early = 0;
    for (int i = 0; i < particles.ntargets; i++)
        early += eh_test_lum(fb2[particles.ty[i] * EH_FB_W + particles.tx[i]]) > 100;
    EH_CHECK_MSG(early * 2 < particles.ntargets, "%d targets already lit at 500 ms", early);

    cam.yaw = 0.9f;
    eh_particles_render(&particles, fb2, EH_PARTICLES_SETTLED_MS, 12000, &cam);
    EH_CHECK(eh_test_diff_fraction(fa, fb2) > 0.02);

    cam.yaw = 0.0f;
    eh_particles_render(&particles, fa, 12000 - 20, 12000, &cam);
    EH_CHECK(eh_test_mean_lum_ring(fa, 0, 180) < 20);
}

void test_particles_trails_follow_motion(void)
{
#if !EH_PARTICLE_TRAILS
    /* Trails compiled out (ablation build): nothing to check. */
#else
    eh_camera cam;
    eh_camera_init(&cam);
    for (int i = 0; i < EH_FB_PIXELS; i++)
        fa[i] = 0xFFE0u;
    eh_particles_init(&particles, fa);

    /* Mid-rebuild (heads are single pixels by then): streaks are several
     * pixels long and light far more pixels than there are particles. */
    eh_particles_render(&particles, fa, 3000, 12000, &cam);
    double len = 0;
    int visible = 0;
    for (int i = 0; i < EH_PARTICLE_COUNT; i++) {
        const eh_particle_seg *s = &particles.seg[i];
        if (s->hx == INT16_MIN)
            continue;
        visible++;
        len += hypot(s->hx - s->tx, s->hy - s->ty) / 16.0;
    }
    EH_CHECK_MSG(visible > EH_PARTICLE_COUNT / 2 && len / visible > 4.0, "mean streak %.2f px",
                 visible ? len / visible : 0.0);
    const eh_rgb565 bg_top = fa[0];
    int lit = 0;
    for (int y = 0; y < EH_FB_H; y++)
        for (int x = 0; x < EH_FB_W; x++)
            lit += eh_test_lum(fa[y * EH_FB_W + x]) > eh_test_lum(bg_top) + 40;
    EH_CHECK_MSG(lit > EH_PARTICLE_COUNT * 2, "%d lit pixels for %d particles", lit,
                 EH_PARTICLE_COUNT);

    /* Settled particles have no streak. */
    eh_particles_render(&particles, fa, EH_PARTICLES_SETTLED_MS + 500, 12000, &cam);
    int streaks = 0;
    for (int i = 0; i < EH_PARTICLE_COUNT; i++)
        streaks += particles.seg[i].hx != particles.seg[i].tx || particles.seg[i].hy != particles.seg[i].ty;
    EH_CHECK_MSG(streaks == 0, "%d particles still streak when settled", streaks);
#endif
}
