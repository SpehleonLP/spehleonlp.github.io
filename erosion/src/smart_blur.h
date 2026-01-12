#ifndef SMART_BLUR_H
#define SMART_BLUR_H

// Smart blur context - diffuses values while respecting per-pixel constraints
typedef struct {
    int width;
    int height;
    float* values;      // Current values
    float* min_values;  // Minimum allowed value per pixel (error bar lower bound)
    float* max_values;  // Maximum allowed value per pixel (error bar upper bound)
    float* temp_values; // Temporary buffer for iteration
} SmartBlurContext;

// Initialize smart blur context
SmartBlurContext* sb_Initialize(int width, int height);

// Set constraints for a pixel (min/max from error bars, initial value)
void sb_SetConstraints(SmartBlurContext* ctx, int x, int y, float min_val, float max_val, float initial_val);

// Perform one iteration of constrained blur
// Returns the maximum change across all pixels
float sb_Iterate(SmartBlurContext* ctx);

// Run blur until convergence
// Returns number of iterations performed
int sb_RunUntilConverged(SmartBlurContext* ctx, float convergence_threshold, int max_iterations);

// Get the value at a pixel
float sb_GetValue(SmartBlurContext* ctx, int x, int y);

// Cleanup
void sb_Free(SmartBlurContext* ctx);

#endif // SMART_BLUR_H
