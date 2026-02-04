// sources/perlin_noise.h
// Perlin (gradient) noise data source

#ifndef PERLIN_NOISE_H
#define PERLIN_NOISE_H

#include "types.h"

typedef struct {
    float scale;        // Base frequency scale
    float octaves;      // Number of octaves (fractal layers)
    float persistence;  // Amplitude decay per octave [0, 1]
    float lacunarity;   // Frequency multiplier per octave [1, 4]
} PerlinNoiseParams;

// Generate Perlin noise
// dst: output buffer (W * H vec3s)
// src: optional input for coordinate offset (can be NULL)
// Blue channel contains the noise value [0, 1]
void perlin_noise(vec3* dst, const vec3* src, int W, int H,
                  PerlinNoiseParams params, uint32_t seed);

#endif // PERLIN_NOISE_H
