#ifndef LAPLACIAN_CMD_H
#define LAPLACIAN_CMD_H

#include <stdint.h>
#include "hessian_cmd.h"

/*
 * LaplacianCmd - Compute Laplacian (∇²f = ∂²f/∂x² + ∂²f/∂y²) from heightmap
 *
 * This is just the trace of the Hessian: xx + yy.
 * Output is a float array that can be exported as a grayscale height map.
 *
 * Positive = local minimum (concave up / valley)
 * Negative = local maximum (concave down / ridge)
 * Zero = saddle or flat
 */
typedef struct {
    /* Input */
    const float* heightmap;
    uint32_t W, H;
    int kernel_size;              // 3 or 5 (passed to Hessian)
    HessianBorderPolicy border;   // edge handling (default UNDEFINED)
    float undefined_value;        // "no data" sentinel

    /* Output (allocated internally if NULL) */
    float* laplacian;             // W*H array, caller-owned or auto-allocated
    int owns_output;              // set to 1 if we allocated laplacian

    /* Range info (filled on output) */
    float min_val;
    float max_val;
} LaplacianCmd;

/*
 * Compute Laplacian field.
 * If cmd->laplacian is NULL, allocates it (and sets owns_output=1).
 * Returns 0 on success, -1 on error.
 */
int laplacian_Execute(LaplacianCmd* cmd);

/*
 * Free output if we allocated it.
 */
void laplacian_Free(LaplacianCmd* cmd);

#endif /* LAPLACIAN_CMD_H */
