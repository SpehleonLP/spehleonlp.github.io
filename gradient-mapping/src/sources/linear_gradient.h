// sources/linear_gradient.h
// Linear gradient data source

#ifndef LINEAR_GRADIENT_H
#define LINEAR_GRADIENT_H

#include "types.h"

typedef struct {
    float angle;   // Radians, direction of gradient
    float scale;   // Scale factor (larger = more repetitions)
    float offset;  // Offset in gradient space [-1, 1]
} LinearGradientParams;

// Generate linear gradient
// dst: output buffer (W * H vec3s)
// src: optional input for coordinate offset (can be NULL)
// Blue channel contains the gradient value [0, 1]
void linear_gradient(vec3* dst, const vec3* src, int W, int H,
                     LinearGradientParams params, uint32_t seed);

#endif // LINEAR_GRADIENT_H
