"""Byte-level scans of Thumb-2 RP2350 images, shared by the audit tools.

Static and host-only: these read bytes and never execute anything.
"""
import re
import struct


def movw_sites(data, imm16):
    """Offsets of Thumb-2 MOVW (T3) instructions loading imm16 into any Rd."""
    i, imm4 = (imm16 >> 11) & 1, imm16 >> 12
    imm3, imm8 = (imm16 >> 8) & 7, imm16 & 0xFF
    hw1 = 0xF240 | (i << 10) | imm4
    sites = []
    for off in range(0, len(data) - 3, 2):
        a, b = struct.unpack_from("<HH", data, off)
        if a == hw1 and not b & 0x8000 and (b >> 12) & 7 == imm3 and b & 0xFF == imm8:
            sites.append(off)
    return sites


def rom_codes(header):
    pattern = re.compile(r"define (ROM_FUNC_\w+)\s+ROM_TABLE_CODE\('(.)',\s*'(.)'\)")
    return [(m.group(1), ord(m.group(2)) | ord(m.group(3)) << 8)
            for m in pattern.finditer(open(header).read())]


def word_literals(data, value):
    return [m.start() for m in re.finditer(re.escape(struct.pack("<I", value)), data)
            if m.start() % 4 == 0]
