/* Host render of the dual-boot menu's on-screen countdown, as PPM frames.
 *
 *   eh_menu_frames OUT_DIR [--fps N] [--until-remaining-ms MS] [--right-at-ms MS]
 *
 * Runs the menu's own selection rules and drawing (device/menu: eh_menu_select_*,
 * eh_menu_draw, eh_menu_draw_countdown) the way device/menu/main.c does after
 * the screen comes up: one full draw, then per frame a step, and a full redraw
 * only when the choice changes, else just the countdown arc. Time and keys are
 * simulated: frames are 1/fps apart, and --right-at-ms holds RIGHT for 150 ms
 * from that time. Stops once the countdown has at most --until-remaining-ms
 * left (default 1000) or the menu decides. The LEDs are not drawn.
 *
 * Writes OUT_DIR/frame_NNNN.ppm (360x360) and one JSON line per frame on
 * stdout: frame, t_ms, choice, remaining_ms. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eh_boot_select.h"
#include "eh_menu_draw.h"

#define RIGHT_HOLD_US 150000u

static uint16_t fb[EH_MENU_W * EH_MENU_H];

static int write_ppm(const char *dir, int n)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/frame_%04d.ppm", dir, n);
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    fprintf(f, "P6\n%d %d\n255\n", EH_MENU_W, EH_MENU_H);
    for (int i = 0; i < EH_MENU_W * EH_MENU_H; i++) {
        const uint16_t c = fb[i];
        const uint8_t rgb[3] = {(uint8_t)((c >> 11) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63),
                                (uint8_t)((c & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    return fclose(f);
}

static void report(int n, uint64_t t_us, eh_boot_choice choice, uint32_t remaining_us)
{
    printf("{\"frame\": %d, \"t_ms\": %.3f, \"choice\": \"%s\", \"remaining_ms\": %.3f}\n", n, t_us / 1000.0,
           choice == EH_BOOT_STOCK ? "stock" : "demo", remaining_us / 1000.0);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: eh_menu_frames OUT_DIR [--fps N] [--until-remaining-ms MS] [--right-at-ms MS]\n");
        return 2;
    }
    const char *dir = argv[1];
    int fps = 30;
    long until_ms = 1000, right_at_ms = -1;
    for (int i = 2; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--fps"))
            fps = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--until-remaining-ms"))
            until_ms = atol(argv[i + 1]);
        else if (!strcmp(argv[i], "--right-at-ms"))
            right_at_ms = atol(argv[i + 1]);
        else {
            fprintf(stderr, "eh_menu_frames: unknown option %s\n", argv[i]);
            return 2;
        }
    }
    if (fps <= 0 || until_ms < 0 || until_ms * 1000 > EH_MENU_TIMEOUT_US) {
        fprintf(stderr, "eh_menu_frames: bad --fps or --until-remaining-ms\n");
        return 2;
    }

    /* As in main.c: the first frame is drawn before the countdown starts. */
    eh_menu_draw(fb, EH_BOOT_STOCK, EH_MENU_TIMEOUT_US, false);
    eh_menu_select m;
    eh_menu_select_init(&m, 0, 0);
    eh_boot_choice shown = m.choice;
    int n = 0;
    if (write_ppm(dir, n))
        return perror(dir), 1;
    report(n, 0, shown, EH_MENU_TIMEOUT_US);

    for (n = 1;; n++) {
        const uint64_t t = (uint64_t)n * 1000000u / (unsigned)fps;
        const uint64_t right_at = (uint64_t)right_at_ms * 1000u;
        const uint32_t keys =
            right_at_ms >= 0 && t >= right_at && t < right_at + RIGHT_HOLD_US ? EH_DEMO_SELECT_BIT : 0;
        const bool decided = eh_menu_select_step(&m, keys, t);
        const uint32_t remaining = eh_menu_select_remaining_us(&m, t);
        if (decided || remaining < (uint32_t)until_ms * 1000u)
            break;
        if (m.choice != shown) {
            eh_menu_draw(fb, m.choice, remaining, false);
            shown = m.choice;
        } else {
            eh_menu_draw_countdown(fb, m.choice, remaining);
        }
        if (write_ppm(dir, n))
            return perror(dir), 1;
        report(n, t, shown, remaining);
    }
    return 0;
}
