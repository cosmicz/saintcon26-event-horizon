#include <stdlib.h>
#include <string.h>

#include "eh/eh_demo.h"
#include "eh/eh_leds.h"
#include "eh_pool.h"
#include "eh_present.h"
#include "eh_test.h"
#include "test_util.h"

static eh_demo da, db;
static eh_rgb565 fa[EH_FB_PIXELS], fb2[EH_FB_PIXELS];

static const uint32_t mids[EH_SCENE_COUNT] = {
    EH_SCENE_TORUS_MS / 2,
    EH_SCENE_TORUS_MS + EH_SCENE_TUNNEL_MS / 2,
    EH_SCENE_TORUS_MS + EH_SCENE_TUNNEL_MS + EH_SCENE_TERRAIN_MS / 2,
    EH_SCENE_TORUS_MS + EH_SCENE_TUNNEL_MS + EH_SCENE_TERRAIN_MS + 8000,
};

void test_timeline_boundaries(void)
{
    struct {
        uint32_t t;
        int scene;
        uint32_t local;
    } cases[] = {
        {0, EH_SCENE_TORUS, 0},
        {13999, EH_SCENE_TORUS, 13999},
        {14000, EH_SCENE_TUNNEL, 0},
        {26000, EH_SCENE_TERRAIN, 0},
        {42000, EH_SCENE_PARTICLES, 0},
        {53999, EH_SCENE_PARTICLES, 11999},
        {54000, EH_SCENE_TORUS, 0},
        {54000 * 3 + 14500, EH_SCENE_TUNNEL, 500},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        eh_timeline_pos p = eh_timeline_at(cases[i].t);
        EH_CHECK_MSG(p.scene == cases[i].scene && p.local_ms == cases[i].local,
                     "t=%u -> scene %d local %u", cases[i].t, p.scene, p.local_ms);
    }
    EH_CHECK(EH_DEMO_LOOP_MS == 54000u);
    for (int s = 0; s < EH_SCENE_COUNT; s++)
        for (int u = s + 1; u < EH_SCENE_COUNT; u++)
            EH_CHECK(strcmp(eh_scene_name(s), eh_scene_name(u)) != 0);
}

static void play(eh_demo *d, uint32_t from, uint32_t to, uint32_t buttons, eh_rgb565 *fb)
{
    for (uint32_t t = from; t < to; t += 33)
        eh_demo_frame(d, t, buttons, fb);
    eh_demo_frame(d, to, buttons, fb);
}

void test_demo_every_scene_renders_with_leds(void)
{
    eh_rgb prev[EH_LED_COUNT];
    for (int s = 0; s < EH_SCENE_COUNT; s++) {
        eh_demo_init(&da);
        eh_demo_frame(&da, mids[s], 0, fa);
        EH_CHECK_MSG(eh_timeline_at(mids[s]).scene == s, "mid of scene %d", s);
        EH_CHECK_MSG(eh_test_distinct_colors(fa) >= 16, "scene %d flat", s);
        int lit = 0;
        for (int k = 0; k < EH_LED_COUNT; k++)
            lit += da.leds[k].r + da.leds[k].g + da.leds[k].b > 0;
        EH_CHECK_MSG(lit > 0, "scene %d leds dark", s);
        for (int k = 0; k < EH_LED_COUNT; k++)
            EH_CHECK(da.leds[k].r <= da.led_max && da.leds[k].g <= da.led_max &&
                     da.leds[k].b <= da.led_max);
        if (s > 0)
            EH_CHECK_MSG(memcmp(prev, da.leds, sizeof prev) != 0, "scene %d leds same as %d", s,
                         s - 1);
        memcpy(prev, da.leds, sizeof prev);
    }
}

void test_demo_dpad_changes_every_scene(void)
{
    for (int s = 0; s < EH_SCENE_COUNT; s++) {
        eh_demo_init(&da);
        eh_demo_init(&db);
        play(&da, mids[s] - 1000, mids[s], 0, fa);
        play(&db, mids[s] - 1000, mids[s], EH_BTN_RIGHT, fb2);
        double d = eh_test_diff_fraction(fa, fb2);
        EH_CHECK_MSG(d > 0.05, "scene %d: RIGHT changed %.3f of pixels", s, d);
        EH_CHECK(db.cam.yaw > 1.0f);
    }
}

void test_demo_seek_matches_playback(void)
{
    /* Stateless scenes: jumping straight to t equals playing up to t. The
     * particle check is mid-shatter, while particles still carry colours
     * sampled from the terrain frame they were seeded from. */
    const uint32_t particles_start = EH_SCENE_TORUS_MS + EH_SCENE_TUNNEL_MS + EH_SCENE_TERRAIN_MS;
    uint32_t t = particles_start + 1500;
    for (int i = 0; i < EH_FB_PIXELS; i++)
        fa[i] = 0x1234; /* unrelated prior content must not leak into the seed */
    eh_demo_init(&da);
    eh_demo_frame(&da, t, 0, fa);
    eh_demo_init(&db);
    play(&db, particles_start - 2000, t, 0, fb2);
    EH_CHECK(memcmp(fa, fb2, sizeof fa) == 0);

    t = mids[EH_SCENE_TUNNEL];
    eh_demo_init(&da);
    eh_demo_frame(&da, t, 0, fa);
    eh_demo_init(&db);
    play(&db, t - 2000, t, 0, fb2);
    EH_CHECK(memcmp(fa, fb2, sizeof fa) == 0);
}

/* Rendering must define every displayed pixel: a fresh demo seeking to t on
 * two framebuffers with different prior contents must present identical
 * panels. Times cover every scene, both entry frames and mid-scene. */
void test_demo_display_independent_of_prior_fb(void)
{
    static uint8_t ca[EH_PRESENT_W * EH_PRESENT_H * 3], cb[EH_PRESENT_W * EH_PRESENT_H * 3];
    static const uint32_t times[] = {0, 7000, 13990, 14000, 20000, 25990, 26000, 34000,
                                     42000, 43500, 50000, 53990};
    for (size_t i = 0; i < sizeof times / sizeof times[0]; i++) {
        for (int k = 0; k < EH_FB_PIXELS; k++) {
            fa[k] = 0x0000;
            fb2[k] = 0x5A5A;
        }
        eh_demo_init(&da);
        eh_demo_frame(&da, times[i], 0, fa);
        eh_demo_init(&db);
        eh_demo_frame(&db, times[i], 0, fb2);
        eh_present(fa, da.leds, da.led_max, ca);
        eh_present(fb2, db.leds, db.led_max, cb);
        int differ = 0;
        for (size_t b = 0; b < sizeof ca; b += 3)
            differ += ca[b] != cb[b] || ca[b + 1] != cb[b + 1] || ca[b + 2] != cb[b + 2];
        EH_CHECK_MSG(differ == 0, "t=%u: %d presented pixels depend on prior framebuffer", times[i],
                     differ);
    }
}

void test_demo_palette_button(void)
{
    uint32_t t = mids[EH_SCENE_TORUS];
    eh_demo_init(&da);
    eh_demo_init(&db);
    play(&da, t - 200, t, 0, fa);
    play(&db, t - 200, t - 100, EH_BTN_A, fb2);
    play(&db, t - 100 + 33, t, 0, fb2);
    EH_CHECK(db.cam.palette == 1);
    EH_CHECK(eh_test_diff_fraction(fa, fb2) > 0.3);
}

/* --- dual-core frame plan --------------------------------------------------- */

static const uint32_t plan_times[] = {500, 7000, 13500, 14100, 20000, 26900, 34000,
                                      42000, 42200, 43500, 47000, 53500};

/* Pixels part `part` may write: rows for every scene but terrain, which
 * splits by columns. */
static int in_part(int scene, int part, int x, int y)
{
    if (scene == EH_SCENE_TERRAIN)
        return x / (EH_FB_W / EH_PARTS) == part;
    return y / EH_PART_ROWS == part;
}

void test_demo_parts_write_disjoint_regions(void)
{
    static eh_rgb565 before[EH_FB_PIXELS];
    for (size_t i = 0; i < sizeof plan_times / sizeof plan_times[0]; i++) {
        eh_demo_init(&da);
        eh_demo_prepare(&da, plan_times[i], 0, fa);
        const int scene = da.pos.scene, passes = eh_demo_passes(&da);
        EH_CHECK(passes >= 1 && passes <= 2);
        for (int pass = 0; pass < passes; pass++) {
            memcpy(before, fa, sizeof before);
            for (int part = 0; part < EH_PARTS; part++) {
                for (int k = 0; k < EH_FB_PIXELS; k++)
                    fb2[k] = in_part(scene, part, k % EH_FB_W, k / EH_FB_W) ? before[k] : 0x5A5A;
                eh_demo_render_part(&da, fb2, pass, part, part % EH_WORKERS);
                int stray = 0;
                for (int k = 0; k < EH_FB_PIXELS; k++)
                    stray += !in_part(scene, part, k % EH_FB_W, k / EH_FB_W) && fb2[k] != 0x5A5A;
                EH_CHECK_MSG(stray == 0, "t=%u pass %d part %d wrote %d pixels outside its region",
                             plan_times[i], pass, part, stray);
            }
            for (int part = 0; part < EH_PARTS; part++)
                eh_demo_render_part(&da, fa, pass, part, part % EH_WORKERS);
        }
    }
}

void test_demo_parts_any_schedule_bit_exact(void)
{
    /* The schedule two cores could produce: parts of a pass in any order,
     * with the other worker's scratch. Must equal the serial frame. */
    for (size_t i = 0; i < sizeof plan_times / sizeof plan_times[0]; i++) {
        const uint32_t t = plan_times[i];
        eh_demo_init(&da);
        play(&da, t > 1000 ? t - 1000 : 0, t, EH_BTN_LEFT, fa);
        eh_demo_init(&db);
        for (uint32_t s = t > 1000 ? t - 1000 : 0; s <= t; s = s + 33 < t || s == t ? s + 33 : t) {
            for (int k = 0; k < EH_FB_PIXELS; k++)
                fb2[k] = (eh_rgb565)(k * 2654435761u >> 16);
            eh_demo_prepare(&db, s, EH_BTN_LEFT, fb2);
            for (int pass = 0; pass < eh_demo_passes(&db); pass++)
                for (int part = EH_PARTS - 1; part >= 0; part--)
                    eh_demo_render_part(&db, fb2, pass, part, (part + 1) % EH_WORKERS);
            eh_demo_finish(&db, fb2);
            if (s == t)
                break;
        }
        int differ = 0;
        for (int y = 0; y < EH_FB_H; y++)
            for (int x = 0; x < EH_FB_W; x++)
                differ += eh_test_inside(x, y) && fa[y * EH_FB_W + x] != fb2[y * EH_FB_W + x];
        EH_CHECK_MSG(differ == 0, "t=%u: %d displayed pixels differ from serial", t, differ);
        EH_CHECK(memcmp(da.leds, db.leds, sizeof da.leds) == 0);
    }
}

void test_pool_two_threads_match_serial(void)
{
    eh_pool *pool = eh_pool_create(EH_WORKERS);
    EH_CHECK(pool != NULL && eh_pool_workers(pool) == EH_WORKERS);
    EH_CHECK(eh_pool_create(3) == NULL);
    if (!pool)
        return;
    eh_demo_init(&da);
    eh_demo_init(&db);
    int mismatched = 0;
    /* Every 4th 30 fps frame of a whole loop, through every scene entry. */
    for (uint32_t k = 0; k * 133u < EH_DEMO_LOOP_MS; k++) {
        const uint32_t t = k * 133u, buttons = (k / 40) % 2 ? EH_BTN_UP : 0;
        eh_demo_frame(&da, t, buttons, fa);
        eh_pool_frame(pool, &db, t, buttons, fb2);
        mismatched += memcmp(fa, fb2, sizeof fa) != 0 || memcmp(da.leds, db.leds, sizeof da.leds) != 0;
    }
    EH_CHECK_MSG(mismatched == 0, "%d threaded frames differ from serial", mismatched);
    eh_pool_destroy(pool);
}

/* --- memory budget ------------------------------------------------------------ */

void test_memory_budget_in_band(void)
{
    /* Operator target: core working set at 80-90% of RP2350 SRAM, with at
     * least 50 KB left for stacks, heap and SDK/driver BSS. */
    const size_t core = sizeof(eh_rgb565) * EH_FB_PIXELS + sizeof(eh_demo);
    EH_CHECK_MSG(core * 100 >= EH_SRAM_BYTES * 80u && core * 100 <= EH_SRAM_BYTES * 90u,
                 "core %zu bytes = %.1f%% of SRAM", core, 100.0 * core / EH_SRAM_BYTES);
    EH_CHECK_MSG(EH_SRAM_BYTES - core >= 50u * 1024u, "headroom %zu", EH_SRAM_BYTES - core);
    /* The union is exactly the largest workspace: nothing is padding. */
    size_t largest = sizeof(eh_torus_ws);
    largest = sizeof(eh_tunnel_ws) > largest ? sizeof(eh_tunnel_ws) : largest;
    largest = sizeof(eh_terrain_ws) > largest ? sizeof(eh_terrain_ws) : largest;
    largest = sizeof(eh_particles_ws) > largest ? sizeof(eh_particles_ws) : largest;
    EH_CHECK(sizeof(((eh_demo *)0)->ws) == largest);
}
