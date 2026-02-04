#ifndef CREATE_ENVELOPES_H
#define CREATE_ENVELOPES_H

#include "../effect_stack_api.h"
#include <stdint.h>

// Define the ImageData structure
// make image pads the data so it can hold the next-power-2 up from the given size. 
typedef struct ImageData {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    // ensure pointer has 8 byte alignment.
	uint32_t pad00;
    uint8_t* data; // GL_RGBA_8888
} ImageData;

union Color
{
	struct
	{
		uint8_t r;
		uint8_t g;
		uint8_t b;
		uint8_t a;
	};

	uint32_t c;
};

ImageData * MakeImage(int width, int height, int depth);

typedef struct MinMax
{
	int min, max;
} MinMax;

typedef struct EnvelopeMetadata
{
	int total_frames;
	MinMax start_attack_frame;
	MinMax end_attack_frame;
	MinMax start_release_frame;
	MinMax end_release_frame;
	union Color key;
	
	float min_quantization;
	float max_quantization;
	u8vec4 colors_used[256];
} EnvelopeMetadata;

typedef struct EnvelopeBuilder EnvelopeBuilder;

// Quantization impact analysis results
typedef struct {
    int total_pixels;
    int pixels_with_envelope;      // pixels that have any envelope (raw or quant)
    int pixels_same_envelope;      // pixels where raw == quant envelope
    int pixels_different_envelope; // pixels where raw != quant envelope

    // For pixels with different envelopes, what's the minimum quantization
    // that would have preserved the raw envelope?
    float min_quant_for_same;      // minimum across all differing pixels
    float max_quant_for_same;      // maximum across all differing pixels
    float avg_quant_for_same;      // average across all differing pixels

    // Breakdown of which field caused the difference
    int diff_attack_start;
    int diff_attack_end;
    int diff_release_start;
    int diff_release_end;
} QuantizationAnalysis;

EnvelopeBuilder * e_Initialize(int width, int height);
int e_ProcessFrame(EnvelopeBuilder * env, ImageData const* src, int frame_id, float target_quantization);
int e_Build(EnvelopeBuilder * env, ImageData * dst, EnvelopeMetadata * out, int total_frames);
void e_Free(EnvelopeBuilder*);

// Quantization analysis - call after all frames processed but before e_Build
int e_AnalyzeQuantization(EnvelopeBuilder* builder, QuantizationAnalysis* out);
void e_PrintQuantizationAnalysis(QuantizationAnalysis* a);

#endif
