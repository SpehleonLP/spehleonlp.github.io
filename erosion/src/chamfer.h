#ifndef CHAMFER_H
#define CHAMFER_H

#include <stdint.h>

/*
 * Simple chamfer distance transform.
 * For each pixel, finds the nearest pixel with a different value.
 */

typedef struct {
    int16_t x, y;
} ChamferPoint;

typedef struct ChamferCmd {
    /* Input */
    const uint8_t *src;     /* Source image (W x H) */
    uint32_t W, H;

    /* Output (caller allocates W x H array) */
    ChamferPoint *nearest;  /* For each pixel: coords of nearest different-value pixel */
    float *distance;        /* Optional: distance to that pixel (can be NULL) */
} ChamferCmd;

/*
 * Compute chamfer transform.
 * For each pixel at (x,y), finds nearest pixel with src[nearest] != src[x,y].
 * Returns 0 on success, negative on error.
 */
int chamfer_compute(ChamferCmd *cmd);

#endif /* CHAMFER_H */
