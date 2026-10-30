/* NEGATIVE WITNESS, never flash: writes to the EEPROM address over I2C0
 * (i2c-symbols, i2c-literal). */
#include "hardware/i2c.h"

static uint8_t buf[2];

int main(void)
{
    i2c_init(i2c0, 400000);
    i2c_write_blocking(i2c0, 0x50, buf, sizeof buf, false);
    for (;;)
        ;
}
