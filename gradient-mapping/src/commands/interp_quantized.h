#ifndef INTERP_QUANTIZED_H
#define INTERP_QUANTIZED_H

#include <stdint.h>
#include "sdf_layered.h"

/* Per-pixel interpolation data */
typedef struct {
    float dist_lower;    /* distance to V-1, -1 if none */
    float dist_higher;   /* distance to V+1, -1 if none */
    int16_t dx_lower, dy_lower;   /* displacement to V-1 boundary pixel */
    int16_t dx_higher, dy_higher; /* displacement to V+1 boundary pixel */
} InterpPixel;

/* Per-region interpolation data */
typedef struct {
    float max_dist_lower;   /* max distance to V-1 across region */
    float max_dist_higher;  /* max distance to V+1 across region */
} InterpRegion;

typedef struct {
    /* Input */
    const uint8_t *src;   /* 0 = transparent/no envelope */
    const uint8_t *prev_color; /* > index = no prev color, < index = V-1  */
    uint32_t W, H;
    int dbg;

    /* Distance metric parameters */
    SDFDistanceParams dist_params;

    /* Intermediate (computed internally or passed in) */
    uint32_t num_regions;
    uint32_t *labels;          /* from label_regions */
    InterpPixel *pixels;      /* [W * H] */
    InterpRegion *regions;    /* [num_regions] */

    /* Output */
    float *output;            /* [W * H] interpolated values [0, 255] */
    
    uint8_t next_color[256];
} InterpolateQuantizedCmd;

/*
 * Initialize the command, allocate intermediate storage.
 * params: distance metric parameters (NULL for Euclidean default)
 * Returns 0 on success, -1 on error.
 */
int iq_Initialize(InterpolateQuantizedCmd *cmd, const uint8_t *src, const uint8_t *prev_color, uint32_t W, uint32_t H,
				  const SDFDistanceParams *params, int dbg);

/*
 * Run the full interpolation pipeline:
 * 1. Run SDF to find distances to V-1 and V+1 for each pixel
 * 2. Compute max distances per region
 * 3. Interpolate output values
 * Returns 0 on success, -1 on error.
 */
int iq_Execute(InterpolateQuantizedCmd *cmd);

/*
 * Run only the SDF flood fill, populating dx/dy displacements and
 * dist_lower/dist_higher. Does NOT compute region max distances or
 * interpolate — call iq_ExecuteFromDistances afterwards.
 * Returns 0 on success, -1 on error.
 */
int iq_ExecuteSDF(InterpolateQuantizedCmd *cmd);

/*
 * Run only the max-distance and interpolation steps on pre-populated pixels.
 * Caller must have already filled cmd->pixels[].dist_lower/dist_higher.
 * Returns 0 on success, -1 on error.
 */
int iq_ExecuteFromDistances(InterpolateQuantizedCmd *cmd);

/*
 * Free all allocated memory.
 */
void iq_Free(InterpolateQuantizedCmd *cmd);

#endif /* INTERP_QUANTIZED_H */
