// sources/noise.c
// Simple noise data source implementation

#include "noise.h"
#include "../commands/fft_blur.h"
#include <stdlib.h>

// PCG32 random number generator
typedef struct {
    uint64_t state;
    uint64_t inc;
} pcg32_t;

static uint32_t pcg32_next(pcg32_t* rng) {
    uint64_t oldstate = rng->state;
    rng->state = oldstate * 6364136223846793005ULL + rng->inc;
    uint32_t xorshifted = (uint32_t)(((oldstate >> 18u) ^ oldstate) >> 27u);
    uint32_t rot = (uint32_t)(oldstate >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31));
}

static void pcg32_seed(pcg32_t* rng, uint64_t seed) {
    rng->state = 0U;
    rng->inc = (seed << 1u) | 1u;
    pcg32_next(rng);
    rng->state += seed;
    pcg32_next(rng);
}

static float pcg32_float(pcg32_t* rng) {
    return (float)(pcg32_next(rng) >> 8) / 16777216.0f;  // [0, 1)
}

// White noise - pure random values using PCG
static void noise_white(vec3* dst, const vec3* src, int W, int H, uint32_t seed) {
    (void)src;  // White noise ignores source perturbation

    pcg32_t rng;
    pcg32_seed(&rng, seed);

    int size = W * H;
    for (int i = 0; i < size; i++) {
		float r = pcg32_float(&rng);
        dst[i] = vec3_make(r, pcg32_float(&rng), r);
    }
}

// Blue noise - generated directly in frequency domain
// Power spectrum P(f) ∝ f (energy increases with frequency, no low-freq content)
static void noise_blue(vec3* dst, const vec3* src, int W, int H, uint32_t seed) {
    (void)src; // Blue noise doesn't use source perturbation

    if (W <= 0 || H <= 0) return;

    int fft_w = next_pow2(W);
    int fft_h = next_pow2(H);

    // next_pow2 guarantees fft_w >= W and fft_h >= H
    if (fft_w < W || fft_h < H) return;
    int fft_size = fft_w * fft_h;

    float* real = (float*)calloc(fft_size, sizeof(float));
    float* imag = (float*)calloc(fft_size, sizeof(float));
    if (!real || !imag) {
        free(real);
        free(imag);
        noise_white(dst, NULL, W, H, seed); // Fallback
        return;
    }

    // Fill frequency domain directly with blue noise spectrum
    // For each frequency, amplitude ∝ distance from DC (high-pass shape)
    float half_w = fft_w * 0.5f;
    float half_h = fft_h * 0.5f;
    float max_dist = sqrtf(half_w * half_w + half_h * half_h);

    pcg32_t rng;
    pcg32_seed(&rng, seed);

    for (int fy = 0; fy < fft_h; fy++) {
        for (int fx = 0; fx < fft_w; fx++) {
            int idx = fy * fft_w + fx;

            // Frequency distance from DC (DC is at corners in non-shifted FFT)
            // Map to centered coordinates
            float freq_x = (fx < fft_w / 2) ? (float)fx : (float)(fx - fft_w);
            float freq_y = (fy < fft_h / 2) ? (float)fy : (float)(fy - fft_h);
            float dist = sqrtf(freq_x * freq_x + freq_y * freq_y);

            // Blue noise: amplitude increases with frequency
            // Normalize distance and use as amplitude
            float amplitude = dist / max_dist;

            // Random phase for this frequency
            float phase = pcg32_float(&rng) * 6.28318530718f; // 2*PI

            real[idx] = amplitude * cosf(phase);
            imag[idx] = amplitude * sinf(phase);
        }
    }

    // Zero DC component (no constant offset)
    real[0] = 0.0f;
    imag[0] = 0.0f;

    // Inverse FFT to get spatial domain
    fft_2d(real, imag, fft_w, fft_h, 1, 1.0f);

    // Find min/max for normalization
    float min_val = real[0];
    float max_val = real[0];
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float v = real[y * fft_w + x];
            if (v < min_val) min_val = v;
            if (v > max_val) max_val = v;
        }
    }

    // Normalize to [0, 1] and copy to output with gradients
    float range = max_val - min_val;
    if (range < 1e-6f) range = 1.0f;
    float inv_range = 1.0f / range;

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float v = (real[y * fft_w + x] - min_val) * inv_range;

            // Compute gradients using central differences
            // Use fft_w stride for correct indexing into the padded buffer
            int xm = (x > 0) ? x - 1 : 0;
            int xp = (x < W - 1) ? x + 1 : W - 1;
            int ym = (y > 0) ? y - 1 : 0;
            int yp = (y < H - 1) ? y + 1 : H - 1;

            float v_xm = (real[y * fft_w + xm] - min_val) * inv_range;
            float v_xp = (real[y * fft_w + xp] - min_val) * inv_range;
            float v_ym = (real[ym * fft_w + x] - min_val) * inv_range;
            float v_yp = (real[yp * fft_w + x] - min_val) * inv_range;

            // Central difference (scale by 0.5 for reasonable magnitude)
            float dx = (v_xp - v_xm) * 0.25f;
            float dy = (v_yp - v_ym) * 0.25f;

            dst[y * W + x] = vec3_make(dx, dy, clampf(v, 0.0f, 1.0f));
        }
    }

    free(real);
    free(imag);
}

// Value noise - bilinearly interpolated grid noise
static void noise_value(vec3* dst, const vec3* src, int W, int H,
                        float scale, uint32_t seed) {
    float inv_w = 1.0f / (float)W;
    float inv_h = 1.0f / (float)H;

    for (int py = 0; py < H; py++) {
        for (int px = 0; px < W; px++) {
            int idx = py * W + px;

            // Base texture coordinate scaled
            float tx = ((float)px + 0.5f) * inv_w * scale;
            float ty = ((float)py + 0.5f) * inv_h * scale;

            // Add source offset if provided
            if (src) {
                tx += src[idx].x * scale;
                ty += src[idx].y * scale;
            }

            // Grid cell
            int x0 = (int)floorf(tx);
            int y0 = (int)floorf(ty);
            int x1 = x0 + 1;
            int y1 = y0 + 1;

            // Fractional position
            float fx = tx - (float)x0;
            float fy = ty - (float)y0;

            // Smooth interpolation weights and derivatives
            float u = smoothstep(fx);
            float v = smoothstep(fy);
            float du = smoothstep_deriv(fx);
            float dv = smoothstep_deriv(fy);

            // Random values at corners
            float v00 = hash2d(x0, y0, seed);
            float v10 = hash2d(x1, y0, seed);
            float v01 = hash2d(x0, y1, seed);
            float v11 = hash2d(x1, y1, seed);

            // Bilinear interpolation for value
            float vx0 = lerp(v00, v10, u);
            float vx1 = lerp(v01, v11, u);
            float value = lerp(vx0, vx1, v);

            // Analytical gradients
            // dvalue/dx = du * (lerp(v10, v11, v) - lerp(v00, v01, v))
            // dvalue/dy = dv * (vx1 - vx0)
            float vy0 = lerp(v00, v01, v);
            float vy1 = lerp(v10, v11, v);
            float dvalue_dx = du * (vy1 - vy0);
            float dvalue_dy = dv * (vx1 - vx0);

            dst[idx] = vec3_make(dvalue_dx * 0.5f, dvalue_dy * 0.5f, value);
        }
    }
}

void noise_generate(vec3* dst, const vec3* src, int W, int H,
                    NoiseParams params, uint32_t seed) {
    // Combine provided seed with params.seed
    uint32_t combined_seed = seed ^ (uint32_t)params.seed;

    switch (params.type) {
    case NOISE_TYPE_WHITE:
        noise_white(dst, src, W, H, combined_seed);
        break;
    case NOISE_TYPE_BLUE:
        noise_blue(dst, src, W, H, combined_seed);
        break;
    case NOISE_TYPE_VALUE:
        noise_value(dst, src, W, H, params.scale, combined_seed);
        break;
    default:
        noise_white(dst, src, W, H, combined_seed);
        break;
    }
}
