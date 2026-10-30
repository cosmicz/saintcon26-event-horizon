#!/bin/sh
# Build the dual-boot images, audit them, and pack the three slots. Host
# only: picotool reads files, never a device.
#   sh device/dualboot/check.sh STOCK_DUMP.bin OUT_DIR   (from the repo root)
# Both the menu and the demo drive the panel, so both need the private stock
# dump for its init table. OUT_DIR receives stock firmware bytes: keep it
# private and outside Git.
set -eu
if [ $# -ne 2 ]; then
    echo "usage: sh device/dualboot/check.sh STOCK_DUMP.bin OUT_DIR" >&2
    exit 2
fi
. device/env.sh
b=device/dualboot/build
cmake -S device/dualboot -B "$b" -G Ninja -DCMAKE_BUILD_TYPE=Release -Dpicotool_DIR="$picotool_DIR" "-DEH_STOCK_DUMP=$1" >/dev/null
cmake --build "$b" >/dev/null
t="$PICO_TOOLCHAIN_PATH/bin/arm-none-eabi"
constants="$PICO_SDK_PATH/src/rp2_common/boot_bootrom_headers/include/boot/bootrom_constants.h"
elfs=$(ls "$b"/eh_*.elf)

echo "== sizes"
"$t-size" $elfs | sed "s|$b/||"
echo "== menu metadata (picotool info on the file)"
picotool info -a "$b/eh_menu.bin" | sed -n '/^Metadata Block/,$p'
echo "== EEPROM/flash-safety audit"
python3 -I device/tools/image_audit.py "$constants" $elfs
echo "== source pin audit"
python3 -I device/tools/pin_audit.py
echo "== negative witnesses (each must FAIL on exactly its own check)"
w=device/tools/witnesses/build
cmake -S device/tools/witnesses -B "$w" -G Ninja -DCMAKE_BUILD_TYPE=Release -Dpicotool_DIR="$picotool_DIR" >/dev/null 2>&1
cmake --build "$w" >/dev/null
for spec in rom:flash-rom i2c:i2c-literal,i2c-symbols gpio45:gpio45; do
    name=${spec%%:*} want=${spec#*:}
    if out=$(python3 -I device/tools/image_audit.py "$constants" "$w/witness_$name.elf"); then
        echo "witness_$name PASSED: audit is vacuous" >&2
        exit 1
    fi
    got=$(printf '%s\n' "$out" | sed -n 's/^  FAILED-CHECKS: //p')
    if [ "$got" != "$want" ]; then
        echo "witness_$name failed on [$got], expected [$want]" >&2
        exit 1
    fi
    echo "witness_$name failed on [$got] as required"
done
empty=$(mktemp)
echo "/* no ROM codes */" > "$empty"
set +e
python3 -I device/tools/image_audit.py "$empty" "$b/eh_menu.elf" >/dev/null 2>&1
rc=$?
set -e
rm -f "$empty"
if [ "$rc" -ne 2 ]; then
    echo "audit with an empty constants header exited $rc, expected 2 (fail closed)" >&2
    exit 1
fi
echo "empty constants header refused (exit 2) as required"

echo "== pack"
python3 -I device/tools/dualboot_pack.py device/dualboot/partitions.json \
    "$b/eh_menu.bin" "$1" "$b/eh_demo.bin" "$2"
echo "== sha256"
shasum -a 256 "$b"/eh_*.bin | sed "s|$b/||"
