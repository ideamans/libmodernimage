#!/bin/bash
set -euo pipefail

# Generate cli-compat.json from built artifacts
# Usage: ./scripts/generate-cli-compat.sh [output_path]

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

OUTPUT="${1:-$ROOT_DIR/build/cli-compat.json}"

# Extract versions
LIBMODERNIMAGE_VERSION=$(sed -n 's/.*MODERNIMAGE_VERSION.*"\(.*\)".*/\1/p' "$ROOT_DIR/src/modernimage.c" | head -1)

WEBP_VERSION=$("$ROOT_DIR/deps/libwebp/build/cwebp" -version 2>&1 | head -1 | awk '{print $1}')

AVIFENC_FULL=$("$ROOT_DIR/deps/libavif/build/avifenc" --version 2>&1 | head -1)
# "Version: 1.4.1 (aom [enc/dec]:3.13.2)"
AVIF_LIB_VERSION=$(echo "$AVIFENC_FULL" | sed -n 's/.*Version: \([0-9.]*\).*/\1/p')
AOM_VERSION=$(echo "$AVIFENC_FULL" | sed -n 's/.*aom[^:]*:\([0-9.]*\).*/\1/p')

cat > "$OUTPUT" << JSONEOF
{
  "libmodernimage_version": "${LIBMODERNIMAGE_VERSION}",
  "build_date": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "platform": "$(uname -s)-$(uname -m)",
  "tools": {
    "cwebp": {
      "upstream_version": "${WEBP_VERSION}",
      "binary_equiv_tested": true,
      "stdin_support": {
        "arg": "-- -",
        "note": "requires -- before - to avoid option parsing"
      },
      "output_arg": "-o",
      "key_options": {
        "-q": { "type": "int", "range": [0, 100], "description": "lossy quality" },
        "-lossless": { "type": "flag", "description": "lossless mode" },
        "-m": { "type": "int", "range": [0, 6], "description": "compression method" },
        "-resize": { "type": "int_pair", "description": "width height" },
        "-short": { "type": "flag", "description": "short output format" },
        "-v": { "type": "flag", "description": "verbose" }
      }
    },
    "gif2webp": {
      "upstream_version": "${WEBP_VERSION}",
      "binary_equiv_tested": true,
      "stdin_support": null,
      "output_arg": "-o",
      "key_options": {
        "-q": { "type": "int", "range": [0, 100], "description": "quality" },
        "-lossy": { "type": "flag", "description": "lossy mode" },
        "-mixed": { "type": "flag", "description": "mixed lossy/lossless per frame" },
        "-m": { "type": "int", "range": [0, 6], "description": "compression method" }
      }
    },
    "avifenc": {
      "upstream_version": "${AVIF_LIB_VERSION}",
      "aom_version": "${AOM_VERSION}",
      "binary_equiv_tested": true,
      "stdin_support": {
        "arg": "--stdin",
        "requires": "--input-format <jpeg|png|y4m>"
      },
      "output_arg": "-o",
      "key_options": {
        "-q": { "type": "int", "range": [0, 100], "description": "quality (color+alpha)" },
        "--qcolor": { "type": "int", "range": [0, 100], "description": "color quality" },
        "--qalpha": { "type": "int", "range": [0, 100], "description": "alpha quality" },
        "-s": { "type": "int", "range": [0, 10], "description": "speed" },
        "-l": { "type": "flag", "description": "lossless" },
        "-j": { "type": "int", "description": "worker threads" }
      }
    }
  }
}
JSONEOF

echo "Generated: $OUTPUT"
