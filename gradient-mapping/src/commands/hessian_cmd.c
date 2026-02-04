#include "hessian_cmd.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * Sample heightmap with border policy
 * Returns 1 if valid sample, 0 if undefined (only for BORDER_UNDEFINED)
 */
static int sample_with_border(const float* h, int W, int H,
                               int x, int y, HessianBorderPolicy border,
                               float undefined_value, float* out) {
    // Check if in bounds
    if (x >= 0 && x < W && y >= 0 && y < H) {
        float val = h[y * W + x];
        if (val == undefined_value) {
            *out = 0.0f;
            return 0;
        }
        *out = val;
        return 1;
    }

    // Out of bounds - apply border policy
    switch (border) {
        case HESSIAN_BORDER_UNDEFINED:
            *out = 0.0f;
            return 0;

        case HESSIAN_BORDER_CLAMP_EDGE: {
            int cx = x < 0 ? 0 : (x >= W ? W - 1 : x);
            int cy = y < 0 ? 0 : (y >= H ? H - 1 : y);
            float val = h[cy * W + cx];
            if (val == undefined_value) {
                *out = 0.0f;
                return 0;
            }
            *out = val;
            return 1;
        }

        case HESSIAN_BORDER_REPEAT: {
            int rx = ((x % W) + W) % W;
            int ry = ((y % H) + H) % H;
            float val = h[ry * W + rx];
            if (val == undefined_value) {
                *out = 0.0f;
                return 0;
            }
            *out = val;
            return 1;
        }

        case HESSIAN_BORDER_MIRROR: {
            // Mirror: reflect coordinates
            int mx = x;
            int my = y;
            // Handle x
            if (mx < 0) mx = -mx - 1;
            if (mx >= W) mx = 2 * W - mx - 1;
            mx = ((mx % W) + W) % W;  // safety clamp
            // Handle y
            if (my < 0) my = -my - 1;
            if (my >= H) my = 2 * H - my - 1;
            my = ((my % H) + H) % H;  // safety clamp
            float val = h[my * W + mx];
            if (val == undefined_value) {
                *out = 0.0f;
                return 0;
            }
            *out = val;
            return 1;
        }
    }

    *out = 0.0f;
    return 0;
}

/*
 * Compute second derivatives using 3x3 stencil
 */
static void compute_hessian_3x3(const float* h, uint32_t W, uint32_t H,
                                 uint32_t x, uint32_t y,
                                 HessianBorderPolicy border, float undefined_value,
                                 Hessian2D* out) {
    int ix = (int)x, iy = (int)y;
    int iW = (int)W, iH = (int)H;

    float f_c, f_xm1, f_xp1, f_ym1, f_yp1, f_pp, f_mp, f_pm, f_mm;

    // Sample all 9 points
    int valid = 1;
    valid &= sample_with_border(h, iW, iH, ix,     iy,     border, undefined_value, &f_c);
    valid &= sample_with_border(h, iW, iH, ix - 1, iy,     border, undefined_value, &f_xm1);
    valid &= sample_with_border(h, iW, iH, ix + 1, iy,     border, undefined_value, &f_xp1);
    valid &= sample_with_border(h, iW, iH, ix,     iy - 1, border, undefined_value, &f_ym1);
    valid &= sample_with_border(h, iW, iH, ix,     iy + 1, border, undefined_value, &f_yp1);
    valid &= sample_with_border(h, iW, iH, ix + 1, iy + 1, border, undefined_value, &f_pp);
    valid &= sample_with_border(h, iW, iH, ix - 1, iy + 1, border, undefined_value, &f_mp);
    valid &= sample_with_border(h, iW, iH, ix + 1, iy - 1, border, undefined_value, &f_pm);
    valid &= sample_with_border(h, iW, iH, ix - 1, iy - 1, border, undefined_value, &f_mm);

    if (!valid) {
        out->xx = 0.0f;
        out->yy = 0.0f;
        out->xy = 0.0f;
        return;
    }

    // ∂²f/∂x² ≈ f[x-1] - 2*f[x] + f[x+1]
    out->xx = f_xm1 - 2.0f * f_c + f_xp1;

    // ∂²f/∂y² ≈ f[y-1] - 2*f[y] + f[y+1]
    out->yy = f_ym1 - 2.0f * f_c + f_yp1;

    // ∂²f/∂x∂y ≈ (f[x+1,y+1] - f[x-1,y+1] - f[x+1,y-1] + f[x-1,y-1]) / 4
    out->xy = (f_pp - f_mp - f_pm + f_mm) * 0.25f;
}

/*
 * Compute second derivatives using 5x5 stencil
 */
static void compute_hessian_5x5(const float* h, uint32_t W, uint32_t H,
                                 uint32_t x, uint32_t y,
                                 HessianBorderPolicy border, float undefined_value,
                                 Hessian2D* out) {
    int ix = (int)x, iy = (int)y;
    int iW = (int)W, iH = (int)H;

    // Sample all 25 points needed for 5x5 stencil
    float f_c, f_xm2, f_xm1, f_xp1, f_xp2;
    float f_ym2, f_ym1, f_yp1, f_yp2;
    float f_m2m2, f_m1m2, f_p1m2, f_p2m2;
    float f_m2m1, f_m1m1, f_p1m1, f_p2m1;
    float f_m2p1, f_m1p1, f_p1p1, f_p2p1;
    float f_m2p2, f_m1p2, f_p1p2, f_p2p2;

    int valid = 1;

    // Center and axis samples
    valid &= sample_with_border(h, iW, iH, ix,     iy,     border, undefined_value, &f_c);
    valid &= sample_with_border(h, iW, iH, ix - 2, iy,     border, undefined_value, &f_xm2);
    valid &= sample_with_border(h, iW, iH, ix - 1, iy,     border, undefined_value, &f_xm1);
    valid &= sample_with_border(h, iW, iH, ix + 1, iy,     border, undefined_value, &f_xp1);
    valid &= sample_with_border(h, iW, iH, ix + 2, iy,     border, undefined_value, &f_xp2);
    valid &= sample_with_border(h, iW, iH, ix,     iy - 2, border, undefined_value, &f_ym2);
    valid &= sample_with_border(h, iW, iH, ix,     iy - 1, border, undefined_value, &f_ym1);
    valid &= sample_with_border(h, iW, iH, ix,     iy + 1, border, undefined_value, &f_yp1);
    valid &= sample_with_border(h, iW, iH, ix,     iy + 2, border, undefined_value, &f_yp2);

    // Row y-2
    valid &= sample_with_border(h, iW, iH, ix - 2, iy - 2, border, undefined_value, &f_m2m2);
    valid &= sample_with_border(h, iW, iH, ix - 1, iy - 2, border, undefined_value, &f_m1m2);
    valid &= sample_with_border(h, iW, iH, ix + 1, iy - 2, border, undefined_value, &f_p1m2);
    valid &= sample_with_border(h, iW, iH, ix + 2, iy - 2, border, undefined_value, &f_p2m2);

    // Row y-1
    valid &= sample_with_border(h, iW, iH, ix - 2, iy - 1, border, undefined_value, &f_m2m1);
    valid &= sample_with_border(h, iW, iH, ix - 1, iy - 1, border, undefined_value, &f_m1m1);
    valid &= sample_with_border(h, iW, iH, ix + 1, iy - 1, border, undefined_value, &f_p1m1);
    valid &= sample_with_border(h, iW, iH, ix + 2, iy - 1, border, undefined_value, &f_p2m1);

    // Row y+1
    valid &= sample_with_border(h, iW, iH, ix - 2, iy + 1, border, undefined_value, &f_m2p1);
    valid &= sample_with_border(h, iW, iH, ix - 1, iy + 1, border, undefined_value, &f_m1p1);
    valid &= sample_with_border(h, iW, iH, ix + 1, iy + 1, border, undefined_value, &f_p1p1);
    valid &= sample_with_border(h, iW, iH, ix + 2, iy + 1, border, undefined_value, &f_p2p1);

    // Row y+2
    valid &= sample_with_border(h, iW, iH, ix - 2, iy + 2, border, undefined_value, &f_m2p2);
    valid &= sample_with_border(h, iW, iH, ix - 1, iy + 2, border, undefined_value, &f_m1p2);
    valid &= sample_with_border(h, iW, iH, ix + 1, iy + 2, border, undefined_value, &f_p1p2);
    valid &= sample_with_border(h, iW, iH, ix + 2, iy + 2, border, undefined_value, &f_p2p2);

    if (!valid) {
        // Missing data: fall back to 3x3
        compute_hessian_3x3(h, W, H, x, y, border, undefined_value, out);
        return;
    }

    // ∂²f/∂x² using 5-point stencil
    // (-f[x-2] + 16*f[x-1] - 30*f[x] + 16*f[x+1] - f[x+2]) / 12
    out->xx = (-f_xm2 + 16.0f * f_xm1 - 30.0f * f_c + 16.0f * f_xp1 - f_xp2) / 12.0f;

    // ∂²f/∂y² using 5-point stencil
    out->yy = (-f_ym2 + 16.0f * f_ym1 - 30.0f * f_c + 16.0f * f_yp1 - f_yp2) / 12.0f;

    // ∂²f/∂x∂y using 5x5 mixed stencil
    out->xy = (
        -1.0f * f_m2m2 +  8.0f * f_m1m2 + -8.0f * f_p1m2 +  1.0f * f_p2m2 +
         8.0f * f_m2m1 + -64.0f * f_m1m1 + 64.0f * f_p1m1 + -8.0f * f_p2m1 +
        -8.0f * f_m2p1 +  64.0f * f_m1p1 + -64.0f * f_p1p1 +  8.0f * f_p2p1 +
         1.0f * f_m2p2 + -8.0f * f_m1p2 +  8.0f * f_p1p2 + -1.0f * f_p2p2
    ) / 144.0f;
}

int hessian_Execute(HessianCmd* cmd) {
    if (!cmd) {
        printf( "[hessian_Execute] Error: cmd is NULL\n");
        return -1;
    }

    if (!cmd->heightmap) {
        printf( "[hessian_Execute] Error: heightmap is NULL\n");
        return -1;
    }

    if (cmd->kernel_size != 3 && cmd->kernel_size != 5) {
        printf( "[hessian_Execute] Error: invalid kernel_size %d (must be 3 or 5)\n",
                cmd->kernel_size);
        return -1;
    }

    uint32_t W = cmd->W;
    uint32_t H = cmd->H;
    uint32_t size = W * H;

    // Allocate output if not provided
    if (!cmd->hessian) {
        cmd->hessian = (Hessian2D*)malloc(size * sizeof(Hessian2D));
        if (!cmd->hessian) {
            printf( "[hessian_Execute] Error: failed to allocate %u bytes for hessian\n",
                    (unsigned)(size * sizeof(Hessian2D)));
            return -1;
        }
    }

    // Compute Hessian for each pixel
    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            uint32_t idx = y * W + x;

            if (cmd->kernel_size == 3) {
                compute_hessian_3x3(cmd->heightmap, W, H, x, y,
                                    cmd->border, cmd->undefined_value,
                                    &cmd->hessian[idx]);
            } else {
                compute_hessian_5x5(cmd->heightmap, W, H, x, y,
                                    cmd->border, cmd->undefined_value,
                                    &cmd->hessian[idx]);
            }
        }
    }

    return 0;
}

void hessian_Free(HessianCmd* cmd) {
    if (cmd && cmd->hessian) {
        free(cmd->hessian);
        cmd->hessian = NULL;
    }
}
