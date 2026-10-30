/* EVENT HORIZON on the badge: the portable core (src/core) driven by the
 * two-core frame plan of eh_demo.h. Core 0 prepares, renders the even parts
 * and finishes; core 1 renders the odd parts; the SIO FIFO is the per-pass
 * barrier. Touches the panel (GPIO7, 9-15), the RGB LEDs (GPIO24) and reads
 * the buttons; never I2C, GPIO4/5 or flash writes. */
#include <stdint.h>

#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/time.h"

#include "eh/eh_demo.h"
#include "eh_badge_pins.h"
#include "eh_lcd.h"
#include "eh_ws2812.pio.h"

#define EH_BACKLIGHT_PERCENT 80

static eh_rgb565 fb[EH_FB_PIXELS];
static eh_demo demo;
static PIO led_pio;
static uint led_sm;

static const struct {
    uint8_t pin;
    uint32_t bit;
} button_map[] = {
    {EH_BADGE_BTN_UP_PIN, EH_BTN_UP},       {EH_BADGE_BTN_DOWN_PIN, EH_BTN_DOWN},
    {EH_BADGE_BTN_LEFT_PIN, EH_BTN_LEFT},   {EH_BADGE_BTN_RIGHT_PIN, EH_BTN_RIGHT},
    {EH_BADGE_BTN_CENTRE_PIN, EH_BTN_CENTER}, {EH_BADGE_BTN_A_PIN, EH_BTN_A},
    {EH_BADGE_BTN_B_PIN, EH_BTN_B},
};

static uint32_t read_buttons(void)
{
    uint32_t in = gpio_get_all(), out = 0;
    for (unsigned i = 0; i < count_of(button_map); i++)
        if (!(in & (1u << button_map[i].pin)))
            out |= button_map[i].bit;
    return out;
}

static void run_parts(int pass, int worker)
{
    for (int part = worker; part < EH_PARTS; part += EH_WORKERS)
        eh_demo_render_part(&demo, fb, pass, part, worker);
}

static void core1_main(void)
{
    for (;;) {
        int pass = (int)multicore_fifo_pop_blocking();
        __dmb();
        run_parts(pass, 1);
        __dmb();
        multicore_fifo_push_blocking((uint32_t)pass);
    }
}

static void leds_init(void)
{
    uint offset;
    hard_assert(pio_claim_free_sm_and_add_program_for_gpio_range(&eh_ws2812_program, &led_pio, &led_sm,
                                                                 &offset, EH_BADGE_RGB_LED_PIN, 1, true));
    eh_ws2812_program_init(led_pio, led_sm, offset, EH_BADGE_RGB_LED_PIN, 800000.0f);
}

static void leds_show(const eh_rgb *leds)
{
    for (int i = 0; i < EH_LED_COUNT; i++)
        pio_sm_put_blocking(led_pio, led_sm,
                            (uint32_t)leds[i].g << 24 | (uint32_t)leds[i].r << 16 | (uint32_t)leds[i].b << 8);
}

int main(void)
{
    for (unsigned i = 0; i < count_of(button_map); i++) {
        gpio_init(button_map[i].pin);
        gpio_pull_up(button_map[i].pin);
    }
    leds_init();
    eh_lcd_init();
    eh_demo_init(&demo);
    multicore_launch_core1(core1_main);

    uint64_t t0 = time_us_64();
    for (bool first = true;; first = false) {
        uint32_t t_ms = (uint32_t)((time_us_64() - t0) / 1000u);
        eh_lcd_wait(); /* the previous frame has left the buffer */
        eh_demo_prepare(&demo, t_ms, read_buttons(), fb);
        for (int pass = 0, n = eh_demo_passes(&demo); pass < n; pass++) {
            __dmb();
            multicore_fifo_push_blocking((uint32_t)pass);
            run_parts(pass, 0);
            multicore_fifo_pop_blocking();
            __dmb();
        }
        eh_demo_finish(&demo, fb);
        eh_lcd_present(fb);
        leds_show(demo.leds);
        if (first) {
            eh_lcd_wait();
            eh_lcd_backlight(EH_BACKLIGHT_PERCENT);
        }
    }
}
