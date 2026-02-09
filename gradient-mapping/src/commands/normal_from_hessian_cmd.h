#ifndef NORMAL_FROM_HESSIAN_CMD_H
#define NORMAL_FROM_HESSIAN_CMD_H

#include <stdint.h>
#include <memory>
#include "hessian_cmd.h"
#include "../effect_stack_api.h"  // for vec3

/*
 * NormalFromHessianCmd - Convert decomposed Hessian fields to normal map
 *                        via constraint solver
 *
 * Solves for major/minor height fields that satisfy:
 *   1. ∇major + ∇minor = ∇height  (gradient constraint, weight 2)
 *   2. Hessian(major) ≈ H1        (curvature constraint, weight 1)
 *   3. Hessian(minor) ≈ H2        (curvature constraint, weight 1)
 *
 * Where H1 + H2 = original Hessian (eigendecomposition into rank-1 components).
 *
 * Uses Gauss-Seidel + SOR iteration to minimize weighted residuals.
 */
typedef struct {
    /* Input */
    const Hessian2D* H1;      // Major curvature Hessian (rank-1: λ1 * e1⊗e1)
    const Hessian2D* H2;      // Minor curvature Hessian (rank-1: λ2 * e2⊗e2)
    const float* height;      // Original heightmap (for gradient constraint)
    uint32_t W, H;

    /* Solver parameters */
    int max_iterations;       // Max iterations (default 100)
    float tolerance;          // Convergence threshold (default 1e-5)
    float sor_omega;          // SOR relaxation factor (default 1.7, range 1.0-1.95)

    /* Output (allocated by caller or internally) */
    std::unique_ptr<vec3[]> major_normals;      // W*H normal map from major curvature field
    std::unique_ptr<vec3[]> minor_normals;      // W*H normal map from minor curvature field
    std::unique_ptr<float[]> major_height;      // W*H major height field (optional debug output)
    std::unique_ptr<float[]> minor_height;      // W*H minor height field (optional debug output)

    /* Diagnostics (filled by Execute) */
    int iterations_used;      // Actual iterations until convergence
    float final_residual;     // Final residual magnitude
} NormalFromHessianCmd;

/*
 * Compute normal maps from decomposed Hessian fields.
 * If cmd->major_normals/minor_normals are NULL, allocates them internally.
 * If cmd->major_height/minor_height are NULL, they are allocated temporarily
 * for the solve and freed (unless you want debug output, pre-allocate them).
 * Returns 0 on success, -1 on error.
 */
int normal_from_hessian_Execute(NormalFromHessianCmd* cmd);

/* RAII: memory is freed automatically when cmd goes out of scope */

#endif /* NORMAL_FROM_HESSIAN_CMD_H */
