/* The dual-boot menu's screen, drawn into a 360x360 RGB565 frame buffer.
 * Portable C (no SDK) so the host suite can check the pixels.
 *
 * Layout on the round panel: two round tiles side by side, Wayfarer on the
 * left (an emblem drawn here, not stock art) and our torus on the right (a
 * host render, eh_menu_art.h). The chosen tile is lit and ringed in its
 * colour, the other dimmed; the chosen name sits above them; an arc around
 * the rim shows the countdown. On the stock path, RELEASE appears below while
 * any key is still down. */
#ifndef EH_MENU_DRAW_H
#define EH_MENU_DRAW_H

#include <stdbool.h>
#include <stdint.h>

#include "eh_boot_select.h"

#define EH_MENU_W 360
#define EH_MENU_H 360

/* Geometry, exported for the tests. */
#define EH_MENU_TILE_R      70  /* tile radius */
#define EH_MENU_TILE_DX     78  /* tile centre offset from the screen centre */
#define EH_MENU_RING_IN     72  /* selection ring, radii from the tile centre */
#define EH_MENU_RING_OUT    77
#define EH_MENU_ARC_IN      170 /* countdown arc, radii from the screen centre */
#define EH_MENU_ARC_OUT     177
#define EH_MENU_TEXT_SCALE  3   /* 5x7 glyphs at 15x21 px */
#define EH_MENU_TEXT_H      (7 * EH_MENU_TEXT_SCALE)
#define EH_MENU_LABEL_TOP   72
#define EH_MENU_RELEASE_TOP 270

#define EH_MENU_STOCK_COLOUR 0xe5a8u /* gold, ~(230, 180, 64) */
#define EH_MENU_DEMO_COLOUR  0xfbc7u /* the post's orange, ~(255, 122, 61) */
#define EH_MENU_ARC_TRACK    0x2104u /* dark grey */

/* remaining_us is the countdown left (0..EH_MENU_TIMEOUT_US); release draws
 * the RELEASE prompt. */
void eh_menu_draw(uint16_t *fb, eh_boot_choice choice, uint32_t remaining_us, bool release);

/* Redraws only the countdown arc; the rest of fb must already hold a full
 * eh_menu_draw for the same choice. */
void eh_menu_draw_countdown(uint16_t *fb, eh_boot_choice choice, uint32_t remaining_us);

#endif
