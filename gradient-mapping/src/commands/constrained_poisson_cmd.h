#ifndef CONSTRAINED_POISSON_CMD_H
#define CONSTRAINED_POISSON_CMD_H

#include <stdint.h>
#include <memory>
#include "../effect_stack_api.h"  // for vec3

/*
 * ConstrainedPoissonCmd - Reconstruct height from normal map with constraints
 *
 * Given a target normal map and original height field, finds a new height
 * field that:
 *   1. Has gradients matching the target normals (Poisson solve)
 *   2. Is zero wherever the original is zero (Dirichlet constraint)
 *   3. Is positive wherever the original is positive (positivity constraint)
 *
 * Uses iterative Gauss-Seidel with constraint projection.
 */
typedef struct {
    /* Input */
    const float* original_height;  // W*H original height field [0,1]
    const vec3* target_normals;    // W*H target normal map (normalized)
    uint32_t W, H;
    int max_iterations;            // Maximum solver iterations (default 1000)
    float tolerance;               // Convergence tolerance (default 1e-5)
    float zero_threshold;          // Values <= this are considered zero (default 1e-6)

    /* Output */
    std::unique_ptr<float[]> result_height;  // W*H reconstructed height field
    int iterations_used;           // Actual iterations before convergence
    float final_residual;          // Final residual norm
} ConstrainedPoissonCmd;

/*
 * Execute constrained Poisson solve.
 * If cmd->result_height is NULL, allocates it internally.
 * Returns 0 on success, -1 on error.
 */
int constrained_poisson_Execute(ConstrainedPoissonCmd* cmd);

#endif /* CONSTRAINED_POISSON_CMD_H */
