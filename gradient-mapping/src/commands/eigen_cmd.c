#include "eigen_cmd.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

/*
 * Compute eigendecomposition of a 2x2 symmetric matrix
 *
 * Given H = [xx xy]
 *           [xy yy]
 *
 * Returns eigenvalues and eigenvectors sorted by magnitude.
 */
static void eigen_2x2_symmetric(float xx, float xy, float yy,
                                 EigenVec2* ev1, EigenVec2* ev2) {
    // Compute eigenvalues using closed-form solution
    // Use numerically stable form: discriminant = sqrt((xx-yy)² + 4*xy²)
    // instead of sqrt(trace² - 4*det) which suffers from catastrophic cancellation
    float trace = xx + yy;
    float diff = xx - yy;
    float discriminant = sqrtf(diff * diff + 4.0f * xy * xy);

    if (!isnormal(discriminant)) {
        discriminant = 0.0f;
    }

    float lambda1 = (trace + discriminant) * 0.5f;
    float lambda2 = (trace - discriminant) * 0.5f;

    // Compute eigenvectors
    vec2 v1, v2;
    
    if (fabsf(xy) > 1e-8f) {
        // Non-diagonal case: use standard formula
        // v1 = [lambda1 - yy, xy]
        v1.x = lambda1 - yy;
        v1.y = xy;

        // v2 = [lambda2 - yy, xy]
        v2.x = lambda2 - yy;
        v2.y = xy;

        // Normalize
        float len1 = sqrtf(v1.x * v1.x + v1.y * v1.y);
        float len2 = sqrtf(v2.x * v2.x + v2.y * v2.y);

        if (len1 > 1e-8f) {
            v1.x /= len1;
            v1.y /= len1;
        } else {
            v1.x = 1.0f;
            v1.y = 0.0f;
        }

        if (len2 > 1e-8f) {
            v2.x /= len2;
            v2.y /= len2;
        } else {
            v2.x = 0.0f;
            v2.y = 1.0f;
        }
    } else {  
        // Diagonal case: eigenvectors are axis-aligned
        if (xx > yy) {
            v1.x = 1.0f; v1.y = 0.0f;
            v2.x = 0.0f; v2.y = 1.0f;
        } else {
            v1.x = 0.0f; v1.y = 1.0f;
            v2.x = 1.0f; v2.y = 0.0f;
        }
    }

    ev1->vector = v1;
    ev1->value = lambda1;
    ev2->vector = v2;
    ev2->value = lambda2;
}

int eigen_Execute(EigenDecomposeCmd* cmd) {
    if (!cmd) {
        printf( "[eigen_Execute] Error: cmd is NULL\n");
        return -1;
    }

    if (!cmd->hessian) {
        printf( "[eigen_Execute] Error: hessian is NULL\n");
        return -1;
    }

    uint32_t W = cmd->W;
    uint32_t H = cmd->H;
    uint32_t size = W * H;

    // Allocate outputs if not provided
    if (!cmd->major) {
        cmd->major = (EigenVec2*)malloc(size * sizeof(EigenVec2));
        if (!cmd->major) {
            printf( "[eigen_Execute] Error: failed to allocate %u bytes for major\n",
                    (unsigned)(size * sizeof(EigenVec2)));
            return -1;
        }
    }

    if (!cmd->minor) {
        cmd->minor = (EigenVec2*)malloc(size * sizeof(EigenVec2));
        if (!cmd->minor) {
            printf( "[eigen_Execute] Error: failed to allocate %u bytes for minor\n",
                    (unsigned)(size * sizeof(EigenVec2)));
            free(cmd->major);
            cmd->major = NULL;
            return -1;
        }
    }
    
    // Compute eigendecomposition for each pixel
    for (uint32_t i = 0; i < size; i++) {
        const Hessian2D* h = &cmd->hessian[i];
        EigenVec2 ev1, ev2;

        eigen_2x2_symmetric(h->xx, h->xy, h->yy, &ev1, &ev2);

        // Sort by magnitude: major = larger, minor = smaller
        if (fabsf(ev1.value) >= fabsf(ev2.value)) {
            cmd->major[i] = ev1;
            cmd->minor[i] = ev2;
        } else {
            cmd->major[i] = ev2;
            cmd->minor[i] = ev1;
        }
    }

    return 0;
}

void eigen_Free(EigenDecomposeCmd* cmd) {
    if (cmd) {
        if (cmd->major) {
            free(cmd->major);
            cmd->major = NULL;
        }
        if (cmd->minor) {
            free(cmd->minor);
            cmd->minor = NULL;
        }
    }
}
