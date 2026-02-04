#ifndef NORMAL_FROM_HESSIAN_CMD_H
#define NORMAL_FROM_HESSIAN_CMD_H

#include <stdint.h>
#include "hessian_cmd.h"
#include "../effect_stack_api.h"  // for vec3

/*
 * NormalFromHessianCmd - Convert Hessian field to normal map via coupled Poisson solve
 *
 * Pipeline:
 *   1. Compute divergence of each Hessian row:
 *      div_x = ∂H.xx/∂x + ∂H.xy/∂y
 *      div_y = ∂H.xy/∂x + ∂H.yy/∂y
 *   2. FFT-based Poisson solve for gradient directly:
 *      ∇²gx = div_x → gx
 *      ∇²gy = div_y → gy
 *   3. Normalize: n = normalize(-gx, -gy, scale)
 *
 * This preserves eigenvector direction (unlike trace-based Poisson).
 * The scale parameter controls normal map "strength" (higher = flatter normals).
 */
typedef struct {
    /* Input */
    const Hessian2D* hessian;
    uint32_t W, H;
    float scale;              // Normal map Z scale (default 1.0)
    const float* orig_height; // Original heightmap (optional, for zero-masking)

    /* Output (allocated by caller or internally) */
    vec3* normals;        // W*H array of normalized normals
    float* height;        // W*H array of recovered height (optional, can be NULL)
} NormalFromHessianCmd;

/*
 * Compute normal map from Hessian field.
 * If cmd->normals is NULL, allocates it internally.
 * If cmd->height is NULL, height buffer is allocated temporarily and freed.
 * Returns 0 on success, -1 on error.
 */
int normal_from_hessian_Execute(NormalFromHessianCmd* cmd);

/*
 * Free allocated memory.
 */
void normal_from_hessian_Free(NormalFromHessianCmd* cmd);

#endif /* NORMAL_FROM_HESSIAN_CMD_H */
