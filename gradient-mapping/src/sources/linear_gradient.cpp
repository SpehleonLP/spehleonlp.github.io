// sources/linear_gradient.c
// Linear gradient data source implementation

#include "linear_gradient.h"

void linear_gradient(vec3* dst, const vec3* src, int W, int H,
                     LinearGradientParams params, uint32_t seed) {
    (void)seed; // Not used for linear gradient

    float cos_a = cosf(params.angle);
    float sin_a = sinf(params.angle);

    // Normalize coordinates to [-0.5, 0.5] range centered on image
    float inv_w = 1.0f / (float)W;
    float inv_h = 1.0f / (float)H;

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int idx = y * W + x;

            // Base texture coordinate (normalized to [0,1] then centered)
            float tx = ((float)x + 0.5f) * inv_w - 0.5f;
            float ty = ((float)y + 0.5f) * inv_h - 0.5f;

            // Add source offset if provided (for chaining)
            if (src) {
                tx += src[idx].x;
                ty += src[idx].y;
            }

            // Project onto gradient direction
            float t = (tx * cos_a + ty * sin_a) * params.scale + params.offset;

            // Map to [0, 1] range (fract for repeating)
            t = (t * 0.5f + 0.5f);

            // Store in blue channel, zero for R and G
            dst[idx] = vec3_make(t, t, t);
        }
    }
}
