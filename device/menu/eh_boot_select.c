#include "eh_boot_select.h"

eh_boot_choice eh_boot_choose(const uint32_t *pressed, unsigned n)
{
    if (n == 0)
        return EH_BOOT_STOCK;
    for (unsigned i = 0; i < n; i++)
        if (!(pressed[i] & EH_DPAD_MASK & EH_DEMO_SELECT_BIT))
            return EH_BOOT_STOCK;
    return EH_BOOT_DEMO;
}

void eh_release_init(eh_release_tracker *t)
{
    t->released = false;
    t->released_since_us = 0;
}

bool eh_release_step(eh_release_tracker *t, uint32_t pressed, uint64_t now_us)
{
    if (pressed & EH_ALL_BUTTONS_MASK) {
        t->released = false;
        return false;
    }
    if (!t->released) {
        t->released = true;
        t->released_since_us = now_us;
    }
    return now_us - t->released_since_us >= EH_RELEASE_HOLD_US;
}

void eh_menu_select_init(eh_menu_select *m, uint32_t pressed, uint64_t now_us)
{
    m->choice = EH_BOOT_STOCK;
    m->deadline_us = now_us + EH_MENU_TIMEOUT_US;
    m->held = pressed & EH_DPAD_MASK;
}

bool eh_menu_select_step(eh_menu_select *m, uint32_t pressed, uint64_t now_us)
{
    uint32_t down = pressed & EH_DPAD_MASK;
    uint32_t presses = down & ~m->held;
    m->held = down;
    if (presses & EH_CONFIRM_BIT)
        return true;
    /* Both sides pressed in one sample: no move, rather than guess. */
    if ((presses & (EH_STOCK_SELECT_BIT | EH_DEMO_SELECT_BIT)) == EH_STOCK_SELECT_BIT) {
        m->choice = EH_BOOT_STOCK;
        m->deadline_us = now_us + EH_MENU_TIMEOUT_US;
    } else if ((presses & (EH_STOCK_SELECT_BIT | EH_DEMO_SELECT_BIT)) == EH_DEMO_SELECT_BIT) {
        m->choice = EH_BOOT_DEMO;
        m->deadline_us = now_us + EH_MENU_TIMEOUT_US;
    }
    return now_us >= m->deadline_us;
}

uint32_t eh_menu_select_remaining_us(const eh_menu_select *m, uint64_t now_us)
{
    return now_us >= m->deadline_us ? 0 : (uint32_t)(m->deadline_us - now_us);
}
