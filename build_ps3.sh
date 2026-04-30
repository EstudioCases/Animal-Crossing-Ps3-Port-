#!/usr/bin/env bash
# Build the PS3 bootstrap/package with PSL1GHT.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [ -z "${PSL1GHT:-}" ]; then
    echo "error: PSL1GHT is not set"
    echo "Set PSL1GHT to your PSL1GHT SDK path before building."
    exit 1
fi

if ! command -v make >/dev/null 2>&1; then
    echo "error: make was not found in PATH"
    exit 1
fi

if ! command -v ppu-gcc >/dev/null 2>&1 && ! command -v powerpc64-ps3-elf-gcc >/dev/null 2>&1; then
    echo "error: PS3 PPU compiler was not found in PATH"
    echo "Expected ppu-gcc or powerpc64-ps3-elf-gcc."
    exit 1
fi

if [ "${ACGC_PS3_FULL_GAME:-0}" = "1" ]; then
    if ! command -v cmake >/dev/null 2>&1; then
        echo "error: cmake was not found in PATH"
        exit 1
    fi
    if ! command -v ninja >/dev/null 2>&1; then
        echo "error: ninja was not found in PATH"
        exit 1
    fi

    cmake -S "$ROOT_DIR/ps3" -B "$ROOT_DIR/ps3/build-full" -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/ps3/cmake/Toolchain-psl1ght.cmake" \
      -DACGC_PS3_BOOTSTRAP_ONLY=OFF
    ninja -C "$ROOT_DIR/ps3/build-full"
    exit 0
fi

cd "$ROOT_DIR/ps3"

if make pkg; then
    echo "PS3 package build finished."
else
    echo "pkg target failed; trying a plain ELF/SELF build."
    make
fi
