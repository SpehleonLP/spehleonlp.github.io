#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/src/build_cli"

echo "=== Building native CLI ==="
cmake -S "$SCRIPT_DIR/src" -B "$BUILD_DIR"
make -C "$BUILD_DIR" -j$(nproc)

CLI="$BUILD_DIR/effect_stack_cli"

echo ""
echo "=== Running erosion stack test ==="
# Dijkstra with Minkowski=1 (param 140) and Chebyshev=0 (param 0)
# Params separated by '.' to avoid conflicts with shell/cxxopts comma handling
"$CLI" \
    -i "$SCRIPT_DIR/demo-images/boom_texture.png" \
    -o "$SCRIPT_DIR/test_output.png" \
    -s erosion \
    -d "$SCRIPT_DIR" \
    -q 0 \
    -e 0x20:140.0

echo ""
echo "=== Done ==="
ls -la "$SCRIPT_DIR/test_output.png"

# Other example invocations:
#
# Gradient stack (no source needed for procedural, but API still requires one):
# "$CLI" -i demo-images/default_gradient.png -o gradient_out.png -s gradient \
#        -e 0x10:128.128.128
#
# Erosion with box blur: iterations=32->142, threshold=0.01->85
# "$CLI" -i demo-images/boom_texture.png -o blurred.png -e 0x22:142.85
#
# Erosion with dijkstra then fourier clamp:
# "$CLI" -i demo-images/boom_texture.png -o filtered.png \
#        -e 0x20:140.0 -e 0x21:0.38
