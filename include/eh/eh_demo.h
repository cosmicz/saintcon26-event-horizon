/* Demo timeline: four scenes in a fixed loop, rendered as a pure function of
 * (time, camera state). Seeking is supported because each scene renders
 * from its local time rather than integrating frame-to-frame state.
 *
 * Frame plan, mirroring the two RP2350 cores:
 *
 *   eh_demo_prepare()                       serial (core 0): input, scene entry, setup
 *   for pass < eh_demo_passes():
 *       eh_demo_render_part(pass, part, w)  EH_PARTS parts, disjoint pixels;
 *                                           parts may run concurrently when
 *                                           their workers differ
 *       (barrier)
 *   eh_demo_finish()                        serial: LED model
 *
 * eh_demo_frame() runs the whole plan serially. Any schedule that respects
 * the barriers and gives concurrent parts distinct workers produces the same
 * bytes. */
#ifndef EH_DEMO_H
#define EH_DEMO_H

#include <stdint.h>

#include "eh_camera.h"
#include "eh_config.h"
#include "eh_scenes.h"
#include "eh_types.h"

enum eh_scene {
    EH_SCENE_TORUS,
    EH_SCENE_TUNNEL,
    EH_SCENE_TERRAIN,
    EH_SCENE_PARTICLES,
    EH_SCENE_COUNT
};

#define EH_SCENE_TORUS_MS 14000u
#define EH_SCENE_TUNNEL_MS 12000u
#define EH_SCENE_TERRAIN_MS 16000u
#define EH_SCENE_PARTICLES_MS 12000u
#define EH_DEMO_LOOP_MS \
    (EH_SCENE_TORUS_MS + EH_SCENE_TUNNEL_MS + EH_SCENE_TERRAIN_MS + EH_SCENE_PARTICLES_MS)

/* RP2350 on-chip SRAM, and the share of it the core may claim. */
#define EH_SRAM_BYTES 532480u

typedef struct {
    int scene;
    uint32_t start_ms;    /* loop-relative start of the scene */
    uint32_t local_ms;    /* time within the scene */
    uint32_t duration_ms; /* scene length */
} eh_timeline_pos;

eh_timeline_pos eh_timeline_at(uint32_t t_ms);
const char *eh_scene_name(int scene);

typedef struct {
    eh_camera cam;
    eh_rgb leds[EH_LED_COUNT];
    uint8_t led_max;
    int active_scene; /* -1 before the first frame */
    uint32_t last_t_ms;
    int have_last;
    eh_timeline_pos pos; /* frame being rendered, from prepare */
    /* One workspace at a time. Particles shatter out of the terrain's final
     * frame: entry renders that frame into the framebuffer, then seeds from
     * the framebuffer alone, so particles can reuse the terrain's memory. */
    union {
        eh_torus_ws torus;
        eh_tunnel_ws tunnel;
        eh_terrain_ws terrain;
        eh_particles_ws particles;
    } ws;
} eh_demo;

void eh_demo_init(eh_demo *d);

void eh_demo_prepare(eh_demo *d, uint32_t t_ms, uint32_t buttons, eh_rgb565 *fb);
int eh_demo_passes(const eh_demo *d);
void eh_demo_render_part(eh_demo *d, eh_rgb565 *fb, int pass, int part, int worker);
void eh_demo_finish(eh_demo *d, const eh_rgb565 *fb);

/* Render the frame at absolute time t_ms (wrapped to the loop) with the
 * given buttons held, into fb, and update d->leds. Camera time steps come
 * from successive t_ms values, clamped to [0, 100] ms per frame. */
void eh_demo_frame(eh_demo *d, uint32_t t_ms, uint32_t buttons, eh_rgb565 *fb);

#endif
