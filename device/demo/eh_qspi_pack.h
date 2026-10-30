/* Single-line (D0-only) QSPI phases on the 4-bit PIO bus: each bit of a byte
 * becomes one nibble with the bit in nibble bit 0, sent MSB first. The PIO
 * shifts 16-bit FIFO writes out MSB first, so one byte is two halfwords.
 * Kept free of SDK calls so the host suite tests it. */
#ifndef EH_QSPI_PACK_H
#define EH_QSPI_PACK_H

#include <stdint.h>

static inline void eh_qspi_single_line(uint8_t b, uint16_t out[2])
{
    for (int half = 0; half < 2; half++) {
        uint16_t v = 0;
        for (int k = 0; k < 4; k++)
            v = (uint16_t)(v << 4 | ((b >> (7 - 4 * half - k)) & 1u));
        out[half] = v;
    }
}

#endif
