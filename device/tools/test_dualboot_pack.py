#!/usr/bin/env python3
"""Tests for dualboot_pack.py on synthetic images (no firmware needed).

    python3 -I device/tools/test_dualboot_pack.py
"""

import hashlib
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import dualboot_pack as pack  # noqa: E402

PARTITIONS = pathlib.Path(__file__).resolve().parent.parent / "dualboot" / "partitions.json"
MIB = 1 << 20


def fake_dump(program_len=0x176AB4, pad_to=0x176B00):
    """16 MiB flash backup shaped like stock: a program, 0x00 page padding, then erased."""
    dump = bytearray(b"\xff" * (16 * MIB))
    dump[:program_len] = (bytes(range(1, 256)) * (program_len // 255 + 1))[:program_len]
    dump[program_len:pad_to] = bytes(pad_to - program_len)
    return dump


class PackTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="eh-pack-")
        self.dir = pathlib.Path(self.tmp.name)
        self.menu = b"M" * 6432
        self.demo = b"D" * 300_000
        self.dump = fake_dump()

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, name, data):
        p = self.dir / name
        p.write_bytes(data)
        return p

    def run_pack(self, partitions=PARTITIONS, menu=None, demo=None, dump=None):
        out = self.dir / "out"
        pack.pack(partitions,
                  self.write("menu.bin", self.menu if menu is None else menu),
                  self.write("dump.bin", self.dump if dump is None else dump),
                  self.write("demo.bin", self.demo if demo is None else demo),
                  out)
        return out

    def test_stock_is_pure_relocation(self):
        out = self.run_pack()
        stock = (out / "slot1-stock.bin").read_bytes()
        self.assertEqual(stock, bytes(self.dump[:0x177000]))
        image = (out / "dualboot.bin").read_bytes()
        # Before != after: stock moves from offset 0 to 0x10000; the menu takes 0.
        self.assertNotEqual(image[:len(stock)], stock)
        self.assertEqual(image[0x10000:0x10000 + len(stock)], stock)
        self.assertEqual(image[:len(self.menu)], self.menu)
        self.assertEqual(image[0x200000:0x200000 + len(self.demo)], self.demo)
        self.assertEqual(len(image), 0x200000 + len(self.demo))
        self.assertEqual(set(image[len(self.menu):0x10000]), {0xFF})
        self.assertEqual(set(image[0x187000:0x200000]), {0xFF})

    def test_manifest_records_offsets_and_hashes(self):
        out = self.run_pack()
        m = json.loads((out / "manifest.json").read_text())
        slots = {s["name"]: s for s in m["slots"]}
        self.assertEqual([s["offset"] for s in m["slots"]], [0, 0x10000, 0x200000])
        self.assertEqual(slots["stock"]["length"], 0x177000)
        self.assertEqual(slots["stock"]["source_range"], [0, 0x177000])
        self.assertEqual(slots["stock"]["sha256"], hashlib.sha256(self.dump[:0x177000]).hexdigest())
        self.assertEqual(m["stock_dump_sha256"], hashlib.sha256(self.dump).hexdigest())
        for s in m["slots"]:
            self.assertEqual(hashlib.sha256((out / s["file"]).read_bytes()).hexdigest(), s["sha256"])
        self.assertLess(max(s["offset"] + s["length"] for s in m["slots"]), 16 * MIB - 256)

    def test_rejects_stock_data_outside_extent(self):
        # 0x180000 lies past the stock partition but before the demo, where
        # only the extent check (not the read-back or last-page guards) sees it.
        for where in (0x180000, 0x200000, 16 * MIB - 1):
            dump = fake_dump()
            dump[where] = 0x00
            with self.subTest(where=hex(where)), self.assertRaises(pack.PackError):
                self.run_pack(dump=dump)

    def test_rejects_oversized_images(self):
        with self.assertRaises(pack.PackError):
            self.run_pack(menu=b"M" * (0x10000 + 1))
        with self.assertRaises(pack.PackError):
            self.run_pack(demo=b"D" * (2 * MIB + 1))
        with self.assertRaises(pack.PackError):
            self.run_pack(demo=b"")

    def test_rejects_partition_in_last_sector(self):
        table = json.loads(PARTITIONS.read_text())
        table["partitions"][1]["start"] = "15360K"
        table["partitions"][1]["size"] = "1024K"
        bad = self.write("bad.json", json.dumps(table).encode())
        with self.assertRaises(pack.PackError):
            self.run_pack(partitions=bad)

    def test_rejects_overlapping_partitions(self):
        table = json.loads(PARTITIONS.read_text())
        table["partitions"][1]["start"] = "1024K"
        bad = self.write("overlap.json", json.dumps(table).encode())
        with self.assertRaises(pack.PackError):
            pack.partitions(bad)
        self.assertEqual(pack.partitions(PARTITIONS), [(0x10000, 0x177000), (0x200000, 0x200000)])

    def test_size_parsing(self):
        self.assertEqual(pack.parse_size("64K"), 0x10000)
        self.assertEqual(pack.parse_size("1500k"), 0x177000)
        self.assertEqual(pack.parse_size("2M"), 0x200000)
        self.assertEqual(pack.parse_size(4096), 4096)
        self.assertEqual(pack.parse_size("0x1000"), 4096)


if __name__ == "__main__":
    unittest.main()
