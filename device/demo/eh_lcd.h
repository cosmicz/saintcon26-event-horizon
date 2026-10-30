/* ST77916 360x360 panel on a PIO QSPI bus; full frames streamed by DMA
 * straight from the renderer's native-endian RGB565 frame buffer. */
#ifndef EH_DEVICE_LCD_H
#define EH_DEVICE_LCD_H

#include <stdint.h>

/* Resets and initialises the panel (stock register values, see
 * device/tools/extract_panel_init.py), clears it, backlight off. */
void eh_lcd_init(void);

/* Starts sending fb (EH_FB_W x EH_FB_H) and returns; fb must stay unchanged
 * until eh_lcd_wait() returns. */
void eh_lcd_present(const uint16_t *fb);
void eh_lcd_wait(void);

void eh_lcd_backlight(uint8_t percent); /* 0..100 */

/* For a program that hands over to another image (the dual-boot menu):
 * backlight off, display off and asleep, a hardware reset, then the DMA
 * channel, state machine and PWM slice stopped, and the pins disconnected
 * (function NULL) with pull-downs and the default slow slew that init
 * changed. This is not a full pad reset: gpio_set_function leaves pad
 * isolation cleared, unlike power-on. The next SDK image's runtime_init
 * resets the IO and pad banks anyway. The PIO program stays loaded:
 * the caller resets the PIO block afterwards. Built only with
 * EH_LCD_RELEASE defined (the menu). */
void eh_lcd_release(void);

#endif
