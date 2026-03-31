#!/bin/bash
set -euo pipefail

# Build all dependencies from source for maximum portability.
# No system libraries required except libc and pthread.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
DEPS_DIR="$ROOT_DIR/deps"

JOBS="${1:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

# Common install prefix for all built deps
PREFIX="$ROOT_DIR/deps-build"
mkdir -p "$PREFIX/lib" "$PREFIX/include"

echo "=== Building dependencies from source (jobs=$JOBS) ==="
echo "Install prefix: $PREFIX"

# ---------- zlib ----------
echo ""
echo "--- zlib ---"
ZLIB_DIR="$DEPS_DIR/zlib"
if [ ! -d "$ZLIB_DIR/.git" ] && [ ! -f "$ZLIB_DIR/CMakeLists.txt" ]; then
  git clone --depth 1 -b v1.3.1 https://github.com/madler/zlib.git "$ZLIB_DIR"
fi
cmake -G Ninja -S "$ZLIB_DIR" -B "$ZLIB_DIR/build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DBUILD_SHARED_LIBS=OFF
cmake --build "$ZLIB_DIR/build" --parallel "$JOBS"
cmake --install "$ZLIB_DIR/build"
echo "zlib: OK"

# ---------- libpng (depends on zlib) ----------
echo ""
echo "--- libpng ---"
LIBPNG_DIR="$DEPS_DIR/libpng"
if [ ! -d "$LIBPNG_DIR/.git" ] && [ ! -f "$LIBPNG_DIR/CMakeLists.txt" ]; then
  git clone --depth 1 -b v1.6.47 https://github.com/pnggroup/libpng.git "$LIBPNG_DIR"
fi
cmake -G Ninja -S "$LIBPNG_DIR" -B "$LIBPNG_DIR/build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_PREFIX_PATH="$PREFIX" \
  -DPNG_SHARED=OFF \
  -DPNG_STATIC=ON \
  -DPNG_TESTS=OFF \
  -DPNG_TOOLS=OFF
cmake --build "$LIBPNG_DIR/build" --parallel "$JOBS"
cmake --install "$LIBPNG_DIR/build"
echo "libpng: OK"

# ---------- libjpeg-turbo ----------
echo ""
echo "--- libjpeg-turbo ---"
LIBJPEG_DIR="$DEPS_DIR/libjpeg-turbo"
if [ ! -d "$LIBJPEG_DIR/.git" ] && [ ! -f "$LIBJPEG_DIR/CMakeLists.txt" ]; then
  git clone --depth 1 -b 3.1.0 https://github.com/libjpeg-turbo/libjpeg-turbo.git "$LIBJPEG_DIR"
fi
cmake -G Ninja -S "$LIBJPEG_DIR" -B "$LIBJPEG_DIR/build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DENABLE_SHARED=OFF \
  -DENABLE_STATIC=ON \
  -DWITH_TURBOJPEG=OFF
cmake --build "$LIBJPEG_DIR/build" --parallel "$JOBS"
cmake --install "$LIBJPEG_DIR/build"
echo "libjpeg-turbo: OK"

# ---------- giflib ----------
echo ""
echo "--- giflib ---"
GIFLIB_DIR="$DEPS_DIR/giflib"
if [ ! -d "$GIFLIB_DIR" ] || [ ! -f "$GIFLIB_DIR/gif_lib.h" ]; then
  GIFLIB_VER="5.2.2"
  echo "Downloading giflib $GIFLIB_VER ..."
  curl -L -o "$DEPS_DIR/giflib.tar.gz" \
    "https://sourceforge.net/projects/giflib/files/giflib-${GIFLIB_VER}.tar.gz/download" \
    || curl -L -o "$DEPS_DIR/giflib.tar.gz" \
    "https://storage.googleapis.com/google-code-archive-downloads/v2/code.google.com/giflib/giflib-${GIFLIB_VER}.tar.gz" \
    || { echo "Failed to download giflib"; exit 1; }
  tar -xzf "$DEPS_DIR/giflib.tar.gz" -C "$DEPS_DIR"
  mv "$DEPS_DIR/giflib-${GIFLIB_VER}" "$GIFLIB_DIR" 2>/dev/null || true
  rm -f "$DEPS_DIR/giflib.tar.gz"
fi
# giflib uses a simple Makefile; build just the static library
(
  cd "$GIFLIB_DIR"
  CFLAGS="-O2 -fPIC" ${CC:-cc} -c -I. dgif_lib.c egif_lib.c gif_err.c gif_font.c gif_hash.c gifalloc.c openbsd-reallocarray.c quantize.c
  ar rcs libgif.a dgif_lib.o egif_lib.o gif_err.o gif_font.o gif_hash.o gifalloc.o openbsd-reallocarray.o quantize.o
  cp libgif.a "$PREFIX/lib/"
  cp gif_lib.h "$PREFIX/include/"
)
echo "giflib: OK"

# ---------- libwebp ----------
echo ""
echo "--- libwebp ---"
WEBP_DIR="$DEPS_DIR/libwebp"
WEBP_BUILD="$WEBP_DIR/build"

cmake -G Ninja -S "$WEBP_DIR" -B "$WEBP_BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_PREFIX_PATH="$PREFIX" \
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
echo ""
echo "--- libaom ---"
AVIF_DIR="$DEPS_DIR/libavif"
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
echo ""
echo "--- libavif ---"
AVIF_BUILD="$AVIF_DIR/build"

cmake -G Ninja -S "$AVIF_DIR" -B "$AVIF_BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PREFIX" \
  -DBUILD_SHARED_LIBS=OFF \
  -DAVIF_CODEC_AOM=LOCAL \
  -DAVIF_BUILD_APPS=ON \
  -DAVIF_LIBYUV=OFF

cmake --build "$AVIF_BUILD" --config Release --parallel "$JOBS"
echo "libavif: OK"

echo ""
echo "=== All dependencies built ==="
echo "Static libraries available in: $PREFIX/lib/"
ls -lh "$PREFIX/lib/"*.a 2>/dev/null || true
