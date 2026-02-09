#include "fft_blur.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

float clamp_f32(float, float, float);

// Round up to next power of 2
int next_pow2(int n) {
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

// Bit-reverse an index for FFT
static int bit_reverse(int x, int bits) {
    int result = 0;
    for (int i = 0; i < bits; i++) {
        result = (result << 1) | (x & 1);
        x >>= 1;
    }
    return result;
}

// Count bits needed to represent n-1
static int count_bits(int n) {
    int bits = 0;
    n--;
    while (n > 0) {
        bits++;
        n >>= 1;
    }
    return bits;
}

// In-place 1D FFT on a row/column
// real and imag are pointers to start, stride is distance between elements
static void fft_1d(float* real, float* imag, int n, int stride, int inverse, float amplify) {
    int bits = count_bits(n);

    // Bit-reversal permutation
    for (int i = 0; i < n; i++) {
        int j = bit_reverse(i, bits);
        if (j > i) {
            float tr = real[i * stride];
            float ti = imag[i * stride];
            real[i * stride] = real[j * stride];
            imag[i * stride] = imag[j * stride];
            real[j * stride] = tr;
            imag[j * stride] = ti;
        }
    }

    // Cooley-Tukey butterfly stages
    for (int mmax = 1; mmax < n; mmax <<= 1) {
        float theta = (inverse ? M_PI : -M_PI) / mmax;
        float wpr = cosf(theta);  // Real part of rotation step
        float wpi = sinf(theta);  // Imag part of rotation step

        float wr = 1.0f;  // Start at angle 0 (cos(0) = 1)
        float wi = 0.0f;  // sin(0) = 0

        for (int m = 0; m < mmax; m++) {
            for (int i = m; i < n; i += mmax * 2) {
                int j = i + mmax;
                float tr = wr * real[j * stride] - wi * imag[j * stride];
                float ti = wr * imag[j * stride] + wi * real[j * stride];

                real[j * stride] = real[i * stride] - tr;
                imag[j * stride] = imag[i * stride] - ti;
                real[i * stride] += tr;
                imag[i * stride] += ti;
            }

            // Rotate twiddle factor by theta: (wr + i*wi) *= (wpr + i*wpi)
            float temp = wr;
            wr = wr * wpr - wi * wpi;
            wi = temp * wpi + wi * wpr;
        }
    }

    // Scale for inverse FFT
    if (inverse) {
        float scale = (1.0f + amplify) / n;
        for (int i = 0; i < n; i++) {
            real[i * stride] *= scale;
            imag[i * stride] *= scale;
        }
    }
}

// 2D FFT (forward or inverse)
static void fft_2d(float* real, float* imag, int width, int height, int inverse, float amplify) {
    // Transform rows
    for (int y = 0; y < height; y++) {
        fft_1d(real + y * width, imag + y * width, width, 1, inverse, amplify);
    }

    // Transform columns
    for (int x = 0; x < width; x++) {
        fft_1d(real + x, imag + x, height, width, inverse, amplify);
    }
}

// Bilinear sample from source image
static float bilinear_sample(float* data, uint8_t* original, int w, int h, int stride,
                             int channel, float x, float y, uint8_t* out_orig) {
    // Clamp coordinates
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > w - 1) x = (float)(w - 1);
    if (y > h - 1) y = (float)(h - 1);

    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    if (x1 >= w) x1 = w - 1;
    if (y1 >= h) y1 = h - 1;

    float fx = x - x0;
    float fy = y - y0;

    // Sample the four corners
    float v00, v10, v01, v11;
    uint8_t o00, o10, o01, o11;

    if (data) {
        v00 = data[(y0 * w + x0) * stride + channel];
        v10 = data[(y0 * w + x1) * stride + channel];
        v01 = data[(y1 * w + x0) * stride + channel];
        v11 = data[(y1 * w + x1) * stride + channel];
    } else {
        // Use original as source
        v00 = original ? original[(y0 * w + x0) * stride + channel] : 0;
        v10 = original ? original[(y0 * w + x1) * stride + channel] : 0;
        v01 = original ? original[(y1 * w + x0) * stride + channel] : 0;
        v11 = original ? original[(y1 * w + x1) * stride + channel] : 0;
    }

    if (original) {
        o00 = original[(y0 * w + x0) * stride + channel];
        o10 = original[(y0 * w + x1) * stride + channel];
        o01 = original[(y1 * w + x0) * stride + channel];
        o11 = original[(y1 * w + x1) * stride + channel];
    } else if (data) {
        o00 = (uint8_t)floorf(v00);
        o10 = (uint8_t)floorf(v10);
        o01 = (uint8_t)floorf(v01);
        o11 = (uint8_t)floorf(v11);
    } else {
        o00 = o10 = o01 = o11 = 0;
    }

    // Bilinear interpolation
    float val = v00 * (1 - fx) * (1 - fy)
              + v10 * fx * (1 - fy)
              + v01 * (1 - fx) * fy
              + v11 * fx * fy;

    // Determine original value: 255 if interpolated across different values
    if (out_orig) {
        if (o00 != 255 && o00 == o10 && o00 == o01 && o00 == o11) {
            *out_orig = o00;
        } else {
            *out_orig = 255;  // Interpolated across multiple values
        }
    }

    return val;
}

int fft_ResizeImage(ResizingImage* dst, ResizingImage* src) {
    if (!dst || !src || (dst->width == 0) || (dst->height == 0)) return 0;
    if (!src->data && !src->original) return 0;

    int src_w = src->width;
    int src_h = src->height;
    int dst_w = dst->width;
    int dst_h = dst->height;

    // Assume 4 channels (RGBA)
    int stride = 4;

    // Allocate destination if needed
    if (!dst->data) {
        dst->data = malloc(dst_w * dst_h * stride * sizeof(float));
        if (!dst->data) return 0;
    }
    if (!dst->original) {
        dst->original = malloc(dst_w * dst_h * stride);
        if (!dst->original) {
            free(dst->data);
            dst->data = NULL;
            return 0;
        }
    }

    // Bilinear upsample each pixel
    for (int y = 0; y < dst_h; y++) {
        for (int x = 0; x < dst_w; x++) {
            // Map to source coordinates
            float src_x = (float)x * (src_w - 1) / (dst_w - 1);
            float src_y = (float)y * (src_h - 1) / (dst_h - 1);

            int dst_idx = (y * dst_w + x) * stride;

            for (int c = 0; c < stride; c++) {
                uint8_t orig;
                float val = bilinear_sample(src->data, src->original,
                                            src_w, src_h, stride, c,
                                            src_x, src_y, &orig);
                dst->data[dst_idx + c] = val;
                dst->original[dst_idx + c] = orig;
            }
        }
    }

    return 1;
}

int fft_Initialize(FFTBlurContext* ctx, int width, int height) {
    if (!ctx || width <= 0 || height <= 0) return -1;

    int size = width * height;

    memset(ctx, 0, sizeof(*ctx));
    ctx->width = width;
    ctx->height = height;

    // Allocate buffers
    ctx->real = malloc(size * sizeof(float));
    ctx->imag = calloc(size, sizeof(float));  // Zero-initialized
    ctx->original = malloc(size);

    if (!ctx->real || !ctx->imag || !ctx->original) {
        fft_Free(ctx);
        return -1;
    }

    return 0;
}

void fft_LoadChannel(FFTBlurContext* ctx, ResizingImage* image, int stride, int channel) {
    if (!ctx || !image || !ctx->real || !ctx->original) return;
    if (channel < 0 || channel >= stride) return;
    if (!image->data && !image->original) return;
    if ((int)image->width != ctx->width || (int)image->height != ctx->height) return;

    int W = ctx->width;
    int H = ctx->height;

    // Copy channel data into real buffer, zero imag
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int src_idx = (y * W + x) * stride + channel;
            int dst_idx = y * W + x;

            if (image->data) {
                ctx->real[dst_idx] = image->data[src_idx];
            } else {
                ctx->real[dst_idx] = image->original[src_idx];
            }

            ctx->imag[dst_idx] = 0.0f;

            if (image->original) {
                ctx->original[dst_idx] = image->original[src_idx];
            } else {
                ctx->original[dst_idx] = (uint8_t)floorf(ctx->real[dst_idx]);
            }
        }
    }
}

void fft_LowPassFilter(FFTBlurContext* ctx, float cutoff_ratio, int flip_around) {
    if (!ctx || !ctx->real || !ctx->imag) return;

	printf("in low pass filter %f %d\n", cutoff_ratio, flip_around);

    int W = ctx->width;
    int H = ctx->height;

    // Forward FFT
    fft_2d(ctx->real, ctx->imag, W, H, 0, 0.f);

    // Apply low-pass filter in frequency domain
    float cutoff_x = (W * 0.5f) * cutoff_ratio;
    float cutoff_y = (H * 0.5f) * cutoff_ratio;

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            // Get frequency coordinates (distance from DC)
            int half_W = W / 2;
            int half_H = H / 2;
            float fx = (x <= half_W) ? (float)x : (float)(W - x);
            float fy = (y <= half_H) ? (float)y : (float)(H - y);

            // Smooth rolloff using Butterworth filter
            float dist_x = fx / cutoff_x;
            float dist_y = fy / cutoff_y;
            float dist_sq = dist_x * dist_x + dist_y * dist_y;

            // Butterworth-style filter for smoother rolloff
            float filter = 1.0f / (1.0f + dist_sq * dist_sq);

			if(flip_around)
				filter = 1.0 - filter;

            int idx = y * W + x;
            ctx->real[idx] *= filter;
            ctx->imag[idx] *= filter;
        }
    }

    // Inverse FFT
    fft_2d(ctx->real, ctx->imag, W, H, 1, flip_around? 8 : 0);
}

void fft_CopyBackToImage(ResizingImage* image, FFTBlurContext* ctx, int stride, int channel) {
    if (!image || !ctx || !ctx->real) return;
    if (channel < 0 || channel >= stride) return;
    if (!image->data) return;

    int W = ctx->width;
    int H = ctx->height;

    // Sanity check dimensions
    if ((int)image->width != W || (int)image->height != H) return;

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int src_idx = y * W + x;
            int dst_idx = (y * W + x) * stride + channel;

            image->data[dst_idx] = clamp_f32(ctx->real[src_idx], 0.0, 1.f);
        }
    }
}

void fft_Free(FFTBlurContext* ctx) {
    if (!ctx) return;
    free(ctx->real);
    free(ctx->imag);
    free(ctx->original);
    ctx->real = NULL;
    ctx->imag = NULL;
    ctx->original = NULL;
}
