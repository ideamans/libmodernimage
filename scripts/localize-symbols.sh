#!/bin/bash
# Localize all symbols in a fat .a except modernimage_* public API.
# Usage: localize-symbols.sh <input.a> <output.a> [arch]

set -euo pipefail

INPUT_A="$1"
OUTPUT_A="$2"
ARCH="${3:-$(uname -m)}"

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# Generate exported symbols list
nm "$INPUT_A" | grep " T " | grep "_modernimage_" | awk '{print $3}' | sort -u > "$WORK_DIR/exports.txt"

if [ "$(uname -s)" = "Darwin" ]; then
  # macOS: ld -r links the archive normally (resolving symbols as needed),
  # -exported_symbols_list makes everything else local.
  # We add an undefined entry point to pull in the first modernimage object,
  # which then transitively pulls in all needed dependencies.
  MACOS_VER=$(sw_vers -productVersion | cut -d. -f1-2)
  ld -r -arch "$ARCH" \
    -platform_version macos "$MACOS_VER" "$MACOS_VER" \
    -exported_symbols_list "$WORK_DIR/exports.txt" \
    "$INPUT_A" \
    -o "$WORK_DIR/combined.o"
  ar rcs "$OUTPUT_A" "$WORK_DIR/combined.o"
  ranlib -no_warning_for_no_symbols "$OUTPUT_A"
else
  # Linux/Windows: localize symbols directly on the archive via objcopy
  cp "$INPUT_A" "$OUTPUT_A"

  nm "$OUTPUT_A" | grep " T " | awk '{print $3}' | \
    grep -v "^_\?modernimage_" > "$WORK_DIR/localize.txt"

  objcopy --localize-symbols="$WORK_DIR/localize.txt" "$OUTPUT_A"
  ranlib "$OUTPUT_A"
fi

GLOBAL_COUNT=$(nm "$OUTPUT_A" 2>/dev/null | grep " T " | wc -l | tr -d ' ')
echo "Created: $OUTPUT_A ($GLOBAL_COUNT global symbols)"
