#!/bin/bash
set -euo pipefail

# Build libmodernimage (.a, .so/.dylib) and tests
# Usage: ./scripts/build.sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "=== Building libmodernimage ==="

cmake -G Ninja -S "$ROOT_DIR" -B "$ROOT_DIR/build" \
  -DCMAKE_BUILD_TYPE=Release

cmake --build "$ROOT_DIR/build" --config Release

echo ""
echo "=== Build artifacts ==="
ls -lh "$ROOT_DIR/build/libmodernimage.a" "$ROOT_DIR/build/libmodernimage."*dylib "$ROOT_DIR/build/libmodernimage.so" 2>/dev/null || true
echo ""
nm -g "$ROOT_DIR/build/libmodernimage."*dylib "$ROOT_DIR/build/libmodernimage.so" 2>/dev/null | grep " T _modernimage" || true
echo "=== Done ==="
