#include "laplacian_cmd.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

int laplacian_Execute(LaplacianCmd* cmd)
{
    if (!cmd || !cmd->heightmap || cmd->W == 0 || cmd->H == 0)
        return -1;

    uint32_t W = cmd->W, H = cmd->H;
    size_t N = (size_t)W * H;

    /* Compute Hessian first */
    HessianCmd hess{};
    hess.heightmap = cmd->heightmap;
    hess.W = W;
    hess.H = H;
    hess.kernel_size = cmd->kernel_size;
    hess.border = cmd->border;
    hess.undefined_value = cmd->undefined_value;

    if (hessian_Execute(&hess) != 0)
        return -1;

    /* Allocate output if needed */
    if (!cmd->laplacian) {
        cmd->laplacian = (float*)malloc(N * sizeof(float));
        if (!cmd->laplacian) return -1;
        cmd->owns_output = 1;
    }

    /* Laplacian = trace of Hessian = xx + yy */
    float lo = FLT_MAX, hi = -FLT_MAX;
    for (size_t i = 0; i < N; i++) {
        float v = hess.hessian[i].xx + hess.hessian[i].yy;
        cmd->laplacian[i] = v;
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    cmd->min_val = lo;
    cmd->max_val = hi;

    return 0;
}

void laplacian_Free(LaplacianCmd* cmd)
{
    if (cmd && cmd->owns_output && cmd->laplacian) {
        free(cmd->laplacian);
        cmd->laplacian = NULL;
        cmd->owns_output = 0;
    }
}
