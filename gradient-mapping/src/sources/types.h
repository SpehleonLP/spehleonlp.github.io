// sources/types.h
// Common types for gradient data sources

#ifndef SOURCES_TYPES_H
#define SOURCES_TYPES_H

#include "effect_stack_api.h"
#include <stdint.h>
#include <math.h>

// Simple hash function for noise generation
static inline uint32_t hash_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// Hash a 2D coordinate to a float in [0, 1)
static inline float hash2d(int x, int y, uint32_t seed) {
    uint32_t h = hash_u32((uint32_t)x + hash_u32((uint32_t)y + seed));
    return (float)(h & 0xFFFFFFu) / (float)0x1000000u;
}

// Hash a 2D coordinate to a vec2 in [0, 1)
static inline vec2 hash2d_vec2(int x, int y, uint32_t seed) {
    uint32_t h1 = hash_u32((uint32_t)x + hash_u32((uint32_t)y + seed));
    uint32_t h2 = hash_u32(h1);
    return vec2(
        (float)(h1 & 0xFFFFFFu) / (float)0x1000000u,
        (float)(h2 & 0xFFFFFFu) / (float)0x1000000u
    );
}

// Smoothstep for interpolation
static inline float smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

// Derivative of smoothstep: 6t(1-t)
static inline float smoothstep_deriv(float t) {
    return 6.0f * t * (1.0f - t);
}

// Quintic smoothstep (better for Perlin)
static inline float quintic(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// Derivative of quintic smoothstep: 30t^2(t-1)^2
static inline float quintic_deriv(float t) {
    return 30.0f * t * t * (t - 1.0f) * (t - 1.0f);
}

// Linear interpolation
static inline float lerpf(float a, float b, float t) {
    return a + t * (b - a);
}

// Clamp
static inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

// Fract (fractional part)
static inline float fractf(float x) {
    return x - floorf(x);
}

#endif // SOURCES_TYPES_H
