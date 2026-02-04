#include "constrained_poisson_cmd.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

/*
 * Derive Laplacian from normal map
 *
 * Normal = (nx, ny, nz), gradient = (-nx/nz, -ny/nz)
 * Laplacian = ∂gx/∂x + ∂gy/∂y
 */
static void normals_to_laplacian(const vec3* normals, uint32_t W, uint32_t H,
                                  float* laplacian) {
    // First compute gradient field from normals
    float* gx = (float*)malloc(W * H * sizeof(float));
    float* gy = (float*)malloc(W * H * sizeof(float));

    if (!gx || !gy) {
        free(gx);
        free(gy);
        memset(laplacian, 0, W * H * sizeof(float));
        return;
    }

    // Convert normals to gradients: g = -n.xy / n.z
    const float epsilon = 1e-6f;
    for (uint32_t i = 0; i < W * H; i++) {
        float nz = normals[i].z;
        if (fabsf(nz) < epsilon) {
            // Near-horizontal normal, undefined gradient
            gx[i] = 0.0f;
            gy[i] = 0.0f;
        } else {
            gx[i] = -normals[i].x / nz;
            gy[i] = -normals[i].y / nz;
        }
    }

    // Compute Laplacian = ∂gx/∂x + ∂gy/∂y using central differences
    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            uint32_t idx = y * W + x;

            // ∂gx/∂x
            float dgx_dx;
            if (x == 0) {
                dgx_dx = gx[idx + 1] - gx[idx];
            } else if (x == W - 1) {
                dgx_dx = gx[idx] - gx[idx - 1];
            } else {
                dgx_dx = (gx[idx + 1] - gx[idx - 1]) * 0.5f;
            }

            // ∂gy/∂y
            float dgy_dy;
            if (y == 0) {
                dgy_dy = gy[idx + W] - gy[idx];
            } else if (y == H - 1) {
                dgy_dy = gy[idx] - gy[idx - W];
            } else {
                dgy_dy = (gy[idx + W] - gy[idx - W]) * 0.5f;
            }

            laplacian[idx] = dgx_dx + dgy_dy;
        }
    }

    free(gx);
    free(gy);
}

/*
 * Gauss-Seidel iteration with constraint projection
 *
 * Solves ∇²h = L with:
 *   - h = 0 where is_zero_mask is true
 *   - h >= epsilon where is_zero_mask is false
 */
static float gauss_seidel_step(float* h, const float* laplacian,
                                const int* is_zero_mask,
                                uint32_t W, uint32_t H,
                                float pos_epsilon) {
    float max_change = 0.0f;

    for (uint32_t y = 1; y < H - 1; y++) {
        for (uint32_t x = 1; x < W - 1; x++) {
            uint32_t idx = y * W + x;

            // Skip zero-constrained pixels
            if (is_zero_mask[idx]) {
                continue;
            }

            // Gauss-Seidel update: h = (h_neighbors - L) / 4
            // ∇²h = h[x-1] + h[x+1] + h[y-1] + h[y+1] - 4*h[x,y] = L
            // => h[x,y] = (h[x-1] + h[x+1] + h[y-1] + h[y+1] - L) / 4
            float neighbors = h[idx - 1] + h[idx + 1] + h[idx - W] + h[idx + W];
            float new_val = (neighbors - laplacian[idx]) * 0.25f;

            // Positivity constraint
            if (new_val < pos_epsilon) {
                new_val = pos_epsilon;
            }

            float change = fabsf(new_val - h[idx]);
            if (change > max_change) {
                max_change = change;
            }

            h[idx] = new_val;
        }
    }

    // Handle edges (excluding corners): use one-sided differences
    // Top and bottom rows
    for (uint32_t x = 1; x < W - 1; x++) {
        // Top row (y = 0)
        uint32_t idx = x;
        if (!is_zero_mask[idx]) {
            float neighbors = h[idx - 1] + h[idx + 1] + 2.0f * h[idx + W];
            float new_val = (neighbors - laplacian[idx]) * 0.25f;
            if (new_val < pos_epsilon) new_val = pos_epsilon;
            h[idx] = new_val;
        }

        // Bottom row (y = H-1)
        idx = (H - 1) * W + x;
        if (!is_zero_mask[idx]) {
            float neighbors = h[idx - 1] + h[idx + 1] + 2.0f * h[idx - W];
            float new_val = (neighbors - laplacian[idx]) * 0.25f;
            if (new_val < pos_epsilon) new_val = pos_epsilon;
            h[idx] = new_val;
        }
    }

    // Left and right columns
    for (uint32_t y = 1; y < H - 1; y++) {
        // Left column (x = 0)
        uint32_t idx = y * W;
        if (!is_zero_mask[idx]) {
            float neighbors = 2.0f * h[idx + 1] + h[idx - W] + h[idx + W];
            float new_val = (neighbors - laplacian[idx]) * 0.25f;
            if (new_val < pos_epsilon) new_val = pos_epsilon;
            h[idx] = new_val;
        }

        // Right column (x = W-1)
        idx = y * W + (W - 1);
        if (!is_zero_mask[idx]) {
            float neighbors = 2.0f * h[idx - 1] + h[idx - W] + h[idx + W];
            float new_val = (neighbors - laplacian[idx]) * 0.25f;
            if (new_val < pos_epsilon) new_val = pos_epsilon;
            h[idx] = new_val;
        }
    }

    // Corners
    uint32_t corners[4] = {0, W - 1, (H - 1) * W, (H - 1) * W + (W - 1)};
    for (int c = 0; c < 4; c++) {
        uint32_t idx = corners[c];
        if (!is_zero_mask[idx]) {
            // Use available neighbors (2 neighbors for corners)
            float neighbors = 0.0f;
            int count = 0;
            if (idx % W > 0) { neighbors += h[idx - 1]; count++; }
            if (idx % W < W - 1) { neighbors += h[idx + 1]; count++; }
            if (idx / W > 0) { neighbors += h[idx - W]; count++; }
            if (idx / W < H - 1) { neighbors += h[idx + W]; count++; }

            if (count > 0) {
                float new_val = (neighbors * 4.0f / count - laplacian[idx]) * 0.25f;
                if (new_val < pos_epsilon) new_val = pos_epsilon;
                h[idx] = new_val;
            }
        }
    }

    return max_change;
}

/*
 * Compute residual: ‖∇²h - L‖
 */
static float compute_residual(const float* h, const float* laplacian,
                               const int* is_zero_mask,
                               uint32_t W, uint32_t H) {
    float sum_sq = 0.0f;
    uint32_t count = 0;

    for (uint32_t y = 1; y < H - 1; y++) {
        for (uint32_t x = 1; x < W - 1; x++) {
            uint32_t idx = y * W + x;

            if (is_zero_mask[idx]) continue;

            float lap_h = h[idx - 1] + h[idx + 1] + h[idx - W] + h[idx + W] - 4.0f * h[idx];
            float residual = lap_h - laplacian[idx];
            sum_sq += residual * residual;
            count++;
        }
    }

    return (count > 0) ? sqrtf(sum_sq / count) : 0.0f;
}

int constrained_poisson_Execute(ConstrainedPoissonCmd* cmd) {
    if (!cmd) {
        printf( "[constrained_poisson_Execute] Error: cmd is NULL\n");
        return -1;
    }

    if (!cmd->original_height || !cmd->target_normals) {
        printf( "[constrained_poisson_Execute] Error: input is NULL\n");
        return -1;
    }

    uint32_t W = cmd->W;
    uint32_t H = cmd->H;
    uint32_t size = W * H;

    int max_iter = cmd->max_iterations > 0 ? cmd->max_iterations : 1000;
    float tolerance = cmd->tolerance > 0 ? cmd->tolerance : 1e-5f;
    float zero_thresh = cmd->zero_threshold > 0 ? cmd->zero_threshold : 1e-6f;
    float pos_epsilon = zero_thresh;  // Minimum positive value

    // Allocate output if needed
    if (!cmd->result_height) {
        cmd->result_height = (float*)malloc(size * sizeof(float));
        if (!cmd->result_height) {
            printf( "[constrained_poisson_Execute] Error: failed to allocate result\n");
            return -1;
        }
    }

    // Create zero mask
    int* is_zero_mask = (int*)malloc(size * sizeof(int));
    if (!is_zero_mask) {
        printf( "[constrained_poisson_Execute] Error: failed to allocate mask\n");
        return -1;
    }

    for (uint32_t i = 0; i < size; i++) {
        is_zero_mask[i] = (cmd->original_height[i] <= zero_thresh) ? 1 : 0;
    }

    // Compute target Laplacian from normals
    float* laplacian = (float*)malloc(size * sizeof(float));
    if (!laplacian) {
        printf( "[constrained_poisson_Execute] Error: failed to allocate laplacian\n");
        free(is_zero_mask);
        return -1;
    }

    normals_to_laplacian(cmd->target_normals, W, H, laplacian);

    // Initialize result with original height (warm start)
    memcpy(cmd->result_height, cmd->original_height, size * sizeof(float));

    // Enforce zero constraints on initial state
    for (uint32_t i = 0; i < size; i++) {
        if (is_zero_mask[i]) {
            cmd->result_height[i] = 0.0f;
        }
    }

    // Gauss-Seidel iteration
    printf( "[constrained_poisson] Starting iteration (max=%d, tol=%.2e)...\n",
            max_iter, tolerance);

    int iter;
    float residual = 0.0f;
    for (iter = 0; iter < max_iter; iter++) {
        float max_change = gauss_seidel_step(cmd->result_height, laplacian,
                                              is_zero_mask, W, H, pos_epsilon);

        // Re-enforce zero constraints (shouldn't be needed, but safety)
        for (uint32_t i = 0; i < size; i++) {
            if (is_zero_mask[i]) {
                cmd->result_height[i] = 0.0f;
            }
        }

        // Check convergence every 250 iterations
        if ((iter + 1) % 250 == 0 || max_change < tolerance) {
            residual = compute_residual(cmd->result_height, laplacian,
                                        is_zero_mask, W, H);
            printf( "[constrained_poisson] Iter %d: max_change=%.2e, residual=%.2e\n",
                    iter + 1, max_change, residual);

            if (max_change < tolerance) {
                printf( "[constrained_poisson] Converged at iteration %d\n", iter + 1);
                break;
            }
        }
    }

    cmd->iterations_used = iter + 1;
    cmd->final_residual = residual;

    if (iter == max_iter) {
        printf( "[constrained_poisson] Warning: did not converge after %d iterations\n",
                max_iter);
    }

    free(laplacian);
    free(is_zero_mask);

    return 0;
}

void constrained_poisson_Free(ConstrainedPoissonCmd* cmd) {
    if (cmd && cmd->result_height) {
        free(cmd->result_height);
        cmd->result_height = NULL;
    }
}
