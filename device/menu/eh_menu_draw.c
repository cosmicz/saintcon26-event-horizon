#include "eh_menu_draw.h"

#include <math.h>

#include "eh_menu_art.h"

#define CX (EH_MENU_W / 2)
#define CY (EH_MENU_H / 2)
#define PI_F 3.14159265f

/* 5x7 block glyphs, one row per byte, bit 4 = leftmost column: the end
 * title's letterforms (src/core/eh_particles.c) plus W, Y, F and L. A copy,
 * so the menu does not depend on the demo's sources. */
static const struct {
    char ch;
    uint8_t rows[7];
} glyphs[] = {
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
};

static const uint8_t *glyph(char ch)
{
    for (unsigned i = 0; i < sizeof glyphs / sizeof glyphs[0]; i++)
        if (glyphs[i].ch == ch)
            return glyphs[i].rows;
    return 0; /* space, or a letter the menu never prints */
}

/* Centred on the screen's vertical axis. */
static void text(uint16_t *fb, const char *s, int top, uint16_t colour)
{
    const int scale = EH_MENU_TEXT_SCALE;
    int len = 0;
    while (s[len])
        len++;
    const int left = CX - (len * 6 * scale - scale) / 2;
    for (int c = 0; c < len; c++) {
        const uint8_t *rows = glyph(s[c]);
        if (!rows)
            continue;
        for (int gy = 0; gy < 7 * scale; gy++)
            for (int gx = 0; gx < 5 * scale; gx++)
                if (rows[gy / scale] & (0x10 >> (gx / scale)))
                    fb[(top + gy) * EH_MENU_W + left + c * 6 * scale + gx] = colour;
    }
}

static uint16_t dim(uint16_t c)
{
    unsigned r = (c >> 11) * 3 / 8, g = ((c >> 5) & 63) * 3 / 8, b = (c & 31) * 3 / 8;
    return (uint16_t)(r << 11 | g << 5 | b);
}

/* Wayfarer emblem, our own: a navy four-point star (an astroid, like the
 * sparkles on the badge's artwork) on a cream disc, a gold inner ring and
 * eight constellation dots. */
static uint16_t wayfarer_pixel(int dx, int dy)
{
    const uint16_t cream = 0xef3a, navy = 0x1908, gold = 0xc509;
    int d2 = dx * dx + dy * dy;
    if (d2 >= 59 * 59 && d2 < 63 * 63)
        return gold;
    float ax = fabsf((float)dx), ay = fabsf((float)dy);
    if (sqrtf(ax) + sqrtf(ay) <= sqrtf(52.0f))
        return navy;
    /* 44 px from the centre at 22.5 + k * 45 degrees */
    static const int8_t dots[8][2] = {{41, 17}, {17, 41}, {-17, 41}, {-41, 17},
                                      {-41, -17}, {-17, -41}, {17, -41}, {41, -17}};
    for (int k = 0; k < 8; k++) {
        int px = dots[k][0] - dx, py = dots[k][1] - dy;
        if (px * px + py * py <= 9)
            return navy;
    }
    return cream;
}

static void tile(uint16_t *fb, int cx, eh_boot_choice which, bool chosen)
{
    const int r = EH_MENU_TILE_R, half = EH_MENU_TILE / 2;
    const uint16_t ring = which == EH_BOOT_STOCK ? EH_MENU_STOCK_COLOUR : EH_MENU_DEMO_COLOUR;
    for (int dy = -EH_MENU_RING_OUT; dy <= EH_MENU_RING_OUT; dy++)
        for (int dx = -EH_MENU_RING_OUT; dx <= EH_MENU_RING_OUT; dx++) {
            int d2 = dx * dx + dy * dy;
            uint16_t c;
            if (d2 < r * r) {
                c = which == EH_BOOT_STOCK
                        ? wayfarer_pixel(dx, dy)
                        : eh_menu_torus_palette[eh_menu_torus_px[(dy + half) * EH_MENU_TILE + dx + half]];
                if (!chosen)
                    c = dim(c);
            } else if (chosen && d2 >= EH_MENU_RING_IN * EH_MENU_RING_IN &&
                       d2 < EH_MENU_RING_OUT * EH_MENU_RING_OUT) {
                c = ring;
            } else {
                continue;
            }
            fb[(CY + dy) * EH_MENU_W + cx + dx] = c;
        }
}

/* Lit from 12 o'clock clockwise over the remaining fraction. */
void eh_menu_draw_countdown(uint16_t *fb, eh_boot_choice choice, uint32_t remaining_us)
{
    const uint16_t colour = choice == EH_BOOT_STOCK ? EH_MENU_STOCK_COLOUR : EH_MENU_DEMO_COLOUR;
    float lit = (float)remaining_us / (float)EH_MENU_TIMEOUT_US * 2.0f * PI_F;
    for (int y = 0; y < EH_MENU_H; y++)
        for (int x = 0; x < EH_MENU_W; x++) {
            int dx = x - CX, dy = y - CY, d2 = dx * dx + dy * dy;
            if (d2 < EH_MENU_ARC_IN * EH_MENU_ARC_IN || d2 >= EH_MENU_ARC_OUT * EH_MENU_ARC_OUT)
                continue;
            float a = atan2f((float)dx, (float)-dy); /* 0 at 12 o'clock, clockwise */
            if (a < 0.0f)
                a += 2.0f * PI_F;
            fb[y * EH_MENU_W + x] = a < lit ? colour : EH_MENU_ARC_TRACK;
        }
}

void eh_menu_draw(uint16_t *fb, eh_boot_choice choice, uint32_t remaining_us, bool release)
{
    const uint16_t colour = choice == EH_BOOT_STOCK ? EH_MENU_STOCK_COLOUR : EH_MENU_DEMO_COLOUR;
    for (int i = 0; i < EH_MENU_W * EH_MENU_H; i++)
        fb[i] = 0;
    tile(fb, CX - EH_MENU_TILE_DX, EH_BOOT_STOCK, choice == EH_BOOT_STOCK);
    tile(fb, CX + EH_MENU_TILE_DX, EH_BOOT_DEMO, choice == EH_BOOT_DEMO);
    text(fb, choice == EH_BOOT_STOCK ? "WAYFARER" : "EVENT HORIZON", EH_MENU_LABEL_TOP, colour);
    if (release)
        text(fb, "RELEASE", EH_MENU_RELEASE_TOP, 0xffffu);
    eh_menu_draw_countdown(fb, choice, remaining_us);
}
