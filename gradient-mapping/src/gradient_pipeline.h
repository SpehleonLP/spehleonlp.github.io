// gradient_pipeline.h
// Gradient ramp texture generation pipeline

#ifndef GRADIENT_PIPELINE_H
#define GRADIENT_PIPELINE_H
#include "effect_stack_api.h"
#include <stdint.h>

enum 
{
	/* Default output dimensions for gradient stack (no source file needed) */
	DEFAULT_GRADIENT_W = 128,
	DEFAULT_GRADIENT_H = 128
};	

// Color ramp definition for pipeline (separate from effect_stack_api.h ColorRamp)
typedef struct {
    ColorStop *stops;
    int stop_count;
    int blend_mode;     // 0=Normal, 1=Multiply, 2=Screen, 3=Overlay, 4=Add, 5=Subtract
    float opacity;
} GPColorRamp;

// Pipeline context
typedef struct {
    vec3 *front;     // Current source buffer (R/G=perturbation, B=value)
    vec3 *back;      // Previous source buffer (for chaining)
    vec4 *output;    // Accumulated RGBA output
    int width, height;
    uint32_t seed;
    int has_output;     // Whether output has been written to
} GradientPipeline;

uint8_t* process_gradient_stack(const Effect *effects, int effect_count, int* out_w, int* out_h);

// Pipeline lifecycle
int gp_init(GradientPipeline *ctx, int width, int height, uint32_t seed);
void gp_free(GradientPipeline *ctx);
void gp_reset(GradientPipeline *ctx);  // Clear buffers for new generation

// Apply a data source (reads from back for perturbation, writes to front)
// Source functions should fill R/G with perturbation vectors, B with value
// Takes the Effect struct directly - no float array conversion needed
void gp_apply_source(GradientPipeline *ctx, const Effect *e);

// Apply color ramp - samples front.z, blends into output, clears front/back
void gp_apply_color_ramp(GradientPipeline *ctx, const GPColorRamp *ramp);

// Finalize - if no color ramp was applied, convert front to greyscale output
// Returns pointer to RGBA uint8 data (width * height * 4 bytes)
uint8_t *gp_finalize(GradientPipeline *ctx);

void gradient_memo_clear(void);

#endif // GRADIENT_PIPELINE_H
