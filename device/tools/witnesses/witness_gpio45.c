/* NEGATIVE WITNESS, never flash: writes GPIO5's pad register through a
 * stored address (gpio45). The pointer is a non-const global so the full
 * register address exists as a word in the image. Code that reaches the
 * register as base + offset leaves no such word; image_audit cannot see
 * that, and pin_audit.py covers it at source level. */
#include <stdint.h>

volatile uint32_t *pads_gpio5 = (volatile uint32_t *)0x40038018u;

int main(void)
{
    *pads_gpio5 = 0;
    for (;;)
        ;
}
