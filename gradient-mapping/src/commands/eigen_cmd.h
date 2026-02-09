#ifndef EIGEN_CMD_H
#define EIGEN_CMD_H

#include <stdint.h>
#include <memory>
#include "hessian_cmd.h"
#include "../effect_stack_api.h"  // for vec2

/*
 * Eigenvalue + eigenvector pair
 *
 * Bundles an eigenvalue (scalar curvature magnitude) with its
 * corresponding eigenvector (curvature direction).
 */
typedef struct {
    vec2 vector;  // normalized eigenvector direction
    float value;  // eigenvalue magnitude
} EigenVec2;

/*
 * EigenDecomposeCmd - Decompose Hessian field into eigenvalues/vectors
 *
 * For each pixel's Hessian matrix, computes the 2 eigenvalue/vector pairs
 * and sorts them by magnitude (major = larger, minor = smaller).
 */
typedef struct {
    /* Input */
    const Hessian2D* hessian;
    uint32_t W, H;

    /* Output (allocated by caller or by eigen_Execute) */
    std::unique_ptr<EigenVec2[]> major;  // W*H array, larger eigenvalue + vector
    std::unique_ptr<EigenVec2[]> minor;  // W*H array, smaller eigenvalue + vector
} EigenDecomposeCmd;

/*
 * Compute eigendecomposition.
 * If cmd->major or cmd->minor are NULL, allocates them internally.
 * Returns 0 on success, -1 on error.
 */
int eigen_Execute(EigenDecomposeCmd* cmd);

#endif /* EIGEN_CMD_H */
