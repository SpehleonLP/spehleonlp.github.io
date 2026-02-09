#ifndef CONTOUR_EXTRACT_H
#define CONTOUR_EXTRACT_H

#include <stdint.h>

/*
 * Contour extraction using marching squares.
 * Extracts boundaries between different value levels as polylines.
 */

/* Contour point in normalized [0,1] space */
typedef struct {
    float x, y;  /* Normalized coordinates: 0 = left/top edge, 1 = right/bottom edge */
} ContourPoint;

typedef struct {
    ContourPoint *points;
    int32_t num_points;
    int32_t capacity;       /* Internal use */
    uint8_t value_low;      /* Value on the "inside" (low) side */
    uint8_t value_high;     /* Value on the "outside" (high) side */
    int closed;             /* 1 if polyline forms a closed loop */
} ContourLine;

typedef struct {
    ContourLine *lines;
    int32_t num_lines;
    int32_t capacity;       /* Internal use */
} ContourSet;

typedef struct ContourExtractCmd {
    /* Input */
    const uint8_t *src;
    uint32_t W, H;

    /* Options */
    int extract_all_levels;     /* If 1, extract boundaries between all adjacent values */
    uint8_t threshold;          /* If extract_all_levels=0, extract contour at this threshold */
    float simplify_epsilon;     /* Douglas-Peucker simplification tolerance (0 = no simplification) */

    /* Output (allocated by contour_extract) */
    ContourSet *result;

} ContourExtractCmd;

/*
 * Extract contours from image.
 * Returns 0 on success, negative on error.
 * Caller must call contour_free() to release result.
 */
int contour_extract(ContourExtractCmd *cmd);

/*
 * Free contour set and all contained polylines.
 */
void contour_free(ContourSet *set);

/*
 * Utility: append a point to a contour line.
 */
void contour_line_append(ContourLine *line, float x, float y);

/*
 * Utility: create new contour line in set.
 */
ContourLine *contour_set_new_line(ContourSet *set, uint8_t val_low, uint8_t val_high);

#include "debug_png.h"
#if DEBUG_IMG_OUT
/*
 * Render contours to RGB image for debugging.
 * Contour points are in normalized [0,1] space, scaled to output W x H.
 * Returns malloc'd buffer (W x H x 3), caller must free().
 * src_gray: optional background image at src_W x src_H (can be NULL)
 */
uint8_t *render_contours(const ContourSet *set, uint32_t W, uint32_t H,
                         const uint8_t *src_gray, uint32_t src_W, uint32_t src_H);

/* Export contour visualization to PNG file */
int debug_export_contours(const char *path, const ContourSet *set,
                          uint32_t W, uint32_t H,
                          const uint8_t *src_gray, uint32_t src_W, uint32_t src_H);
#endif

#endif /* CONTOUR_EXTRACT_H */
