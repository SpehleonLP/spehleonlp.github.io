// sources/curl_noise.h
// Curl noise data source (swirling patterns derived from Perlin)

#ifndef CURL_NOISE_H
#define CURL_NOISE_H

#include "types.h"

typedef struct {
    float scale;        // Base frequency scale
    float octaves;      // Number of octaves (fractal layers)
    float persistence;  // Amplitude decay per octave [0, 1]
    float lacunarity;   // Frequency multiplier per octave [1, 4]
} CurlNoiseParams;

// Generate curl noise
// dst: output buffer (W * H vec3s)
// src: optional input for coordinate offset (can be NULL)
// Blue channel contains the curl noise value [0, 1]
void curl_noise(vec3* dst, const vec3* src, int W, int H,
                CurlNoiseParams params, uint32_t seed);

#endif // CURL_NOISE_H
