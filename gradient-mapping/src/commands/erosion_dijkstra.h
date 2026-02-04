#ifndef EROSION_DIJKSTRA_H
#define EROSION_DIJKSTRA_H

#include <stdint.h>
#include "sdf_layered.h"

/*
 * Per-channel Dijkstra-based interpolation for erosion images.
 *
 * Processes each RGBA channel independently using the SDF algorithm
 * with configurable distance metrics.
 */

typedef struct {
    /* Input */
    const uint8_t *src;     /* RGBA image data (W * H * 4 bytes) */
    uint32_t W, H;

    /* Distance metric parameters */
    SDFDistanceParams dist_params;

    /* Color neighbor mapping (optional, 256 entries as u8vec4) */
    /* If NULL, uses V-1 and V+1 directly */
    /* Otherwise, colors_used[V].{x,y,z,w} gives the "previous" color for each channel */
    /* 255 means no previous color exists for that value */
    const void *colors_used;  /* pointer to u8vec4[256] array */

    /* Pre-computed region labels (optional) */
    /* If NULL, computes labels internally per channel */
    const int32_t *labels;
    uint32_t num_regions;

    /* Output */
    float *output;          /* [W * H * 4] floats, one per channel per pixel */

    int dbg;
} ErosionDijkstraCmd;

/*
 * Initialize the command. Allocates output buffer.
 * Returns 0 on success, -1 on error.
 */
int ed_Initialize(ErosionDijkstraCmd *cmd, const uint8_t *src, uint32_t W, uint32_t H,
                  const SDFDistanceParams *params, int dbg);

/*
 * Run Dijkstra interpolation on all 4 channels.
 * Returns 0 on success, -1 on error.
 */
int ed_Execute(ErosionDijkstraCmd *cmd);

/*
 * Free allocated resources.
 */
void ed_Free(ErosionDijkstraCmd *cmd);

#endif /* EROSION_DIJKSTRA_H */
