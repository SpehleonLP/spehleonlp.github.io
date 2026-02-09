#ifndef SMART_BLUR_H
#define SMART_BLUR_H

#include <stdint.h>

// Smart blur context - diffuses values while respecting per-pixel constraints
// Input is int16_t (-1 = transparent/no envelope), output is float
// Constraints: min = input - 1, max = input
typedef struct {
    int width;
    int height;
    int16_t* input;     // Original input values (int16_t, -1 = transparent)
    float* output;      // Current output values (float)
    float* temp;        // Temporary buffer for iteration
} SmartBlurContext;

// Initialize smart blur context
SmartBlurContext* sb_Initialize(int width, int height);
void sb_Setup(SmartBlurContext*);

// Set input value for a pixel (-1 = transparent/no envelope)
void sb_SetValue(SmartBlurContext* ctx, int x, int y, int16_t val);

// Run blur until convergence
// Returns number of iterations performed
int sb_RunUntilConverged(SmartBlurContext* ctx, float convergence_threshold, int max_iterations);

// Get the output value at a pixel
float sb_GetValue(SmartBlurContext* ctx, int x, int y);

// Cleanup
void sb_Free(SmartBlurContext* ctx);

#endif // SMART_BLUR_H
