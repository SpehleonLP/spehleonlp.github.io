#include "create_envelopes.h"
#include "../commands/fft_blur.h"
#include "../utility.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>


enum
{
	NOT_IN_ENVELOPE,
	IN_ATTACK,
	IN_SUSTAIN,
	IN_RELEASE,
	ALPHA_THRESHOLD = 16,
	NOISE_ALPHA = 32,
};

const float g_NoiseFramePercent = 0.02;

ImageData * MakeImage(int width, int height, int depth)
{
    ImageData* img = (ImageData*)malloc(sizeof(ImageData));
    if (!img) return NULL;

    img->width = width;
    img->height = height;
    img->depth = depth;
    img->pad00 = 0;
    img->data = (uint8_t*)calloc(width * height * depth, 4);  // RGBA

    if (!img->data) {
        free(img);
        return NULL;
    }

    return img;
}

#define TEST_X1 ((uint32_t)(107))
#define TEST_Y1 ((uint32_t)(118))
#define TEST_X2 ((uint32_t)(90))
#define TEST_Y2 ((uint32_t)(40))
#define TEST_IDX1 (builder->width*TEST_Y1 + TEST_X1)
#define TEST_IDX2 (builder->width*TEST_Y2 + TEST_X2)
#define PRINT_IF(...) { if((TEST_X1) == x && (TEST_Y1) == y) printf(__VA_ARGS__); }

uint8_t GetAlpha(const union Color* key, const union Color* sample)
{
    // Check if key is greyscale (for luminance-based alpha)
    int key_grey_delta = abs(key->r - key->g) + abs(key->g - key->b) + abs(key->b - key->r);
    int is_greyscale_key = (key_grey_delta < 10);

    if (is_greyscale_key) {
        // Luminance-based mode for greyscale keys
        // Key luminance
        float key_lum = (key->r * 0.299f + key->g * 0.587f + key->b * 0.114f) / 255.0f;

        // Sample luminance
        float sample_lum = (sample->r * 0.299f + sample->g * 0.587f + sample->b * 0.114f) / 255.0f;

        // Compute alpha based on luminance difference from key
        // If key is white (lum≈1): white→transparent, black→opaque
        // If key is black (lum≈0): black→transparent, white→opaque
        float lum_diff = fabsf(sample_lum - key_lum);

        // Apply smooth falloff
        float alpha = lum_diff;
        alpha = powf(alpha, 0.8f); // Slight gamma to soften the transition

        uint8_t alpha_8 = (uint8_t)clamp_i32(roundf(alpha * 255.0f), 0, 255);
        return alpha_8;
    } else {
        // Color chromakey mode using Euclidean distance
        float dr = (sample->r - key->r) / 255.0f;
        float dg = (sample->g - key->g) / 255.0f;
        float db = (sample->b - key->b) / 255.0f;

        // Euclidean distance in RGB space
        float distance = sqrtf(dr*dr + dg*dg + db*db);

        // Normalize distance (max distance in RGB cube is sqrt(3) ≈ 1.732)
        float normalized_dist = distance / 1.732f;

        // Apply steeper falloff for cleaner keying
        float alpha = powf(normalized_dist, 0.6f);

        uint8_t alpha_8 = (uint8_t)clamp_i32(roundf(alpha * 255.0f), 0, 255);

        // Apply threshold to eliminate near-key colors
        return alpha_8 < 32 ? 0 : alpha_8;
    }
}

struct Envelope
{
    int attack_start;
    int attack_end;
    int release_start;
    int release_end;
	int area;

    // Alpha values at each key point (for quantization analysis)
    uint8_t alpha_at_attack_start;   // alpha when we first crossed threshold
    uint8_t alpha_at_attack_end;     // alpha at peak (same as max_alpha)
    uint8_t alpha_at_release_start;  // alpha when we started declining
    uint8_t alpha_at_release_end;    // alpha when we exited envelope

    uint8_t min_attack_alpha;
    uint8_t min_release_alpha;
    uint8_t max_alpha;
};

// Single envelope tracker - can run multiple in parallel
struct EnvelopeTracker {
    struct Envelope current;
    struct Envelope best;
    uint8_t state;        // NOT_IN_ENVELOPE, IN_ATTACK, IN_SUSTAIN, IN_RELEASE
    uint8_t last_alpha;
    uint8_t has_best;
    uint8_t pad;
};

struct PixelState {
    // Quantized envelope tracker (the one we use for output)
    struct EnvelopeTracker quant;
    // Raw (unquantized) envelope tracker (for comparison)
    struct EnvelopeTracker raw;
};

struct EnvelopeBuilder {
    int width;
    int height;
    union Color key;
    struct PixelState* pixels;  // Array of width * height
};

int compare_env(struct Envelope * a, struct Envelope * b)
{
	return a->area - b->area;
}

// Returns 1 if an envelope was completed this step, 0 otherwise
static int envelope_step(struct EnvelopeTracker* t, uint8_t alpha, int frame_id)
{
    int completed_envelope = 0;
    int check_it_again = 0;

    do {
        check_it_again = 0;

        switch(t->state)
        {
        case NOT_IN_ENVELOPE:
            if (alpha > 4) {
                t->state = IN_ATTACK;
                t->current.attack_start = frame_id;
                t->current.alpha_at_attack_start = alpha;
                t->current.min_attack_alpha = alpha;
                t->current.max_alpha = alpha;
            }
            break;

        case IN_ATTACK:
            if (alpha > t->current.max_alpha) {
                t->current.max_alpha = alpha;
                t->current.alpha_at_attack_end = alpha;
                t->current.attack_end = frame_id;
                break;
            }
            else
            {
                t->state = IN_SUSTAIN;
            }
            // fall through to IN_SUSTAIN

        case IN_SUSTAIN:
            if (alpha > t->current.max_alpha) {
                t->current.max_alpha = alpha;
                t->current.alpha_at_attack_end = alpha;
                t->current.attack_end = frame_id;
                t->state = IN_ATTACK;
                break;
            }

            if (alpha < t->current.max_alpha) {
                t->current.release_start = frame_id;
                t->current.release_end = frame_id;
                t->current.alpha_at_release_start = alpha;
                t->current.min_release_alpha = alpha;
                t->state = IN_RELEASE;

                if(alpha == 0)
                {
                    t->state = NOT_IN_ENVELOPE;
                    t->current.min_release_alpha = 0;
                    t->current.alpha_at_release_end = 0;
                    t->current.release_end = frame_id;
                    goto left_envelope;
                }
            }
            break;

        case IN_RELEASE:
            if (alpha <= t->current.min_release_alpha)
            {
                if(alpha > NOISE_ALPHA)
                {
                    t->current.min_release_alpha = alpha;
                    t->current.release_end = frame_id;
                    t->current.alpha_at_release_end = alpha;
                }

                if(alpha < NOISE_ALPHA)
                {
                    t->state = NOT_IN_ENVELOPE;
                    t->current.release_end = frame_id;
                    t->current.alpha_at_release_end = alpha;
                    t->current.min_release_alpha = 0;
                }
            }

            if (alpha > t->current.min_release_alpha)
            {
                check_it_again = 1;
                t->state = IN_SUSTAIN;
            }

left_envelope:
            if(t->state == NOT_IN_ENVELOPE
            && (t->current.max_alpha > NOISE_ALPHA) )
            {
                t->current.attack_end = max_i32(t->current.attack_start, t->current.attack_end);

                if(!t->has_best || compare_env(&t->current, &t->best) > 0)
                {
                    t->best = t->current;
                    t->has_best = 1;
                }

                // reset
                memset(&t->current, 0, sizeof(struct Envelope));
                completed_envelope = 1;
            }
            break;

        default:
            break;
        };
    } while(check_it_again);

    if(t->state != NOT_IN_ENVELOPE)
    {
        t->current.area += alpha;
    }

    t->last_alpha = alpha;

    return completed_envelope;
}

// Compare two envelopes - returns 1 if they're equivalent for our purposes
static int envelope_equal(struct Envelope* a, struct Envelope* b)
{
    return a->attack_start == b->attack_start
        && a->attack_end == b->attack_end
        && a->release_start == b->release_start
        && a->release_end == b->release_end;
}

void PrintEnvelope(struct Envelope * e)
{
    printf(
    	"attack_start %d (alpha=%d)\n"
    	"attack_end %d (alpha=%d)\n"
    	"release_start %d (alpha=%d)\n"
    	"release_end %d (alpha=%d)\n"
    	"area %d\n"
    	"min_attack_alpha %d\n"
    	"min_release_alpha %d\n"
    	"max_alpha %d\n",
    	e->attack_start, e->alpha_at_attack_start,
    	e->attack_end, e->alpha_at_attack_end,
    	e->release_start, e->alpha_at_release_start,
    	e->release_end, e->alpha_at_release_end,
    	e->area,
    	e->min_attack_alpha,
    	e->min_release_alpha,
    	e->max_alpha
);
}

EnvelopeBuilder * e_Initialize(int width, int height)
{
    EnvelopeBuilder* builder = (EnvelopeBuilder*)malloc(sizeof(EnvelopeBuilder));
    if (!builder) return NULL;

    builder->width = width;
    builder->height = height;
    builder->pixels = (struct PixelState*)calloc(width * height, sizeof(struct PixelState));
	builder->key.c = 0;
 
    if (!builder->pixels) {
        free(builder);
        return NULL;
    }

    return builder;
}

int e_ProcessFrame(EnvelopeBuilder * builder, ImageData const* src, int frame_id, float quantization)
{
 	if (!builder || !src || !src->data)
    {
        printf("Process frame failed (invalid argument)\n");
        return -1;
    }

	if(frame_id == 0)
	{
		builder->key = *(union Color const*)(src->data);
	}

	const float bits = 1.0f + quantization * 7.0f;  /* 1 to 8 bits */
	int levels = (int)(powf(2.0f, bits) + 0.5f);
	if (levels < 2) levels = 2;
	if (levels > 256) levels = 256;
	const float step = 255.0f / (levels - 1);
                
#define LOOP 1
#if LOOP
    for (uint32_t y = 0; y < (uint32_t)builder->height; y++) {
        for (uint32_t x = 0; x < (uint32_t)builder->width; x++) {
#else
#define PRINT_F(...) printf(__VA_ARGS__)
#define PRINT_X(...) printf(__VA_ARGS__)
#endif
            int idx = (y * builder->width + x);
            struct PixelState* pixel = &builder->pixels[idx];

            // Get current alpha from RGBA data
            int src_idx = (y * src->width + x) * 4;
            uint8_t raw_alpha = src->data[src_idx + 3];

            if(builder->key.a > 32)
            {
            	raw_alpha = GetAlpha(&builder->key, (union Color const*)(&src->data[src_idx])) * raw_alpha / 255;
            }

            // Run raw tracker (no quantization)
            envelope_step(&pixel->raw, raw_alpha, frame_id);

            // Apply quantization for the quantized tracker
            uint8_t quant_alpha = raw_alpha;
            if(quantization < 1.0f)
            {
                int level = (int)((raw_alpha / step) + 0.5f);
                if (level >= levels) level = levels - 1;
                quant_alpha = (uint8_t)(level * step + 0.5f);
            }

           // PRINT_IF("raw alpha: %i, quant alpha: %i, frame %d\n", (int)raw_alpha, (int)quant_alpha, frame_id);

            // Run quantized tracker
            envelope_step(&pixel->quant, quant_alpha, frame_id);

#if LOOP
        }
    }
#endif

    return 0;
}

/*
typedef struct EnvelopeMetadata
{
	int total_frames;
	int min_attack_frame;
	int max_attack_frame;
	int min_release_frame;
	int max_release_frame;
	union Color key;
} EnvelopeMetadata;
*/

/*
typedef struct NormalizedPixelData
{
	float attack_start;
	float attack_end;
	float decay_start;
	float decay_end;
} EnvelopeMetadata;
*/

static void update_minmax(MinMax * m, int v)
{
	m->min = min_i32(m->min, v);
	m->max = max_i32(m->max, v);
}

static MinMax minmax_i32(MinMax a, MinMax b)
{
	MinMax m;
	m.min = min_i32(a.min, b.min);
	m.max = max_i32(a.max, b.max);
	return m;
}

int e_GetBuilderMetadata(EnvelopeMetadata * out, EnvelopeBuilder * builder, int total_frames)
{
	if(builder == 0L || out == 0L)
	{
       printf("Build failed (invalid argument)\n");
       return -1;
    }

// add black frame at the end to finish any unifinished business.
    {
        ImageData * last_frame = MakeImage(builder->width, builder->height, 1);
       
        // reset key so that black frame is consistent
        builder->key.c = 0;
        
        e_ProcessFrame(builder, last_frame, total_frames, 2.0);
        free(last_frame->data);
        free(last_frame);
    }

    // First pass: find global timing bounds
    EnvelopeMetadata m;
    memset(&m, 0, sizeof(m));

    m.total_frames = total_frames;

    m.start_attack_frame.min=total_frames;
    m.end_attack_frame.min=total_frames;
    m.start_release_frame.min=total_frames;
    m.end_release_frame.min=total_frames;

    m.start_attack_frame.max=0;
    m.end_attack_frame.max=0;
    m.start_release_frame.max=0;
    m.end_release_frame.max=0;

    for (int i = 0; i < builder->width * builder->height; i++) {

        if (builder->pixels[i].quant.has_best)
        {
			int duration = builder->pixels[i].quant.best.release_end - builder->pixels[i].quant.best.attack_start;

			if((duration / (float)(total_frames)) < g_NoiseFramePercent)
			{
				builder->pixels[i].quant.has_best = 0;
			}
		}

        if (builder->pixels[i].quant.has_best) {
           update_minmax(&m.start_attack_frame,    builder->pixels[i].quant.best.attack_start);
           update_minmax(&m.end_attack_frame,    builder->pixels[i].quant.best.attack_end);

           update_minmax(&m.start_release_frame,    builder->pixels[i].quant.best.release_start);
           update_minmax(&m.end_release_frame,    builder->pixels[i].quant.best.release_end);
        }
    }

	if(out) *out = m;
	
    if(m.start_attack_frame.min > m.start_attack_frame.max
    && m.start_release_frame.min > m.start_release_frame.max)
    {
  		return -1;
    }

	return 0;
}

int e_NormalizeBuilder(float * dst, EnvelopeBuilder * builder, EnvelopeMetadata * m)
{
	uint32_t N = builder->width*builder->height;

	int   start[4] = 
	{
		m->start_attack_frame.min-1,
		m->start_attack_frame.min,
		m->start_release_frame.min,
		m->end_release_frame.min,
	};
	
	float inverse[4] = 
	{
		1.f / (m->start_attack_frame.max+1 - start[0]),
		1.f / (m->end_attack_frame.max - start[1]),
		1.f / (m->end_release_frame.max - start[2]),
		1.f / (m->end_release_frame.max - start[3]),
	};
	
	for(uint32_t j = 0u; j < N; ++j)
	{
		int * value = (int*)(&(builder->pixels[j].quant.best.attack_start));
		
		if(builder->pixels[j].quant.has_best)
		{					
			dst[j*4 + 0] = (value[0] - start[0]) * inverse[0];
			dst[j*4 + 1] = (value[1] - start[1]) * inverse[1];
			dst[j*4 + 2] = (value[2] - start[2]) * inverse[2];
			dst[j*4 + 3] = (value[3] - start[3]) * inverse[3];
		}
		else
		{
			dst[j*4+0] = 0.f;
			dst[j*4+1] = 0.f;
			dst[j*4+2] = 0.f;
			dst[j*4+3] = 0.f;
		}
	}
	
	return 0;	
}

int next_pow2(int n);

int e_Build(EnvelopeBuilder * builder, ImageData * dst, EnvelopeMetadata * out, int total_frames)
{
	if (!builder || !dst || !dst->data)
    {
        printf("Build failed (invalid argument)\n");
    	return -1;
    }

	EnvelopeMetadata m;
	memset(&m, 0, sizeof(m));

	int r = e_GetBuilderMetadata(&m, builder, total_frames);

	if(out) *out = m;
	if(r == -1) return -1;
	
	ResizingImage image = {
		.width=(uint32_t)builder->width,
		.height=(uint32_t)builder->height,
		.data=(float*)malloc(sizeof(float)*4*builder->width*builder->height),
		.original = 0L
	};
	
	int did_crackle = e_NormalizeBuilder(image.data, builder, &m);


	uint32_t W = image.width;
	uint32_t H = image.height;

  // float attack_duration = max_i32(1, m.end_attack_frame.max - m.start_attack_frame.min);
  //  float release_duration = max_i32(1, m.end_release_frame.max - m.start_release_frame.min);

    // resize destination buffer.
	if(dst->width != W
	|| dst->height != H)
	{
		if(dst->width*dst->height != W*H)
		{
			free(dst->data);
			dst->data = (uint8_t*)calloc(W*H*dst->depth, 4);
		}
		
		dst->width = W;
		dst->height = H;
	}
	
    // Write texture using normalized values from e_NormalizeBuilder
    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
        
            int idx = (y * W + x);
            int dst_idx = (y * dst->width + x) * 4;
            
			if(builder->pixels[idx].quant.has_best == 0)
			{
				*(uint32_t*)(&dst->data[dst_idx]) = 0xFF000000;
				continue;
			}


            {
                float attack_start = image.data[idx*4+0];
                float attack_end = image.data[idx*4+1];
                float release_start = image.data[idx*4+2];
                float release_end = image.data[idx*4+3];
                
                float texEffect_r = lerp_f32(8 / 256.0, 1.0, 1.0 - attack_start); // works
                float texEffect_g = lerp_f32(8 / 256.0, 1.0, release_end); // works

                // B: Edge hardness based on attack/release speed

				// Attack
				float attack_speed = 1.0 / max_f32(1.0f, attack_end - attack_start);
				
				// Release
				float release_speed = 1.0 / max_f32(1.0f, release_end - release_start);
				
				float attack_softness = 1.0f - (attack_speed * (m.end_attack_frame.max - m.start_attack_frame.min) / (15.0f * m.total_frames));
				float release_softness = 1.0f - (release_speed * (m.end_release_frame.max - m.start_release_frame.min) / (15.0f * m.total_frames));
				
				// todo: actually figure this out. 
				float texEffect_b = 0; //min_f32(attack_softness, release_softness);

                // R: Inverted normalized attack timing (for shader)
                uint8_t r = (uint8_t)clamp_i32(texEffect_r * 255, 4, 255);
                // G: Normalized release timing
                uint8_t g = (uint8_t)clamp_i32(texEffect_g * 255, 4, 255);
                // B: Edge softness
                uint8_t b = (uint8_t)clamp_i32(texEffect_b * 255, 0, 255);
                // A: Full opacity for valid pixels
                uint8_t a = 255;

                dst->data[dst_idx + 0] = r;
                dst->data[dst_idx + 1] = g;
                dst->data[dst_idx + 2] = b;
                dst->data[dst_idx + 3] = a;
            }
        }
    }

    free(image.data);
    free(image.original);
    
    return 0;
}

void e_Free(EnvelopeBuilder* builder)
{
    if (builder) {
        free(builder->pixels);
        free(builder);
    }
}

// Helper: compute minimum quantization needed to distinguish two alpha values
// Returns 0.0 if they're already different, 1.0 if impossible to distinguish
static float min_quant_to_distinguish(uint8_t a, uint8_t b)
{
    if (a == b) return 1.0f;  // Can't distinguish identical values

    int gap = abs((int)a - (int)b);
    if (gap == 0) return 1.0f;

    // We need step_size < gap for them to be in different buckets
    // step = 255.0 / (2^bits - 1)
    // So we need: 255.0 / (2^bits - 1) < gap
    // => 2^bits > 255.0/gap + 1
    // => bits > log2(255.0/gap + 1)
    // => quantization > (log2(255.0/gap + 1) - 1) / 7

    float min_bits = log2f(255.0f / gap + 1.0f);
    float min_quant = (min_bits - 1.0f) / 7.0f;

    return clamp_f32(min_quant, 0.0f, 1.0f);
}

// Analyze quantization impact after all frames have been processed
// This compares the raw (unquantized) envelopes with the quantized ones
int e_AnalyzeQuantization(EnvelopeBuilder* builder, QuantizationAnalysis* out)
{
    if (!builder || !out) return -1;

    memset(out, 0, sizeof(*out));
    out->min_quant_for_same = 1.0f;
    out->max_quant_for_same = 0.0f;

    int N = builder->width * builder->height;
    float quant_sum = 0.0f;
    int quant_count = 0;

    for (int i = 0; i < N; i++) {
        struct PixelState* p = &builder->pixels[i];
        out->total_pixels++;

        int has_raw = p->raw.has_best;
        int has_quant = p->quant.has_best;

        // Skip pixels with no envelope at all
        if (!has_raw && !has_quant) continue;

        out->pixels_with_envelope++;

        // If one has envelope and other doesn't, they're definitely different
        if (has_raw != has_quant) {
            out->pixels_different_envelope++;
            // Can't compute meaningful min_quant here - fundamentally different
            continue;
        }

        // Both have envelopes - compare them
        struct Envelope* raw = &p->raw.best;
        struct Envelope* quant = &p->quant.best;

        if (envelope_equal(raw, quant)) {
            out->pixels_same_envelope++;
        } else {
            out->pixels_different_envelope++;

            // Track which fields differ
            if (raw->attack_start != quant->attack_start) out->diff_attack_start++;
            if (raw->attack_end != quant->attack_end) out->diff_attack_end++;
            if (raw->release_start != quant->release_start) out->diff_release_start++;
            if (raw->release_end != quant->release_end) out->diff_release_end++;

            // Compute minimum quantization that would have preserved raw envelope
            // by looking at the alpha values at transition points
            float pixel_min_quant = 0.0f;

            // For attack_start: if it changed, we need to distinguish the alpha
            // that triggered attack in raw vs what triggered in quant
            if (raw->attack_start != quant->attack_start) {
                // The raw envelope started when raw alpha crossed threshold 4
                // We need quantization fine enough to preserve that distinction
                float q = min_quant_to_distinguish(raw->alpha_at_attack_start, 4);
                pixel_min_quant = max_f32(pixel_min_quant, q);
            }

            // For attack_end: need to distinguish peak alpha
            if (raw->attack_end != quant->attack_end) {
                // Compare the alphas at attack_end
                float q = min_quant_to_distinguish(
                    raw->alpha_at_attack_end,
                    quant->alpha_at_attack_end
                );
                pixel_min_quant = max_f32(pixel_min_quant, q);
            }

            // For release_start: need to distinguish when we started declining
            if (raw->release_start != quant->release_start) {
                float q = min_quant_to_distinguish(
                    raw->alpha_at_release_start,
                    quant->alpha_at_release_start
                );
                pixel_min_quant = max_f32(pixel_min_quant, q);
            }

            // For release_end: need to distinguish when we crossed NOISE_ALPHA
            if (raw->release_end != quant->release_end) {
                float q = min_quant_to_distinguish(raw->alpha_at_release_end, NOISE_ALPHA);
                pixel_min_quant = max_f32(pixel_min_quant, q);
            }

            out->min_quant_for_same = min_f32(out->min_quant_for_same, pixel_min_quant);
            out->max_quant_for_same = max_f32(out->max_quant_for_same, pixel_min_quant);
            quant_sum += pixel_min_quant;
            quant_count++;
        }
    }

    if (quant_count > 0) {
        out->avg_quant_for_same = quant_sum / quant_count;
    }

    // If no pixels differed, set reasonable defaults
    if (out->pixels_different_envelope == 0) {
        out->min_quant_for_same = 0.0f;
        out->max_quant_for_same = 0.0f;
    }

    return 0;
}

// Print analysis results
void e_PrintQuantizationAnalysis(QuantizationAnalysis* a)
{
    printf("\n=== Quantization Analysis ===\n");
    printf("Total pixels: %d\n", a->total_pixels);
    printf("Pixels with envelope: %d (%.1f%%)\n",
           a->pixels_with_envelope,
           100.0f * a->pixels_with_envelope / a->total_pixels);
    printf("Same envelope (raw == quant): %d (%.1f%%)\n",
           a->pixels_same_envelope,
           a->pixels_with_envelope > 0 ?
               100.0f * a->pixels_same_envelope / a->pixels_with_envelope : 0);
    printf("Different envelope: %d (%.1f%%)\n",
           a->pixels_different_envelope,
           a->pixels_with_envelope > 0 ?
               100.0f * a->pixels_different_envelope / a->pixels_with_envelope : 0);

    if (a->pixels_different_envelope > 0) {
        printf("\nDifference breakdown:\n");
        printf("  attack_start differs: %d\n", a->diff_attack_start);
        printf("  attack_end differs: %d\n", a->diff_attack_end);
        printf("  release_start differs: %d\n", a->diff_release_start);
        printf("  release_end differs: %d\n", a->diff_release_end);

        printf("\nQuantization needed to preserve raw envelope:\n");
        printf("  Min: %.3f\n", a->min_quant_for_same);
        printf("  Max: %.3f\n", a->max_quant_for_same);
        printf("  Avg: %.3f\n", a->avg_quant_for_same);
    } else {
        printf("\nQuantization had NO impact on envelopes!\n");
        printf("Safe to use any quantization in [0.0, 1.0]\n");
    }
    printf("=============================\n\n");
}
