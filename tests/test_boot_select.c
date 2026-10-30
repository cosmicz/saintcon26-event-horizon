/* Dual-boot menu selection rules (device/menu/eh_boot_select.h). */
#include "eh_boot_select.h"
#include "eh_test.h"

#define RIGHT EH_DEMO_SELECT_BIT
#define BIT(pin) EH_BTN_BIT(pin)

void test_boot_choose_dpad_only(void)
{
    uint32_t s[EH_SELECT_SAMPLES];

    for (unsigned i = 0; i < EH_SELECT_SAMPLES; i++)
        s[i] = 0;
    EH_CHECK(eh_boot_choose(s, EH_SELECT_SAMPLES) == EH_BOOT_STOCK);

    for (unsigned i = 0; i < EH_SELECT_SAMPLES; i++)
        s[i] = RIGHT;
    EH_CHECK(eh_boot_choose(s, EH_SELECT_SAMPLES) == EH_BOOT_DEMO);

    /* A or B, alone or with other D-pad keys, never selects the demo. */
    for (unsigned i = 0; i < EH_SELECT_SAMPLES; i++)
        s[i] = BIT(EH_BADGE_BTN_A_PIN) | BIT(EH_BADGE_BTN_B_PIN) | BIT(EH_BADGE_BTN_UP_PIN);
    EH_CHECK(eh_boot_choose(s, EH_SELECT_SAMPLES) == EH_BOOT_STOCK);

    /* A/B and foreign pins do not veto RIGHT either: only RIGHT is read. */
    for (unsigned i = 0; i < EH_SELECT_SAMPLES; i++)
        s[i] = RIGHT | BIT(EH_BADGE_BTN_A_PIN) | BIT(EH_BADGE_EEPROM_SDA_PIN);
    EH_CHECK(eh_boot_choose(s, EH_SELECT_SAMPLES) == EH_BOOT_DEMO);

    /* One bounce anywhere in the window falls back to stock. */
    for (unsigned k = 0; k < EH_SELECT_SAMPLES; k++) {
        for (unsigned i = 0; i < EH_SELECT_SAMPLES; i++)
            s[i] = i == k ? 0 : RIGHT;
        EH_CHECK_MSG(eh_boot_choose(s, EH_SELECT_SAMPLES) == EH_BOOT_STOCK, "bounce at %u", k);
    }

    /* A foreign bit at RIGHT's neighbour must not count as RIGHT. */
    for (unsigned i = 0; i < EH_SELECT_SAMPLES; i++)
        s[i] = BIT(EH_BADGE_BTN_RIGHT_PIN - 1) | BIT(EH_BADGE_BTN_RIGHT_PIN + 1);
    EH_CHECK(eh_boot_choose(s, EH_SELECT_SAMPLES) == EH_BOOT_STOCK);

    EH_CHECK(eh_boot_choose(s, 0) == EH_BOOT_STOCK);
}

void test_release_wait_all_buttons(void)
{
    static const unsigned pins[] = {
        EH_BADGE_BTN_B_PIN,     EH_BADGE_BTN_A_PIN,   EH_BADGE_BTN_RIGHT_PIN, EH_BADGE_BTN_CENTRE_PIN,
        EH_BADGE_BTN_DOWN_PIN,  EH_BADGE_BTN_LEFT_PIN, EH_BADGE_BTN_UP_PIN,
    };
    eh_release_tracker t;

    EH_CHECK(EH_ALL_BUTTONS_MASK == 0x00fb0000u); /* GPIO16, 17, 19-23 */

    /* Released from the start: done exactly at the hold time, not before. */
    eh_release_init(&t);
    EH_CHECK(!eh_release_step(&t, 0, 1000));
    EH_CHECK(!eh_release_step(&t, 0, 1000 + EH_RELEASE_HOLD_US - 1));
    EH_CHECK(eh_release_step(&t, 0, 1000 + EH_RELEASE_HOLD_US));

    /* Each button, held, blocks; a press late in the interval restarts it. */
    for (unsigned i = 0; i < sizeof pins / sizeof pins[0]; i++) {
        uint32_t b = BIT(pins[i]);
        eh_release_init(&t);
        EH_CHECK_MSG(!eh_release_step(&t, b, 0), "GPIO%u", pins[i]);
        EH_CHECK_MSG(!eh_release_step(&t, b, 10 * EH_RELEASE_HOLD_US), "GPIO%u", pins[i]);
        EH_CHECK(!eh_release_step(&t, 0, 10 * EH_RELEASE_HOLD_US + 1));
        EH_CHECK(!eh_release_step(&t, b, 10 * EH_RELEASE_HOLD_US + EH_RELEASE_HOLD_US - 10));
        EH_CHECK(!eh_release_step(&t, 0, 10 * EH_RELEASE_HOLD_US + EH_RELEASE_HOLD_US));
        EH_CHECK_MSG(!eh_release_step(&t, 0, 12 * EH_RELEASE_HOLD_US - 1), "GPIO%u restart", pins[i]);
        EH_CHECK_MSG(eh_release_step(&t, 0, 12 * EH_RELEASE_HOLD_US), "GPIO%u", pins[i]);
    }

    /* Pins that are not buttons (EEPROM bus, LCD) do not hold the menu. */
    eh_release_init(&t);
    uint32_t foreign = BIT(EH_BADGE_EEPROM_SDA_PIN) | BIT(EH_BADGE_EEPROM_SCL_PIN) |
                       BIT(EH_BADGE_BTN_RIGHT_PIN - 1) | BIT(EH_BADGE_LCD_CS_PIN);
    EH_CHECK(!eh_release_step(&t, foreign, 0));
    EH_CHECK(eh_release_step(&t, foreign, EH_RELEASE_HOLD_US));
}
