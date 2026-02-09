#include "smart_blur.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static float clamp_f32(float val, float min_val, float max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

// Initialize smart blur context
SmartBlurContext* sb_Initialize(int width, int height)
{
    int size = width * height;

    SmartBlurContext* ctx = calloc(1, sizeof(SmartBlurContext) + size * (sizeof(float) + sizeof(int16_t)));
    if (!ctx) return NULL;

    ctx->width = width;
    ctx->height = height;

    ctx->output = (float*)(ctx+1);
    ctx->input = (int16_t*)(ctx->output + size);

    // Initialize input to -1 (transparent)
    for (int i = 0; i < size; i++) {
        ctx->input[i] = -1;
    }

    return ctx;
}

void sb_Setup(SmartBlurContext* ctx)
{
	(void)ctx;
}

// Set input value for a pixel (-1 = transparent/no envelope)
void sb_SetValue(SmartBlurContext* ctx, int x, int y, int16_t val)
{
    if (x < 0 || x >= ctx->width || y < 0 || y >= ctx->height) return;

    int idx = y * ctx->width + x;
    ctx->input[idx] = val;
    ctx->output[idx] = (float)val;
}

// Perform one iteration of constrained blur
// Returns the maximum change across all pixels
static float sb_Iterate(SmartBlurContext* ctx, int red, int clamp)
{
    int W = ctx->width;
    int H = ctx->height;
    float max_change = 0.0f;

    // Compute blurred values into temp buffer
    for (int y = 0; y < H; y++) {
        for (int x = (y+red)%2; x < W; x += 2) {
            int idx = y * W + x;

			float old =  ctx->output[idx];
            // Constraints derived from input: min = input - 1, max = input
            float max_val = (float)ctx->input[idx];
        //    float min_val = max_val - 1.0f;

			if(max_val < 0)  /* -1 = transparent */
			{
				continue;
			}

            // Box blur with 3x3 kernel
            float sum = 0;
            int count = 0;
            int is_highest = clamp;

            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx;
                    int ny = y + dy;

                    if (nx >= 0 && nx < W && ny >= 0 && ny < H) {
                        int nidx = ny * W + nx;
                        
                        if(ctx->output[nidx] >= 0)
                        {
							sum += ctx->output[nidx];
							count++;

							is_highest &= (ctx->output[nidx] <= ctx->output[idx]);

							if(clamp && ctx->input[nidx]-1 == ctx->input[idx])
							{
								goto is_neighbor_clamped;
							}
						}
					}
                }
            }
            
            if(is_highest)
				 continue;
            
            // Compute blurred value
            float blurred = (count > 0) ? (sum / count) : ctx->output[idx];

            // Clamp to this pixel's constraints
            float clamped = blurred;
    //        if(clamp)	clamped = clamp_f32(clamped, min_val, max_val);
            
            ctx->output[idx] = clamped;

            // Track max change for convergence detection
            float change = fabsf(clamped - old);
            if (change > max_change) {
                max_change = change;
            }
            continue;
            
is_neighbor_clamped:
			ctx->output[idx] = ctx->input[idx];
        }
    }
	

    return max_change;
}

// Run blur until convergence
int sb_RunUntilConverged(SmartBlurContext* ctx, float convergence_threshold, int max_iterations)
{
	int iter = 0;
    for (iter = 0; iter < max_iterations; iter++) {
        float max_change = sb_Iterate(ctx, 0, 1);
        max_change += sb_Iterate(ctx, 1, 1);

        if (max_change < convergence_threshold) {
            break; // Return number of iterations
        }
    }

    for (int i = 0; i < 3; i++) {
        sb_Iterate(ctx, 0, 0);
        sb_Iterate(ctx, 1, 0);
    }
    
    return iter; // Didn't fully converge
}

// Get the output value at a pixel
float sb_GetValue(SmartBlurContext* ctx, int x, int y)
{
    if (x < 0 || x >= ctx->width || y < 0 || y >= ctx->height) return -1.0f;
    return ctx->output[y * ctx->width + x];
}

// Cleanup
void sb_Free(SmartBlurContext* ctx)
{
    free(ctx);
}
