/* Toolchain smoke image: counts forever. It touches no peripheral beyond
 * what the SDK runtime sets up (clocks), so it is safe to inspect but is
 * not meant to be flashed. */
#include "pico/stdlib.h"

volatile uint32_t eh_smoke_ticks;

int main(void)
{
    for (;;) {
        eh_smoke_ticks++;
        busy_wait_us_32(1000);
    }
}
