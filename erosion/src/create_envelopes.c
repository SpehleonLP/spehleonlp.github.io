#include "create_envelopes.h"
#include "smart_blur.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

int min_i32(int a, int b) { return a < b? a : b; }
int max_i32(int a, int b) { return a > b? a : b; }
float min_f32(float a, float b) { return a < b? a : b; }
float max_f32(float a, float b) { return a > b? a : b; }
int clamp_i32(int a, int b, int c);
float clamp_f32(float a, float b, float c);


enum
{
	NOT_IN_ENVELOPE,
	IN_ATTACK,
	IN_SUSTAIN,
	IN_RELEASE,
	ALPHA_THRESHOLD = 16,
	NOISE_FRAMES = 4,
	NOISE_ALPHA = 32,
};



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

    uint8_t min_attack_alpha;
    uint8_t min_release_alpha;
    uint8_t max_alpha;
};

struct PixelState {
    // Current envelope being built
    struct Envelope current;
    uint8_t in_envelope;  // Are we currently tracking an envelope?
    uint8_t last_alpha;  // Previous frame's alpha value


    // Best envelope found so far
    struct Envelope best;
    uint8_t has_best;    // Have we found any valid envelope?
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

void PrintEnvelope(struct Envelope * e)
{
    printf(
    	"attack_start %d\n"
    	"attack_end %d\n"
    	"release_start %d\n"
    	"release_end %d\n"
    	"area %d\n"
    	"min_attack_alpha %d\n"
    	"min_release_alpha %d\n"
    	"max_alpha %d\n",
    	e->attack_start,
    	e->attack_end,
    	e->release_start,
    	e->release_end,
    	e->area,
    	e->min_attack_alpha,
    	e->min_release_alpha,
    	e->max_alpha
);
}

EnvelopeBuilder * e_Initialize(int width, int height)
{
    EnvelopeBuilder* builder = malloc(sizeof(EnvelopeBuilder));
    if (!builder) return NULL;

    builder->width = width;
    builder->height = height;
    builder->pixels = calloc(width * height, sizeof(struct PixelState));

    if (!builder->pixels) {
        free(builder);
        return NULL;
    }

    return builder;
}

#define TEST_X (317)
#define TEST_Y (153)

int e_ProcessFrame(EnvelopeBuilder * builder, ImageData const* src, int frame_id)
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

	int x = TEST_X;
	int y = TEST_Y;

#define LOOP 1
#if LOOP
#define PRINT_F(...)
    for (y = 0; y < builder->height; y++) {
        for (x = 0; x < builder->width; x++) {
#else
#define PRINT_F(...) printf(__VA_ARGS__)
#define PRINT_X(...) printf(__VA_ARGS__)
#endif
            int idx = (y * builder->width + x);
            struct PixelState* pixel = &builder->pixels[idx];

            // Get current alpha from RGBA data
            int src_idx = (y * src->width + x) * 4;
            uint8_t current_alpha = src->data[src_idx + 3];

            if(builder->key.a > 32)
            {
            	current_alpha = GetAlpha(&builder->key, (union Color const*)(&src->data[src_idx])) * current_alpha / 255;
            }

            PRINT_F("current alpha: %i\n", (int)current_alpha);

			int check_it_again = 0;
			do {
        	    check_it_again = 0;

				switch(pixel->in_envelope)
				{
				case NOT_IN_ENVELOPE:
		            if (current_alpha > 4) {
		                PRINT_F("Process frame: entering attack state, %d\n", frame_id);
		                pixel->in_envelope = IN_ATTACK;
		                pixel->current.attack_start = frame_id;
		                pixel->current.min_attack_alpha = current_alpha;
		                pixel->current.max_alpha = current_alpha;
		            }
		            break;
		        case IN_ATTACK:
		            if (current_alpha > pixel->current.max_alpha) {
		                pixel->current.max_alpha = current_alpha;
		                pixel->current.attack_end = frame_id;
		        	    break;
		            }
		            else
		            {
		                PRINT_F("Process frame: entering sustain state, %d\n", frame_id);
		            	pixel->in_envelope = IN_SUSTAIN;
		            }
		        case IN_SUSTAIN:
		            if (current_alpha > pixel->current.max_alpha) {
		                pixel->current.max_alpha = current_alpha;
		                pixel->current.attack_end = frame_id;
		            	pixel->in_envelope = IN_ATTACK;
		            	PRINT_F("Process frame: backtracking to attack state %d\n", frame_id);
		            	break;
		            }

		            if (current_alpha  < pixel->current.max_alpha) {
		                pixel->current.release_start = frame_id;
		               	pixel->current.release_end = frame_id;
		                pixel->current.min_release_alpha = current_alpha;
		            	pixel->in_envelope = IN_RELEASE;

		                PRINT_F("Process frame: entering release state, %d\n", frame_id);
		                if(current_alpha == 0)
		                {
		                    pixel->in_envelope = NOT_IN_ENVELOPE;
		                    pixel->current.min_release_alpha = 0;
		                    pixel->current.release_end = frame_id;
		                    goto left_envelope;
		                }
		            }
		            break;
		      case IN_RELEASE:
		            if (current_alpha < pixel->current.min_release_alpha)
		            {
		                if(current_alpha)
		                {
		                	pixel->current.min_release_alpha = current_alpha;
		               		pixel->current.release_end = frame_id;
		               	}
		                else
		                {
		            		pixel->in_envelope = NOT_IN_ENVELOPE;
		                   PRINT_F("Process frame: exiting envelope, %d\n", frame_id);
		                }
		            }

		            if (current_alpha > pixel->current.min_release_alpha)
		            {
						check_it_again = 1;

						if((frame_id - pixel->current.attack_end) < NOISE_FRAMES)
		            		pixel->in_envelope = NOT_IN_ENVELOPE;
		            	else
		            		pixel->in_envelope = IN_SUSTAIN;
		            }

	left_envelope:
		            if(pixel->in_envelope == NOT_IN_ENVELOPE
		            && (pixel->current.release_end - pixel->current.attack_start) > NOISE_FRAMES
		            && (pixel->current.max_alpha > NOISE_ALPHA) )
		            {
		            	if(!pixel->has_best || compare_env(&pixel->current, &pixel->best) > 0)
		            	{
		            	/*
		            		if(pixel->has_best)
		            		{
		            			printf("REPLACING:\n");
		            			PrintEnvelope(&builder->pixels[idx].best);
		            			printf("WITH:\n");
		            			PrintEnvelope(&builder->pixels[idx].current);
		            		}*/


		                    pixel->best = pixel->current;
		                    pixel->has_best = 1;
		            	}

		            // reset
		                memset(&pixel->current, 0, sizeof(struct Envelope));
		            }
		            else
		            {
						PRINT_F("pixel->in_envelope = %d\npixel->current.release_end = %d\npixel->current.attack_start = %d\npixel->current.max_alpha=%d\n",
								pixel->in_envelope,
								pixel->current.release_end,
								pixel->current.attack_start,
								pixel->current.max_alpha
							);
					}
		            break;
		       default:
		           break;
				};
			} while(check_it_again);
			if(pixel->in_envelope != NOT_IN_ENVELOPE)
			{
				pixel->current.area += current_alpha;
			}

            pixel->last_alpha = current_alpha;

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
       
        uint32_t N = builder->width * builder->height;
		for(uint32_t i = 0u; i < N; ++i)
		{
			((union Color*)(last_frame->data))[i] = builder->key;
		} 
        
        e_ProcessFrame(builder, last_frame, total_frames);
        free(last_frame->data);
        free(last_frame);
    }

    // First pass: find global timing bounds
    EnvelopeMetadata m;

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
        if (builder->pixels[i].has_best) {
           update_minmax(&m.start_attack_frame,    builder->pixels[i].best.attack_start);
           update_minmax(&m.end_attack_frame,    builder->pixels[i].best.attack_end);
           
           update_minmax(&m.start_release_frame,    builder->pixels[i].best.release_start);
           update_minmax(&m.end_release_frame,    builder->pixels[i].best.release_end);
        }
    }

	if(out) *out = m;
	
    if(m.start_attack_frame.min > m.start_attack_frame.max
    && m.start_release_frame.min > m.start_release_frame.max)
    {
        printf("Build failed (no envelopes)\n");
  		return -1;
    }

	return 0;
}

// error bars is ratio of target quantization to current quantization. 
// we want to use a memoization to get the area of contigous bands
// banding is in terms of non-color data. 
// Large Widths + Minimum Riser: This is definitive banding.
// Short Widths + Random Risers: This is noise or legitimate high-frequency data.
//
float e_MeasureBanding(EnvelopeBuilder * builder, int channel, float error_bars)
{
	uint32_t W = builder->width;
	uint32_t H = builder->height;
	
	for(uint32_t y = 0; y < H; ++y)
	{
		for(uint32_t x = 0; x < W; ++x)
		{
			uint32_t i = y*W+x;
			uint32_t sample = (&(builder->pixels[i].best.attack_start))[channel];
			
			
		}
	}
	


}

int e_NormalizeBuilder(float * dst, EnvelopeBuilder * builder, EnvelopeMetadata * m)
{
	static const char * layer_names[4] =
	{
		"attack start", "attack end", "release start", "release end"
	};

	MinMax * ranges = &m->start_attack_frame;
	int durations[4];
	float error_bars[4];
	
	uint32_t N = builder->width*builder->height;
	SmartBlurContext* blur = 0L;
	
	for(int i = 0; i < 4; ++i)
	{
		durations[i]  = max_i32(1, ranges[i].max - ranges[i].min);
		error_bars[i] =  256.0 / durations[i];
		
		float inv = 1.0 / durations[i];
		
		if(durations[i] > 255) // high res enough that we don't need to blur
		{
			for(uint32_t j = 0u; j < N; ++j)
			{
				if(builder->pixels[j].has_best)
				{
					dst[j*4 + i] = (&(builder->pixels[j].best.attack_start))[i] * inv;
				}
				else
				{
					dst[j*4+i] = 0.f;
				}
			}
		} 
		else
		{
		    if(blur == 0L)
				blur = sb_Initialize(builder->width, builder->height);
		
			int W = builder->width;
			
			for(uint32_t j = 0u; j < N; ++j)
			{
				if(builder->pixels[j].has_best)
				{
					float value =  (&(builder->pixels[j].best.attack_start))[i] * inv;
                   sb_SetConstraints(blur, j % W, j / W, value - error_bars[i], value + error_bars[i], value);
                }
                else
                {
                    // No envelope - interpret as always fully transparent.
                    sb_SetConstraints(blur, j % W, j / W, 0.0f, 0.0f, 0.0f);
                }
			}
			
            int iters = sb_RunUntilConverged(blur, 0.01f, 1000);
			printf("Smart blur converged: %s in %d iterations\n", layer_names[i], iters);
			
			for(uint32_t j = 0u; j < N; ++j)
			{
				dst[j*4 + i] = blur->values[j];
			}
		}
	}
	
	if(blur)
		sb_Free(blur);
	
	return 0;	
}


int e_Build(EnvelopeBuilder * builder, ImageData * dst, EnvelopeMetadata * out, int total_frames)
{
	float delta_alpha;

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

	float * tmp = malloc(sizeof(float)*4*builder->width*builder->height);
	e_NormalizeBuilder(tmp, builder, &m);

  // float attack_duration = max_i32(1, m.end_attack_frame.max - m.start_attack_frame.min);
  //  float release_duration = max_i32(1, m.end_release_frame.max - m.start_release_frame.min);

    int H = min_i32(builder->height, dst->height);
    int W = min_i32(builder->width, dst->width);

    // Write texture using normalized values from e_NormalizeBuilder
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int idx = (y * builder->width + x);
            struct PixelState* pixel = &builder->pixels[idx];

            int dst_idx = (y * dst->width + x) * 4;

            if (pixel->has_best == 0) {
                *(uint32_t*)(&dst->data[dst_idx]) = 0xFF000000;
            } else {
                // Get normalized values from tmp array
                // tmp[idx*4 + 0] = attack_start (normalized)
                // tmp[idx*4 + 1] = attack_end (normalized)
                // tmp[idx*4 + 2] = release_start (normalized)
                // tmp[idx*4 + 3] = release_end (normalized)
                float attack_norm = tmp[idx*4 + 0];
                float release_norm = tmp[idx*4 + 3];

                // B: Edge hardness based on attack/release speed
                delta_alpha = (pixel->best.max_alpha - pixel->best.min_attack_alpha);
                float attack_speed = delta_alpha / max_f32(1.0f, pixel->best.attack_end - pixel->best.attack_start);

                delta_alpha = (pixel->best.max_alpha - pixel->best.min_release_alpha);
                float release_speed = delta_alpha / max_f32(1.0f, pixel->best.release_end - pixel->best.release_start);

                float attack_softness = 1.0f - (attack_speed * (m.end_attack_frame.max - m.start_attack_frame.min) / (15.0f * m.total_frames));
                float release_softness = 1.0f - (release_speed * (m.end_release_frame.max - m.start_release_frame.min) / (15.0f * m.total_frames));

                float softness = min_f32(attack_softness, release_softness);

                // R: Inverted normalized attack timing (for shader)
                dst->data[dst_idx + 0] = (uint8_t)clamp_i32((1.0f - attack_norm) * 255, 0, 255);
                // G: Normalized release timing
                dst->data[dst_idx + 1] = (uint8_t)clamp_i32(release_norm * 255, 0, 255);
                // B: Edge softness
                dst->data[dst_idx + 2] = (uint8_t)clamp_i32(softness * 255, 0, 255);
                // A: Full opacity for valid pixels
                dst->data[dst_idx + 3] = 255;
            }
        }
    }

    free(tmp);
    return 0;
}

void e_Free(EnvelopeBuilder* builder)
{
    if (builder) {
        free(builder->pixels);
        free(builder);
    }
}
