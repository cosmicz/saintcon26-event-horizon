/* QSPI single-line packing for the panel driver (device/demo/eh_qspi_pack.h). */
#include "eh_qspi_pack.h"
#include "eh_test.h"

/* What D0 carries, clock by clock, for the nibble stream of halfwords sent
 * MSB first: bit 0 of each successive nibble. */
static uint8_t d0_byte(const uint16_t hw[2])
{
    uint8_t out = 0;
    for (int h = 0; h < 2; h++)
        for (int n = 3; n >= 0; n--)
            out = (uint8_t)(out << 1 | ((hw[h] >> (4 * n)) & 1u));
    return out;
}

void test_qspi_single_line_pack(void)
{
    uint16_t hw[2];

    for (unsigned b = 0; b < 256; b++) {
        eh_qspi_single_line((uint8_t)b, hw);
        EH_CHECK_MSG(d0_byte(hw) == b, "byte 0x%02x", b);
        /* D1..D3 stay low: only nibble bit 0 may be set. */
        EH_CHECK_MSG(((hw[0] | hw[1]) & 0xEEEEu) == 0, "byte 0x%02x", b);
    }

    /* Concrete wire form, equal to the gif-player's byte-wise expansion
     * (nibble pairs 0x10, 0x01, ...) taken two FIFO bytes at a time. */
    eh_qspi_single_line(0x02, hw); /* QSPI write-command opcode */
    EH_CHECK(hw[0] == 0x0000 && hw[1] == 0x0010);
    eh_qspi_single_line(0x32, hw); /* write-pixels opcode */
    EH_CHECK(hw[0] == 0x0011 && hw[1] == 0x0010);
    eh_qspi_single_line(0xA5, hw);
    EH_CHECK(hw[0] == 0x1010 && hw[1] == 0x0101);
}
