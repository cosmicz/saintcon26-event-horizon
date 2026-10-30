#!/bin/sh
# Compile-only Cortex-M33 view of the core: Thumb-2 assembly, innermost-loop
# instruction counts and section sizes. Nothing is linked or run on a device.
# Needs a Clang with the ARM backend (Apple clang 17 has it) and llvm-size.
#
#   sh docs/measurements/native-360/probes/m33.sh [OUT_DIR]   # from the repo root
set -eu
out=${1:-build/m33}
here=$(dirname "$0")
mkdir -p "$out"
flags="--target=thumbv8m.main-none-eabihf -mcpu=cortex-m33 -mfloat-abi=hard -O2 -fno-math-errno -nostdlibinc"
for f in src/core/*.c; do
    base=$(basename "$f" .c)
    # shellcheck disable=SC2086
    clang $flags -I"$here/m33_shim" -Iinclude -Isrc/core -S "$f" -o "$out/$base.s"
    # shellcheck disable=SC2086
    clang $flags -I"$here/m33_shim" -Iinclude -Isrc/core -c "$f" -o "$out/$base.o"
done
python3 "$here/m33_loops.py" "$out"/*.s
size=$(command -v llvm-size || xcrun -f llvm-size)
"$size" "$out"/*.o | sed "s|$out/||"
