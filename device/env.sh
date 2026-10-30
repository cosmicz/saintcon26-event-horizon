# Source from the repository root: . device/env.sh
# Host-only RP2350 toolchain locations (see device/README.org). Override any
# of these before sourcing to use another install.
: "${RP2350_HOME:=$HOME/.local/share/rp2350}"
: "${PICO_SDK_PATH:=$RP2350_HOME/pico-sdk-2.3.1}"
: "${PICO_TOOLCHAIN_PATH:=$RP2350_HOME/arm-gnu-toolchain-15.3.rel2-darwin-arm64-arm-none-eabi}"
: "${picotool_DIR:=$(brew --prefix picotool 2>/dev/null)/lib/cmake/picotool}"
export RP2350_HOME PICO_SDK_PATH PICO_TOOLCHAIN_PATH picotool_DIR
