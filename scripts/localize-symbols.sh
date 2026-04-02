#!/bin/bash
# Localize all symbols in a fat .a except modernimage_* public API.
# Usage: localize-symbols.sh <input.a> <output.a> [arch]

set -euo pipefail

INPUT_A="$1"
OUTPUT_A="$2"
ARCH="${3:-$(uname -m)}"

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

if [ "$(uname -s)" = "Darwin" ]; then
  # macOS: nm prefixes symbols with _, so grep for _modernimage_
  nm "$INPUT_A" | grep " T " | grep "_modernimage_" | awk '{print $3}' | sort -u > "$WORK_DIR/exports.txt"

  # ld -r with -exported_symbols_list makes everything else local.
  MACOS_VER=$(sw_vers -productVersion | cut -d. -f1-2)
  ld -r -arch "$ARCH" \
    -platform_version macos "$MACOS_VER" "$MACOS_VER" \
    -exported_symbols_list "$WORK_DIR/exports.txt" \
    "$INPUT_A" \
    -o "$WORK_DIR/combined.o"
  ar rcs "$OUTPUT_A" "$WORK_DIR/combined.o"
  ranlib -no_warning_for_no_symbols "$OUTPUT_A"
else
  # Linux/Windows: use ld -r to merge all objects (resolves internal cross-references),
  # then localize non-public symbols via objcopy.
  # This mirrors the macOS approach and avoids the issue where objcopy --localize-symbols
  # on an archive makes dependency symbols invisible to internal references.

  # Extract public symbol names (no _ prefix on Linux)
  nm "$INPUT_A" | grep " T " | awk '{print $3}' | \
    grep "^modernimage_" | sort -u > "$WORK_DIR/keep-global.txt"

  # Merge all objects into a single relocatable .o (resolves internal refs)
  ld -r --whole-archive "$INPUT_A" -o "$WORK_DIR/combined.o"

  # Localize everything except modernimage_* public API
  nm "$WORK_DIR/combined.o" | grep " T " | awk '{print $3}' | \
    grep -v "^modernimage_" > "$WORK_DIR/localize.txt"

  objcopy --localize-symbols="$WORK_DIR/localize.txt" "$WORK_DIR/combined.o"

  ar rcs "$OUTPUT_A" "$WORK_DIR/combined.o"
  ranlib "$OUTPUT_A"
fi

GLOBAL_COUNT=$(nm "$OUTPUT_A" 2>/dev/null | grep " T " | wc -l | tr -d ' ')
echo "Created: $OUTPUT_A ($GLOBAL_COUNT global symbols)"
