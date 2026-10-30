/* The dual-boot menu's on-screen selection (eh_boot_select.h) and its
 * drawing (device/menu/eh_menu_draw.h). */
#include <string.h>

#include "eh_boot_select.h"
#include "eh_menu_art.h"
#include "eh_menu_draw.h"
#include "eh_test.h"

#define BIT(pin) EH_BTN_BIT(pin)
#define LEFT     BIT(EH_BADGE_BTN_LEFT_PIN)
#define RIGHT    BIT(EH_BADGE_BTN_RIGHT_PIN)
#define CENTRE   BIT(EH_BADGE_BTN_CENTRE_PIN)
#define T0       5000u /* an arbitrary start time */
#define TOUT     EH_MENU_TIMEOUT_US

void test_menu_select_times_out_to_stock(void)
{
    eh_menu_select m;
    EH_CHECK(TOUT == 3000000u); /* 1 s proved too short on the badge */
    eh_menu_select_init(&m, 0, T0);
    EH_CHECK(m.choice == EH_BOOT_STOCK);
    EH_CHECK(eh_menu_select_remaining_us(&m, T0) == TOUT);
    EH_CHECK(!eh_menu_select_step(&m, 0, T0 + TOUT - 1));
    EH_CHECK(eh_menu_select_remaining_us(&m, T0 + TOUT - 1) == 1);
    EH_CHECK(eh_menu_select_step(&m, 0, T0 + TOUT));
    EH_CHECK(m.choice == EH_BOOT_STOCK);
    EH_CHECK(eh_menu_select_remaining_us(&m, T0 + 2 * TOUT) == 0);
}

void test_menu_select_moves_restart_the_countdown(void)
{
    eh_menu_select m;
    eh_menu_select_init(&m, 0, T0);
    const uint64_t t1 = T0 + TOUT / 2;
    EH_CHECK(!eh_menu_select_step(&m, RIGHT, t1));
    EH_CHECK(m.choice == EH_BOOT_DEMO);
    EH_CHECK(!eh_menu_select_step(&m, RIGHT, T0 + TOUT)); /* held, not a new press */
    EH_CHECK(eh_menu_select_remaining_us(&m, T0 + TOUT) == TOUT / 2);
    EH_CHECK(!eh_menu_select_step(&m, 0, t1 + TOUT - 1));
    EH_CHECK(eh_menu_select_step(&m, 0, t1 + TOUT));
    EH_CHECK(m.choice == EH_BOOT_DEMO);

    /* And back: LEFT returns to stock with a fresh countdown. */
    eh_menu_select_init(&m, 0, T0);
    eh_menu_select_step(&m, RIGHT, T0 + 10);
    eh_menu_select_step(&m, 0, T0 + 20);
    EH_CHECK(!eh_menu_select_step(&m, LEFT, T0 + 900000));
    EH_CHECK(m.choice == EH_BOOT_STOCK);
    EH_CHECK(!eh_menu_select_step(&m, 0, T0 + 900000 + TOUT - 1));
    EH_CHECK(eh_menu_select_step(&m, 0, T0 + 900000 + TOUT));
    EH_CHECK(m.choice == EH_BOOT_STOCK);
}

void test_menu_select_centre_confirms(void)
{
    eh_menu_select m;
    eh_menu_select_init(&m, 0, T0);
    EH_CHECK(eh_menu_select_step(&m, CENTRE, T0 + 1));
    EH_CHECK(m.choice == EH_BOOT_STOCK);

    eh_menu_select_init(&m, 0, T0);
    eh_menu_select_step(&m, RIGHT, T0 + 1);
    EH_CHECK(eh_menu_select_step(&m, RIGHT | CENTRE, T0 + 2));
    EH_CHECK(m.choice == EH_BOOT_DEMO);
}

void test_menu_select_ignores_other_keys(void)
{
    static const unsigned pins[] = {EH_BADGE_BTN_A_PIN, EH_BADGE_BTN_B_PIN, EH_BADGE_BTN_UP_PIN,
                                    EH_BADGE_BTN_DOWN_PIN, EH_BADGE_EEPROM_SDA_PIN};
    for (unsigned i = 0; i < sizeof pins / sizeof pins[0]; i++) {
        eh_menu_select m;
        eh_menu_select_init(&m, 0, T0);
        EH_CHECK_MSG(!eh_menu_select_step(&m, BIT(pins[i]), T0 + 1), "pin %u confirmed", pins[i]);
        EH_CHECK_MSG(!eh_menu_select_step(&m, 0, T0 + 2), "pin %u", pins[i]);
        EH_CHECK_MSG(m.choice == EH_BOOT_STOCK, "pin %u moved the choice", pins[i]);
        EH_CHECK_MSG(eh_menu_select_remaining_us(&m, T0 + 2) == TOUT - 2, "pin %u restarted", pins[i]);
    }
    /* Both sides at once: no move. */
    eh_menu_select m;
    eh_menu_select_init(&m, 0, T0);
    eh_menu_select_step(&m, LEFT | RIGHT, T0 + 1);
    EH_CHECK(m.choice == EH_BOOT_STOCK);
    EH_CHECK(eh_menu_select_remaining_us(&m, T0 + 1) == TOUT - 1);
}

void test_menu_select_keys_down_at_start_are_not_presses(void)
{
    eh_menu_select m;
    eh_menu_select_init(&m, RIGHT | CENTRE, T0);
    EH_CHECK(!eh_menu_select_step(&m, RIGHT | CENTRE, T0 + 1));
    EH_CHECK(m.choice == EH_BOOT_STOCK);
    EH_CHECK(!eh_menu_select_step(&m, 0, T0 + 2));
    EH_CHECK(!eh_menu_select_step(&m, RIGHT, T0 + 3));
    EH_CHECK(m.choice == EH_BOOT_DEMO);
}

/* ---- drawing ---- */

static uint16_t fb[EH_MENU_W * EH_MENU_H];

static uint16_t px(int x, int y)
{
    return fb[y * EH_MENU_W + x];
}

static uint16_t dim(uint16_t c)
{
    unsigned r = (c >> 11) * 3 / 8, g = ((c >> 5) & 63) * 3 / 8, b = (c & 31) * 3 / 8;
    return (uint16_t)(r << 11 | g << 5 | b);
}

static int count_colour(int top, int rows, uint16_t c)
{
    int n = 0;
    for (int y = top; y < top + rows; y++)
        for (int x = 0; x < EH_MENU_W; x++)
            n += px(x, y) == c;
    return n;
}

#define CX (EH_MENU_W / 2)
#define CY (EH_MENU_H / 2)
#define STOCK_X (CX - EH_MENU_TILE_DX)
#define DEMO_X  (CX + EH_MENU_TILE_DX)
#define NAVY    0x1908u /* the emblem's star */

void test_menu_draw_marks_the_choice(void)
{
    const int ring = (EH_MENU_RING_IN + EH_MENU_RING_OUT) / 2;
    const uint16_t torus_centre =
        eh_menu_torus_palette[eh_menu_torus_px[(EH_MENU_TILE / 2) * EH_MENU_TILE + EH_MENU_TILE / 2]];

    eh_menu_draw(fb, EH_BOOT_STOCK, TOUT, false);
    EH_CHECK(px(STOCK_X, CY) == NAVY);                       /* lit */
    EH_CHECK(px(DEMO_X, CY) == dim(torus_centre));           /* dimmed */
    EH_CHECK(px(STOCK_X, CY - ring) == EH_MENU_STOCK_COLOUR); /* ringed */
    EH_CHECK(px(DEMO_X, CY - ring) == 0);
    int stock_label = count_colour(EH_MENU_LABEL_TOP, EH_MENU_TEXT_H, EH_MENU_STOCK_COLOUR);
    EH_CHECK(stock_label > 0);

    eh_menu_draw(fb, EH_BOOT_DEMO, TOUT, false);
    EH_CHECK(px(STOCK_X, CY) == dim(NAVY));
    EH_CHECK(px(DEMO_X, CY) == torus_centre);
    EH_CHECK(px(DEMO_X, CY - ring) == EH_MENU_DEMO_COLOUR);
    EH_CHECK(px(STOCK_X, CY - ring) == 0);
    EH_CHECK(count_colour(EH_MENU_LABEL_TOP, EH_MENU_TEXT_H, EH_MENU_STOCK_COLOUR) == 0);
    /* EVENT HORIZON has more lit cells than WAYFARER. */
    EH_CHECK(count_colour(EH_MENU_LABEL_TOP, EH_MENU_TEXT_H, EH_MENU_DEMO_COLOUR) > stock_label);

    /* Everything off the round glass stays black, and nothing reaches the
     * label or prompt bands except text. */
    for (int y = 0; y < EH_MENU_H; y++)
        for (int x = 0; x < EH_MENU_W; x++) {
            int dx = x - CX, dy = y - CY;
            if (dx * dx + dy * dy >= 180 * 180)
                EH_CHECK_MSG(px(x, y) == 0, "(%d, %d) off the glass", x, y);
        }
}

void test_menu_draw_release_prompt(void)
{
    eh_menu_draw(fb, EH_BOOT_STOCK, TOUT, false);
    EH_CHECK(count_colour(EH_MENU_RELEASE_TOP, EH_MENU_TEXT_H, 0xffffu) == 0);
    eh_menu_draw(fb, EH_BOOT_STOCK, TOUT, true);
    EH_CHECK(count_colour(EH_MENU_RELEASE_TOP, EH_MENU_TEXT_H, 0xffffu) > 0);
}

void test_menu_draw_countdown_arc(void)
{
    const int r = (EH_MENU_ARC_IN + EH_MENU_ARC_OUT) / 2;
    eh_menu_draw(fb, EH_BOOT_DEMO, TOUT, false);
    EH_CHECK(px(CX + 1, CY - r) == EH_MENU_DEMO_COLOUR); /* 12 o'clock */
    EH_CHECK(px(CX - r, CY + 1) == EH_MENU_DEMO_COLOUR); /* 9 o'clock */

    /* Half left: the clockwise half from 12 to 6 o'clock stays lit. */
    eh_menu_draw_countdown(fb, EH_BOOT_DEMO, TOUT / 2);
    EH_CHECK(px(CX + r, CY) == EH_MENU_DEMO_COLOUR); /* 3 o'clock */
    EH_CHECK(px(CX - r, CY) == EH_MENU_ARC_TRACK);   /* 9 o'clock */

    eh_menu_draw_countdown(fb, EH_BOOT_DEMO, 0);
    EH_CHECK(px(CX + r, CY) == EH_MENU_ARC_TRACK);
    EH_CHECK(px(CX + 1, CY - r) == EH_MENU_ARC_TRACK);
}

void test_menu_draw_countdown_touches_only_the_arc(void)
{
    static uint16_t before[EH_MENU_W * EH_MENU_H];
    eh_menu_draw(fb, EH_BOOT_STOCK, TOUT, true);
    memcpy(before, fb, sizeof fb);
    eh_menu_draw_countdown(fb, EH_BOOT_STOCK, TOUT / 3);
    int changed = 0;
    for (int y = 0; y < EH_MENU_H; y++)
        for (int x = 0; x < EH_MENU_W; x++) {
            int dx = x - CX, dy = y - CY, d2 = dx * dx + dy * dy;
            int on_arc = d2 >= EH_MENU_ARC_IN * EH_MENU_ARC_IN && d2 < EH_MENU_ARC_OUT * EH_MENU_ARC_OUT;
            if (px(x, y) != before[y * EH_MENU_W + x]) {
                changed++;
                EH_CHECK_MSG(on_arc, "(%d, %d) changed off the arc", x, y);
            }
        }
    EH_CHECK(changed > 0);
}
