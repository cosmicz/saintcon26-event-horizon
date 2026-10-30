/* Scene 1: a (2,3) torus knot tube, sphere-environment-mapped for a chrome
 * look, over the plasma. The "moving lights" are the environment highlights
 * rotating about the view axis. The scene opens from black and ends with
 * the camera diving into the knot's centre while fading to white, which the
 * tunnel picks up.
 *
 * Native resolution without a full-frame depth buffer: each part rasterises
 * only its own band of rows against a band-sized depth buffer owned by the
 * worker running it. The mesh is stored as one Frenet frame per ring and
 * expanded into packed projected vertices each frame. */
#include <math.h>

#include "eh/eh_plasma.h"
#include "eh/eh_scenes.h"
#include "eh/eh_tables.h"
#include "eh_math.h"

#define TUBE_RADIUS 0.42f
#define CAM_DIST 9.0f
#define NEAR_Z 0.25f
#define DEPTH_RANGE 14.0f
#define FOCAL 300.0f
#define FADE_IN_MS 1000u
#define DIVE_MS 3000u
#define WHITE_MS 1500u
#define XY_LIMIT 32767.0f /* packed 1/8 px; ~4096 px either side */

static void knot_point(float phi, float out[3])
{
    const float r = cosf(EH_TORUS_Q * phi) + 2.0f;
    out[0] = r * cosf(EH_TORUS_P * phi);
    out[1] = r * sinf(EH_TORUS_P * phi);
    out[2] = -sinf(EH_TORUS_Q * phi);
}

static void normalize3(float v[3])
{
    const float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 0.0f) {
        v[0] /= l;
        v[1] /= l;
        v[2] /= l;
    }
}

static void cross3(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

/* Chrome environment: palette-tinted sky above a bright horizon streak, dark
 * ground below, and two specular highlight blobs. Indexed by the view-space
 * normal's (x, y) mapped to [0, 1). */
static void build_env(eh_torus_ws *ws, unsigned palette)
{
    const eh_rgb565 *pal = eh_palettes[palette % EH_PALETTE_COUNT];
    for (int y = 0; y < EH_ENV_SIZE; y++)
        for (int x = 0; x < EH_ENV_SIZE; x++) {
            const float nx = (x + 0.5f) / (EH_ENV_SIZE / 2) - 1.0f;
            const float ny = 1.0f - (y + 0.5f) / (EH_ENV_SIZE / 2);
            float r, g, b;
            if (ny > 0.0f) {
                eh_rgb s = eh_rgb565_unpack(pal[(int)(40 + ny * 60) & 255]);
                const float w = ny * ny * ny * 0.6f;
                r = s.r + (255 - s.r) * w;
                g = s.g + (255 - s.g) * w;
                b = s.b + (255 - s.b) * w;
            } else {
                eh_rgb s = eh_rgb565_unpack(pal[160]);
                const float k = 0.3f + 0.25f * -ny;
                r = s.r * k;
                g = s.g * k;
                b = s.b * k;
            }
            const float horizon = expf(-(ny * ny) / 0.004f) * 200.0f;
            const float d1 = (nx + 0.45f) * (nx + 0.45f) + (ny - 0.5f) * (ny - 0.5f);
            const float d2 = (nx - 0.5f) * (nx - 0.5f) + (ny + 0.35f) * (ny + 0.35f);
            const float spec = 255.0f * (expf(-d1 / 0.015f) + 0.7f * expf(-d2 / 0.01f));
            r += horizon + spec;
            g += horizon + spec;
            b += horizon + spec;
            ws->env[y * EH_ENV_SIZE + x] = eh_rgb565_pack((unsigned)eh_clampf(r, 0, 255),
                                                          (unsigned)eh_clampf(g, 0, 255),
                                                          (unsigned)eh_clampf(b, 0, 255));
        }
    ws->env_palette = palette;
}

void eh_torus_init(eh_torus_ws *ws, unsigned palette)
{
    const float eps = 1e-3f;
    for (int u = 0; u < EH_TORUS_NU; u++) {
        const float phi = EH_TAU * u / EH_TORUS_NU;
        float c[3], n[3], p[3], tangent[3], accel[3], binormal[3], normal[3];
        knot_point(phi, c);
        knot_point(phi + eps, n);
        knot_point(phi - eps, p);
        /* Frenet frame; a torus knot's curvature never vanishes, so the frame
         * is well defined and closes on itself around the loop. */
        for (int k = 0; k < 3; k++) {
            tangent[k] = n[k] - p[k];
            accel[k] = n[k] + p[k] - 2.0f * c[k];
        }
        normalize3(tangent);
        cross3(tangent, accel, binormal);
        normalize3(binormal);
        cross3(binormal, tangent, normal);
        for (int k = 0; k < 3; k++) {
            ws->ring[u][k] = c[k];
            ws->ring[u][3 + k] = normal[k];
            ws->ring[u][6 + k] = binormal[k];
        }
    }
    for (int v = 0; v < EH_TORUS_NV; v++) {
        const float th = EH_TAU * v / EH_TORUS_NV;
        ws->tube_cos[v] = cosf(th);
        ws->tube_sin[v] = sinf(th);
    }
    build_env(ws, palette);
}

static void rotation(float ax, float ay, float az, float m[3][3])
{
    const float cx = cosf(ax), sx = sinf(ax), cy = cosf(ay), sy = sinf(ay), cz = cosf(az),
                sz = sinf(az);
    /* R = Rz * Ry * Rx */
    const float r[3][3] = {{cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx},
                           {sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx},
                           {-sy, cy * sx, cy * cx}};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            m[i][j] = r[i][j];
}

static void apply(const float m[3][3], const float v[3], float out[3])
{
    for (int k = 0; k < 3; k++)
        out[k] = m[k][0] * v[0] + m[k][1] * v[1] + m[k][2] * v[2];
}

void eh_torus_prepare(eh_torus_ws *ws, uint32_t local_ms, uint32_t duration_ms,
                      const eh_camera *cam)
{
    if (ws->env_palette != cam->palette)
        build_env(ws, cam->palette);
    const float t = local_ms / 1000.0f;
    ws->local_ms = local_ms;
    ws->palette = cam->palette;
    ws->wireframe = cam->wireframe;
    ws->wire_color = eh_palettes[cam->palette % EH_PALETTE_COUNT][200] | 0x8410u;

    ws->dist = CAM_DIST;
    if (local_ms + DIVE_MS > duration_ms) {
        const float s = (float)(local_ms + DIVE_MS - duration_ms) / DIVE_MS;
        ws->dist = CAM_DIST - (CAM_DIST - 0.6f) * eh_smoothstep(s);
    }
    rotation(t * 0.37f + cam->pitch, t * 0.6f + cam->yaw, t * 0.21f, ws->rot);
    ws->light_cos = cosf(t * 0.8f);
    ws->light_sin = sinf(t * 0.8f);

    ws->fade_black = local_ms < FADE_IN_MS ? 256 - local_ms * 256 / FADE_IN_MS : 0;
    ws->fade_white =
        local_ms + WHITE_MS > duration_ms ? (local_ms + WHITE_MS - duration_ms) * 256 / WHITE_MS : 0;
}

/* Per-vertex visibility bits in ws->flags. */
#define VIS_NEAR 1u   /* in front of the near plane */
#define VIS_FACING 2u /* outward normal points back toward the camera */

static int16_t pack_xy(float v)
{
    return (int16_t)lrintf(eh_clampf(v * 8.0f, -XY_LIMIT, XY_LIMIT));
}

static void transform_rings(eh_torus_ws *ws, int u0, int u1)
{
    for (int u = u0; u < u1; u++) {
        float c[3], n[3], b[3];
        apply(ws->rot, &ws->ring[u][0], c);
        apply(ws->rot, &ws->ring[u][3], n);
        apply(ws->rot, &ws->ring[u][6], b);
        c[2] += ws->dist;
        int ymin = INT16_MAX, ymax = INT16_MIN;
        for (int v = 0; v < EH_TORUS_NV; v++) {
            const int i = u * EH_TORUS_NV + v;
            float nrm[3], p[3];
            for (int k = 0; k < 3; k++) {
                nrm[k] = n[k] * ws->tube_cos[v] + b[k] * ws->tube_sin[v];
                p[k] = c[k] + TUBE_RADIUS * nrm[k];
            }
            ws->flags[i] =
                (uint8_t)((p[2] > NEAR_Z ? VIS_NEAR : 0u) |
                          (nrm[0] * p[0] + nrm[1] * p[1] + nrm[2] * p[2] < 0.0f ? VIS_FACING : 0u));
            if (!(ws->flags[i] & VIS_NEAR))
                continue;
            eh_torus_pvtx *pv = &ws->pv[i];
            pv->x8 = pack_xy(EH_FB_W / 2.0f + FOCAL * p[0] / p[2]);
            pv->y8 = pack_xy(EH_FB_H / 2.0f - FOCAL * p[1] / p[2]);
            ymin = pv->y8 < ymin ? pv->y8 : ymin;
            ymax = pv->y8 > ymax ? pv->y8 : ymax;
            pv->z = (uint16_t)(eh_clampf((p[2] - NEAR_Z) / DEPTH_RANGE, 0.0f, 1.0f) * 65535.0f);
            const float lx = nrm[0] * ws->light_cos - nrm[1] * ws->light_sin;
            const float ly = nrm[0] * ws->light_sin + nrm[1] * ws->light_cos;
            pv->u = (uint8_t)((0.5f + 0.49f * lx) * 256.0f);
            pv->v = (uint8_t)((0.5f - 0.49f * ly) * 256.0f);
        }
        ws->ring_ymin[u] = (int16_t)ymin;
        ws->ring_ymax[u] = (int16_t)ymax;
    }
}

static eh_vtx unpack(const eh_torus_pvtx *p)
{
    eh_vtx v = {p->x8 * 0.125f, p->y8 * 0.125f, p->z * (1.0f / 65535.0f), p->u * (1.0f / 256.0f),
                p->v * (1.0f / 256.0f)};
    return v;
}

static int min3i(int a, int b, int c) { return a < b ? (a < c ? a : c) : (b < c ? b : c); }
static int max3i(int a, int b, int c) { return a > b ? (a > c ? a : c) : (b > c ? b : c); }

static int draw_face(const eh_torus_ws *ws, const eh_raster_target *rt, int a, int b, int c)
{
    const unsigned fa = ws->flags[a], fb = ws->flags[b], fc = ws->flags[c];
    if (!(fa & fb & fc & VIS_NEAR))
        return 0;
    /* Back-face cull from the outward vertex normals, so the result does not
     * depend on the mesh index winding; the depth test resolves the faces on
     * the silhouette where the vertices disagree. */
    if (!((fa | fb | fc) & VIS_FACING))
        return 0;
    const eh_torus_pvtx *qa = &ws->pv[a], *qb = &ws->pv[b], *qc = &ws->pv[c];
    /* Cheap band rejection before any float setup: most faces miss a band. */
    if (max3i(qa->y8, qb->y8, qc->y8) < (rt->y0 - 1) * 8 ||
        min3i(qa->y8, qb->y8, qc->y8) > (rt->y1 + 1) * 8)
        return 0;
    const eh_vtx pa = unpack(qa), pb = unpack(qb), pc = unpack(qc);
    if (ws->wireframe) {
        eh_raster_line(rt, (int)pa.x, (int)pa.y, (int)pb.x, (int)pb.y, ws->wire_color);
        eh_raster_line(rt, (int)pb.x, (int)pb.y, (int)pc.x, (int)pc.y, ws->wire_color);
        return 0;
    }
    const float area = (pb.x - pa.x) * (pc.y - pa.y) - (pb.y - pa.y) * (pc.x - pa.x);
    return area > 0.0f ? eh_raster_tri(rt, &pa, &pb, &pc) : eh_raster_tri(rt, &pa, &pc, &pb);
}

void eh_torus_render_part(eh_torus_ws *ws, eh_rgb565 *fb, int pass, int part, int worker)
{
    const int y0 = part * EH_PART_ROWS, y1 = y0 + EH_PART_ROWS;
    if (pass == 0) {
        eh_plasma_render(fb, ws->local_ms, ws->palette, y0, y1, 1);
        transform_rings(ws, part * EH_TORUS_NU / EH_PARTS, (part + 1) * EH_TORUS_NU / EH_PARTS);
        return;
    }

    uint16_t *zb = ws->zband[worker];
    eh_raster_clear_depth(zb, EH_PART_ROWS);
    eh_raster_stats *stats = &ws->stats[part];
    *stats = (eh_raster_stats){0, 0, 0};
    const eh_raster_target rt = {fb, zb, ws->env, EH_ENV_SHIFT, y0, y1, stats};
    uint32_t covered = 0;
    for (int u = 0; u < EH_TORUS_NU; u++) {
        const int u1 = (u + 1) % EH_TORUS_NU;
        /* The same band test as draw_face, for the whole strip at once. */
        const int smin = ws->ring_ymin[u] < ws->ring_ymin[u1] ? ws->ring_ymin[u] : ws->ring_ymin[u1];
        const int smax = ws->ring_ymax[u] > ws->ring_ymax[u1] ? ws->ring_ymax[u] : ws->ring_ymax[u1];
        if (smax < (y0 - 1) * 8 || smin > (y1 + 1) * 8)
            continue;
        for (int v = 0; v < EH_TORUS_NV; v++) {
            const int v1 = (v + 1) % EH_TORUS_NV;
            const int i00 = u * EH_TORUS_NV + v, i10 = u1 * EH_TORUS_NV + v,
                      i01 = u * EH_TORUS_NV + v1, i11 = u1 * EH_TORUS_NV + v1;
            covered += (uint32_t)draw_face(ws, &rt, i00, i10, i11);
            covered += (uint32_t)draw_face(ws, &rt, i00, i11, i01);
        }
    }
    ws->covered[part] = covered;

    eh_fade_rect(fb, 0x0000, ws->fade_black, 0, EH_FB_W, y0, y1);
    eh_fade_rect(fb, 0xFFFF, ws->fade_white, 0, EH_FB_W, y0, y1);
}

void eh_torus_render(eh_torus_ws *ws, eh_rgb565 *fb, uint32_t local_ms, uint32_t duration_ms,
                     const eh_camera *cam)
{
    eh_torus_prepare(ws, local_ms, duration_ms, cam);
    for (int pass = 0; pass < EH_TORUS_PASSES; pass++)
        for (int part = 0; part < EH_PARTS; part++)
            eh_torus_render_part(ws, fb, pass, part, part % EH_WORKERS);
}
