// SPDX-License-Identifier: MIT
// Derived from display.c, Copyright (c) 2026 redactd (saintcon26-gif-player
// 9726dad5); see THIRD_PARTY_NOTICES.org. Changed: no private frame buffer
// (the renderer's buffer is streamed as 16-bit DMA, MSB first, so no
// big-endian copy is needed), FIFO writes are 16-bit, the init table is the
// one extracted from our own stock backup, and TE (GPIO8) is left alone.
#include "eh_lcd.h"

#include <stdbool.h>
#include <stddef.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "pico/time.h"

#include "eh/eh_types.h"
#include "eh_badge_pins.h"
#include "eh_lcd_qspi.pio.h"
#include "eh_qspi_pack.h"
#include "eh_st77916_stock_init.h" /* generated, private: never committed */

/* QSPI opcodes, sent on D0 and followed by a 24-bit address 0x00 cmd 0x00. */
#define QSPI_OP_WRITE_CMD    0x02 /* command and parameters, single line */
#define QSPI_OP_WRITE_PIXELS 0x32 /* command single line, pixels on four */

/* SCLK = clk_sys / (2 * div): 75 MHz at 150 MHz, the rate the gif-player's
 * author reports stock using for this panel. */
#define EH_LCD_PIO_CLKDIV 1.0f

static PIO pio;
static uint sm;
static int dma_chan;
static bool frame_in_flight;
static volatile uint16_t *txf16; /* 16-bit view of the state machine's TX FIFO */

static inline void fifo_put16(uint16_t v)
{
    while (pio_sm_is_tx_fifo_full(pio, sm))
        tight_loop_contents();
    *txf16 = v; /* a 16-bit write is replicated into bits 31:16 */
}

static void send_single_line(uint8_t b)
{
    uint16_t hw[2];
    eh_qspi_single_line(b, hw);
    fifo_put16(hw[0]);
    fifo_put16(hw[1]);
}

static void wait_bus_idle(void)
{
    uint32_t stall = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
    pio->fdebug = stall; /* clear, then wait for the SM to stall on an empty FIFO */
    while (!pio_sm_is_tx_fifo_empty(pio, sm) || !(pio->fdebug & stall))
        tight_loop_contents();
}

static inline void cs(bool active)
{
    gpio_put(EH_BADGE_LCD_CS_PIN, !active);
    if (!active)
        busy_wait_at_least_cycles(16); /* CS-high time >= 107 ns at 150 MHz */
}

static void write_command(uint8_t cmd, const uint8_t *data, size_t len)
{
    cs(true);
    send_single_line(QSPI_OP_WRITE_CMD);
    send_single_line(0x00);
    send_single_line(cmd);
    send_single_line(0x00);
    for (size_t i = 0; i < len; i++)
        send_single_line(data[i]);
    wait_bus_idle();
    cs(false);
}

static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    const uint8_t caset[] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
    const uint8_t raset[] = {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1};
    write_command(0x2a, caset, sizeof caset);
    write_command(0x2b, raset, sizeof raset);
}

void eh_lcd_wait(void)
{
    if (!frame_in_flight)
        return;
    dma_channel_wait_for_finish_blocking(dma_chan);
    wait_bus_idle();
    cs(false);
    frame_in_flight = false;
}

void eh_lcd_present(const uint16_t *fb)
{
    eh_lcd_wait();
    set_window(0, 0, EH_FB_W - 1, EH_FB_H - 1);
    cs(true);
    send_single_line(QSPI_OP_WRITE_PIXELS);
    send_single_line(0x00);
    send_single_line(0x2c); /* RAMWR */
    send_single_line(0x00);
    frame_in_flight = true;
    dma_channel_set_read_addr(dma_chan, fb, false);
    dma_channel_set_trans_count(dma_chan, EH_FB_PIXELS, true);
}

void eh_lcd_backlight(uint8_t percent)
{
    if (percent > 100)
        percent = 100;
    pwm_set_gpio_level(EH_BADGE_LCD_BACKLIGHT_PIN, (uint16_t)(percent * 10)); /* wrap 999 */
}

void eh_lcd_init(void)
{
    gpio_init(EH_BADGE_LCD_CS_PIN);
    gpio_put(EH_BADGE_LCD_CS_PIN, 1);
    gpio_set_dir(EH_BADGE_LCD_CS_PIN, GPIO_OUT);
    gpio_set_slew_rate(EH_BADGE_LCD_CS_PIN, GPIO_SLEW_RATE_FAST);
    gpio_init(EH_BADGE_LCD_RESET_PIN);
    gpio_set_dir(EH_BADGE_LCD_RESET_PIN, GPIO_OUT);

    /* Backlight PWM at ~20 kHz, off until the first frame is on the glass. */
    gpio_set_function(EH_BADGE_LCD_BACKLIGHT_PIN, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(EH_BADGE_LCD_BACKLIGHT_PIN);
    pwm_config pc = pwm_get_default_config();
    pwm_config_set_wrap(&pc, 999);
    pwm_config_set_clkdiv(&pc, (float)clock_get_hz(clk_sys) / (1000.0f * 20000.0f));
    pwm_init(slice, &pc, true);
    eh_lcd_backlight(0);

    uint offset;
    hard_assert(pio_claim_free_sm_and_add_program_for_gpio_range(&eh_lcd_qspi_program, &pio, &sm, &offset,
                                                                 EH_BADGE_LCD_SCLK_PIN, 5, true));
    eh_lcd_qspi_program_init(pio, sm, offset, EH_BADGE_LCD_D0_PIN, EH_BADGE_LCD_SCLK_PIN, EH_LCD_PIO_CLKDIV);
    txf16 = (volatile uint16_t *)(uintptr_t)&pio->txf[sm];
    for (int i = 0; i < 5; i++)
        gpio_set_slew_rate(EH_BADGE_LCD_SCLK_PIN + i, GPIO_SLEW_RATE_FAST);

    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_16);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pio_get_dreq(pio, sm, true));
    dma_channel_configure(dma_chan, &dc, &pio->txf[sm], NULL, 0, false);

    /* Hardware reset, 100 ms each way. */
    gpio_put(EH_BADGE_LCD_RESET_PIN, 0);
    sleep_ms(100);
    gpio_put(EH_BADGE_LCD_RESET_PIN, 1);
    sleep_ms(100);

    for (size_t i = 0; i < count_of(eh_st77916_stock_init); i++) {
        const eh_st77916_cmd *c = &eh_st77916_stock_init[i];
        write_command(c->cmd, c->data, c->len);
        if (c->delay_ms)
            sleep_ms(c->delay_ms);
    }
}

/* Only the menu hands over to another image; leaving this out of the demo's
 * build keeps the demo's bytes identical to its reviewed image. */
#ifdef EH_LCD_RELEASE
void eh_lcd_release(void)
{
    eh_lcd_wait();
    eh_lcd_backlight(0);
    write_command(0x28, NULL, 0); /* DISPOFF */
    write_command(0x10, NULL, 0); /* SLPIN */
    sleep_ms(5);
    gpio_put(EH_BADGE_LCD_RESET_PIN, 0);
    sleep_ms(20); /* RESX low: well beyond the panel's reset pulse minimum */

    dma_channel_abort(dma_chan);
    dma_channel_unclaim(dma_chan);
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_unclaim(pio, sm); /* the caller resets the PIO block, program included */
    pwm_set_enabled(pwm_gpio_to_slice_num(EH_BADGE_LCD_BACKLIGHT_PIN), false);

    gpio_deinit(EH_BADGE_LCD_BACKLIGHT_PIN);
    gpio_set_pulls(EH_BADGE_LCD_BACKLIGHT_PIN, false, true);
    gpio_deinit(EH_BADGE_LCD_RESET_PIN);
    gpio_set_pulls(EH_BADGE_LCD_RESET_PIN, false, true);
    for (int i = 0; i < 6; i++) { /* SCLK, D0..D3 and CS: GPIO10..15 */
        gpio_deinit(EH_BADGE_LCD_SCLK_PIN + i);
        gpio_set_pulls(EH_BADGE_LCD_SCLK_PIN + i, false, true);
        gpio_set_slew_rate(EH_BADGE_LCD_SCLK_PIN + i, GPIO_SLEW_RATE_SLOW); /* init set these fast */
    }
}
#endif
