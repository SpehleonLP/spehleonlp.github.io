// sources/perlin_noise.c
// Perlin (gradient) noise data source implementation

#include "perlin_noise.h"

// Generate a random unit gradient vector for a grid point
static vec2 perlin_gradient(int x, int y, uint32_t seed) {
    // Hash to get an angle
    float angle = hash2d(x, y, seed) * 6.28318530718f;  // 2*PI
    return vec2_make(cosf(angle), sinf(angle));
}

// Single octave of Perlin noise with analytical derivatives
// Returns vec3(dvalue/dx, dvalue/dy, value)
static vec3 perlin_single_with_deriv(float x, float y, uint32_t seed) {
    // Grid cell coordinates
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    // Local position within cell [0, 1)
    float fx = x - (float)x0;
    float fy = y - (float)y0;

    // Interpolation weights and their derivatives
    float u = quintic(fx);
    float v = quintic(fy);
    float du = quintic_deriv(fx);
    float dv = quintic_deriv(fy);

    // Gradients at corners
    vec2 g00 = perlin_gradient(x0, y0, seed);
    vec2 g10 = perlin_gradient(x1, y0, seed);
    vec2 g01 = perlin_gradient(x0, y1, seed);
    vec2 g11 = perlin_gradient(x1, y1, seed);

    // Dot products: n = g · d where d is offset from corner to point
    // n00 = g00.x * fx + g00.y * fy
    // dn00/dx = g00.x, dn00/dy = g00.y
    float n00 = g00.x * fx + g00.y * fy;
    float n10 = g10.x * (fx - 1.0f) + g10.y * fy;
    float n01 = g01.x * fx + g01.y * (fy - 1.0f);
    float n11 = g11.x * (fx - 1.0f) + g11.y * (fy - 1.0f);

    // Bilinear interpolation for value
    float nx0 = lerp(n00, n10, u);
    float nx1 = lerp(n01, n11, u);
    float value = lerp(nx0, nx1, v);

    // Analytical derivatives using chain rule
    // d(nx0)/dx = lerp(g00.x, g10.x, u) + du * (n10 - n00)
    // d(nx1)/dx = lerp(g01.x, g11.x, u) + du * (n11 - n01)
    float dnx0_dx = lerp(g00.x, g10.x, u) + du * (n10 - n00);
    float dnx1_dx = lerp(g01.x, g11.x, u) + du * (n11 - n01);
    float dvalue_dx = lerp(dnx0_dx, dnx1_dx, v);

    // d(nx0)/dy = lerp(g00.y, g10.y, u)
    // d(nx1)/dy = lerp(g01.y, g11.y, u)
    // d(value)/dy = lerp(dnx0_dy, dnx1_dy, v) + dv * (nx1 - nx0)
    float dnx0_dy = lerp(g00.y, g10.y, u);
    float dnx1_dy = lerp(g01.y, g11.y, u);
    float dvalue_dy = lerp(dnx0_dy, dnx1_dy, v) + dv * (nx1 - nx0);

    return vec3_make(dvalue_dx, dvalue_dy, value);
}

void perlin_noise(vec3* dst, const vec3* src, int W, int H,
                  PerlinNoiseParams params, uint32_t seed) {

    float inv_w = 1.0f / (float)W;
    float inv_h = 1.0f / (float)H;

    int num_octaves = (int)params.octaves;
    if (num_octaves < 1) num_octaves = 1;
    if (num_octaves > 16) num_octaves = 16;

    for (int py = 0; py < H; py++) {
        for (int px = 0; px < W; px++) {
            int idx = py * W + px;

            // Base texture coordinate
            float tx = ((float)px + 0.5f) * inv_w * params.scale;
            float ty = ((float)py + 0.5f) * inv_h * params.scale;

            // Add source offset if provided
            if (src) {
                tx += src[idx].x;
                ty += src[idx].y;
            }

            // Accumulate octaves (fBm) with derivatives
            float value = 0.0f;
            float deriv_x = 0.0f;
            float deriv_y = 0.0f;
            float amplitude = 1.0f;
            float frequency = 1.0f;
            float max_amplitude = 0.0f;

            for (int o = 0; o < num_octaves; o++) {
                vec3 result = perlin_single_with_deriv(
                    tx * frequency, ty * frequency,
                    seed + (uint32_t)o * 12345
                );

                value += amplitude * result.z;
                // Derivatives are scaled by frequency (chain rule) and amplitude
                deriv_x += amplitude * frequency * result.x;
                deriv_y += amplitude * frequency * result.y;

                max_amplitude += amplitude;
                amplitude *= params.persistence;
                frequency *= params.lacunarity;
            }

            // Normalize value to [-1, 1] then map to [0, 1]
            float inv_max = 1.0f / max_amplitude;
            value = value * inv_max;
            value = value * 0.5f + 0.5f;
            value = clampf(value, 0.0f, 1.0f);

            // Normalize derivatives (scale matches value normalization)
            // Derivatives are in [-1, 1] range, scale to reasonable magnitude
            deriv_x = deriv_x * inv_max * 0.5f;
            deriv_y = deriv_y * inv_max * 0.5f;

            dst[idx] = vec3_make(deriv_x, deriv_y, value);
        }
    }
}
