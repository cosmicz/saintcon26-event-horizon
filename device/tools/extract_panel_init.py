#!/usr/bin/env python3
"""Extract the ST77916 panel register-init table from the private stock
Wayfarer v0.1 flash backup, as a C header for the demo's display driver.

    python3 -I device/tools/extract_panel_init.py STOCK_DUMP.bin OBJDUMP OUT.h

The values come from your own stock dump and stay private: they are the
badge maker's, not ours to redistribute. OUT.h must land in an ignored build
directory and is never committed or published; this script contains no
firmware bytes.

Method (static; no firmware code is executed): stock's panel-init function
(0x10010e54) builds an array of 16-byte entries {u8 cmd; const u8 *data;
u32 len; u32 delay_ms} on its stack frame with immediate stores, then loops
over it from the pointer kept at [r7+4] to r7+0xf48 (loop at 0x1001249e).
This walks objdump's text for the straight-line builder, tracks register
immediates and r7-relative addresses, records the stores into the frame,
and decodes the entries. Any instruction it cannot model makes the register
unknown; any unknown byte in the result is an error.

Cross-check (2026-10-29): the 194 entries equal, in order, the first 194 of
the community gif-player's st77916_init.h (9726dad5), which its author also
read back from stock; that table appends a MADCTL 0x36=0x00 of its own.
"""

import hashlib
import pathlib
import re
import struct
import subprocess
import sys

XIP = 0x10000000
FLASH_SIZE = 16 << 20
# The firmware this was written against, Wayfarer v0.1: flash 0x0-0x177000,
# the dual-boot stock slot. Only the bytes this tool reads are pinned; the
# packer separately requires the rest of flash to be erased. The addresses
# below assume this build.
STOCK_SLOT = 0x177000
STOCK_SLOT_SHA256 = "504a1d8fc8672ddcf89391c03f203e75ee14fe5aff8927b9c807c199fc5935ad"
BUILDER, LOOP, END = 0x10010e54, 0x1001249e, 0x1001254c
TABLE_PTR_SLOT, TABLE_END, ENTRY = 4, 0xF48, 16
EXPECTED_ENTRIES = 194
REG = r"r\d+|sl|fp|ip|lr"


class ExtractError(Exception):
    pass


def disassemble(dump_path, objdump, start):
    text = subprocess.run(
        [objdump, "-D", "-b", "binary", "-marm", "-Mforce-thumb", f"--adjust-vma={XIP:#x}",
         f"--start-address={start:#x}", f"--stop-address={END:#x}", str(dump_path)],
        capture_output=True, text=True, check=True).stdout
    out = {}
    for line in text.splitlines():
        m = re.match(r"\s*([0-9a-f]+):\s+((?:[0-9a-f]{4} ?)+)\s+(\S+)\s*(.*)", line)
        if m:
            out[int(m.group(1), 16)] = (2 * len(m.group(2).split()), m.group(3), m.group(4).split("@")[0].strip())
    return out


class Frame:
    """Registers hold ('i', value) or ('f', r7 offset); frame bytes hold an
    int, ('ptr', r7 offset) at the first byte of a stored pointer, or None."""

    def __init__(self, image):
        self.image, self.regs, self.mem = image, {}, {}

    def word(self, addr):
        return struct.unpack_from("<I", self.image, addr - XIP)[0]

    def store(self, base, off, val, size):
        b = self.regs.get(base)
        if not b or b[0] != "f":
            return  # outside the frame (peripheral set-up before the table)
        at = b[1] + off
        if val is None:
            for k in range(size):
                self.mem[at + k] = None
        elif val[0] == "f":
            self.mem[at] = ("ptr", val[1])
            for k in range(1, size):
                self.mem[at + k] = ("ptrbyte",)
        else:
            for k in range(size):
                self.mem[at + k] = (val[1] >> (8 * k)) & 0xFF

    def read(self, off, size):
        raw = [self.mem.get(off + k) for k in range(size)]
        if all(isinstance(b, int) for b in raw):
            return int.from_bytes(bytes(raw), "little")
        if isinstance(raw[0], tuple) and raw[0][0] == "ptr":
            return raw[0]
        return None


def step(f, pc, size, op, args):
    """Apply one instruction; return the next pc."""
    a = [x.strip() for x in re.split(r",\s*(?![^\[{]*[\]}])", args)] if args else []
    r = f.regs
    base = op.split(".")[0]
    imm = lambda s: int(s.lstrip("#"), 0)  # noqa: E731
    mem_operand = lambda s: re.match(r"\[(\w+)(?:, #(-?\d+))?\]$", s)  # noqa: E731
    if base in ("mov", "movs", "movw") and a[1].startswith("#"):
        r[a[0]] = ("i", imm(a[1]) & 0xFFFFFFFF)
    elif base in ("mov", "movs") and re.fullmatch(REG + "|sp", a[1]):
        r[a[0]] = ("f", 0) if a[1] == "sp" and a[0] == "r7" else r.get(a[1])
    elif base == "movt":
        v = r.get(a[0])
        r[a[0]] = ("i", (v[1] & 0xFFFF) | imm(a[1]) << 16) if v and v[0] == "i" else None
    elif base in ("mvn", "mvns") and a[1].startswith("#"):
        r[a[0]] = ("i", ~imm(a[1]) & 0xFFFFFFFF)
    elif base in ("add", "adds", "addw") and len(a) == 3 and a[2].startswith("#"):
        v = ("f", 0) if a[1] == "sp" and a[0] == "r7" else r.get(a[1])
        r[a[0]] = (v[0], v[1] + imm(a[2])) if v and v[0] == "f" else \
            ("i", (v[1] + imm(a[2])) & 0xFFFFFFFF) if v else None
    elif base in ("add", "adds") and len(a) == 3 and re.fullmatch(REG, a[2]):
        x, y = r.get(a[1]), r.get(a[2])
        if x and y and {x[0], y[0]} == {"f", "i"}:
            ptr, num = (x, y) if x[0] == "f" else (y, x)
            r[a[0]] = ("f", ptr[1] + num[1])
        elif x and y and x[0] == y[0] == "i":
            r[a[0]] = ("i", (x[1] + y[1]) & 0xFFFFFFFF)
        else:
            r[a[0]] = None
    elif base == "ldr" and a[1].startswith("[pc"):
        off = int(re.search(r"#(-?\d+)", a[1]).group(1)) if "#" in a[1] else 0
        r[a[0]] = ("i", f.word(((pc + 4) & ~3) + off))
    elif base == "ldr" and mem_operand(a[1]) and mem_operand(a[1]).group(1) == "r7":
        v = f.mem.get(int(mem_operand(a[1]).group(2) or 0))
        r[a[0]] = ("f", v[1]) if isinstance(v, tuple) and v[0] == "ptr" else None
    elif base in ("str", "strb", "strh") and mem_operand(a[1]):
        m = mem_operand(a[1])
        f.store(m.group(1), int(m.group(2) or 0), r.get(a[0]), {"str": 4, "strb": 1, "strh": 2}[base])
    elif base == "strd":
        m = mem_operand(a[2])
        off = int(m.group(2) or 0)
        f.store(m.group(1), off, r.get(a[0]), 4)
        f.store(m.group(1), off + 4, r.get(a[1]), 4)
    elif base == "ldmia":
        src, regs = r.get(a[0].rstrip("!")), re.findall(r"r\d+", a[1])
        for k, reg in enumerate(regs):
            r[reg] = ("i", f.word(src[1] + 4 * k)) if src and src[0] == "i" and src[1] >= XIP else None
    elif base == "stmia":
        dst, regs = a[0].rstrip("!"), re.findall(r"r\d+", a[1])
        for k, reg in enumerate(regs):
            f.store(dst, 4 * k, r.get(reg), 4)
        if a[0].endswith("!") and r.get(dst):
            r[dst] = (r[dst][0], r[dst][1] + 4 * len(regs))
    elif base == "bl":
        for reg in ("r0", "r1", "r2", "r3", "ip", "lr"):
            r[reg] = None
    elif base == "b":
        return int(a[0], 16)
    elif a and re.fullmatch(REG, a[0]) and base not in ("cmp", "cmn", "tst", "stmdb", "push", "it", "mcrr"):
        r[a[0]] = None  # unmodelled write: the register becomes unknown
    return pc + size


def check_dump(image):
    if len(image) != FLASH_SIZE:
        raise ExtractError(f"dump is {len(image)} B, expected a full {FLASH_SIZE} B backup")
    if hashlib.sha256(image[:STOCK_SLOT]).hexdigest() != STOCK_SLOT_SHA256:
        raise ExtractError("the dump's firmware is not the Wayfarer v0.1 build this extractor knows")


def extract(dump_path, objdump):
    image = pathlib.Path(dump_path).read_bytes()
    check_dump(image)
    insns = disassemble(dump_path, objdump, BUILDER)
    f, pc = Frame(image), BUILDER
    for _ in range(10000):
        if pc == LOOP:
            break
        if pc not in insns:  # objdump lost sync on a literal pool: resume here
            insns.update(disassemble(dump_path, objdump, pc))
        pc = step(f, pc, *insns[pc])
    else:
        raise ExtractError("did not reach the send loop")
    start = f.mem.get(TABLE_PTR_SLOT)
    if not (isinstance(start, tuple) and start[0] == "ptr"):
        raise ExtractError("table start unknown")
    entries = []
    for at in range(start[1], TABLE_END, ENTRY):
        cmd, data, length, delay = f.read(at, 1), f.read(at + 4, 4), f.read(at + 8, 4), f.read(at + 12, 4)
        if None in (cmd, length, delay):
            raise ExtractError(f"entry at r7+{at:#x} has unknown fields")
        if length == 0:
            payload = []
        elif isinstance(data, tuple):
            payload = [f.mem.get(data[1] + k) for k in range(length)]
        elif isinstance(data, int) and data >= XIP:
            payload = list(image[data - XIP:data - XIP + length])
        else:
            payload = [None]
        if len(payload) != length or not all(isinstance(b, int) for b in payload):
            raise ExtractError(f"entry at r7+{at:#x} (cmd {cmd:#04x}) has unknown data")
        entries.append((cmd, payload, delay))
    if len(entries) != EXPECTED_ENTRIES or [e[0] for e in entries[-3:]] != [0x21, 0x11, 0x29]:
        raise ExtractError(f"unexpected table shape ({len(entries)} entries)")
    return entries


def header(entries):
    width = max(len(p) for _, p, _ in entries)
    rows = "\n".join(
        f"    {{0x{c:02X}, {len(p):2d}, {d:3d}, {{{', '.join(f'0x{b:02X}' for b in p) or '0'}}}}},"
        for c, p, d in entries)
    return f"""/* GENERATED by device/tools/extract_panel_init.py from the private stock
 * backup. Stock firmware register values, not ours to redistribute: never commit
 * or publish this file. */
#ifndef EH_ST77916_STOCK_INIT_H
#define EH_ST77916_STOCK_INIT_H

#include <stdint.h>

typedef struct {{
    uint8_t cmd;
    uint8_t len;
    uint16_t delay_ms;
    uint8_t data[{width}];
}} eh_st77916_cmd;

static const eh_st77916_cmd eh_st77916_stock_init[{len(entries)}] = {{
{rows}
}};

#endif
"""


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    try:
        entries = extract(sys.argv[1], sys.argv[2])
    except ExtractError as e:
        sys.exit(f"extract_panel_init: {e}")
    pathlib.Path(sys.argv[3]).write_text(header(entries))
    print(f"extract_panel_init: {len(entries)} commands -> {sys.argv[3]}")


if __name__ == "__main__":
    main()
