/* RP2350 frame-cost ESTIMATE for the native-360 demo (not a measurement).
 *
 * Plays the whole 54 s loop at 30 fps on the host and, for each frame,
 * counts the work units each part does (pixels, samples, vertices,
 * triangle setups, particle evaluations), sums them per worker under the
 * even/odd part assignment, and prices them with per-unit Cortex-M33 cycle
 * costs read from clang 17 -O2 -mcpu=cortex-m33 assembly of the core
 * (see README.org in this directory). The critical path of a frame is the
 * busier worker's total plus the serial prepare/finish (not counted: small
 * except on scene entry). Results are cycles and ms at an assumed 150 MHz.
 *
 * Counting replicates the renderers' own loop bounds from their public
 * workspace state and per-part diagnostics (torus covered[], terrain
 * sky_px[]).
 *
 * Build from the repository root after building the default project:
 *   cc -O2 -std=c11 -Iinclude docs/measurements/native-360/probes/device_estimate.c \
 *      build/libeh_core.a -lm -o build/device_estimate && build/device_estimate */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eh/eh_demo.h"
#include "eh/eh_tables.h"

/* Per-unit cycle costs, low and high. */
enum unit {
    PLASMA_PX,  /* plasma inner loop: 75 instrs per 4 px */
    TUNNEL_PX,  /* 127 instrs per 2 px incl. glow path; ~48 on the plain path */
    VERTEX,     /* ring expansion + projection: 122 instrs incl. 2 VDIV (14 c each) */
    STRIP_CHECK, /* ring y-range band test per strip per part */
    FACE_CHECK, /* flag tests + band reject per face in a kept strip */
    TRI_SETUP,  /* 194 straight-line instrs incl. 4 VDIV: edge, z/u/v planes, bbox */
    TRI_ROW,    /* per covered row: 3 edge run bounds, floor/ceil, clamps, steps */
    EDGE_REJ_PX, /* in the run's slack, outside an edge: 12-18 instrs */
    DEPTH_FAIL_PX, /* inside, behind the depth band: 31 instrs */
    WRITTEN_PX, /* 47 instrs: 3 edge tests, depth, 2 VCVT, texel, 2 stores, steps */
    FADE_PX,    /* 15 instrs per pixel */
    T_SAMPLE,   /* terrain march sample */
    T_FILL_PX,  /* 9 instrs per 4 px */
    SKY_PX,     /* 109 instrs per 2 px incl. VDIV */
    LOCATE,     /* particle path: soft expf, VSQRT, 3 VDIV, rotate, project */
    SEG_CHECK,  /* band test per segment per part */
    STREAK_PX,  /* 2 SDIV + 565 lerp */
    HEAD_PX,
    BG_PX,
    UNIT_COUNT
};
static const char *const unit_name[UNIT_COUNT] = {
    "plasma_px", "tunnel_px", "vertex", "strip_check", "face_check", "tri_setup", "tri_row", "edge_reject_px", "depth_fail_px", "written_px",
    "fade_px", "terrain_sample", "terrain_fill_px", "sky_px", "particle_locate", "seg_check",
    "streak_px", "head_px", "bg_px"};
static const double cost_lo[UNIT_COUNT] = {19, 48, 120, 10, 12, 200, 50, 14, 34, 55, 15, 35, 2.3, 55, 220, 8, 30, 5, 1.5};
static const double cost_hi[UNIT_COUNT] = {28, 70, 170, 16, 20, 320, 90, 22, 45, 80, 20, 55, 4, 75, 380, 14, 45, 8, 3};

#define MHZ 150.0
#define BUDGET_30FPS (MHZ * 1e6 / 30.0)

static eh_demo demo;
static eh_rgb565 fb[EH_FB_PIXELS];
static double work[EH_WORKERS][UNIT_COUNT];

static void add(int part, enum unit u, double n) { work[part % EH_WORKERS][u] += n; }

static int span_pixels(int y0, int y1)
{
    int n = 0;
    for (int y = y0; y < y1; y++)
        n += eh_span_x1[y] - eh_span_x0[y];
    return n;
}

static void count_torus(const eh_torus_ws *ws)
{
    for (int part = 0; part < EH_PARTS; part++) {
        const int y0 = part * EH_PART_ROWS, y1 = y0 + EH_PART_ROWS;
        add(part, PLASMA_PX, span_pixels(y0, y1));
        add(part, VERTEX, (double)EH_TORUS_NVERT / EH_PARTS);
        if (ws->fade_black || ws->fade_white)
            add(part, FADE_PX, (ws->fade_black && ws->fade_white ? 2 : 1) * span_pixels(y0, y1));
        double setups = 0;
        for (int u = 0; u < EH_TORUS_NU; u++) {
            const int u1 = (u + 1) % EH_TORUS_NU;
            add(part, STRIP_CHECK, 1);
            if ((ws->ring_ymax[u] < (y0 - 1) * 8 && ws->ring_ymax[u1] < (y0 - 1) * 8) ||
                (ws->ring_ymin[u] > (y1 + 1) * 8 && ws->ring_ymin[u1] > (y1 + 1) * 8))
                continue;
            for (int v = 0; v < EH_TORUS_NV; v++)
                for (int tri = 0; tri < 2; tri++) {
                    const int v1 = (v + 1) % EH_TORUS_NV;
                    const int a = u * EH_TORUS_NV + v;
                    const int b = tri ? u1 * EH_TORUS_NV + v1 : u1 * EH_TORUS_NV + v;
                    const int c = tri ? u * EH_TORUS_NV + v1 : u1 * EH_TORUS_NV + v1;
                    add(part, FACE_CHECK, 1);
                    const unsigned fa = ws->flags[a], fb2 = ws->flags[b], fc = ws->flags[c];
                    if (!(fa & fb2 & fc & 1u) || !((fa | fb2 | fc) & 2u))
                        continue;
                    int ymin = ws->pv[a].y8, ymax = ymin;
                    ymin = ws->pv[b].y8 < ymin ? ws->pv[b].y8 : ymin;
                    ymin = ws->pv[c].y8 < ymin ? ws->pv[c].y8 : ymin;
                    ymax = ws->pv[b].y8 > ymax ? ws->pv[b].y8 : ymax;
                    ymax = ws->pv[c].y8 > ymax ? ws->pv[c].y8 : ymax;
                    if (ymax < (y0 - 1) * 8 || ymin > (y1 + 1) * 8)
                        continue;
                    setups++;
                }
        }
        const eh_raster_stats *st = &ws->stats[part];
        add(part, TRI_SETUP, setups);
        add(part, TRI_ROW, st->rows);
        add(part, EDGE_REJ_PX, (double)st->tested - ws->covered[part] - st->depth_failed);
        add(part, DEPTH_FAIL_PX, st->depth_failed);
        add(part, WRITTEN_PX, ws->covered[part]);
    }
}

static void count_tunnel(const eh_tunnel_ws *ws)
{
    for (int part = 0; part < EH_PARTS; part++) {
        const int px = span_pixels(part * EH_PART_ROWS, (part + 1) * EH_PART_ROWS);
        add(part, TUNNEL_PX, px);
        if (ws->fade_white)
            add(part, FADE_PX, px);
    }
}

static void count_terrain(const eh_terrain_ws *ws)
{
    int slices = 0;
    for (float z = 1.0f, dz = 0.2f; z < 140.0f; z += dz, dz += 0.006f)
        slices++;
    const int cols = EH_FB_W / EH_PARTS;
    for (int part = 0; part < EH_PARTS; part++) {
        int visible = 0;
        for (int x = part * cols; x < (part + 1) * cols; x++)
            visible += eh_span_x1[x] - eh_span_x0[x];
        add(part, T_SAMPLE, (double)slices * cols);
        add(part, T_FILL_PX, visible - (double)ws->sky_px[part]);
        add(part, SKY_PX, ws->sky_px[part]);
        if (ws->fade_white)
            add(part, FADE_PX, visible);
    }
}

static void count_particles(const eh_particles_ws *ws)
{
    for (int part = 0; part < EH_PARTS; part++) {
        const int y0 = part * EH_PART_ROWS, y1 = y0 + EH_PART_ROWS;
        add(part, BG_PX, EH_PART_ROWS * EH_FB_W);
        add(part, LOCATE, 2.0 * EH_PARTICLE_COUNT / EH_PARTS);
        add(part, SEG_CHECK, EH_PARTICLE_COUNT);
        double streak = 0, head = 0;
        for (int i = 0; i < EH_PARTICLE_COUNT; i++) {
            const eh_particle_seg *s = &ws->seg[i];
            if (s->hx == INT16_MIN)
                continue;
            const int hx = s->hx >> 4, hy = s->hy >> 4;
#if EH_PARTICLE_TRAILS
            const int tx = s->tx >> 4, ty = s->ty >> 4, dx = hx - tx, dy = hy - ty;
            const int n = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
            if (n > 0 && !((ty < y0 && hy < y0) || (ty >= y1 && hy >= y1)))
                streak += n; /* iterations, as the loop runs them */
#endif
            for (int y = hy; y < hy + (int)ws->size; y++)
                if (y >= y0 && y < y1)
                    for (int x = hx; x < hx + (int)ws->size; x++)
                        head += x >= 0 && x < EH_FB_W;
        }
        add(part, STREAK_PX, streak);
        add(part, HEAD_PX, head);
        if (ws->fade_black)
            add(part, FADE_PX, span_pixels(y0, y1));
    }
}

static int cmp(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

int main(void)
{
    const int frames = (int)(EH_DEMO_LOOP_MS * 3u / 100u);
    static double lo[EH_SCENE_COUNT][2000], hi[EH_SCENE_COUNT][2000];
    static double unit_sum[EH_SCENE_COUNT][UNIT_COUNT];
    static double worst_units[EH_SCENE_COUNT][EH_WORKERS][UNIT_COUNT], worst[EH_SCENE_COUNT];
    static uint32_t worst_t[EH_SCENE_COUNT];
    int n[EH_SCENE_COUNT] = {0};
    eh_demo_init(&demo);
    for (int k = 0; k < frames; k++) {
        const uint32_t t = (uint32_t)(k * 100u / 3u);
        eh_demo_frame(&demo, t, 0, fb);
        memset(work, 0, sizeof work);
        const int s = demo.pos.scene;
        switch (s) {
        case EH_SCENE_TORUS: count_torus(&demo.ws.torus); break;
        case EH_SCENE_TUNNEL: count_tunnel(&demo.ws.tunnel); break;
        case EH_SCENE_TERRAIN: count_terrain(&demo.ws.terrain); break;
        default: count_particles(&demo.ws.particles); break;
        }
        double wl[EH_WORKERS] = {0}, wh[EH_WORKERS] = {0};
        for (int w = 0; w < EH_WORKERS; w++)
            for (int u = 0; u < UNIT_COUNT; u++) {
                wl[w] += work[w][u] * cost_lo[u];
                wh[w] += work[w][u] * cost_hi[u];
                unit_sum[s][u] += work[w][u];
            }
        lo[s][n[s]] = wl[0] > wl[1] ? wl[0] : wl[1];
        hi[s][n[s]] = wh[0] > wh[1] ? wh[0] : wh[1];
        if (hi[s][n[s]] > worst[s]) {
            worst[s] = hi[s][n[s]];
            worst_t[s] = t;
            memcpy(worst_units[s], work, sizeof work);
        }
        n[s]++;
    }
    printf("{\n  \"kind\": \"ESTIMATE from host work counts x Cortex-M33 per-unit cycle costs; "
           "not a device measurement\",\n  \"clock_mhz_assumed\": %.0f,\n"
           "  \"cycles_per_core_at_30fps\": %.0f,\n  \"workers\": %d,\n  \"scenes\": {\n",
           MHZ, BUDGET_30FPS, EH_WORKERS);
    for (int s = 0; s < EH_SCENE_COUNT; s++) {
        qsort(lo[s], (size_t)n[s], sizeof(double), cmp);
        qsort(hi[s], (size_t)n[s], sizeof(double), cmp);
        const double p50l = lo[s][(n[s] - 1) / 2], p50h = hi[s][(n[s] - 1) / 2];
        const double maxl = lo[s][n[s] - 1], maxh = hi[s][n[s] - 1];
        const double p90l = lo[s][n[s] * 9 / 10], p90h = hi[s][n[s] * 9 / 10];
        int over_l = 0, over_h = 0;
        for (int i = 0; i < n[s]; i++) {
            over_l += lo[s][i] > BUDGET_30FPS;
            over_h += hi[s][i] > BUDGET_30FPS;
        }
        printf("    \"%s\": {\"frames\": %d, \"critical_path_mcycles_p50\": [%.2f, %.2f], "
               "\"critical_path_mcycles_max\": [%.2f, %.2f], \"ms_p50\": [%.1f, %.1f], "
               "\"ms_max\": [%.1f, %.1f], \"fps_p50\": [%.0f, %.0f], \"fps_p90\": [%.0f, %.0f], "
               "\"fps_worst\": [%.0f, %.0f], \"frames_over_30fps_budget\": [%d, %d],\n"
               "      \"mean_units_per_frame\": {",
               eh_scene_name(s), n[s], p50l / 1e6, p50h / 1e6, maxl / 1e6, maxh / 1e6,
               p50l / MHZ / 1e3, p50h / MHZ / 1e3, maxl / MHZ / 1e3, maxh / MHZ / 1e3,
               MHZ * 1e6 / p50h, MHZ * 1e6 / p50l, MHZ * 1e6 / p90h, MHZ * 1e6 / p90l,
               MHZ * 1e6 / maxh, MHZ * 1e6 / maxl, over_l, over_h);
        int first = 1;
        for (int u = 0; u < UNIT_COUNT; u++)
            if (unit_sum[s][u] > 0) {
                printf("%s\"%s\": %.0f", first ? "" : ", ", unit_name[u], unit_sum[s][u] / n[s]);
                first = 0;
            }
        printf("},\n      \"worst_frame\": {\"t_ms\": %u, \"units_by_worker\": [", worst_t[s]);
        for (int w = 0; w < EH_WORKERS; w++) {
            printf("%s{", w ? ", " : "");
            first = 1;
            for (int u = 0; u < UNIT_COUNT; u++)
                if (worst_units[s][w][u] > 0) {
                    printf("%s\"%s\": %.0f", first ? "" : ", ", unit_name[u], worst_units[s][w][u]);
                    first = 0;
                }
            printf("}");
        }
        printf("]}}%s\n", s + 1 < EH_SCENE_COUNT ? "," : "");
    }
    printf("  }\n}\n");
    return 0;
}
