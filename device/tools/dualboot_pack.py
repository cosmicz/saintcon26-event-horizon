#!/usr/bin/env python3
"""Assemble the three dual-boot slots from partitions.json and check the
layout invariants. Host only: writes files, never
contacts a device.

    python3 -I device/tools/dualboot_pack.py PARTITIONS.json MENU.bin STOCK_DUMP.bin DEMO.bin OUT_DIR

STOCK_DUMP.bin is the private 16 MiB backup of the badge. Its used extent
(from 0 to the end of the last 4 KiB sector holding a non-0xFF byte) is
copied unchanged into partition 0: a pure relocation. OUT_DIR receives
slot0-menu.bin, slot1-stock.bin, slot2-demo.bin, dualboot.bin (flash offset
0 to the end of the demo, gaps 0xFF) and manifest.json (offsets, lengths,
SHA-256). OUT_DIR holds stock firmware bytes, so it must stay private.

Rejected: a menu that reaches partition 0; partitions that overlap, are
unaligned or reach the last flash sector (the last 256 B page is never
allocated); stock bytes outside its partition; an empty or oversized demo.
"""

import hashlib
import json
import pathlib
import re
import sys

FLASH_SIZE = 16 << 20
SECTOR = 4096
LAST_PAGE = FLASH_SIZE - 256
SLOT_NAMES = ("stock", "demo")


class PackError(Exception):
    pass


def parse_size(value):
    if isinstance(value, int):
        return value
    m = re.fullmatch(r"\s*(0x[0-9a-fA-F]+|\d+)\s*([kKmM]?)\s*", value)
    if not m:
        raise PackError(f"bad size {value!r}")
    n = int(m.group(1), 0)
    return n * {"": 1, "k": 1 << 10, "m": 1 << 20}[m.group(2).lower()]


def partitions(path):
    table = json.loads(pathlib.Path(path).read_text())["partitions"]
    if len(table) != len(SLOT_NAMES):
        raise PackError(f"expected {len(SLOT_NAMES)} partitions, found {len(table)}")
    spans = []
    for p in table:
        start, size = parse_size(p["start"]), parse_size(p["size"])
        if start % SECTOR or size % SECTOR or size == 0:
            raise PackError(f"partition {p.get('name')} not sector aligned")
        if start + size > FLASH_SIZE - SECTOR:
            raise PackError(f"partition {p.get('name')} reaches the last flash sector")
        spans.append((start, size))
    ordered = sorted(spans)
    for (a, alen), (b, _) in zip(ordered, ordered[1:]):
        if a + alen > b:
            raise PackError("partitions overlap")
    return spans


def stock_extent(dump):
    if len(dump) != FLASH_SIZE:
        raise PackError(f"stock dump is {len(dump)} B, expected {FLASH_SIZE}")
    last = len(dump.rstrip(b"\xff"))
    return -(-last // SECTOR) * SECTOR


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def pack(partitions_json, menu_path, dump_path, demo_path, out_dir):
    (stock_start, stock_size), (demo_start, demo_size) = partitions(partitions_json)
    menu = pathlib.Path(menu_path).read_bytes()
    dump = pathlib.Path(dump_path).read_bytes()
    demo = pathlib.Path(demo_path).read_bytes()

    if len(menu) > min(stock_start, demo_start):
        raise PackError(f"menu ({len(menu)} B) reaches partition space")
    extent = stock_extent(dump)
    if extent > stock_size:
        raise PackError(f"stock extent {extent:#x} exceeds its partition ({stock_size:#x})")
    stock = dump[:extent]
    if not 0 < len(demo) <= demo_size:
        raise PackError(f"demo is {len(demo)} B; partition holds {demo_size}")

    slots = [("menu", 0, menu), ("stock", stock_start, stock), ("demo", demo_start, demo)]
    end = max(offset + len(data) for _, offset, data in slots)
    if end > LAST_PAGE:
        raise PackError("image reaches the last flash page")
    image = bytearray(b"\xff" * end)
    for _, offset, data in slots:
        image[offset:offset + len(data)] = data

    out = pathlib.Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    manifest = {"flash_size": FLASH_SIZE, "stock_dump_sha256": sha256(dump), "slots": []}
    for index, (name, offset, data) in enumerate(slots):
        file = f"slot{index}-{name}.bin"
        (out / file).write_bytes(data)
        entry = {"name": name, "file": file, "offset": offset, "length": len(data), "sha256": sha256(data)}
        if name == "stock":
            entry["source_range"] = [0, extent]
        manifest["slots"].append(entry)
    (out / "dualboot.bin").write_bytes(image)
    manifest["dualboot"] = {"file": "dualboot.bin", "length": len(image), "sha256": sha256(image)}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    # Read back what was written: the relocation claim rests on these bytes.
    if (out / "slot1-stock.bin").read_bytes() != dump[:extent]:
        raise PackError("stock slot read-back differs from the dump")
    if (out / "dualboot.bin").read_bytes()[stock_start:stock_start + extent] != dump[:extent]:
        raise PackError("dualboot.bin stock range differs from the dump")
    return manifest


def main():
    if len(sys.argv) != 6:
        sys.exit(__doc__)
    try:
        manifest = pack(*sys.argv[1:])
    except PackError as e:
        sys.exit(f"dualboot_pack: {e}")
    for s in manifest["slots"]:
        print(f"{s['name']:6s} {s['offset']:#09x} +{s['length']:#09x}  {s['sha256']}")
    d = manifest["dualboot"]
    print(f"dualboot.bin {d['length']} B  {d['sha256']}")
    print(f"stock dump sha256 {manifest['stock_dump_sha256']}")


if __name__ == "__main__":
    main()
