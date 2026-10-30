/* NEGATIVE WITNESS, never flash: erases and programs flash (flash-rom). */
#include "hardware/flash.h"

static uint8_t buf[FLASH_PAGE_SIZE];

int main(void)
{
    flash_range_erase(0x00FFF000u, FLASH_SECTOR_SIZE);
    flash_range_program(0x00FFF000u, buf, sizeof buf);
    for (;;)
        ;
}
