#ifndef SDF_LAYERED_H
#define SDF_LAYERED_H

#include <stdint.h>

/* Distance metric parameters */
typedef struct {
    float minkowski;  /* -10 to +10, exponent p = exp2(minkowski) */
    float chebyshev;  /* 0-1 blend: 0 = pure Minkowski, 1 = pure Chebyshev */
} SDFDistanceParams;

/* Displacement from source pixel */
typedef struct {
    int16_t dx, dy;        /* -1,-1 = no result yet */
    int16_t source_value;  /* src value at the source pixel, 256 = invalid */
    double  distance;        /* computed distance using current metric */
} SDFCell;

/* Priority queue entry for flood fill corrections */
typedef struct {
    int32_t x, y;
    int16_t dx, dy;
    int16_t source_value;
} SDFQueueEntry;

typedef struct {
    SDFQueueEntry *data;
    int32_t size;
    int32_t capacity;
} SDFQueue;

/* Per-region tracking */
typedef struct {
    int16_t region_value;   /* the src value for this region */
    int16_t target_floor;   /* find values > this (current iteration) */
    int16_t next_floor;     /* min source_value taken this iteration (becomes target_floor next) */
} SDFRegion;

typedef struct {
    /* Input */
    uint8_t const *src;          /* original quantized image, 0 = transparent */
    uint32_t  W, H;

    /* Distance metric parameters */
    SDFDistanceParams dist_params;

    /* Region labeling (computed internally or provided) */
    uint32_t  *labels;       /* W*H array of region IDs */
    uint32_t  num_regions;
    int       owns_labels;   /* 1 if we allocated labels, 0 if passed in */

    /* Per-region state (allocated to num_regions) */
    SDFRegion *regions;

    /* Output - per pixel */
    SDFCell  *cells;        /* W*H array */

    /* Status */
    int       more_work;    /* set if higher values exist beyond what we found */
    int       dbg;
} SDFContext;

/* Initialize context, compute region labels, allocate arrays */
int sdf_Initialize(SDFContext *ctx, const uint8_t *src, uint32_t W, uint32_t H,
				   const SDFDistanceParams *params, int dbg);

/* Run one iteration: chamfer + flood corrections
 * Returns 1 if more_work, 0 if done, -1 on error */
int sdf_Iterate(SDFContext *ctx);

/* Run all iterations until done */
int sdf_Run(SDFContext *ctx);

/* Get distance for a pixel (returns -1 if no result) */
double sdf_GetDistance(SDFContext *ctx, uint32_t x, uint32_t y);

/* Compute distance using current metric parameters */
double sdf_ComputeDistance(const SDFDistanceParams *params, int16_t dx, int16_t dy);

/* Free all allocated memory */
void sdf_Free(SDFContext *ctx);

#endif /* SDF_LAYERED_H */
