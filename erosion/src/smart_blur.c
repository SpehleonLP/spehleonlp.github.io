#include "smart_blur.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static float clamp_f32(float a, float b, float c) {
    if (a < b) return b;
    if (a > c) return c;
    return a;
}

// Initialize smart blur context
SmartBlurContext* sb_Initialize(int width, int height)
{
    SmartBlurContext* ctx = malloc(sizeof(SmartBlurContext));
    if (!ctx) return NULL;

    ctx->width = width;
    ctx->height = height;

    int size = width * height;
    ctx->values = calloc(size, sizeof(float));
    ctx->min_values = calloc(size, sizeof(float));
    ctx->max_values = calloc(size, sizeof(float));
    ctx->temp_values = calloc(size, sizeof(float));

    if (!ctx->values || !ctx->min_values || !ctx->max_values || !ctx->temp_values) {
        sb_Free(ctx);
        return NULL;
    }

    return ctx;
}

// Set constraints for a pixel
void sb_SetConstraints(SmartBlurContext* ctx, int x, int y, float min_val, float max_val, float initial_val)
{
    if (x < 0 || x >= ctx->width || y < 0 || y >= ctx->height) return;

    int idx = y * ctx->width + x;
    ctx->min_values[idx] = min_val;
    ctx->max_values[idx] = max_val;
    ctx->values[idx] = clamp_f32(initial_val, min_val, max_val);
}

// Perform one iteration of constrained blur
// Returns the maximum change across all pixels
float sb_Iterate(SmartBlurContext* ctx)
{
    int W = ctx->width;
    int H = ctx->height;
    float max_change = 0.0f;

    // Compute blurred values into temp buffer
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int idx = y * W + x;

            // Skip pixels with invalid constraints
            if (ctx->min_values[idx] > ctx->max_values[idx]) {
                ctx->temp_values[idx] = ctx->values[idx];
                continue;
            }

            // Box blur with 3x3 kernel
            float sum = 0;
            int count = 0;

            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx;
                    int ny = y + dy;

                    if (nx >= 0 && nx < W && ny >= 0 && ny < H) {
                        int nidx = ny * W + nx;
                        // Only include neighbors with valid constraints
                        if (ctx->min_values[nidx] <= ctx->max_values[nidx]) {
                            sum += ctx->values[nidx];
                            count++;
                        }
                    }
                }
            }

            // Compute blurred value
            float blurred = (count > 0) ? (sum / count) : ctx->values[idx];

            // Clamp to this pixel's constraints
            float clamped = clamp_f32(blurred, ctx->min_values[idx], ctx->max_values[idx]);

            ctx->temp_values[idx] = clamped;

            // Track max change for convergence detection
            float change = fabsf(clamped - ctx->values[idx]);
            if (change > max_change) {
                max_change = change;
            }
        }
    }

    // Copy temp back to values
    memcpy(ctx->values, ctx->temp_values, W * H * sizeof(float));

    return max_change;
}

// Run blur until convergence
int sb_RunUntilConverged(SmartBlurContext* ctx, float convergence_threshold, int max_iterations)
{
    for (int iter = 0; iter < max_iterations; iter++) {
        float max_change = sb_Iterate(ctx);

        if (max_change < convergence_threshold) {
            return iter + 1; // Return number of iterations
        }
    }

    return max_iterations; // Didn't fully converge
}

// Get the value at a pixel
float sb_GetValue(SmartBlurContext* ctx, int x, int y)
{
    if (x < 0 || x >= ctx->width || y < 0 || y >= ctx->height) return -1.0f;
    return ctx->values[y * ctx->width + x];
}

// Cleanup
void sb_Free(SmartBlurContext* ctx)
{
    if (ctx) {
        free(ctx->values);
        free(ctx->min_values);
        free(ctx->max_values);
        free(ctx->temp_values);
        free(ctx);
    }
}
