/* Dual-boot menu: the image at flash offset 0.
 *
 * Its block loop carries an IMAGE_DEF and an embedded PARTITION_TABLE, so
 * the bootrom loads the table and enters this image directly
 * ("partition-table-in-image boot", RP2350 datasheet 5.1.14). The menu then
 * picks an image and chains its partition with the bootrom's chain_image
 * (5.4.8.2), which maps that partition to 0x10000000 (5.1.19) so stock runs
 * unmodified from its relocated copy.
 *
 * RIGHT held through power-on starts the demo at once, as before. Otherwise
 * the panel shows Wayfarer and the torus side by side for a three-second
 * countdown (eh_menu_draw.h; rules in eh_boot_select.h), and the LEDs take
 * the chosen side's colour. Stock starts only after every key has been
 * released for 100 ms.
 *
 * Touches the seven button GPIOs, the panel (GPIO7, 9-15) and the LEDs
 * (GPIO24). Before chaining it blanks the LEDs, resets the panel, disconnects
 * those pins and holds PIO, DMA and PWM in reset (screen_hand_back). No I2C, no USB, no flash writes; see
 * device/dualboot/README.org. */
#include <stdbool.h>
#include <stdint.h>

#include "boot/picobin.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/resets.h"
#include "hardware/sync.h"
#include "pico/bootrom.h"
#include "pico/time.h"

#include "eh/eh_types.h"
#include "eh_boot_select.h"
#include "eh_lcd.h"
#include "eh_menu_draw.h"
#include "eh_ws2812.pio.h"

/* Partition indices in device/dualboot/partitions.json. */
#define EH_PARTITION_STOCK 0u
#define EH_PARTITION_DEMO  1u

#define EH_MENU_BACKLIGHT_PERCENT 80

/* chain_image needs 3,064 B (datasheet) to 3,264 B (SDK comment); it must not
 * overlap the chained image, which runs from flash. */
static uint32_t chain_workarea[4096 / sizeof(uint32_t)];

static uint16_t fb[EH_FB_PIXELS];
static bool screen_up;
static PIO led_pio;
static uint led_sm, led_offset;

static const uint8_t button_pins[] = {
    EH_BADGE_BTN_B_PIN,     EH_BADGE_BTN_A_PIN,    EH_BADGE_BTN_RIGHT_PIN, EH_BADGE_BTN_CENTRE_PIN,
    EH_BADGE_BTN_DOWN_PIN,  EH_BADGE_BTN_LEFT_PIN, EH_BADGE_BTN_UP_PIN,
};

static uint32_t buttons_pressed(void)
{
    return ~gpio_get_all() & EH_ALL_BUTTONS_MASK;
}

/* Returns only on failure (a negative bootrom error, or -1 for a malformed
 * partition entry). */
static int chain_partition(uint32_t index)
{
    uint32_t info[3];
    int words = rom_get_partition_table_info(
        info, 3, PT_INFO_PARTITION_LOCATION_AND_FLAGS | PT_INFO_SINGLE_PARTITION | (index << 24));
    if (words < 0)
        return words;
    if (words != 3)
        return -1;
    uint32_t first = (info[1] & PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_BITS) >>
                     PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_LSB;
    uint32_t last = (info[1] & PICOBIN_PARTITION_LOCATION_LAST_SECTOR_BITS) >>
                    PICOBIN_PARTITION_LOCATION_LAST_SECTOR_LSB;
    if (first == 0 || last < first)
        return -1; /* sector 0 is this menu */
    uint32_t start = first * 4096u, size = (last + 1u - first) * 4096u;
    return rom_chain_image((uint8_t *)chain_workarea, sizeof chain_workarea, XIP_BASE + start, size);
}

/* All nine LEDs one colour (8-bit RGB), GRB on the wire. */
static void leds_fill(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < EH_LED_COUNT; i++)
        pio_sm_put_blocking(led_pio, led_sm, (uint32_t)g << 24 | (uint32_t)r << 16 | (uint32_t)b << 8);
}

static void leds_show_choice(eh_boot_choice choice)
{
    if (choice == EH_BOOT_DEMO)
        leds_fill(64, 20, 6); /* the torus's orange, kept dim */
    else
        leds_fill(40, 28, 6); /* gold */
}

static void screen_show(eh_boot_choice choice, uint32_t remaining_us, bool release)
{
    eh_lcd_wait();
    eh_menu_draw(fb, choice, remaining_us, release);
    eh_lcd_present(fb);
    leds_show_choice(choice);
}

static void screen_bring_up(eh_boot_choice choice, uint32_t remaining_us)
{
    uint offset;
    hard_assert(pio_claim_free_sm_and_add_program_for_gpio_range(&eh_ws2812_program, &led_pio, &led_sm,
                                                                 &offset, EH_BADGE_RGB_LED_PIN, 1, true));
    led_offset = offset;
    eh_ws2812_program_init(led_pio, led_sm, offset, EH_BADGE_RGB_LED_PIN, 800000.0f);
    eh_lcd_init();
    screen_show(choice, remaining_us, false);
    eh_lcd_wait();
    eh_lcd_backlight(EH_MENU_BACKLIGHT_PERCENT);
    screen_up = true;
}

/* Undo what the screen set up, as far as it matters to the next image:
 * WS2812s keep their last colour until rewritten, so they are blanked first;
 * the panel is reset and its pins disconnected (eh_lcd_release); PIO, DMA
 * and PWM are held in reset. Not a full pad reset (that bank also holds the
 * EEPROM pins); the next SDK image's runtime_init resets IO and pads itself. */
static void screen_hand_back(void)
{
    if (!screen_up)
        return;
    leds_fill(0, 0, 0);
    while (!pio_sm_is_tx_fifo_empty(led_pio, led_sm))
        tight_loop_contents();
    busy_wait_us_32(400); /* last 9 x 30 us frame out, then the >280 us latch */
    pio_remove_program_and_unclaim_sm(&eh_ws2812_program, led_pio, led_sm, led_offset);
    gpio_deinit(EH_BADGE_RGB_LED_PIN);
    gpio_set_pulls(EH_BADGE_RGB_LED_PIN, false, true);
    eh_lcd_release();
    reset_block_mask(RESETS_RESET_PIO0_BITS | RESETS_RESET_PIO1_BITS | RESETS_RESET_PIO2_BITS |
                     RESETS_RESET_DMA_BITS | RESETS_RESET_PWM_BITS);
    screen_up = false;
}

/* Stock latches B during start-up; never hand over to it with any key down.
 * A stuck key keeps the badge here, which is the safe failure. */
static void wait_for_release(void)
{
    eh_release_tracker release;
    eh_release_init(&release);
    bool prompted = false;
    while (!eh_release_step(&release, buttons_pressed(), time_us_64())) {
        if (screen_up && !prompted && buttons_pressed()) {
            screen_show(EH_BOOT_STOCK, 0, true);
            prompted = true;
        }
        busy_wait_us_32(1000);
    }
}

int main(void)
{
    for (unsigned i = 0; i < sizeof button_pins; i++) {
        gpio_init(button_pins[i]);
        gpio_pull_up(button_pins[i]);
    }
    busy_wait_us_32(1000); /* let the pull-ups settle */

    uint32_t samples[EH_SELECT_SAMPLES];
    for (unsigned i = 0; i < EH_SELECT_SAMPLES; i++) {
        samples[i] = buttons_pressed() & EH_DPAD_MASK;
        busy_wait_us_32(EH_SELECT_INTERVAL_US);
    }
    eh_boot_choice choice = eh_boot_choose(samples, EH_SELECT_SAMPLES);

    if (choice == EH_BOOT_STOCK) {
        screen_bring_up(EH_BOOT_STOCK, EH_MENU_TIMEOUT_US);
        /* The countdown starts once the screen is lit, not during panel init. */
        eh_menu_select m;
        eh_menu_select_init(&m, buttons_pressed(), time_us_64());
        eh_boot_choice shown = m.choice;
        while (!eh_menu_select_step(&m, buttons_pressed(), time_us_64())) {
            uint32_t remaining = eh_menu_select_remaining_us(&m, time_us_64());
            eh_lcd_wait();
            if (m.choice != shown) {
                eh_menu_draw(fb, m.choice, remaining, false);
                leds_show_choice(m.choice);
                shown = m.choice;
            } else {
                eh_menu_draw_countdown(fb, m.choice, remaining);
            }
            eh_lcd_present(fb);
        }
        choice = m.choice;
    }

    if (choice == EH_BOOT_DEMO) {
        screen_hand_back();
        chain_partition(EH_PARTITION_DEMO); /* returns only if the demo slot is unbootable */
    }
    wait_for_release();
    screen_hand_back();
    chain_partition(EH_PARTITION_STOCK);

    for (;;)
        __wfi();
}
