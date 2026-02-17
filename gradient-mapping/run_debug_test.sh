#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/src/build_cli"
CLI="$BUILD_DIR/effect_stack_cli"
DEBUG_DIR="$SCRIPT_DIR/debug_output"

# Build if needed
if [ ! -f "$CLI" ]; then
    echo "=== Building native CLI ==="
    cmake -S "$SCRIPT_DIR/src" -B "$BUILD_DIR"
    make -C "$BUILD_DIR" -j$(nproc)
fi

mkdir -p "$DEBUG_DIR"

echo ""
echo "=== Fourier low-pass 0.3 → Ridge mesh extraction ==="
# Low-pass filter first to remove 8-bit quantization noise,
# then extract ridge/valley mesh.
# 0x21:0.77 = Fourier low-pass at 0.3
# 0x44:51.64.26 = Ridge mesh: F=0.01, high_thresh=0.25, low_thresh=0.10
"$CLI" \
    -i "$SCRIPT_DIR/demo-images/fxMapInOut-boost.png" \
    -o "$DEBUG_DIR/rmesh_" \
    -s erosion \
    -q 1 \
    -e 0x21:0.77 \
    -e 0x44:51.64.26

echo ""
echo "=== Output files ==="
ls -la "$DEBUG_DIR/"
