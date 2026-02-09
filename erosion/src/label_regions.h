#ifndef LABEL_REGIONS_H
#define LABEL_REGIONS_H

#include <stdint.h>

/*
 * Connected components labeling.
 * Assigns unique integer IDs to each contiguous region of same-value pixels.
 */

typedef enum {
    LABEL_CONNECT_4 = 4,   /* Cardinal neighbors only */
    LABEL_CONNECT_8 = 8    /* Include diagonals */
} LabelConnectivity;

typedef struct LabelRegionsCmd {
    /* Input */
    const int16_t *src;         /* Source image (W x H), -1 = transparent */
    uint32_t W, H;
    LabelConnectivity connectivity;

    /* Output (caller allocates W x H array) */
    int32_t *labels;            /* Region ID for each pixel (0 = background/unlabeled) */
    int32_t num_regions;        /* Number of regions found (set by label_regions) */
} LabelRegionsCmd;

/*
 * Label connected components.
 * Each contiguous region of same-value pixels gets a unique ID (1, 2, 3, ...).
 * Returns 0 on success, negative on error.
 */
int label_regions(LabelRegionsCmd *cmd);

#endif /* LABEL_REGIONS_H */
