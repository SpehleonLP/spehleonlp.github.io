#include "constrained_poisson_cmd.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <memory>

/*
 * Derive Laplacian from normal map
 *
 * Normal = (nx, ny, nz), gradient = (-nx/nz, -ny/nz)
 * Laplacian = dg_x/dx + dg_y/dy
 */
static void normals_to_laplacian(const vec3* normals, uint32_t W, uint32_t H,
                                  float* laplacian) {
    // First compute gradient field from normals
    auto gx = std::unique_ptr<float[]>(new float[W * H]);
    auto gy = std::unique_ptr<float[]>(new float[W * H]);

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

    // Compute Laplacian = dg_x/dx + dg_y/dy using central differences
    // Out-of-bounds gradient is 0 (clamp-to-border: height=0 outside -> flat -> zero gradient)
    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            uint32_t idx = y * W + x;

            float gx_l = (x > 0)     ? gx[idx - 1] : 0.0f;
            float gx_r = (x < W - 1) ? gx[idx + 1] : 0.0f;
            float gy_d = (y > 0)     ? gy[idx - W] : 0.0f;
            float gy_u = (y < H - 1) ? gy[idx + W] : 0.0f;

            float dgx_dx = (gx_r - gx_l) * 0.5f;
            float dgy_dy = (gy_u - gy_d) * 0.5f;

            laplacian[idx] = dgx_dx + dgy_dy;
        }
    }
}

/*
 * Gauss-Seidel iteration with constraint projection
 *
 * Solves nabla^2 h = L with:
 *   - h = 0 where is_zero_mask is true
 *   - h >= epsilon where is_zero_mask is false
 */
static float gauss_seidel_step(float* h, const float* laplacian,
                                const int* is_zero_mask,
                                uint32_t W, uint32_t H,
                                float pos_epsilon) {
    float max_change = 0.0f;

    // Out-of-bounds neighbors are 0 (clamp-to-border)
    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            uint32_t idx = y * W + x;

            if (is_zero_mask[idx]) continue;

            float left  = (x > 0)     ? h[idx - 1] : 0.0f;
            float right = (x < W - 1) ? h[idx + 1] : 0.0f;
            float up    = (y > 0)     ? h[idx - W] : 0.0f;
            float down  = (y < H - 1) ? h[idx + W] : 0.0f;

            float new_val = (left + right + up + down - laplacian[idx]) * 0.25f;

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

    return max_change;
}

/*
 * Compute residual: ||nabla^2 h - L||
 */
static float compute_residual(const float* h, const float* laplacian,
                               const int* is_zero_mask,
                               uint32_t W, uint32_t H) {
    float sum_sq = 0.0f;
    uint32_t count = 0;

    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            uint32_t idx = y * W + x;

            if (is_zero_mask[idx]) continue;

            float left  = (x > 0)     ? h[idx - 1] : 0.0f;
            float right = (x < W - 1) ? h[idx + 1] : 0.0f;
            float up    = (y > 0)     ? h[idx - W] : 0.0f;
            float down  = (y < H - 1) ? h[idx + W] : 0.0f;

            float lap_h = left + right + up + down - 4.0f * h[idx];
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
        cmd->result_height = std::unique_ptr<float[]>(new float[size]);
        if (!cmd->result_height) {
            printf( "[constrained_poisson_Execute] Error: failed to allocate result\n");
            return -1;
        }
    }

    // Create zero mask
    auto is_zero_mask = std::unique_ptr<int[]>(new int[size]);

    for (uint32_t i = 0; i < size; i++) {
        is_zero_mask[i] = (cmd->original_height[i] <= zero_thresh) ? 1 : 0;
    }

    // Compute target Laplacian from normals
    auto laplacian = std::unique_ptr<float[]>(new float[size]);

    normals_to_laplacian(cmd->target_normals, W, H, laplacian.get());

    // Initialize result with original height (warm start)
    memcpy(cmd->result_height.get(), cmd->original_height, size * sizeof(float));

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
        float max_change = gauss_seidel_step(cmd->result_height.get(), laplacian.get(),
                                              is_zero_mask.get(), W, H, pos_epsilon);

        // Re-enforce zero constraints (shouldn't be needed, but safety)
        for (uint32_t i = 0; i < size; i++) {
            if (is_zero_mask[i]) {
                cmd->result_height[i] = 0.0f;
            }
        }

        // Check convergence every 250 iterations
        if ((iter + 1) % 250 == 0 || max_change < tolerance) {
            residual = compute_residual(cmd->result_height.get(), laplacian.get(),
                                        is_zero_mask.get(), W, H);
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

    return 0;
}

