// sources/worley_noise.h
// Worley (cellular) noise data source

#ifndef WORLEY_NOISE_H
#define WORLEY_NOISE_H

#include "types.h"

typedef enum {
    WORLEY_METRIC_EUCLIDEAN = 0,
    WORLEY_METRIC_MANHATTAN = 1,
    WORLEY_METRIC_CHEBYSHEV = 2
} WorleyMetric;

typedef enum {
    WORLEY_MODE_F1 = 0,      // Distance to closest point
    WORLEY_MODE_F2 = 1,      // Distance to second closest point
    WORLEY_MODE_F2_F1 = 2    // F2 - F1 (cell edges)
} WorleyMode;

typedef struct {
    float scale;        // Cell scale (larger = more cells)
    float jitter;       // Point jitter [0, 1] (0 = grid, 1 = random)
    WorleyMetric metric;
    WorleyMode mode;
} WorleyNoiseParams;

// Generate Worley noise
// dst: output buffer (W * H vec3s)
// src: optional input for coordinate offset (can be NULL)
// Blue channel contains the noise value [0, 1]
void worley_noise(vec3* dst, const vec3* src, int W, int H,
                  WorleyNoiseParams params, uint32_t seed);

#endif // WORLEY_NOISE_H
