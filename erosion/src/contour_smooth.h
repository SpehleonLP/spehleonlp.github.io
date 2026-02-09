#ifndef CONTOUR_SMOOTH_H
#define CONTOUR_SMOOTH_H

#include "contour_extract.h"

/*
 * Contour smoothing for stair-step artifacts.
 *
 * For each L-corner in the contour, examines the 2x2 pixel neighborhood
 * to detect if the edge is diagonal. Diagonal edges get their corners
 * smoothed by moving them toward the midpoint of their neighbors.
 *
 * Axis-aligned edges (horizontal/vertical) are left unchanged.
 */

typedef struct ContourSmoothCmd {
    /* Input */
    const uint8_t *src;         /* Original image (for gradient sampling) */
    uint32_t W, H;              /* Image dimensions */
    ContourSet *contours;       /* Contours to smooth (modified in place) */

    /* Options */
    int radius;                 /* Sampling radius for edge detection (default 3) */
    float max_shift;            /* Maximum vertex shift in pixels (default 1.0) */

} ContourSmoothCmd;

/*
 * Smooth contour stair-steps using image gradient direction.
 * Modifies contours in place.
 * Returns 0 on success, negative on error.
 */
int contour_smooth(ContourSmoothCmd *cmd);

#endif /* CONTOUR_SMOOTH_H */
