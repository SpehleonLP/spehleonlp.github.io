// sources/noise.h
// Simple noise data source (White, Blue, Value noise)

#ifndef NOISE_H
#define NOISE_H

#include "types.h"

typedef enum {
    NOISE_TYPE_WHITE = 0,   // Pure random noise
    NOISE_TYPE_BLUE = 1,    // Low-frequency filtered (spatially decorrelated)
    NOISE_TYPE_VALUE = 2    // Interpolated grid noise
} NoiseType;

typedef struct {
    NoiseType type;
    float scale;        // Scale factor (for value noise frequency)
    float seed;         // Random seed (cast to uint32_t)
} NoiseParams;

// Generate noise
// dst: output buffer (W * H vec3s)
// src: optional input for coordinate offset (can be NULL)
// Blue channel contains the noise value [0, 1]
void noise_generate(vec3* dst, const vec3* src, int W, int H,
                    NoiseParams params, uint32_t seed);

#endif // NOISE_H
