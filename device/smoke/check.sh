#!/bin/sh
# Build the smoke image and print file-only evidence. Never contacts a device:
# `picotool info` is given a file path, not a bus/address.
#   sh device/smoke/check.sh        (from the repository root)
set -eu
. device/env.sh
b=device/smoke/build
cmake -S device/smoke -B "$b" -G Ninja -DCMAKE_BUILD_TYPE=Release -Dpicotool_DIR="$picotool_DIR" >/dev/null
cmake --build "$b" >/dev/null
t="$PICO_TOOLCHAIN_PATH/bin/arm-none-eabi"
echo "== versions"
"$t-gcc" --version | head -1
echo "pico-sdk $(git -C "$PICO_SDK_PATH" describe --tags) $(git -C "$PICO_SDK_PATH" rev-parse HEAD)"
picotool version | head -1
cmake --version | head -1
echo "== ELF attributes"
"$t-readelf" -A "$b/eh_smoke.elf" | grep -E "Tag_CPU_name|Tag_CPU_arch:|Tag_FP_arch"
"$t-size" "$b/eh_smoke.elf"
echo "== picotool info (file only)"
picotool info -a "$b/eh_smoke.uf2"
echo "== UF2 blocks by family"
python3 - "$b/eh_smoke.uf2" <<'PY'
import collections, struct, sys
d = open(sys.argv[1], "rb").read()
fams = collections.Counter()
lo, hi = {}, {}
for i in range(0, len(d), 512):
    m0, m1, flags, addr, size, n, total, fam = struct.unpack("<8I", d[i:i + 32])
    assert (m0, m1) == (0x0A324655, 0x9E5D5157)
    fams[fam] += 1
    lo[fam] = min(lo.get(fam, addr), addr)
    hi[fam] = max(hi.get(fam, addr + size), addr + size)
for fam, n in fams.items():
    print(f"family {fam:#010x}: {n} blocks, {lo[fam]:#010x}-{hi[fam]:#010x}")
PY
echo "== I2C references in the ELF symbol table (expect 0)"
"$t-nm" "$b/eh_smoke.elf" | grep -ci i2c || true
echo "== sha256"
shasum -a 256 "$b/eh_smoke.elf" "$b/eh_smoke.uf2" "$b/eh_smoke.bin" | sed "s|$b/||"
