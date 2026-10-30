#!/usr/bin/env python3
"""Tests for pin_audit.audit_sources on synthetic sources: every approved
pin form passes, and anything outside the grammar, or reaching GPIO4/5,
fails.

    python3 -I device/tools/test_pin_audit.py
"""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import pin_audit  # noqa: E402

PINS = {"EH_BADGE_BTN_A_PIN": 17, "EH_BADGE_BTN_B_PIN": 16, "EH_BADGE_LCD_D0_PIN": 11,
        "EH_BADGE_LCD_SCLK_PIN": 10, "EH_BADGE_LOW_PIN": 2, "EH_BADGE_EEPROM_SDA_PIN": 4}
CLEAN = """/* No I2C here. */
static const uint8_t button_pins[] = {EH_BADGE_BTN_A_PIN, EH_BADGE_BTN_B_PIN};
static const struct {
    uint8_t pin;
    uint32_t bit;
} button_map[] = {{EH_BADGE_BTN_A_PIN, 1u << 0}, {EH_BADGE_BTN_B_PIN, 1u << 1}};
void f(void) {
    gpio_init(EH_BADGE_BTN_A_PIN); // not i2c either
    for (unsigned i = 0; i < sizeof button_pins; i++) gpio_pull_up(button_pins[i]);
    for (unsigned i = 0; i < count_of(button_map); i++) {
        gpio_init(button_map[i].pin);
    }
    uint32_t in = gpio_get_all();
    pio_gpio_init(pio, EH_BADGE_LCD_D0_PIN);
    for (int i = 0; i < 5; i++)
        gpio_set_slew_rate(EH_BADGE_LCD_SCLK_PIN + i, GPIO_SLEW_RATE_FAST);
    hard_assert(pio_claim_free_sm_and_add_program_for_gpio_range(&p, &pio, &sm, &off, EH_BADGE_LCD_SCLK_PIN, 5, true));
    x_program_init(pio, sm, off, EH_BADGE_LCD_D0_PIN, 1.0f);
}
"""
PIO = """.program x
    out pins, 4
% c-sdk {
static inline void x_program_init(PIO pio, uint sm, uint offset, uint d0_pin, float div) {
    for (uint i = 0; i < 4; i++) pio_gpio_init(pio, d0_pin + i);
    pio_sm_set_consecutive_pindirs(pio, sm, d0_pin, 4, true);
    sm_config_set_out_pins(&c, d0_pin, 4);
}
%}
"""


class PinAuditTest(unittest.TestCase):
    def failures(self, src, pio="", pins=PINS, board=""):
        return pin_audit.audit_sources({"device/demo/x.c": src, "device/demo/x.pio": pio}, pins, board)

    def test_clean_source_passes(self):
        self.assertEqual(self.failures(CLEAN, pio=PIO), [])

    def test_rejections(self):
        self.assertEqual(self.failures("void f(void){}"), [])
        tables = "static const uint8_t button_pins[] = {4};\n"
        cases = {
            "i2c call": "void f(void){ i2c_init(i2c0, 1); }",
            "eeprom pin name": "void f(void){ gpio_init(EH_BADGE_EEPROM_SDA_PIN); }",
            "bare pin number": "void f(void){ gpio_set_function(4, 3); }",
            "computed pin": "void f(void){ gpio_init(k + 1); }",
            "pio bare pin": "void f(void){ pio_gpio_init(pio, 5); }",
            "unapproved arithmetic": "void f(void){ gpio_init(EH_BADGE_BTN_A_PIN - 13); }",
            "arithmetic outside a loop": "void f(void){ gpio_init(EH_BADGE_BTN_A_PIN + i); }",
            "loop without literal bound":
                "void f(void){ for (int i = 0; i < n; i++) gpio_init(EH_BADGE_BTN_A_PIN + i); }",
            "table holds a literal": tables + "void f(void){ gpio_init(button_pins[i]); }",
            # The initializer's i is not the call site's loop variable (here an enum = 2: GPIO4).
            "table entry with arithmetic":
                "enum { i = 2 };\nstatic const uint8_t t[] = {EH_BADGE_LOW_PIN + i};\n"
                "void f(void){ for (int i = 0; i < 2; i++) gpio_init(t[i]); }",
            "table not const": "static uint8_t t[] = {EH_BADGE_BTN_A_PIN};\nvoid f(void){ gpio_init(t[i]); }",
            "table undefined": "void f(void){ gpio_init(nowhere[i]); }",
            "struct table literal":
                "static const struct { uint8_t pin; uint32_t bit; } m[] = {{4, 1}};\n"
                "void f(void){ gpio_init(m[i].pin); }",
            "running-SM pin setter": "void f(void){ pio_sm_set_out_pins(pio, sm, EH_BADGE_LOW_PIN, 3); }",
            "adc pin": "void f(void){ adc_gpio_init(4); }",
            "mask write": "void f(void){ gpio_set_mask(1u << 4); }",
            "raw bank handle": "void f(void){ pads_bank0_hw->io[4] = 0; }",
            "macro hides a call": "#define G(p) gpio_init(p)\nvoid f(void){ G(4); }",
            "program_init without definition": "void f(void){ y_program_init(pio, sm, 0, EH_BADGE_BTN_A_PIN); }",
            "raw bank base": "void f(void){ *(volatile uint32_t *)(PADS_BANK0_BASE + 0x14) = 0; }",
            "loop variable modified in body":
                "void f(void){ for (int i = 0; i < 2; i++) { i += 7; gpio_init(EH_BADGE_LCD_SCLK_PIN + i); } }",
        }
        for name, src in cases.items():
            with self.subTest(name=name):
                self.assertNotEqual(self.failures(src), [])

    def test_pio_c_sdk_sections_audited(self):
        bare = PIO.replace("pio_sm_set_consecutive_pindirs(pio, sm, d0_pin, 4, true);", "gpio_init(4);")
        self.assertNotEqual(self.failures(CLEAN, pio=bare), [])
        helper = PIO.replace("%}", "static inline void helper(void) { gpio_init(4); }\n%}")
        self.assertNotEqual(self.failures(CLEAN, pio=helper), [])
        unbound = PIO.replace("x_program_init", "z_program_init")
        self.assertEqual(self.failures("void f(void){}"), [])
        self.assertNotEqual(self.failures("void f(void){}", pio=unbound), [])

    def test_ranges_reaching_eeprom_rejected(self):
        # Each pair: (passes with count/bound 2 -> GPIO2..3, fails with 3 -> GPIO2..4).
        forms = [
            "void f(void){{ for (int i = 0; i < {n}; i++) gpio_init(EH_BADGE_LOW_PIN + i); }}",
            "void f(void){{ pio_sm_set_consecutive_pindirs(pio, sm, EH_BADGE_LOW_PIN, {n}, true); }}",
        ]
        for form in forms:
            with self.subTest(form=form):
                self.assertEqual(self.failures(form.format(n=2)), [])
                self.assertNotEqual(self.failures(form.format(n=3)), [])

    def test_c_sdk_parameter_bound_to_caller_range(self):
        call = "void f(void){ x_program_init(pio, sm, off, EH_BADGE_LCD_D0_PIN, 1.0f); }"
        self.assertEqual(self.failures(call, pio=PIO), [])
        low = dict(PINS, EH_BADGE_LCD_D0_PIN=2)  # D0..D3 = GPIO2..5
        self.assertNotEqual(self.failures(call, pio=PIO, pins=low), [])

    def test_board_default_i2c_rejected(self):
        self.assertTrue(self.failures(CLEAN, pio=PIO, board="#define PICO_DEFAULT_I2C 0\n"))


if __name__ == "__main__":
    unittest.main()
