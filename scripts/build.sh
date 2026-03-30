#!/bin/bash
set -euo pipefail

# Build libmodernimage (.a, .so/.dylib/.dll) and tests
# Usage: ./scripts/build.sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo "=== Building libmodernimage ==="

cmake -G Ninja -S "$ROOT_DIR" -B "$ROOT_DIR/build" \
  -DCMAKE_BUILD_TYPE=Release

cmake --build "$ROOT_DIR/build" --config Release --parallel "$JOBS"

# Create fat static library via cmake --install
cmake --install "$ROOT_DIR/build"

echo ""
echo "=== Build artifacts ==="
ls -lh "$ROOT_DIR/build/libmodernimage.a" 2>/dev/null || true
ls -lh "$ROOT_DIR/build/libmodernimage."*dylib "$ROOT_DIR/build/libmodernimage.so" "$ROOT_DIR/build/libmodernimage.dll" 2>/dev/null || true
echo "=== Done ==="
