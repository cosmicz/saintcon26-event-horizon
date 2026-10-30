/* Headless host CLI: frame dumps, benchmarks and the static memory table.
 *
 *   eh_cli frames --out DIR [--at T1,T2,...] [--hold BTN:FROM-TO]...
 *   eh_cli bench plasma [--frames N] [--warmup W] [--noclip] [--bands B] [--palette P]
 *   eh_cli bench demo [--warmup-loops L] [--threads 1|2]
 *   eh_cli memory
 *
 * Benchmarks report host wall-clock times (see EH_CLOCK below). They are
 * measurements of this host build only, not of an RP2350 or the badge. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#include "eh/eh_demo.h"
#include "eh/eh_leds.h"
#include "eh/eh_plasma.h"
#include "eh/eh_tables.h"
#include "eh_pool.h"
#include "eh_present.h"

#ifndef EH_BUILD_INFO
#define EH_BUILD_INFO "unknown"
#endif

#define STEP_MS(k) ((uint32_t)((k) * 100u / 3u)) /* 30 fps timeline steps */

static eh_rgb565 fb[EH_FB_PIXELS];
static uint8_t canvas[EH_PRESENT_W * EH_PRESENT_H * 3];
static eh_demo demo;

/* --- timing and statistics ---------------------------------------------- */

/* macOS CLOCK_MONOTONIC ticks in whole microseconds, too coarse for ~30 us
 * frames; CLOCK_MONOTONIC_RAW resolves ~42 ns there. */
#ifdef CLOCK_MONOTONIC_RAW
#define EH_CLOCK CLOCK_MONOTONIC_RAW
#define EH_CLOCK_NAME "CLOCK_MONOTONIC_RAW"
#else
#define EH_CLOCK CLOCK_MONOTONIC
#define EH_CLOCK_NAME "CLOCK_MONOTONIC"
#endif

static double now_us(void)
{
    struct timespec ts;
    clock_gettime(EH_CLOCK, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static long clock_res_ns(void)
{
    struct timespec r;
    return clock_getres(EH_CLOCK, &r) == 0 ? r.tv_sec * 1000000000L + r.tv_nsec : -1;
}

static int cmp_double(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

typedef struct {
    int n;
    double min, p50, mean, p95, p99, max;
} stats;

static stats summarize(const double *samples, int n)
{
    stats s = {n, 0, 0, 0, 0, 0, 0};
    if (n == 0)
        return s;
    double *v = malloc(sizeof(double) * (size_t)n);
    memcpy(v, samples, sizeof(double) * (size_t)n);
    qsort(v, (size_t)n, sizeof(double), cmp_double);
    double sum = 0;
    for (int i = 0; i < n; i++)
        sum += v[i];
    s.min = v[0];
    s.max = v[n - 1];
    s.mean = sum / n;
    s.p50 = v[(n - 1) / 2];
    s.p95 = v[(int)((n - 1) * 0.95)];
    s.p99 = v[(int)((n - 1) * 0.99)];
    free(v);
    return s;
}

static void print_stats(const char *name, stats s, const char *indent, int trailing_comma)
{
    printf("%s\"%s\": {\"n\": %d, \"min\": %.2f, \"p50\": %.2f, \"mean\": %.2f, \"p95\": %.2f, "
           "\"p99\": %.2f, \"max\": %.2f}%s\n",
           indent, name, s.n, s.min, s.p50, s.mean, s.p95, s.p99, s.max, trailing_comma ? "," : "");
}

static long long peak_rss_bytes(void)
{
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0)
        return -1;
#ifdef __APPLE__
    return (long long)ru.ru_maxrss; /* bytes on macOS */
#else
    return (long long)ru.ru_maxrss * 1024; /* KiB on Linux */
#endif
}

static void host_cpu(char *out, size_t n)
{
    snprintf(out, n, "unknown");
#ifdef __APPLE__
    size_t len = n;
    if (sysctlbyname("machdep.cpu.brand_string", out, &len, NULL, 0) != 0)
        snprintf(out, n, "unknown");
#endif
}

static uint32_t fnv1a(const eh_rgb565 *p, int n)
{
    uint32_t h = 2166136261u;
    const uint8_t *b = (const uint8_t *)p;
    for (int i = 0; i < n * 2; i++)
        h = (h ^ b[i]) * 16777619u;
    return h;
}

static void print_header(const char *bench)
{
    char cpu[128];
    host_cpu(cpu, sizeof cpu);
    printf("{\n  \"benchmark\": \"%s\",\n  \"scope\": \"host only; not an RP2350 or badge "
           "measurement\",\n",
           bench);
    printf("  \"host_cpu\": \"%s\",\n  \"build\": \"%s\",\n", cpu, EH_BUILD_INFO);
    printf("  \"clock\": \"%s wall time, microseconds\",\n  \"clock_res_ns\": %ld,\n",
           EH_CLOCK_NAME, clock_res_ns());
}

/* --- memory --------------------------------------------------------------- */

/* v1 (180x180, commit 5889e0d) as measured by its own `eh_cli memory`. */
#define V1_CORE_RAM 205188u

typedef struct {
    const char *name;
    size_t bytes;
    const char *buys;
    char shape[48]; /* from the build's knobs, so ablated builds report themselves */
} block;

static void print_blocks(const char *scene, size_t total, const block *b, int n, int comma)
{
    printf("    \"%s\": {\"bytes\": %zu, \"blocks\": [\n", scene, total);
    for (int i = 0; i < n; i++)
        printf("      {\"name\": \"%s\", \"bytes\": %zu, \"shape\": \"%s\", \"buys\": \"%s\"}%s\n",
               b[i].name, b[i].bytes, b[i].shape, b[i].buys, i + 1 < n ? "," : "");
    printf("    ]}%s\n", comma ? "," : "");
}

/* Bytes of [base, base + n) written since they were painted with fill: the
 * offset one past the last byte that differs. Two fills rule out a write
 * that happens to store the fill value. */
static size_t high_water(const uint8_t *base, size_t n, uint8_t fill)
{
    while (n > 0 && base[n - 1] == fill)
        n--;
    return n;
}

/* Run one scene from entry to its last frame with the union painted, and
 * return the extent of the workspace union it wrote. */
static size_t measure_scene(int scene)
{
    static const uint8_t fills[2] = {0xA5, 0x5A};
    size_t extent = 0;
    for (int f = 0; f < 2; f++) {
        eh_demo_init(&demo);
        memset(&demo.ws, fills[f], sizeof demo.ws);
        uint32_t start = 0;
        for (int s = 0; s < scene; s++)
            start += eh_timeline_at(start).duration_ms;
        const uint32_t dur = eh_timeline_at(start).duration_ms;
        for (uint32_t t = start; t < start + dur; t += 100u / 3u)
            eh_demo_frame(&demo, t, 0, fb);
        eh_demo_frame(&demo, start + dur - 1, 0, fb);
        const size_t e = high_water((const uint8_t *)&demo.ws, sizeof demo.ws, fills[f]);
        extent = e > extent ? e : extent;
    }
    return extent;
}

static int cmd_memory(void)
{
    const size_t sram = EH_SRAM_BYTES;
    const size_t fb_bytes = sizeof fb;
    const size_t demo_bytes = sizeof(eh_demo);
    const size_t ws_union = sizeof(((eh_demo *)0)->ws);
    const size_t core = fb_bytes + demo_bytes;
    const long long rss_before = peak_rss_bytes();

    const eh_torus_ws *to = 0;
    const eh_tunnel_ws *tu = 0;
    const eh_terrain_ws *te = 0;
    const eh_particles_ws *pa = 0;
    block torus[] = {
        {"zband", sizeof to->zband, "native-res occlusion without a full-frame depth buffer (v1: 180x180)", ""},
        {"env", sizeof to->env, "chrome environment map (v1: 64x64)", ""},
        {"ring", sizeof to->ring + sizeof to->tube_cos + sizeof to->tube_sin,
         "Frenet frame per ring: smooth knot silhouette (v1: 96 rings)", ""},
        {"pv", sizeof to->pv, "packed projected vertices: round tube (v1: 96x8 float)", ""},
        {"flags", sizeof to->flags, "per-vertex near/facing bits", ""},
    };
    snprintf(torus[0].shape, sizeof torus[0].shape, "%d x %dx%d u16", EH_WORKERS, EH_FB_W, EH_PART_ROWS);
    snprintf(torus[1].shape, sizeof torus[1].shape, "%dx%d rgb565", EH_ENV_SIZE, EH_ENV_SIZE);
    snprintf(torus[2].shape, sizeof torus[2].shape, "%d rings", EH_TORUS_NU);
    snprintf(torus[3].shape, sizeof torus[3].shape, "%dx%d vertices", EH_TORUS_NU, EH_TORUS_NV);
    snprintf(torus[4].shape, sizeof torus[4].shape, "%d vertices", EH_TORUS_NVERT);
    block tunnel[] = {
        {"qang+depth", sizeof tu->qang + sizeof tu->depth,
         "quadrant LUT at native res, 1024 angle steps per turn (v1: 220x220 full-plane, 256 steps)", ""},
        {"tex", sizeof tu->tex, "bevelled wall texture (v1: 64x64)", ""},
    };
    snprintf(tunnel[0].shape, sizeof tunnel[0].shape, "2 x %dx%d u8", EH_TUNNEL_Q, EH_TUNNEL_Q);
    snprintf(tunnel[1].shape, sizeof tunnel[1].shape, "%dx%d rgb565", EH_TUNNEL_TEX_SIZE,
             EH_TUNNEL_TEX_SIZE);
    block terrain[] = {
        {"height", sizeof te->height, "heightmap: finer noise octave (v1: 128x128)", ""},
        {"color", sizeof te->color, "slope-shaded colour map (v1: 128x128)", ""},
    };
    snprintf(terrain[0].shape, sizeof terrain[0].shape, "%dx%d u8", EH_TERRAIN_SIZE, EH_TERRAIN_SIZE);
    snprintf(terrain[1].shape, sizeof terrain[1].shape, "%dx%d rgb565", EH_TERRAIN_SIZE,
             EH_TERRAIN_SIZE);
    block particles[] = {
        {"start", sizeof pa->sx + sizeof pa->sy + sizeof pa->scol + sizeof pa->target,
         "particles seeded from the terrain frame (v1: 2048)", ""},
        {"targets", sizeof pa->tx + sizeof pa->ty, "native-res logo target pixels", ""},
        {"seg", sizeof pa->seg,
         "per-frame segment cache for the row-banded two-core draw; tails add the motion streaks", ""},
    };
    snprintf(particles[0].shape, sizeof particles[0].shape, "%d particles", EH_PARTICLE_COUNT);
    snprintf(particles[1].shape, sizeof particles[1].shape, "%d max, %d used", EH_LOGO_MAX_TARGETS,
             eh_logo_targets(demo.ws.particles.tx, demo.ws.particles.ty, EH_LOGO_MAX_TARGETS));
    snprintf(particles[2].shape, sizeof particles[2].shape, "%d segments, trails %s",
             EH_PARTICLE_COUNT, EH_PARTICLE_TRAILS ? "on" : "off");

    printf("{\n  \"kind\": \"calculated = sizeof; measured = bytes of the workspace union "
           "written while each scene plays (painted high-water mark) and host peak RSS\",\n");
    printf("  \"rp2350_sram\": %zu,\n", sram);
    printf("  \"framebuffer_360x360_rgb565\": %zu,\n", fb_bytes);
    printf("  \"scene_workspaces_calculated\": {\n");
    print_blocks("torus-plasma", sizeof(eh_torus_ws), torus, 5, 1);
    print_blocks("tunnel", sizeof(eh_tunnel_ws), tunnel, 2, 1);
    print_blocks("terrain", sizeof(eh_terrain_ws), terrain, 2, 1);
    print_blocks("particles-logo", sizeof(eh_particles_ws), particles, 3, 0);
    printf("  },\n");
    printf("  \"workspace_union\": %zu,\n", ws_union);
    printf("  \"demo_state_total\": %zu,\n", demo_bytes);
    printf("  \"demo_state_overhead\": %zu,\n", demo_bytes - ws_union);

    printf("  \"per_scene_core_ram\": {");
    const size_t ws_sizes[EH_SCENE_COUNT] = {sizeof(eh_torus_ws), sizeof(eh_tunnel_ws),
                                             sizeof(eh_terrain_ws), sizeof(eh_particles_ws)};
    for (int s = 0; s < EH_SCENE_COUNT; s++) {
        const size_t r = fb_bytes + (demo_bytes - ws_union) + ws_sizes[s];
        printf("%s\n    \"%s\": {\"bytes\": %zu, \"fraction\": %.3f}", s ? "," : "", eh_scene_name(s), r,
               (double)r / sram);
    }
    printf("\n  },\n");

    printf("  \"measured_union_high_water\": {");
    size_t measured_peak = 0;
    for (int s = 0; s < EH_SCENE_COUNT; s++) {
        const size_t m = measure_scene(s);
        measured_peak = m > measured_peak ? m : measured_peak;
        printf("%s\"%s\": %zu", s ? ", " : "", eh_scene_name(s), m);
    }
    printf("},\n");
    printf("  \"measured_note\": \"particles entry re-renders the terrain's last frame in the same "
           "union, so its high-water includes the terrain workspace\",\n");

    printf("  \"core_ram_union_peak\": %zu,\n", core);
    printf("  \"core_ram_fraction_of_sram\": %.4f,\n", (double)core / sram);
    printf("  \"core_ram_measured_peak\": %zu,\n", fb_bytes + (demo_bytes - ws_union) + measured_peak);
    printf("  \"headroom_bytes\": %zu,\n", sram - core);
    printf("  \"headroom_note\": \"left for stack (two cores), heap, SDK BSS, USB/LED drivers and "
           "the plasma per-call arrays (%d bytes)\",\n",
           EH_FB_W + EH_FB_W + EH_FB_H);
    printf("  \"v1_core_ram\": %u,\n  \"v1_fraction_of_sram\": %.4f,\n", V1_CORE_RAM,
           (double)V1_CORE_RAM / sram);
    printf("  \"const_tables_flash\": {\"sin8\": %zu, \"circle_spans\": %zu, \"palettes\": %zu, "
           "\"led_samples\": %zu},\n",
           sizeof eh_sin8, sizeof eh_span_x0 + sizeof eh_span_x1, sizeof eh_palettes,
           sizeof eh_led_samples);
    printf("  \"host_peak_rss_bytes\": {\"before_touching_core\": %lld, \"after_all_scenes\": %lld},\n",
           rss_before, peak_rss_bytes());
    printf("  \"host_present_canvas_not_on_device\": %zu\n}\n", sizeof canvas);
    return 0;
}

/* --- bench plasma ---------------------------------------------------------- */

static int cmd_bench_plasma(int argc, char **argv)
{
    int frames = 1800, warmup = 120, clip = 1, bands = 1;
    unsigned palette = 0;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--warmup") && i + 1 < argc)
            warmup = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--noclip"))
            clip = 0;
        else if (!strcmp(argv[i], "--bands") && i + 1 < argc)
            bands = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--palette") && i + 1 < argc)
            palette = (unsigned)atoi(argv[++i]);
        else {
            fprintf(stderr, "bench plasma: unknown argument %s\n", argv[i]);
            return 2;
        }
    }
    if (frames < 1 || warmup < 0 || bands < 1 || bands > EH_FB_H) {
        fprintf(stderr, "bench plasma: invalid frames/warmup/bands\n");
        return 2;
    }

    double *frame_us = malloc(sizeof(double) * (size_t)frames);
    double *band_max_us = malloc(sizeof(double) * (size_t)frames);
    for (int k = 0; k < warmup; k++)
        eh_plasma_render(fb, STEP_MS(k), palette, 0, EH_FB_H, clip);
    for (int k = 0; k < frames; k++) {
        const uint32_t t = STEP_MS(k + warmup);
        double worst = 0;
        const double start = now_us();
        for (int b = 0; b < bands; b++) {
            const double bs = now_us();
            eh_plasma_render(fb, t, palette, b * EH_FB_H / bands, (b + 1) * EH_FB_H / bands, clip);
            const double be = now_us() - bs;
            worst = be > worst ? be : worst;
        }
        frame_us[k] = now_us() - start;
        band_max_us[k] = worst;
    }

    print_header("plasma");
    printf("  \"what\": \"eh_plasma_render only (no LED, no presentation); %s\",\n",
           clip ? "circle-clipped" : "full 360x360 rectangle");
    printf("  \"warmup_frames\": %d,\n  \"frames\": %d,\n  \"bands\": %d,\n  \"palette\": %u,\n",
           warmup, frames, bands, palette);
    printf("  \"timeline\": \"t_ms = k*100/3 (30 fps steps)\",\n");
    print_stats("frame_us", summarize(frame_us, frames), "  ", 1);
    if (bands > 1) {
        printf("  \"band_note\": \"bands rendered sequentially on one thread; slowest band "
               "approximates a parallel split's critical path, excluding sync and memory "
               "contention\",\n");
        print_stats("slowest_band_us", summarize(band_max_us, frames), "  ", 1);
    }
    printf("  \"last_frame_fnv1a\": \"%08x\",\n", fnv1a(fb, EH_FB_PIXELS));
    printf("  \"peak_rss_bytes\": %lld\n}\n", peak_rss_bytes());
    free(frame_us);
    free(band_max_us);
    return 0;
}

/* --- bench demo -------------------------------------------------------------- */

static int cmd_bench_demo(int argc, char **argv)
{
    int warmup_loops = 1, threads = 1;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--warmup-loops") && i + 1 < argc)
            warmup_loops = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--threads") && i + 1 < argc)
            threads = atoi(argv[++i]);
        else {
            fprintf(stderr, "bench demo: unknown argument %s\n", argv[i]);
            return 2;
        }
    }
    eh_pool *pool = eh_pool_create(threads);
    if (!pool) {
        fprintf(stderr, "bench demo: --threads must be 1 or %d\n", EH_WORKERS);
        return 2;
    }
    const int frames = (int)(EH_DEMO_LOOP_MS * 3u / 100u);
    for (int l = 0; l < warmup_loops; l++) {
        eh_demo_init(&demo);
        for (int k = 0; k < frames; k++)
            eh_pool_frame(pool, &demo, STEP_MS(k), 0, fb);
    }

    double *render[EH_SCENE_COUNT], *entry = malloc(sizeof(double) * EH_SCENE_COUNT);
    int nrender[EH_SCENE_COUNT] = {0}, entry_scene[EH_SCENE_COUNT], nentry = 0;
    for (int s = 0; s < EH_SCENE_COUNT; s++)
        render[s] = malloc(sizeof(double) * (size_t)frames);
    double *leds_us = malloc(sizeof(double) * (size_t)frames);
    double *present_us = malloc(sizeof(double) * (size_t)frames);

    eh_demo_init(&demo);
    for (int k = 0; k < frames; k++) {
        const int before = demo.active_scene;
        const double s0 = now_us();
        eh_pool_frame(pool, &demo, STEP_MS(k), 0, fb);
        const double s1 = now_us();
        eh_rgb leds[EH_LED_COUNT];
        eh_leds_from_frame(fb, leds, demo.led_max);
        const double s2 = now_us();
        eh_present(fb, demo.leds, demo.led_max, canvas);
        const double s3 = now_us();
        if (demo.active_scene != before) {
            entry[nentry] = s1 - s0;
            entry_scene[nentry++] = demo.active_scene;
        } else {
            render[demo.active_scene][nrender[demo.active_scene]++] = s1 - s0;
        }
        leds_us[k] = s2 - s1;
        present_us[k] = s3 - s2;
    }

    print_header("demo");
    printf("  \"what\": \"one full %u ms loop at 30 fps steps after %d untimed warmup loop(s); "
           "render_us = one frame plan (prepare, parts, LED model) on %d host thread(s); "
           "scene-entry frames that build tables are reported separately\",\n",
           EH_DEMO_LOOP_MS, warmup_loops, threads);
    printf("  \"threads\": %d,\n  \"parts\": %d,\n", threads, EH_PARTS);
    printf("  \"frames\": %d,\n  \"render_us_by_scene\": {\n", frames);
    for (int s = 0; s < EH_SCENE_COUNT; s++)
        print_stats(eh_scene_name(s), summarize(render[s], nrender[s]), "    ",
                    s + 1 < EH_SCENE_COUNT);
    printf("  },\n  \"scene_entry_frame_us\": {");
    for (int i = 0; i < nentry; i++)
        printf("%s\"%s\": %.2f", i ? ", " : "", eh_scene_name(entry_scene[i]), entry[i]);
    printf("},\n");
    print_stats("led_model_us", summarize(leds_us, frames), "  ", 1);
    print_stats("host_present_composite_us", summarize(present_us, frames), "  ", 1);
    printf("  \"last_frame_fnv1a\": \"%08x\",\n", fnv1a(fb, EH_FB_PIXELS));
    printf("  \"peak_rss_bytes\": %lld\n}\n", peak_rss_bytes());

    eh_pool_destroy(pool);
    for (int s = 0; s < EH_SCENE_COUNT; s++)
        free(render[s]);
    free(entry);
    free(leds_us);
    free(present_us);
    return 0;
}

/* --- frames ---------------------------------------------------------------- */

typedef struct {
    uint32_t mask, from, to;
} hold;

static uint32_t parse_button(const char *s, size_t n)
{
    static const struct {
        const char *name;
        uint32_t bit;
    } names[] = {{"UP", EH_BTN_UP},       {"DOWN", EH_BTN_DOWN},     {"LEFT", EH_BTN_LEFT},
                 {"RIGHT", EH_BTN_RIGHT}, {"CENTER", EH_BTN_CENTER}, {"A", EH_BTN_A},
                 {"B", EH_BTN_B}};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (strlen(names[i].name) == n && !strncmp(s, names[i].name, n))
            return names[i].bit;
    return 0;
}

static int cmp_u32(const void *a, const void *b)
{
    const uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

static int render_frame(const char *out, uint32_t t, const hold *holds, int nholds, int emit)
{
    uint32_t buttons = 0;
    for (int h = 0; h < nholds; h++)
        if (t >= holds[h].from && t < holds[h].to)
            buttons |= holds[h].mask;
    eh_demo_frame(&demo, t, buttons, fb);
    if (!emit)
        return 0;
    char path[1024];
    eh_present(fb, demo.leds, demo.led_max, canvas);
    snprintf(path, sizeof path, "%s/frame_%05u.ppm", out, t);
    if (eh_ppm_write(path, canvas, EH_PRESENT_W, EH_PRESENT_H) != 0) {
        perror(path);
        return 1;
    }
    const eh_timeline_pos p = eh_timeline_at(t);
    printf("{\"t_ms\": %u, \"scene\": \"%s\", \"local_ms\": %u, \"file\": \"%s\", "
           "\"yaw\": %.3f, \"pitch\": %.3f, \"palette\": %u, \"leds\": [",
           t, eh_scene_name(p.scene), p.local_ms, path, demo.cam.yaw, demo.cam.pitch,
           demo.cam.palette);
    for (int l = 0; l < EH_LED_COUNT; l++)
        printf("%s[%u, %u, %u]", l ? ", " : "", demo.leds[l].r, demo.leds[l].g, demo.leds[l].b);
    printf("], \"fb_fnv1a\": \"%08x\"}\n", fnv1a(fb, EH_FB_PIXELS));
    return 0;
}

static int cmd_frames(int argc, char **argv)
{
    const char *out = NULL;
    uint32_t at[64];
    int nat = 0, nholds = 0;
    hold holds[16];
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--out") && i + 1 < argc) {
            out = argv[++i];
        } else if (!strcmp(argv[i], "--at") && i + 1 < argc) {
            char *p = argv[++i];
            while (*p) {
                char *end;
                const unsigned long v = strtoul(p, &end, 10);
                if (end == p || (*end && *end != ',') || nat >= 64) {
                    fprintf(stderr, "frames: bad --at list (want up to 64 ms values T1,T2,...)\n");
                    return 2;
                }
                at[nat++] = (uint32_t)v;
                p = *end ? end + 1 : end;
            }
        } else if (!strcmp(argv[i], "--hold") && i + 1 < argc && nholds < 16) {
            const char *h = argv[++i], *colon = strchr(h, ':');
            unsigned from, to;
            if (!colon || sscanf(colon + 1, "%u-%u", &from, &to) != 2 ||
                !(holds[nholds].mask = parse_button(h, (size_t)(colon - h)))) {
                fprintf(stderr, "frames: bad --hold %s (want BTN:FROM-TO)\n", h);
                return 2;
            }
            holds[nholds].from = from;
            holds[nholds++].to = to;
        } else {
            fprintf(stderr, "frames: unknown argument %s\n", argv[i]);
            return 2;
        }
    }
    if (!out) {
        fprintf(stderr, "frames: --out DIR is required\n");
        return 2;
    }
    if (nat == 0) {
        /* Default: the middle of every scene. */
        uint32_t start = 0;
        for (int sc = 0; sc < EH_SCENE_COUNT; sc++) {
            const eh_timeline_pos p = eh_timeline_at(start);
            at[nat++] = p.start_ms + p.duration_ms / 2;
            start += p.duration_ms;
        }
    }
    qsort(at, (size_t)nat, sizeof at[0], cmp_u32);
    if (mkdir(out, 0755) != 0 && errno != EEXIST) {
        perror(out);
        return 1;
    }

    /* Play from t = 0 in 30 fps steps so held buttons integrate as they
     * would live; each requested time is rendered exactly, in order. */
    eh_demo_init(&demo);
    int next = 0;
    for (int k = 0; next < nat; k++) {
        const uint32_t step = STEP_MS(k);
        while (next < nat && at[next] <= step) {
            if (render_frame(out, at[next], holds, nholds, 1) != 0)
                return 1;
            next++;
        }
        if (next < nat)
            render_frame(out, step, holds, nholds, 0);
    }
    return 0;
}

static int usage(void)
{
    fprintf(stderr, "usage: eh_cli frames --out DIR [--at T,...] [--hold BTN:FROM-TO]...\n"
                    "       eh_cli bench plasma [--frames N] [--warmup W] [--noclip] [--bands B] "
                    "[--palette P]\n"
                    "       eh_cli bench demo [--warmup-loops L] [--threads 1|2]\n"
                    "       eh_cli memory\n");
    return 2;
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return usage();
    if (!strcmp(argv[1], "memory"))
        return cmd_memory();
    if (!strcmp(argv[1], "frames"))
        return cmd_frames(argc - 2, argv + 2);
    if (!strcmp(argv[1], "bench") && argc >= 3) {
        if (!strcmp(argv[2], "plasma"))
            return cmd_bench_plasma(argc - 3, argv + 3);
        if (!strcmp(argv[2], "demo"))
            return cmd_bench_demo(argc - 3, argv + 3);
    }
    return usage();
}
