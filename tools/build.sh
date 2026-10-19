#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="${1:-linux}"
BUILD="${BUILD_DIR:-$ROOT/build/$TARGET}"
ARGS=(-DCMAKE_BUILD_TYPE=Release -DGAME_VULKAN=ON)
if [ "$TARGET" = switch ]; then
    export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
    : "${NVK_SDK:?Set NVK_SDK to your mesa-switch installation}"
    : "${GLSLANG_NX:?Set GLSLANG_NX to your Switch glslang installation}"
    ARGS+=(-DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" -DNVK_SDK="$NVK_SDK" -Dglslang_DIR="$GLSLANG_NX/lib/cmake/glslang")
elif [ "$TARGET" != linux ]; then
    echo "Usage: tools/build.sh linux|switch" >&2; exit 2
fi
cmake -S "$ROOT" -B "$BUILD" -G Ninja "${ARGS[@]}"
cmake --build "$BUILD" -j"${JOBS:-2}"
