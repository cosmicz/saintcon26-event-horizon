#!/usr/bin/env python3
"""EEPROM/flash-safety audit of an RP2350 image we build (6nj hard criterion:
our images must never touch stock's I2C EEPROM at 0x50-0x53).

    python3 -I device/tools/image_audit.py BOOTROM_CONSTANTS_H IMAGE.elf [IMAGE.elf ...]

Each IMAGE.elf needs its IMAGE.bin beside it (pico_add_bin_output). Any hit
fails the image:
  i2c-symbols  a symbol containing "i2c" (any case) in the ELF symbol table;
  i2c-literal  a word literal of the I2C0/I2C1 register blocks (all four
               atomic aliases) in the flash image;
  flash-rom    a ROM-table lookup of flash erase/program/flash_op (Thumb-2
               MOVW of the ROM code, the pattern the gate #2 audit set up);
  gpio45       a word literal of GPIO4/5's IO_BANK0 control or PADS_BANK0
               register, or of their atomic aliases (the EEPROM bus pins).
Reported only: every ROM function code looked up, QMI/XIP_CTRL literals.

Fails closed: the constants header must define every forbidden ROM code,
or the audit stops with exit status 2. Exit status 0 iff every image passes.
Negative witnesses (device/tools/witnesses) must each fail on exactly their
own check. This is static evidence of absence over these patterns, not
proof. In particular, code that reaches GPIO4/5's registers as base +
offset (what GCC emits even for a constant address, and what the SDK's
gpio_* calls do) leaves no literal; device/tools/pin_audit.py covers that
at source level.
"""

import os
import pathlib
import re
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from thumb_scan import movw_sites, rom_codes, word_literals  # noqa: E402

FORBIDDEN_ROM = ("ROM_FUNC_FLASH_RANGE_ERASE", "ROM_FUNC_FLASH_RANGE_PROGRAM", "ROM_FUNC_FLASH_OP")
I2C_BLOCKS = (("I2C0", 0x40090000), ("I2C1", 0x40098000))
GPIO45_REGS = (("IO_BANK0 GPIO4_CTRL", 0x40028024), ("IO_BANK0 GPIO5_CTRL", 0x4002802C),
               ("PADS_BANK0 GPIO4", 0x40038014), ("PADS_BANK0 GPIO5", 0x40038018))
REPORTED = (("QMI", 0x400D0000), ("XIP_CTRL", 0x400C8000))


class AuditConfigError(Exception):
    pass


def checked_codes(codes):
    """The ROM codes, refusing a header that lacks any forbidden one."""
    names = dict(codes)
    missing = [n for n in FORBIDDEN_ROM if n not in names]
    if missing:
        raise AuditConfigError(f"constants header lacks {missing}; refusing to audit (fail closed)")
    return codes


def literal_hits(image, base, aliases=4):
    return sum(len(word_literals(image, base + a * 0x1000)) for a in range(aliases))


def audit_image(image, symbols, codes):
    """Return (failures, report) for a flash image and its symbol names.
    failures maps check name -> detail; empty means PASS."""
    codes = checked_codes(codes)
    failures, report = {}, []
    i2c_symbols = sorted({s for s in symbols if re.search("i2c", s, re.I)})
    report.append(f"symbols containing 'i2c': {len(i2c_symbols)}")
    if i2c_symbols:
        failures["i2c-symbols"] = i2c_symbols[:8]
    for name, base in I2C_BLOCKS:
        hits = literal_hits(image, base)
        report.append(f"{name} literals (incl. atomic aliases): {hits}")
        if hits:
            failures.setdefault("i2c-literal", []).append(name)
    looked_up = {n: len(movw_sites(image, c)) for n, c in codes}
    report.append("ROM functions looked up: " + (", ".join(f"{n[9:]}x{k}" for n, k in looked_up.items() if k) or "none"))
    forbidden = [n for n in FORBIDDEN_ROM if looked_up[n]]
    if forbidden:
        failures["flash-rom"] = forbidden
    for name, addr in GPIO45_REGS:
        hits = literal_hits(image, addr)
        report.append(f"{name} literals: {hits}")
        if hits:
            failures.setdefault("gpio45", []).append(name)
    for name, base in REPORTED:
        report.append(f"(reported) {name} literals: {literal_hits(image, base)}")
    return failures, report


def nm():
    toolchain = os.environ.get("PICO_TOOLCHAIN_PATH")
    return str(pathlib.Path(toolchain) / "bin" / "arm-none-eabi-nm") if toolchain else "arm-none-eabi-nm"


def audit_elf(elf, codes):
    image = pathlib.Path(elf).with_suffix(".bin").read_bytes()
    out = subprocess.run([nm(), elf], capture_output=True, text=True, check=True).stdout
    symbols = [line.split()[-1] for line in out.splitlines() if line.split()]
    failures, report = audit_image(image, symbols, codes)
    print(f"== {elf} ({len(image)} B flash image)")
    for line in report:
        print(f"  {line}")
    verdict = "PASS" if not failures else "FAIL " + " ".join(f"[{k}: {v}]" for k, v in sorted(failures.items()))
    print(f"  RESULT: {verdict}")
    print(f"  FAILED-CHECKS: {','.join(sorted(failures)) or '-'}")
    return not failures


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    try:
        codes = checked_codes(rom_codes(sys.argv[1]))
    except (AuditConfigError, OSError) as e:
        print(f"image_audit: {e}", file=sys.stderr)
        sys.exit(2)
    results = [audit_elf(elf, codes) for elf in sys.argv[2:]]
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
