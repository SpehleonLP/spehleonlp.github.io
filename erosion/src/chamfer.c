#include "chamfer.h"
#include <stdlib.h>
#include <float.h>

/*
 * Two-pass chamfer distance transform.
 * Forward pass: top-left to bottom-right
 * Backward pass: bottom-right to top-left
 *
 * Uses 3-4 chamfer weights for better approximation:
 *   orthogonal neighbors: 3
 *   diagonal neighbors: 4
 * Scale factor: 1/3 to get approximate Euclidean distance
 */

#define CHAMFER_ORTHO 3
#define CHAMFER_DIAG  4
#define CHAMFER_SCALE (1.0f / 3.0f)

/* Large value representing "no source found yet" */
#define CHAMFER_INF 0x7FFFFFFF

int chamfer_compute(ChamferCmd *cmd)
{
    if (!cmd || !cmd->src || !cmd->nearest)
        return -1;
    if (cmd->W == 0 || cmd->H == 0)
        return -1;

    uint32_t W = cmd->W;
    uint32_t H = cmd->H;
    uint32_t N = W * H;

    const uint8_t *src = cmd->src;
    ChamferPoint *nearest = cmd->nearest;

    /* Temporary distance buffer (integer chamfer distances) */
    int32_t *dist = malloc(N * sizeof(int32_t));
    if (!dist) return -2;

    /* Initialize: boundary pixels (neighbors with different value) get dist=0 */
    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            uint32_t i = y * W + x;
            uint8_t v = src[i];
            int is_boundary = 0;

            /* Check 4-connected neighbors for different value */
            if (x > 0     && src[i - 1] != v) is_boundary = 1;
            if (x < W - 1 && src[i + 1] != v) is_boundary = 1;
            if (y > 0     && src[i - W] != v) is_boundary = 1;
            if (y < H - 1 && src[i + W] != v) is_boundary = 1;

            if (is_boundary) {
                dist[i] = 0;
                nearest[i].x = (int16_t)x;
                nearest[i].y = (int16_t)y;
            } else {
                dist[i] = CHAMFER_INF;
                nearest[i].x = -1;
                nearest[i].y = -1;
            }
        }
    }

    /* Forward pass: top-left to bottom-right */
    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            uint32_t i = y * W + x;
            int32_t d = dist[i];
            ChamferPoint p = nearest[i];

            /* Check neighbors already visited */
            /* Left */
            if (x > 0) {
                int32_t nd = dist[i - 1] + CHAMFER_ORTHO;
                if (nd < d) {
                    d = nd;
                    p = nearest[i - 1];
                }
            }
            /* Top */
            if (y > 0) {
                int32_t nd = dist[i - W] + CHAMFER_ORTHO;
                if (nd < d) {
                    d = nd;
                    p = nearest[i - W];
                }
            }
            /* Top-left */
            if (x > 0 && y > 0) {
                int32_t nd = dist[i - W - 1] + CHAMFER_DIAG;
                if (nd < d) {
                    d = nd;
                    p = nearest[i - W - 1];
                }
            }
            /* Top-right */
            if (x < W - 1 && y > 0) {
                int32_t nd = dist[i - W + 1] + CHAMFER_DIAG;
                if (nd < d) {
                    d = nd;
                    p = nearest[i - W + 1];
                }
            }

            dist[i] = d;
            nearest[i] = p;
        }
    }

    /* Backward pass: bottom-right to top-left */
    for (int32_t y = (int32_t)H - 1; y >= 0; y--) {
        for (int32_t x = (int32_t)W - 1; x >= 0; x--) {
            uint32_t i = (uint32_t)y * W + (uint32_t)x;
            int32_t d = dist[i];
            ChamferPoint p = nearest[i];

            /* Check neighbors already visited in backward pass */
            /* Right */
            if ((uint32_t)x < W - 1) {
                int32_t nd = dist[i + 1] + CHAMFER_ORTHO;
                if (nd < d) {
                    d = nd;
                    p = nearest[i + 1];
                }
            }
            /* Bottom */
            if ((uint32_t)y < H - 1) {
                int32_t nd = dist[i + W] + CHAMFER_ORTHO;
                if (nd < d) {
                    d = nd;
                    p = nearest[i + W];
                }
            }
            /* Bottom-right */
            if ((uint32_t)x < W - 1 && (uint32_t)y < H - 1) {
                int32_t nd = dist[i + W + 1] + CHAMFER_DIAG;
                if (nd < d) {
                    d = nd;
                    p = nearest[i + W + 1];
                }
            }
            /* Bottom-left */
            if (x > 0 && (uint32_t)y < H - 1) {
                int32_t nd = dist[i + W - 1] + CHAMFER_DIAG;
                if (nd < d) {
                    d = nd;
                    p = nearest[i + W - 1];
                }
            }

            dist[i] = d;
            nearest[i] = p;
        }
    }

    /* Convert to float distances if requested */
    if (cmd->distance) {
        for (uint32_t i = 0; i < N; i++) {
            if (dist[i] == CHAMFER_INF) {
                cmd->distance[i] = FLT_MAX;
            } else {
                cmd->distance[i] = (float)dist[i] * CHAMFER_SCALE;
            }
        }
    }

    free(dist);
    return 0;
}
