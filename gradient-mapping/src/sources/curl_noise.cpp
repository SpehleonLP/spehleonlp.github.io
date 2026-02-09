// sources/curl_noise.c
// Curl noise data source implementation

#include "curl_noise.h"

// Forward declaration - we use the same Perlin noise internally
// Copy the perlin_single function here to avoid cross-file dependencies

static vec2 perlin_gradient_curl(int x, int y, uint32_t seed) {
    float angle = hash2d(x, y, seed) * 6.28318530718f;
    return vec2_make(cosf(angle), sinf(angle));
}

static float perlin_single_curl(float x, float y, uint32_t seed) {
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    float fx = x - (float)x0;
    float fy = y - (float)y0;

    float u = quintic(fx);
    float v = quintic(fy);

    vec2 g00 = perlin_gradient_curl(x0, y0, seed);
    vec2 g10 = perlin_gradient_curl(x1, y0, seed);
    vec2 g01 = perlin_gradient_curl(x0, y1, seed);
    vec2 g11 = perlin_gradient_curl(x1, y1, seed);

    vec2 d00 = vec2_make(fx, fy);
    vec2 d10 = vec2_make(fx - 1.0f, fy);
    vec2 d01 = vec2_make(fx, fy - 1.0f);
    vec2 d11 = vec2_make(fx - 1.0f, fy - 1.0f);

    float n00 = vec2_dot(g00, d00);
    float n10 = vec2_dot(g10, d10);
    float n01 = vec2_dot(g01, d01);
    float n11 = vec2_dot(g11, d11);

    float nx0 = lerpf(n00, n10, u);
    float nx1 = lerpf(n01, n11, u);
    return lerpf(nx0, nx1, v);
}

// fBm (fractal) Perlin noise
static float fbm_perlin(float x, float y, int octaves, float persistence,
                        float lacunarity, uint32_t seed) {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float max_amplitude = 0.0f;

    for (int o = 0; o < octaves; o++) {
        value += amplitude * perlin_single_curl(x * frequency, y * frequency, seed + (uint32_t)o * 12345);
        max_amplitude += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return value / max_amplitude;
}

void curl_noise(vec3* dst, const vec3* src, int W, int H,
                CurlNoiseParams params, uint32_t seed) {

    float inv_w = 1.0f / (float)W;
    float inv_h = 1.0f / (float)H;

    int num_octaves = (int)params.octaves;
    if (num_octaves < 1) num_octaves = 1;
    if (num_octaves > 16) num_octaves = 16;

    // Epsilon for finite differences (in scaled space)
    float eps = 0.01f;

    // First pass: compute curl values and store in Z channel
    for (int py = 0; py < H; py++) {
        for (int px = 0; px < W; px++) {
            int idx = py * W + px;

            // Base texture coordinate
            float tx = ((float)px + 0.5f) * inv_w * params.scale;
            float ty = ((float)py + 0.5f) * inv_h * params.scale;

            // Add source offset if provided
            if (src) {
                tx += src[idx].x * params.scale;
                ty += src[idx].y * params.scale;
            }

            // Compute curl using finite differences
            // For 2D, we use two noise functions offset by seed
            // Curl = (∂n2/∂x - ∂n1/∂y)

            // First noise field - only need Y gradient
            float n1_py = fbm_perlin(tx, ty + eps, num_octaves, params.persistence,
                                     params.lacunarity, seed);
            float n1_ny = fbm_perlin(tx, ty - eps, num_octaves, params.persistence,
                                     params.lacunarity, seed);

            // Gradient of first noise field (Y direction)
            float dn1_dy = (n1_py - n1_ny) / (2.0f * eps);

            // Second noise field (different seed)
            float n2_px = fbm_perlin(tx + eps, ty, num_octaves, params.persistence,
                                     params.lacunarity, seed + 99999);
            float n2_nx = fbm_perlin(tx - eps, ty, num_octaves, params.persistence,
                                     params.lacunarity, seed + 99999);

            // Gradient of second noise field
            float dn2_dx = (n2_px - n2_nx) / (2.0f * eps);

            // Curl (z-component in 2D)
            float curl = dn2_dx - dn1_dy;

            // Map to [0, 1] (curl is typically in [-1, 1] range)
            float value = curl * 0.5f + 0.5f;
            value = clampf(value, 0.0f, 1.0f);

            // Store value (gradients computed in second pass)
            dst[idx].z = value;
        }
    }

    // Second pass: compute gradients of curl field using finite differences
    for (int py = 0; py < H; py++) {
        for (int px = 0; px < W; px++) {
            int idx = py * W + px;

            // Neighbor indices with clamping at boundaries
            int xm = (px > 0) ? px - 1 : 0;
            int xp = (px < W - 1) ? px + 1 : W - 1;
            int ym = (py > 0) ? py - 1 : 0;
            int yp = (py < H - 1) ? py + 1 : H - 1;

            float v_xm = dst[py * W + xm].z;
            float v_xp = dst[py * W + xp].z;
            float v_ym = dst[ym * W + px].z;
            float v_yp = dst[yp * W + px].z;

            // Central difference (scale by 0.25 for reasonable magnitude)
            float dx = (v_xp - v_xm) * 0.25f;
            float dy = (v_yp - v_ym) * 0.25f;

            dst[idx].x = dx;
            dst[idx].y = dy;
        }
    }
}
