#!/bin/bash
set -euo pipefail

# Package build artifacts into a release archive
# Usage: ./scripts/package.sh [output_dir]

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"

OUTPUT_DIR="${1:-$BUILD_DIR/dist}"
mkdir -p "$OUTPUT_DIR"

OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"

# Normalize OS name
case "$OS" in
  msys*|mingw*|cygwin*) OS="windows" ;;
esac

PLATFORM="${OS}-${ARCH}"

STAGING="$OUTPUT_DIR/libmodernimage-${PLATFORM}"
rm -rf "$STAGING"
mkdir -p "$STAGING"

# Copy static library
cp "$BUILD_DIR/libmodernimage.a" "$STAGING/"

# Copy shared library (platform-dependent extension)
for ext in dylib so dll; do
  if ls "$BUILD_DIR"/libmodernimage.${ext}* 1>/dev/null 2>&1; then
    cp "$BUILD_DIR"/libmodernimage.${ext}* "$STAGING/"
  fi
done

# Copy header and metadata
cp "$ROOT_DIR/include/modernimage.h" "$STAGING/"
cp "$BUILD_DIR/cli-compat.json" "$STAGING/" 2>/dev/null || true

# Create archive
ARCHIVE="$OUTPUT_DIR/libmodernimage-${PLATFORM}.tar.gz"
tar -czf "$ARCHIVE" -C "$OUTPUT_DIR" "libmodernimage-${PLATFORM}"

echo "=== Package created ==="
echo "Archive: $ARCHIVE"
ls -lh "$ARCHIVE"
echo ""
echo "Contents:"
tar -tzf "$ARCHIVE"
