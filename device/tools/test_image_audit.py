#!/usr/bin/env python3
"""Tests for image_audit.audit_image on synthetic images: every check fails
on its own trigger, and the audit fails closed on a bad constants header.

    python3 -I device/tools/test_image_audit.py
"""

import pathlib
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import image_audit as audit  # noqa: E402
from thumb_scan import rom_codes  # noqa: E402

CODES = [("ROM_FUNC_FLASH_RANGE_ERASE", ord("R") | ord("E") << 8),
         ("ROM_FUNC_FLASH_RANGE_PROGRAM", ord("R") | ord("P") << 8),
         ("ROM_FUNC_FLASH_OP", ord("F") | ord("O") << 8),
         ("ROM_FUNC_CHAIN_IMAGE", ord("C") | ord("I") << 8)]


def movw(rd, imm16):
    """Thumb-2 MOVW (T3) encoding."""
    hw1 = 0xF240 | ((imm16 >> 11) & 1) << 10 | imm16 >> 12
    hw2 = ((imm16 >> 8) & 7) << 12 | rd << 8 | (imm16 & 0xFF)
    return struct.pack("<HH", hw1, hw2)


def image(*chunks):
    body = b"".join(chunks)
    body += b"\x00" * (-len(body) % 4)
    return b"\x00" * 64 + body + b"\x00" * 64


class AuditTest(unittest.TestCase):
    def failures(self, img, symbols=()):
        return audit.audit_image(img, list(symbols), CODES)[0]

    def test_clean_image_passes(self):
        self.assertEqual(self.failures(image(movw(0, CODES[3][1])), ["main", "gpio_init"]), {})

    def test_each_check_fails_on_its_own_trigger(self):
        cases = [
            ("i2c-symbols", image(), ["i2c_write_blocking"]),
            ("i2c-literal", image(struct.pack("<I", 0x40091000)), []),  # I2C0 XOR alias
            ("flash-rom", image(movw(3, CODES[0][1])), []),
            ("gpio45", image(struct.pack("<I", 0x40038018)), []),  # PADS GPIO5
            ("gpio45", image(struct.pack("<I", 0x4002B024)), []),  # GPIO4_CTRL CLR alias
        ]
        for check, img, symbols in cases:
            with self.subTest(check=check, image=img[64:68].hex()):
                self.assertEqual(set(self.failures(img, symbols)), {check})

    def test_every_forbidden_rom_code_detected(self):
        for name, code in CODES[:3]:
            with self.subTest(name=name):
                self.assertEqual(self.failures(image(movw(5, code)))["flash-rom"], [name])

    def test_fails_closed_without_forbidden_codes(self):
        with self.assertRaises(audit.AuditConfigError):
            audit.audit_image(image(movw(0, CODES[0][1])), [], [])
        with self.assertRaises(audit.AuditConfigError):
            audit.audit_image(image(), [], CODES[1:])

    def test_empty_header_parses_to_refusal(self):
        with tempfile.NamedTemporaryFile("w", suffix=".h") as f:
            f.write("/* empty */\n")
            f.flush()
            with self.assertRaises(audit.AuditConfigError):
                audit.checked_codes(rom_codes(f.name))


if __name__ == "__main__":
    unittest.main()
