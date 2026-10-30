#include "eh/eh_demo.h"

#include "eh/eh_leds.h"

static const uint32_t scene_ms[EH_SCENE_COUNT] = {EH_SCENE_TORUS_MS, EH_SCENE_TUNNEL_MS,
                                                  EH_SCENE_TERRAIN_MS, EH_SCENE_PARTICLES_MS};
static const char *const scene_names[EH_SCENE_COUNT] = {"torus-plasma", "tunnel", "terrain",
                                                        "particles-logo"};

eh_timeline_pos eh_timeline_at(uint32_t t_ms)
{
    eh_timeline_pos p = {0, 0, t_ms % EH_DEMO_LOOP_MS, 0};
    while (p.local_ms >= scene_ms[p.scene]) {
        p.local_ms -= scene_ms[p.scene];
        p.start_ms += scene_ms[p.scene];
        p.scene++;
    }
    p.duration_ms = scene_ms[p.scene];
    return p;
}

const char *eh_scene_name(int scene)
{
    return scene >= 0 && scene < EH_SCENE_COUNT ? scene_names[scene] : "?";
}

void eh_demo_init(eh_demo *d)
{
    eh_camera_init(&d->cam);
    for (int k = 0; k < EH_LED_COUNT; k++)
        d->leds[k] = (eh_rgb){0, 0, 0};
    d->led_max = EH_LED_DEFAULT_MAX;
    d->active_scene = -1;
    d->last_t_ms = 0;
    d->have_last = 0;
}

/* Scene entry builds that scene's tables. The particles shatter out of the
 * terrain's final frame, re-rendered here at its exact end time so a seek
 * produces the same particles as continuous playback; seeding then reads
 * only the framebuffer, so the particle state may overwrite the terrain's. */
static void enter_scene(eh_demo *d, int scene, eh_rgb565 *fb)
{
    switch (scene) {
    case EH_SCENE_TORUS:
        eh_torus_init(&d->ws.torus, d->cam.palette);
        break;
    case EH_SCENE_TUNNEL:
        eh_tunnel_init(&d->ws.tunnel, d->cam.palette);
        break;
    case EH_SCENE_TERRAIN:
        eh_terrain_init(&d->ws.terrain);
        break;
    case EH_SCENE_PARTICLES:
        if (d->active_scene != EH_SCENE_TERRAIN)
            eh_terrain_init(&d->ws.terrain);
        eh_terrain_render(&d->ws.terrain, fb, EH_SCENE_TERRAIN_MS - 1, EH_SCENE_TERRAIN_MS,
                          &d->cam);
        eh_particles_init(&d->ws.particles, fb);
        break;
    }
    d->active_scene = scene;
}

void eh_demo_prepare(eh_demo *d, uint32_t t_ms, uint32_t buttons, eh_rgb565 *fb)
{
    float dt = 0.0f;
    if (d->have_last && t_ms >= d->last_t_ms) {
        const uint32_t step = t_ms - d->last_t_ms;
        dt = (step > 100u ? 100u : step) / 1000.0f;
    }
    d->last_t_ms = t_ms;
    d->have_last = 1;
    eh_camera_update(&d->cam, buttons, dt);

    const eh_timeline_pos p = eh_timeline_at(t_ms);
    d->pos = p;
    if (p.scene != d->active_scene)
        enter_scene(d, p.scene, fb);

    switch (p.scene) {
    case EH_SCENE_TORUS:
        eh_torus_prepare(&d->ws.torus, p.local_ms, p.duration_ms, &d->cam);
        break;
    case EH_SCENE_TUNNEL:
        eh_tunnel_prepare(&d->ws.tunnel, p.local_ms, p.duration_ms, &d->cam);
        break;
    case EH_SCENE_TERRAIN:
        eh_terrain_prepare(&d->ws.terrain, p.local_ms, p.duration_ms, &d->cam);
        break;
    case EH_SCENE_PARTICLES:
        eh_particles_prepare(&d->ws.particles, p.local_ms, p.duration_ms, &d->cam);
        break;
    }
}

int eh_demo_passes(const eh_demo *d)
{
    switch (d->pos.scene) {
    case EH_SCENE_TORUS:
        return EH_TORUS_PASSES;
    case EH_SCENE_PARTICLES:
        return EH_PARTICLES_PASSES;
    default:
        return 1;
    }
}

void eh_demo_render_part(eh_demo *d, eh_rgb565 *fb, int pass, int part, int worker)
{
    switch (d->pos.scene) {
    case EH_SCENE_TORUS:
        eh_torus_render_part(&d->ws.torus, fb, pass, part, worker);
        break;
    case EH_SCENE_TUNNEL:
        eh_tunnel_render_part(&d->ws.tunnel, fb, part);
        break;
    case EH_SCENE_TERRAIN:
        eh_terrain_render_part(&d->ws.terrain, fb, part);
        break;
    case EH_SCENE_PARTICLES:
        eh_particles_render_part(&d->ws.particles, fb, pass, part);
        break;
    }
}

void eh_demo_finish(eh_demo *d, const eh_rgb565 *fb)
{
    eh_leds_from_frame(fb, d->leds, d->led_max);
}

void eh_demo_frame(eh_demo *d, uint32_t t_ms, uint32_t buttons, eh_rgb565 *fb)
{
    eh_demo_prepare(d, t_ms, buttons, fb);
    const int passes = eh_demo_passes(d);
    for (int pass = 0; pass < passes; pass++)
        for (int part = 0; part < EH_PARTS; part++)
            eh_demo_render_part(d, fb, pass, part, part % EH_WORKERS);
    eh_demo_finish(d, fb);
}
