#!/usr/bin/env python3
"""Tests for extract_panel_init.py's dump gate on synthetic images (no
firmware needed; the extraction itself needs a real backup and runs in
device/dualboot/check.sh).

    python3 -I device/tools/test_extract_panel_init.py
"""

import hashlib
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import extract_panel_init as ext  # noqa: E402


def fake_dump():
    dump = bytearray(b"\xff" * ext.FLASH_SIZE)
    dump[:ext.STOCK_SLOT] = (bytes(range(256)) * (ext.STOCK_SLOT // 256 + 1))[:ext.STOCK_SLOT]
    return dump


class DumpGateTest(unittest.TestCase):
    def setUp(self):
        self.dump = fake_dump()
        self.saved = ext.STOCK_SLOT_SHA256
        ext.STOCK_SLOT_SHA256 = hashlib.sha256(self.dump[:ext.STOCK_SLOT]).hexdigest()

    def tearDown(self):
        ext.STOCK_SLOT_SHA256 = self.saved

    def test_accepts_matching_stock_slot(self):
        ext.check_dump(bytes(self.dump))

    def test_ignores_flash_past_the_stock_slot(self):
        # The rest of flash is the packer's rule (it must be erased); the
        # extractor pins only the firmware it reads.
        self.dump[ext.STOCK_SLOT] = 0x00
        self.dump[-1] = 0x5A
        ext.check_dump(bytes(self.dump))

    def test_rejects_a_changed_stock_slot(self):
        for at in (0, ext.STOCK_SLOT - 1):
            with self.subTest(at=at):
                d = bytearray(self.dump)
                d[at] ^= 1
                with self.assertRaises(ext.ExtractError):
                    ext.check_dump(bytes(d))

    def test_rejects_a_partial_dump(self):
        with self.assertRaises(ext.ExtractError):
            ext.check_dump(bytes(self.dump[:ext.FLASH_SIZE // 2]))


if __name__ == "__main__":
    unittest.main()
