#include "normal_from_hessian_cmd.h"
#include "fft_blur.h"
#include <float.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * Poisson solve in frequency domain with initial guess
 *
 * Solves: ∇²h = L (Laplacian)
 *
 * The initial guess provides the DC component and low-frequency bias.
 * We compute: h = h_guess + Poisson_solve(L - ∇²h_guess)
 *
 * This preserves the overall shape of the initial guess while
 * adjusting it to match the target Laplacian.
 */
static void poisson_solve_freq(float* real, float* imag, int W, int H,
                                float dc_value) {
    float inv_W = 1.0f / W;
    float inv_H = 1.0f / H;

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float fx = (x <= W/2) ? (float)x * inv_W : (float)(x - W) * inv_W;
            float fy = (y <= H/2) ? (float)y * inv_H : (float)(y - H) * inv_H;

            float freq_sq = fx * fx + fy * fy;
            int idx = y * W + x;

            if (freq_sq < 1e-10f) {
                // DC component: use provided value
                real[idx] = dc_value;
                imag[idx] = 0.0f;
            } else {
                // Divide by -4π²(fx² + fy²)
                float denom = -4.0f * (float)(M_PI * M_PI) * freq_sq;
                float inv_denom = 1.0f / denom;
                real[idx] *= inv_denom;
                imag[idx] *= inv_denom;
            }
        }
    }
}

int normal_from_hessian_Execute(NormalFromHessianCmd* cmd) {
    if (!cmd) {
        printf("[normal_from_hessian_Execute] Error: cmd is NULL\n");
        return -1;
    }

    if (!cmd->hessian) {
        printf("[normal_from_hessian_Execute] Error: hessian is NULL\n");
        return -1;
    }

    uint32_t W = cmd->W;
    uint32_t H = cmd->H;
    uint32_t size = W * H;

    float scale = cmd->scale;
    if (scale <= 0.0f) scale = 1.0f;

    // Allocate normals output if not provided
    int allocated_normals = 0;
    if (!cmd->normals) {
        cmd->normals = (vec3*)malloc(size * sizeof(vec3));
        if (!cmd->normals) {
            printf("[normal_from_hessian_Execute] Error: failed to allocate normals\n");
            return -1;
        }
        allocated_normals = 1;
    }

    // Allocate height buffer
    int allocated_height = 0;
    float* height = cmd->height;
    if (!height) {
        height = (float*)malloc(size * sizeof(float));
        if (!height) {
            printf("[normal_from_hessian_Execute] Error: failed to allocate height\n");
            if (allocated_normals) {
                free(cmd->normals);
                cmd->normals = NULL;
            }
            return -1;
        }
        allocated_height = 1;
    }

    // Compute Laplacian = trace(H) = Hxx + Hyy
    float* laplacian = (float*)malloc(size * sizeof(float));
    if (!laplacian) {
        printf("[normal_from_hessian_Execute] Error: failed to allocate laplacian\n");
        if (allocated_height) free(height);
        if (allocated_normals) {
            free(cmd->normals);
            cmd->normals = NULL;
        }
        return -1;
    }

    // Compute target Laplacian from Hessian
    // If we have an initial guess (orig_height), solve for correction:
    //   target_laplacian = Hxx + Hyy
    //   guess_laplacian = ∇²(orig_height)
    //   residual = target_laplacian - guess_laplacian
    //   ∇²correction = residual
    //   final_height = orig_height + correction
    float dc_value = 0.0f;
    int use_initial_guess = (cmd->orig_height != NULL);

    if (use_initial_guess) {
        // Compute target Laplacian
        for (uint32_t i = 0; i < size; i++) {
            laplacian[i] = cmd->hessian[i].xx + cmd->hessian[i].yy;
        }

        // Compute Laplacian of initial guess and subtract
        for (uint32_t y = 0; y < H; y++) {
            for (uint32_t x = 0; x < W; x++) {
                uint32_t idx = y * W + x;

                // Laplacian using central differences (clamped at edges)
                uint32_t xm = (x > 0) ? x - 1 : 0;
                uint32_t xp = (x < W - 1) ? x + 1 : W - 1;
                uint32_t ym = (y > 0) ? y - 1 : 0;
                uint32_t yp = (y < H - 1) ? y + 1 : H - 1;

                float d2x = cmd->orig_height[y * W + xp] - 2.0f * cmd->orig_height[idx] + cmd->orig_height[y * W + xm];
                float d2y = cmd->orig_height[yp * W + x] - 2.0f * cmd->orig_height[idx] + cmd->orig_height[ym * W + x];
                float guess_laplacian = d2x + d2y;

                // Residual = target - guess
                laplacian[idx] -= guess_laplacian;
            }
        }

        // DC for correction should be 0 (don't shift the initial guess)
        dc_value = 0.0f;
    } else {
        for (uint32_t i = 0; i < size; i++) {
            laplacian[i] = cmd->hessian[i].xx + cmd->hessian[i].yy;
        }
    }

    // FFT-based Poisson solve
    int fft_W = next_pow2(W);
    int fft_H = next_pow2(H);

    FFTBlurContext ctx;
    if (fft_Initialize(&ctx, fft_W, fft_H) != 0) {
        printf("[normal_from_hessian_Execute] Error: FFT init failed\n");
        free(laplacian);
        if (allocated_height) free(height);
        if (allocated_normals) {
            free(cmd->normals);
            cmd->normals = NULL;
        }
        return -1;
    }

    // Load laplacian into FFT buffer (zero-padded)
    memset(ctx.real, 0, fft_W * fft_H * sizeof(float));
    memset(ctx.imag, 0, fft_W * fft_H * sizeof(float));
    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            ctx.real[y * fft_W + x] = laplacian[y * W + x];
        }
    }

    free(laplacian);

    // Forward FFT
    fft_2d(ctx.real, ctx.imag, fft_W, fft_H, 0, 0.0f);

    // Poisson solve in frequency domain
    poisson_solve_freq(ctx.real, ctx.imag, fft_W, fft_H, dc_value);

    // Inverse FFT
    fft_2d(ctx.real, ctx.imag, fft_W, fft_H, 1, 0.0f);

    // Copy height back (add correction to initial guess if provided)
    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            uint32_t idx = y * W + x;
            float correction = ctx.real[y * fft_W + x];

            if (use_initial_guess) {
                height[idx] = cmd->orig_height[idx] + correction;
            } else {
                height[idx] = correction;
            }
        }
    }

    fft_Free(&ctx);

    // Compute gradient and normalize to get normals
    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            uint32_t idx = y * W + x;

            // If original height is exactly 0, use flat normal
            if (cmd->orig_height && cmd->orig_height[idx] == 0.0f) {
                cmd->normals[idx].x = 0.0f;
                cmd->normals[idx].y = 0.0f;
                cmd->normals[idx].z = 1.0f;
                continue;
            }

            // Gradient using central differences (clamped at edges)
            uint32_t xm = (x > 0) ? x - 1 : 0;
            uint32_t xp = (x < W - 1) ? x + 1 : W - 1;
            uint32_t ym = (y > 0) ? y - 1 : 0;
            uint32_t yp = (y < H - 1) ? y + 1 : H - 1;

            float dx = (height[y * W + xp] - height[y * W + xm]) * 0.5f;
            float dy = (height[yp * W + x] - height[ym * W + x]) * 0.5f;

            // Normal = normalize(-dx, -dy, scale)
            float nx = -dx;
            float ny = -dy;
            float nz = scale;

            float len = sqrtf(nx * nx + ny * ny + nz * nz);
            if (len > 1e-8f) {
                nx /= len;
                ny /= len;
                nz /= len;
            } else {
                nx = 0.0f;
                ny = 0.0f;
                nz = 1.0f;
            }

            cmd->normals[idx].x = nx;
            cmd->normals[idx].y = ny;
            cmd->normals[idx].z = nz;
        }
    }

    // Clean up
    if (allocated_height) {
        free(height);
    } else {
        cmd->height = height;
    }

    return 0;
}

void normal_from_hessian_Free(NormalFromHessianCmd* cmd) {
    if (cmd) {
        if (cmd->normals) {
            free(cmd->normals);
            cmd->normals = NULL;
        }
        if (cmd->height) {
            free(cmd->height);
            cmd->height = NULL;
        }
    }
}
