#include "hessian_from_eigen_cmd.h"
#include <stdlib.h>
#include <stdio.h>

int hessian_from_eigen_Execute(HessianFromEigenCmd* cmd) {
    if (!cmd) {
        printf( "[hessian_from_eigen_Execute] Error: cmd is NULL\n");
        return -1;
    }

    if (!cmd->eigen) {
        printf( "[hessian_from_eigen_Execute] Error: eigen is NULL\n");
        return -1;
    }

    uint32_t W = cmd->W;
    uint32_t H = cmd->H;
    uint32_t size = W * H;

    // Allocate output if not provided
    if (!cmd->hessian) {
        cmd->hessian = (Hessian2D*)malloc(size * sizeof(Hessian2D));
        if (!cmd->hessian) {
            printf( "[hessian_from_eigen_Execute] Error: failed to allocate %u bytes\n",
                    (unsigned)(size * sizeof(Hessian2D)));
            return -1;
        }
    }

    // Reconstruct Hessian from outer product: H = λ * v ⊗ v
    for (uint32_t i = 0; i < size; i++) {
        float lambda = cmd->eigen[i].value;
        float vx = cmd->eigen[i].vector.x;
        float vy = cmd->eigen[i].vector.y;

        // H = λ * [vx*vx  vx*vy]
        //         [vx*vy  vy*vy]
        cmd->hessian[i].xx = lambda * vx * vx;
        cmd->hessian[i].xy = lambda * vx * vy;
        cmd->hessian[i].yy = lambda * vy * vy;
    }

    return 0;
}

void hessian_from_eigen_Free(HessianFromEigenCmd* cmd) {
    if (cmd && cmd->hessian) {
        free(cmd->hessian);
        cmd->hessian = NULL;
    }
}
