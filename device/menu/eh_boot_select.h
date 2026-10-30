/* Boot selection rules for the dual-boot menu, kept free of SDK calls so the
 * host suite can test them.
 *
 * Why these rules: stock firmware reads buttons held during its start-up,
 * and one such combination resets the badge's saved state in its EEPROM. So
 * the menu selects with the D-pad only, and
 * it hands over to stock only after every button has read released for
 * EH_RELEASE_HOLD_US without a break. The demo reads keys (A/B included) only
 * as controls, and none of its key handling can reach the EEPROM: no I2C, no
 * GPIO4/5, no flash writes (image_audit, pin_audit). So it may start with
 * keys still down.
 *
 * Masks are "pressed" masks indexed by GPIO number (bit n = GPIO n pressed),
 * i.e. the inverted SIO GPIO_IN. Bits for any other pin are ignored. */
#ifndef EH_BOOT_SELECT_H
#define EH_BOOT_SELECT_H

#include <stdbool.h>
#include <stdint.h>

#include "eh_badge_pins.h"

#define EH_BTN_BIT(pin) (1u << (pin))
#define EH_DPAD_MASK                                                          \
    (EH_BTN_BIT(EH_BADGE_BTN_UP_PIN) | EH_BTN_BIT(EH_BADGE_BTN_DOWN_PIN) |   \
     EH_BTN_BIT(EH_BADGE_BTN_LEFT_PIN) | EH_BTN_BIT(EH_BADGE_BTN_RIGHT_PIN) | \
     EH_BTN_BIT(EH_BADGE_BTN_CENTRE_PIN))
#define EH_ALL_BUTTONS_MASK \
    (EH_DPAD_MASK | EH_BTN_BIT(EH_BADGE_BTN_A_PIN) | EH_BTN_BIT(EH_BADGE_BTN_B_PIN))
#define EH_DEMO_SELECT_BIT  EH_BTN_BIT(EH_BADGE_BTN_RIGHT_PIN)
#define EH_STOCK_SELECT_BIT EH_BTN_BIT(EH_BADGE_BTN_LEFT_PIN)
#define EH_CONFIRM_BIT      EH_BTN_BIT(EH_BADGE_BTN_CENTRE_PIN)

#define EH_SELECT_SAMPLES     16     /* D-pad samples at power-on */
#define EH_SELECT_INTERVAL_US 2000   /* 16 x 2 ms = 32 ms window */
#define EH_RELEASE_HOLD_US    100000 /* all released for 100 ms */
#define EH_MENU_TIMEOUT_US    3000000 /* on-screen choice: 3 s after the last move (1 s proved too short) */

typedef enum { EH_BOOT_STOCK = 0, EH_BOOT_DEMO = 1 } eh_boot_choice;

/* Demo only if RIGHT reads pressed in every one of n samples; anything else,
 * including n == 0 or a bounce, boots stock. Only D-pad bits are consulted. */
eh_boot_choice eh_boot_choose(const uint32_t *pressed, unsigned n);

typedef struct {
    bool released;            /* all buttons read released at the last step */
    uint64_t released_since_us;
} eh_release_tracker;

void eh_release_init(eh_release_tracker *t);

/* Feed one sample; true once every button has read released continuously
 * for at least EH_RELEASE_HOLD_US. Any press restarts the interval. */
bool eh_release_step(eh_release_tracker *t, uint32_t pressed, uint64_t now_us);

/* On-screen selection (when RIGHT was not held at power-on). LEFT picks
 * stock, RIGHT the demo; each such press restarts the countdown. CENTRE
 * confirms at once. A, B, UP and DOWN do nothing. Only presses count: keys
 * already down at init are not presses. */
typedef struct {
    eh_boot_choice choice;
    uint64_t deadline_us;
    uint32_t held; /* D-pad bits down at the previous step */
} eh_menu_select;

/* Starts on stock, the default when nobody touches the badge. */
void eh_menu_select_init(eh_menu_select *m, uint32_t pressed, uint64_t now_us);

/* Feed one sample; true once the choice is confirmed (CENTRE or timeout). */
bool eh_menu_select_step(eh_menu_select *m, uint32_t pressed, uint64_t now_us);

/* Countdown left, 0..EH_MENU_TIMEOUT_US. */
uint32_t eh_menu_select_remaining_us(const eh_menu_select *m, uint64_t now_us);

#endif
