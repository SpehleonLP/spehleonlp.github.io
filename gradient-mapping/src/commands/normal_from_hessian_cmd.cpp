#include "normal_from_hessian_cmd.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <memory>

/*
 * Constraint solver for major/minor height field decomposition
 *
 * Minimizes:
 *   E = 2 * ||∇major + ∇minor - ∇height||²
 *     + ||Hessian(major) - H1||²
 *     + ||Hessian(minor) - H2||²
 *
 * Using Gauss-Seidel iteration with SOR relaxation.
 */

/* Weights for constraint terms */
#define GRADIENT_WEIGHT 2.0f
#define HESSIAN_WEIGHT  1.0f

/* Safe pixel access with clamping */
static inline float get_pixel(const float* img, int x, int y, int W, int H) {
    if (x < 0) x = 0;
    if (x >= W) x = W - 1;
    if (y < 0) y = 0;
    if (y >= H) y = H - 1;
    return img[y * W + x];
}

/* Unused but kept for potential future use
static inline Hessian2D get_hessian(const Hessian2D* h, int x, int y, int W, int H) {
    if (x < 0) x = 0;
    if (x >= W) x = W - 1;
    if (y < 0) y = 0;
    if (y >= H) y = H - 1;
    return h[y * W + x];
}
*/

/*
 * Compute gradient of height field at pixel (x,y) using central differences
 */
static void compute_gradient(const float* field, int x, int y, int W, int H,
                             float* gx, float* gy) {
    float left  = get_pixel(field, x - 1, y, W, H);
    float right = get_pixel(field, x + 1, y, W, H);
    float up    = get_pixel(field, x, y - 1, W, H);
    float down  = get_pixel(field, x, y + 1, W, H);

    *gx = (right - left) * 0.5f;
    *gy = (down - up) * 0.5f;
}

/*
 * Solve local 2x2 system for major[i,j] and minor[i,j]
 *
 * Minimizes the local energy:
 *
 *   E_local(m, n) = w_h * [ (L-2m+R - H1.xx)² + (U-2m+D - H1.yy)²
 *                          + (L-2n+R - H2.xx)² + (U-2n+D - H2.yy)² ]
 *                 + w_g * (m + n - height)²
 *
 * Setting ∂E/∂m = 0 and ∂E/∂n = 0 gives a 2x2 linear system:
 *
 *   A*m + B*n = rhs_m
 *   B*m + A*n = rhs_n
 *
 * Where:
 *   A = 16*w_h + 2*w_g     (diagonal: Hessian self-coupling + sum constraint)
 *   B = 2*w_g               (off-diagonal: sum constraint couples major/minor)
 *   rhs_m = 4*w_h*(neighbor_sum_major - trace(H1)) + 2*w_g*height
 *   rhs_n = 4*w_h*(neighbor_sum_minor - trace(H2)) + 2*w_g*height
 *
 * Solved via Cramer's rule: det = A² - B²
 */
static void solve_local_system(
    const float* major, const float* minor, const float* height,
    const Hessian2D* H1, const Hessian2D* H2,
    int x, int y, int W, int H,
    float* new_major, float* new_minor)
{
    int idx = y * W + x;

    /* Get neighbor values */
    float maj_left  = get_pixel(major, x - 1, y, W, H);
    float maj_right = get_pixel(major, x + 1, y, W, H);
    float maj_up    = get_pixel(major, x, y - 1, W, H);
    float maj_down  = get_pixel(major, x, y + 1, W, H);

    float min_left  = get_pixel(minor, x - 1, y, W, H);
    float min_right = get_pixel(minor, x + 1, y, W, H);
    float min_up    = get_pixel(minor, x, y - 1, W, H);
    float min_down  = get_pixel(minor, x, y + 1, W, H);

    /* Target Hessians at this pixel */
    Hessian2D h1 = H1[idx];
    Hessian2D h2 = H2[idx];

    float target_height = height[idx];

    /* Neighbor sums */
    float maj_neighbor_sum = maj_left + maj_right + maj_up + maj_down;
    float min_neighbor_sum = min_left + min_right + min_up + min_down;

    /* 2x2 system coefficients (derived from ∂E/∂m = 0, ∂E/∂n = 0) */
    float A = 16.0f * HESSIAN_WEIGHT + 2.0f * GRADIENT_WEIGHT;
    float B = 2.0f * GRADIENT_WEIGHT;

    float rhs_m = 4.0f * HESSIAN_WEIGHT * (maj_neighbor_sum - (h1.xx + h1.yy))
                + 2.0f * GRADIENT_WEIGHT * target_height;
    float rhs_n = 4.0f * HESSIAN_WEIGHT * (min_neighbor_sum - (h2.xx + h2.yy))
                + 2.0f * GRADIENT_WEIGHT * target_height;

    /* Cramer's rule: det = A² - B² = (A+B)(A-B) */
    float inv_det = 1.0f / (A * A - B * B);

    *new_major = (A * rhs_m - B * rhs_n) * inv_det;
    *new_minor = (A * rhs_n - B * rhs_m) * inv_det;
}

/*
 * Compute total residual for convergence check
 */
static float compute_residual(
    const float* major, const float* minor, const float* height,
    const Hessian2D* H1, const Hessian2D* H2,
    int W, int H)
{
    float total = 0.0f;

    for (int y = 1; y < H - 1; y++) {
        for (int x = 1; x < W - 1; x++) {
            int idx = y * W + x;

            /* Hessian residuals for major */
            float maj_xx = major[idx - 1] - 2.0f * major[idx] + major[idx + 1];
            float maj_yy = major[idx - W] - 2.0f * major[idx] + major[idx + W];
            float r_maj_xx = maj_xx - H1[idx].xx;
            float r_maj_yy = maj_yy - H1[idx].yy;

            /* Hessian residuals for minor */
            float min_xx = minor[idx - 1] - 2.0f * minor[idx] + minor[idx + 1];
            float min_yy = minor[idx - W] - 2.0f * minor[idx] + minor[idx + W];
            float r_min_xx = min_xx - H2[idx].xx;
            float r_min_yy = min_yy - H2[idx].yy;

            /* Gradient residual */
            float maj_gx = (major[idx + 1] - major[idx - 1]) * 0.5f;
            float maj_gy = (major[idx + W] - major[idx - W]) * 0.5f;
            float min_gx = (minor[idx + 1] - minor[idx - 1]) * 0.5f;
            float min_gy = (minor[idx + W] - minor[idx - W]) * 0.5f;
            float ht_gx = (height[idx + 1] - height[idx - 1]) * 0.5f;
            float ht_gy = (height[idx + W] - height[idx - W]) * 0.5f;

            float r_gx = maj_gx + min_gx - ht_gx;
            float r_gy = maj_gy + min_gy - ht_gy;

            total += HESSIAN_WEIGHT * (r_maj_xx * r_maj_xx + r_maj_yy * r_maj_yy);
            total += HESSIAN_WEIGHT * (r_min_xx * r_min_xx + r_min_yy * r_min_yy);
            total += GRADIENT_WEIGHT * (r_gx * r_gx + r_gy * r_gy);
        }
    }

    return sqrtf(total / ((W - 2) * (H - 2)));
}

int normal_from_hessian_Execute(NormalFromHessianCmd* cmd) {
    if (!cmd) {
        fprintf(stderr, "[normal_from_hessian] Error: cmd is NULL\n");
        return -1;
    }

    if (!cmd->H1 || !cmd->H2 || !cmd->height) {
        fprintf(stderr, "[normal_from_hessian] Error: H1, H2, or height is NULL\n");
        return -1;
    }

    uint32_t W = cmd->W;
    uint32_t H = cmd->H;
    uint32_t size = W * H;

    /* Apply defaults */
    int max_iter = cmd->max_iterations > 0 ? cmd->max_iterations : 100;
    float tol = cmd->tolerance > 0.0f ? cmd->tolerance : 1e-5f;
    float omega = cmd->sor_omega > 0.0f ? cmd->sor_omega : 1.7f;

    /* Clamp omega to valid SOR range */
    if (omega < 1.0f) omega = 1.0f;
    if (omega > 1.95f) omega = 1.95f;

    /* Allocate outputs if not already provided */
    if (!cmd->major_normals) {
        cmd->major_normals = std::unique_ptr<vec3[]>(new vec3[size]);
    }
    if (!cmd->minor_normals) {
        cmd->minor_normals = std::unique_ptr<vec3[]>(new vec3[size]);
    }
    if (!cmd->major_height) {
        cmd->major_height = std::unique_ptr<float[]>(new float[size]);
    }
    if (!cmd->minor_height) {
        cmd->minor_height = std::unique_ptr<float[]>(new float[size]);
    }

    float* major = cmd->major_height.get();
    float* minor = cmd->minor_height.get();

    /* Initialize: major = minor = height */
    for (uint32_t i = 0; i < size; i++) {
        major[i] = cmd->height[i];
        minor[i] = cmd->height[i];
    }

    /* Gauss-Seidel + SOR iteration */
    float residual = 0.0f;
    int iter;

    for (iter = 0; iter < max_iter; iter++) {
        /* Red-black ordering for better convergence (optional, using simple row-major for now) */
        for (uint32_t y = 1; y < H - 1; y++) {
            for (uint32_t x = 1; x < W - 1; x++) {
                uint32_t idx = y * W + x;

                float new_major, new_minor;
                solve_local_system(major, minor, cmd->height,
                                   cmd->H1, cmd->H2,
                                   x, y, W, H,
                                   &new_major, &new_minor);

                /* SOR relaxation */
                major[idx] = major[idx] + omega * (new_major - major[idx]);
                minor[idx] = minor[idx] + omega * (new_minor - minor[idx]);
            }
        }

        /* Check convergence every 10 iterations */
        if ((iter + 1) % 10 == 0 || iter == max_iter - 1) {
            residual = compute_residual(major, minor, cmd->height,
                                        cmd->H1, cmd->H2, W, H);
            if (residual < tol) {
                iter++;
                break;
            }
        }
    }

    cmd->iterations_used = iter;
    cmd->final_residual = residual;

    /* Compute normals from major and minor gradients, then orthogonalize */
    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            uint32_t idx = y * W + x;

            float gx_maj, gy_maj, gx_min, gy_min;
            compute_gradient(major, x, y, W, H, &gx_maj, &gy_maj);
            compute_gradient(minor, x, y, W, H, &gx_min, &gy_min);

            /* Major normal (treated as ground truth) */
            float mx = -gx_maj;
            float my = -gy_maj;

            /* Minor normal XY before correction */
            float nx = -gx_min;
            float ny = -gy_min;

            /* Normalize major */
            {
                float nmx = mx, nmy = my, nmz = 1.0f;
                float len = sqrtf(nmx * nmx + nmy * nmy + nmz * nmz);
                if (len > 1e-8f) {
                    cmd->major_normals[idx].x = nmx / len;
                    cmd->major_normals[idx].y = nmy / len;
                    cmd->major_normals[idx].z = nmz / len;
                } else {
                    cmd->major_normals[idx].x = 0.0f;
                    cmd->major_normals[idx].y = 0.0f;
                    cmd->major_normals[idx].z = 1.0f;
                }
            }

            /* Normalize minor */
            {
                float nnx = nx, nny = ny, nnz = 1.0f;
                float len = sqrtf(nnx * nnx + nny * nny + nnz * nnz);
                if (len > 1e-8f) {
                    cmd->minor_normals[idx].x = nnx / len;
                    cmd->minor_normals[idx].y = nny / len;
                    cmd->minor_normals[idx].z = nnz / len;
                } else {
                    cmd->minor_normals[idx].x = 0.0f;
                    cmd->minor_normals[idx].y = 0.0f;
                    cmd->minor_normals[idx].z = 1.0f;
                }
            }
        }
    }

    /* Height buffers are owned by cmd->major_height / cmd->minor_height */
    return 0;
}
