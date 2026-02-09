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

echo "=== Fourier low-pass 0.3 → Hessian flow debug ==="
# -o is a prefix: {prefix}output.png, {prefix}hessian.png, etc.
"$CLI" \
    -i "$SCRIPT_DIR/demo-images/fxMapInOut-boost.png" \
    -o "$DEBUG_DIR/hflow_" \
    -s erosion \
    -q 1 \
    -e 0x21:0.77 \
    -e 0x40:1

echo ""
echo "=== LIC with tangent field → split channels ==="
# Fourier low-pass → LIC (tangent field) → split channels
"$CLI" \
    -i "$SCRIPT_DIR/demo-images/fxMapInOut-boost.png" \
    -o "$DEBUG_DIR/lic_" \
    -s erosion \
    -q 1 \
    -e 0x21:0.77 \
    -e 0x42:2.128.128 \
    -e 0x41

echo ""
echo "=== Output files ==="
ls -la "$DEBUG_DIR/"
