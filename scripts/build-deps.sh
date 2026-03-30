#!/bin/bash
set -euo pipefail

# Build all dependencies (libwebp, libaom, libavif)
# Usage: ./scripts/build-deps.sh [--jobs N]

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

JOBS="${1:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

echo "=== Building dependencies (jobs=$JOBS) ==="

# ---------- libwebp ----------
echo "--- libwebp ---"
WEBP_DIR="$ROOT_DIR/deps/libwebp"
WEBP_BUILD="$WEBP_DIR/build"

cmake -G Ninja -S "$WEBP_DIR" -B "$WEBP_BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DWEBP_UNICODE=OFF \
  -DWEBP_BUILD_CWEBP=ON \
  -DWEBP_BUILD_DWEBP=ON \
  -DWEBP_BUILD_GIF2WEBP=ON \
  -DWEBP_BUILD_IMG2WEBP=ON \
  -DWEBP_BUILD_WEBPMUX=ON \
  -DWEBP_BUILD_WEBPINFO=ON \
  -DWEBP_BUILD_ANIM_UTILS=ON \
  -DWEBP_BUILD_VWEBP=OFF \
  -DWEBP_BUILD_EXTRAS=OFF

cmake --build "$WEBP_BUILD" --config Release --parallel "$JOBS"
echo "libwebp: OK"

# ---------- libaom ----------
echo "--- libaom ---"
AVIF_DIR="$ROOT_DIR/deps/libavif"
AOM_SRC="$AVIF_DIR/ext/aom"
AOM_BUILD="$AOM_SRC/build.libavif"

if [ ! -d "$AOM_SRC/.git" ]; then
  AOM_VERSION=$(sed -n 's/.*-b \(v[0-9.]*\).*/\1/p' "$AVIF_DIR/ext/aom.cmd" | head -1)
  AOM_VERSION="${AOM_VERSION:-v3.13.2}"
  echo "Cloning libaom $AOM_VERSION ..."
  git clone -b "$AOM_VERSION" --depth 1 https://aomedia.googlesource.com/aom "$AOM_SRC"
fi

cmake -G Ninja -S "$AOM_SRC" -B "$AOM_BUILD" \
  -DBUILD_SHARED_LIBS=OFF \
  -DCONFIG_PIC=1 \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_DOCS=0 \
  -DENABLE_EXAMPLES=0 \
  -DENABLE_TESTDATA=0 \
  -DENABLE_TESTS=0 \
  -DENABLE_TOOLS=0

cmake --build "$AOM_BUILD" --config Release --parallel "$JOBS"
echo "libaom: OK"

# ---------- libavif ----------
echo "--- libavif ---"
AVIF_BUILD="$AVIF_DIR/build"

cmake -G Ninja -S "$AVIF_DIR" -B "$AVIF_BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DAVIF_CODEC_AOM=LOCAL \
  -DAVIF_BUILD_APPS=ON \
  -DAVIF_LIBYUV=OFF

cmake --build "$AVIF_BUILD" --config Release --parallel "$JOBS"
echo "libavif: OK"

echo "=== All dependencies built ==="
