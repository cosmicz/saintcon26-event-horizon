/* Interactive SDL2 preview of the circular display and LED ring.
 *
 * Only the video subsystem is initialised: no joystick, game controller,
 * HID, audio or sensor subsystems, so nothing enumerates attached devices.
 * Keyboard stands in for the badge controls:
 *   arrows = D-pad, Enter/Space = D-pad centre, Z = A (palette), X = B (wireframe)
 *   P = pause, [ / ] = seek -/+5 s, 1-4 = jump to scene, R = restart, Esc/Q = quit
 *
 *   eh_preview [--scale N] [--start MS] [--frames N] [--threads 1|2] [--screenshot FILE.ppm]
 * --frames runs N fixed 33 ms steps then exits (used by the smoke test).
 * --threads defaults to 2, the dual-core frame plan on host threads; the
 * smoke tests compare its output with the single-threaded CLI. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eh/eh_demo.h"
#include "eh_pool.h"
#include "eh_present.h"

static eh_rgb565 fb[EH_FB_PIXELS];
static uint8_t canvas[EH_PRESENT_W * EH_PRESENT_H * 3];
static eh_demo demo;

static uint32_t read_buttons(void)
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    uint32_t b = 0;
    if (k[SDL_SCANCODE_UP])
        b |= EH_BTN_UP;
    if (k[SDL_SCANCODE_DOWN])
        b |= EH_BTN_DOWN;
    if (k[SDL_SCANCODE_LEFT])
        b |= EH_BTN_LEFT;
    if (k[SDL_SCANCODE_RIGHT])
        b |= EH_BTN_RIGHT;
    if (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_SPACE])
        b |= EH_BTN_CENTER;
    if (k[SDL_SCANCODE_Z])
        b |= EH_BTN_A;
    if (k[SDL_SCANCODE_X])
        b |= EH_BTN_B;
    return b;
}

static uint32_t scene_start(int scene)
{
    uint32_t t = 0;
    for (int s = 0; s < scene; s++)
        t += eh_timeline_at(t).duration_ms;
    return t;
}

int main(int argc, char **argv)
{
    int scale = 1, frames = 0, threads = EH_WORKERS;
    uint32_t t_ms = 0;
    const char *screenshot = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--scale") && i + 1 < argc)
            scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--start") && i + 1 < argc)
            t_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc)
            screenshot = argv[++i];
        else if (!strcmp(argv[i], "--threads") && i + 1 < argc)
            threads = atoi(argv[++i]);
        else {
            fprintf(stderr, "usage: eh_preview [--scale N] [--start MS] [--frames N] "
                            "[--threads 1|2] [--screenshot FILE.ppm]\n");
            return 2;
        }
    }
    if (scale < 1)
        scale = 1;
    eh_pool *pool = eh_pool_create(threads);
    if (!pool) {
        fprintf(stderr, "eh_preview: --threads must be 1 or %d\n", EH_WORKERS);
        return 2;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init(VIDEO): %s\n", SDL_GetError());
        eh_pool_destroy(pool);
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow("EVENT HORIZON preview", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, EH_PRESENT_W * scale,
                                       EH_PRESENT_H * scale, 0);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, frames ? 0 : SDL_RENDERER_PRESENTVSYNC)
                            : NULL;
    if (win && !ren)
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    SDL_Texture *tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24,
                                               SDL_TEXTUREACCESS_STREAMING, EH_PRESENT_W,
                                               EH_PRESENT_H)
                           : NULL;
    if (!tex) {
        fprintf(stderr, "SDL setup: %s\n", SDL_GetError());
        SDL_Quit();
        eh_pool_destroy(pool);
        return 1;
    }

    eh_demo_init(&demo);
    int running = 1, paused = 0, count = 0;
    Uint64 last = SDL_GetTicks64(), title_at = last;
    double render_acc = 0;
    int render_n = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT)
                running = 0;
            if (e.type != SDL_KEYDOWN || e.key.repeat)
                continue;
            switch (e.key.keysym.sym) {
            case SDLK_ESCAPE:
            case SDLK_q:
                running = 0;
                break;
            case SDLK_p:
                paused = !paused;
                break;
            case SDLK_r:
                t_ms = 0;
                eh_demo_init(&demo);
                break;
            case SDLK_LEFTBRACKET:
                t_ms = t_ms > 5000 ? t_ms - 5000 : 0;
                break;
            case SDLK_RIGHTBRACKET:
                t_ms += 5000;
                break;
            case SDLK_1:
            case SDLK_2:
            case SDLK_3:
            case SDLK_4:
                t_ms = scene_start(e.key.keysym.sym - SDLK_1);
                break;
            default:
                break;
            }
        }

        const Uint64 now = SDL_GetTicks64();
        if (frames)
            t_ms += count ? 33u : 0u;
        else if (!paused)
            t_ms += (uint32_t)(now - last);
        last = now;

        const Uint64 r0 = SDL_GetPerformanceCounter();
        eh_pool_frame(pool, &demo, t_ms, read_buttons(), fb);
        render_acc += (double)(SDL_GetPerformanceCounter() - r0) * 1000.0 /
                      (double)SDL_GetPerformanceFrequency();
        render_n++;

        eh_present(fb, demo.leds, demo.led_max, canvas);
        SDL_UpdateTexture(tex, NULL, canvas, EH_PRESENT_W * 3);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        if (now - title_at >= 500) {
            char title[160];
            const eh_timeline_pos p = eh_timeline_at(t_ms);
            snprintf(title, sizeof title,
                     "EVENT HORIZON  %s  t=%.1fs  render %.2f ms (host, %d thr)  pal %u  wire %d%s",
                     eh_scene_name(p.scene), (t_ms % EH_DEMO_LOOP_MS) / 1000.0,
                     render_acc / render_n, threads, demo.cam.palette, demo.cam.wireframe,
                     paused ? "  [paused]" : "");
            SDL_SetWindowTitle(win, title);
            title_at = now;
            render_acc = 0;
            render_n = 0;
        }
        if (frames && ++count >= frames)
            running = 0;
    }

    int rc = 0;
    if (screenshot && eh_ppm_write(screenshot, canvas, EH_PRESENT_W, EH_PRESENT_H) != 0) {
        perror(screenshot);
        rc = 1;
    }
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    eh_pool_destroy(pool);
    return rc;
}
