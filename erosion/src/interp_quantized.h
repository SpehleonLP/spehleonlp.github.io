#ifndef INTERP_QUANTIZED_H
#define INTERP_QUANTIZED_H

#include <stdint.h>

/* Per-pixel interpolation data */
typedef struct {
    float dist_lower;    /* distance to V-1, -1 if none */
    float dist_higher;   /* distance to V+1, -1 if none */
} InterpPixel;

/* Per-region interpolation data */
typedef struct {
    float max_dist_lower;   /* max distance to V-1 across region */
    float max_dist_higher;  /* max distance to V+1 across region */
} InterpRegion;

typedef struct {
    /* Input */
    const int16_t *src;   /* -1 = transparent/no envelope */
    uint32_t W, H;
    int dbg;

    /* Intermediate (computed internally) */
    uint32_t num_regions;
    int32_t *labels;          /* from label_regions */
    InterpPixel *pixels;      /* [W * H] */
    InterpRegion *regions;    /* [num_regions] */

    /* Output */
    float *output;            /* [W * H] interpolated values [0, 255] */
} InterpolateQuantizedCmd;

/*
 * Initialize the command, allocate intermediate storage.
 * Returns 0 on success, -1 on error.
 */
int iq_Initialize(InterpolateQuantizedCmd *cmd, const int16_t *src, uint32_t W, uint32_t H, int dbg);

/*
 * Run the full interpolation pipeline:
 * 1. Run SDF to find distances to V-1 and V+1 for each pixel
 * 2. Compute max distances per region
 * 3. Interpolate output values
 * Returns 0 on success, -1 on error.
 */
int iq_Execute(InterpolateQuantizedCmd *cmd);

/*
 * Free all allocated memory.
 */
void iq_Free(InterpolateQuantizedCmd *cmd);

#endif /* INTERP_QUANTIZED_H */
