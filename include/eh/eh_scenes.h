/* The four EVENT HORIZON scenes.
 *
 * Each scene owns a workspace. The demo keeps all four in one union because
 * only one is live at a time, so RAM cost is the largest workspace, not the
 * sum. Every scene follows the same frame plan:
 *
 *   eh_X_prepare(ws, local_ms, duration_ms, cam)   serial, per-frame setup
 *   eh_X_render_part(ws, fb, pass, part, worker)   EH_PARTS parts per pass;
 *                                                  parts write disjoint pixels
 *
 * Parts of one pass may run concurrently on different workers (cores);
 * passes are separated by a barrier. The result is bit-identical to running
 * every part serially in any order. eh_X_render() does exactly that, for
 * tests and simple callers. Scenes run their own entry/exit transitions, so
 * no second framebuffer is needed. */
#ifndef EH_SCENES_H
#define EH_SCENES_H

#include <stdint.h>

#include "eh_camera.h"
#include "eh_config.h"
#include "eh_raster.h"
#include "eh_types.h"

/* Rows owned by part p when a scene partitions by rows. */
#define EH_PART_ROWS (EH_FB_H / EH_PARTS)

/* Lerp every displayed pixel (inside the round panel) in the rectangle
 * [x0, x1) x [y0, y1) toward color; amount 0 = unchanged, 256 = solid
 * color. Pixels outside the circle are left alone: they are never shown. */
void eh_fade_rect(eh_rgb565 *fb, eh_rgb565 color, unsigned amount, int x0, int x1, int y0,
                  int y1);

/* --- Scene 1: chrome torus knot over plasma ------------------------------ */

#define EH_TORUS_P 2
#define EH_TORUS_Q 3
#define EH_TORUS_NVERT (EH_TORUS_NU * EH_TORUS_NV)
#define EH_ENV_SIZE (1 << EH_ENV_SHIFT)

/* Projected vertex, packed: screen position in 1/8 px, depth, env coords. */
typedef struct {
    int16_t x8, y8;
    uint16_t z;
    uint8_t u, v;
} eh_torus_pvtx;

typedef struct {
    /* One depth band per worker: native-resolution occlusion without a
     * full 259 KB depth buffer. */
    uint16_t zband[EH_WORKERS][EH_PART_ROWS * EH_FB_W];
    eh_rgb565 env[EH_ENV_SIZE * EH_ENV_SIZE];
    /* Frenet frame per ring (centre, normal, binormal); vertices are rebuilt
     * from these each frame instead of storing the whole mesh. */
    float ring[EH_TORUS_NU][9];
    float tube_cos[EH_TORUS_NV], tube_sin[EH_TORUS_NV];
    eh_torus_pvtx pv[EH_TORUS_NVERT];
    uint8_t flags[EH_TORUS_NVERT];
    /* Screen y range (1/8 px) of each ring's near vertices, so a band skips
     * whole strips without testing their faces. */
    int16_t ring_ymin[EH_TORUS_NU], ring_ymax[EH_TORUS_NU];
    unsigned env_palette;
    /* Per-frame state from prepare. */
    uint32_t local_ms;
    float rot[3][3], dist, light_cos, light_sin;
    unsigned palette;
    int wireframe;
    eh_rgb565 wire_color;
    unsigned fade_black, fade_white;
    /* Knot pixels written, and rasteriser work, per part in the last frame
     * (diagnostics). */
    uint32_t covered[EH_PARTS];
    eh_raster_stats stats[EH_PARTS];
} eh_torus_ws;

void eh_torus_init(eh_torus_ws *ws, unsigned palette);
void eh_torus_prepare(eh_torus_ws *ws, uint32_t local_ms, uint32_t duration_ms,
                      const eh_camera *cam);
/* Pass 0: plasma rows of the part's band and the part's share of rings
 * (vertex transform). Pass 1: rasterise the knot into the band, using the
 * worker's depth band. */
#define EH_TORUS_PASSES 2
void eh_torus_render_part(eh_torus_ws *ws, eh_rgb565 *fb, int pass, int part, int worker);
void eh_torus_render(eh_torus_ws *ws, eh_rgb565 *fb, uint32_t local_ms, uint32_t duration_ms,
                     const eh_camera *cam);

/* --- Scene 2: twisting textured tunnel ----------------------------------- */

/* The tunnel centre follows the camera by up to EH_TUNNEL_MARGIN px. The
 * angle/depth table covers one quadrant of distances from the centre and is
 * mirrored, which is what makes a native-resolution table affordable. */
#define EH_TUNNEL_MARGIN 40
#define EH_TUNNEL_Q (EH_FB_W / 2 + EH_TUNNEL_MARGIN)
#define EH_TUNNEL_TEX_SIZE (1 << EH_TUNNEL_TEX_SHIFT)

typedef struct {
    /* Indexed by (|dy|, |dx|) pixel-centre distance from the tunnel centre:
     * angle within the quadrant in 1/1024 turn (0..255), and inverse
     * distance (0..255, larger is deeper). */
    uint8_t qang[EH_TUNNEL_Q * EH_TUNNEL_Q];
    uint8_t depth[EH_TUNNEL_Q * EH_TUNNEL_Q];
    eh_rgb565 tex[EH_TUNNEL_TEX_SIZE * EH_TUNNEL_TEX_SIZE];
    unsigned tex_palette;
    /* Per-frame state from prepare. */
    int cx, cy;
    unsigned roll, advance, glow, fade_white;
    int twist;
} eh_tunnel_ws;

void eh_tunnel_init(eh_tunnel_ws *ws, unsigned palette);
void eh_tunnel_prepare(eh_tunnel_ws *ws, uint32_t local_ms, uint32_t duration_ms,
                       const eh_camera *cam);
void eh_tunnel_render_part(const eh_tunnel_ws *ws, eh_rgb565 *fb, int part);
/* Angle of pixel (x, y) around the current tunnel centre, in 1/1024 turn
 * (screen y down, 0 along +x), unfolded from the quadrant table. */
unsigned eh_tunnel_angle(const eh_tunnel_ws *ws, int x, int y);
void eh_tunnel_render(eh_tunnel_ws *ws, eh_rgb565 *fb, uint32_t local_ms, uint32_t duration_ms,
                      const eh_camera *cam);

/* --- Scene 3: procedural terrain flythrough with fog and rising sun ------ */

#define EH_TERRAIN_SIZE (1 << EH_TERRAIN_SHIFT)

typedef struct {
    uint8_t height[EH_TERRAIN_SIZE * EH_TERRAIN_SIZE];
    eh_rgb565 color[EH_TERRAIN_SIZE * EH_TERRAIN_SIZE];
    /* Per-frame state from prepare. */
    float horizon, sun_x, sun_y, fx, fy, rx, ry, px, py, altitude;
    eh_rgb top, low, fog;
    unsigned fade_white;
    /* Sky pixels drawn by each part in the last frame (diagnostics). */
    uint32_t sky_px[EH_PARTS];
} eh_terrain_ws;

void eh_terrain_init(eh_terrain_ws *ws);
void eh_terrain_prepare(eh_terrain_ws *ws, uint32_t local_ms, uint32_t duration_ms,
                        const eh_camera *cam);
/* Terrain partitions by columns: part p owns columns [p, p + 1) * W / EH_PARTS. */
void eh_terrain_render_part(eh_terrain_ws *ws, eh_rgb565 *fb, int part);
void eh_terrain_render(eh_terrain_ws *ws, eh_rgb565 *fb, uint32_t local_ms,
                       uint32_t duration_ms, const eh_camera *cam);
/* Screen row of the sun's centre at local_ms (smaller is higher). */
float eh_terrain_sun_y(uint32_t local_ms, uint32_t duration_ms);
/* Camera eye height at local_ms: clears the terrain around the flight path
 * and varies continuously with time. Needs an initialised workspace. */
float eh_terrain_altitude(const eh_terrain_ws *ws, uint32_t local_ms);

/* --- Scene 4: particles rebuild the logo --------------------------------- */

#define EH_LOGO_MAX_TARGETS 8192
/* Scene-local time by which every particle has reached its logo target. */
#define EH_PARTICLES_SETTLED_MS 6500u

/* Screen-space segment for one particle this frame: head and (with trails)
 * streak tail in 1/16 px, colour. Pass 0 fills these by particle index;
 * pass 1 draws them by rows. */
typedef struct {
    int16_t hx, hy;
#if EH_PARTICLE_TRAILS
    int16_t tx, ty;
#endif
    eh_rgb565 col;
} eh_particle_seg;

typedef struct {
    int16_t sx[EH_PARTICLE_COUNT], sy[EH_PARTICLE_COUNT]; /* start, 1/16 px */
    eh_rgb565 scol[EH_PARTICLE_COUNT];
    uint16_t target[EH_PARTICLE_COUNT];
    uint16_t tx[EH_LOGO_MAX_TARGETS], ty[EH_LOGO_MAX_TARGETS];
    eh_particle_seg seg[EH_PARTICLE_COUNT];
    int ntargets;
    /* Per-frame state from prepare. */
    uint32_t local_ms;
    unsigned palette, fade_black, size;
    float cos_yaw, sin_yaw, cos_pitch, sin_pitch;
} eh_particles_ws;

/* Logo target pixels ("EVENT HORIZON" over "SAINTCON 2026" in a built-in
 * block font, not the official logo artwork). Returns the count written. */
int eh_logo_targets(uint16_t *tx, uint16_t *ty, int max);

/* Seed particles from the frame they shatter out of. */
void eh_particles_init(eh_particles_ws *ws, const eh_rgb565 *source_fb);
void eh_particles_prepare(eh_particles_ws *ws, uint32_t local_ms, uint32_t duration_ms,
                          const eh_camera *cam);
/* Pass 0: background rows of the part's band and the part's share of
 * particles (motion, projection, segment cache). Pass 1: draw every cached
 * segment that crosses the band, in particle order. */
#define EH_PARTICLES_PASSES 2
void eh_particles_render_part(eh_particles_ws *ws, eh_rgb565 *fb, int pass, int part);
void eh_particles_render(eh_particles_ws *ws, eh_rgb565 *fb, uint32_t local_ms,
                         uint32_t duration_ms, const eh_camera *cam);

#endif
