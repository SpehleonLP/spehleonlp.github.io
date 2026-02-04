#ifndef HESSIAN_FROM_EIGEN_CMD_H
#define HESSIAN_FROM_EIGEN_CMD_H

#include <stdint.h>
#include "eigen_cmd.h"
#include "hessian_cmd.h"

/*
 * HessianFromEigenCmd - Reconstruct Hessian from eigenvalue/vector field
 *
 * Given an EigenVec2 field (either major or minor from decomposition),
 * reconstructs the rank-1 Hessian matrix:
 *   H = λ * v ⊗ v = λ * [vx*vx  vx*vy]
 *                       [vx*vy  vy*vy]
 *
 * This allows separating major/minor curvature for independent processing.
 */
typedef struct {
    /* Input */
    const EigenVec2* eigen;  // W*H array of eigenvalue/vector pairs
    uint32_t W, H;

    /* Output (allocated by caller or internally) */
    Hessian2D* hessian;      // W*H array of reconstructed Hessians
} HessianFromEigenCmd;

/*
 * Reconstruct Hessian field from eigen field.
 * If cmd->hessian is NULL, allocates it internally.
 * Returns 0 on success, -1 on error.
 */
int hessian_from_eigen_Execute(HessianFromEigenCmd* cmd);

/*
 * Free allocated memory.
 */
void hessian_from_eigen_Free(HessianFromEigenCmd* cmd);

#endif /* HESSIAN_FROM_EIGEN_CMD_H */
