#include "create_envelopes.h"
#include "contour_extract.h"
#include "contour_smooth.h"
#include "fft_blur.h"
#include "fluid_solver.h"
#include "interp_quantized.h"
#include "smart_blur.h"
#include <limits.h>
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
	NOISE_ALPHA = 32,
};

const float g_NoiseFramePercent = 0.02;

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
	builder->key.c = 0;
 
    if (!builder->pixels) {
        free(builder);
        return NULL;
    }

    return builder;
}

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
            uint8_t current_alpha = src->data[src_idx + 3];

            if(builder->key.a > 32)
            {
            	current_alpha = GetAlpha(&builder->key, (union Color const*)(&src->data[src_idx])) * current_alpha / 255;
            }

            PRINT_IF("current alpha: %i, frame %d\n", (int)current_alpha, frame_id);

			int check_it_again = 0;
			do {
        	    check_it_again = 0;

				switch(pixel->in_envelope)
				{
				case NOT_IN_ENVELOPE:
		            if (current_alpha > 4) {
		                PRINT_IF("Process frame: entering attack state, %d\n", frame_id);
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
		                PRINT_IF("Process frame: entering sustain state, %d\n", frame_id);
		            	pixel->in_envelope = IN_SUSTAIN;
		            }
		        case IN_SUSTAIN:
		            if (current_alpha > pixel->current.max_alpha) {
		                pixel->current.max_alpha = current_alpha;
		                pixel->current.attack_end = frame_id;
		            	pixel->in_envelope = IN_ATTACK;
		            	PRINT_IF("Process frame: backtracking to attack state %d\n", frame_id);
		            	break;
		            }

		            if (current_alpha  < pixel->current.max_alpha) {
		                pixel->current.release_start = frame_id;
		               	pixel->current.release_end = frame_id;
		                pixel->current.min_release_alpha = current_alpha;
		            	pixel->in_envelope = IN_RELEASE;

		                PRINT_IF("Process frame: entering release state, %d\n", frame_id);
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
		            if (current_alpha <= pixel->current.min_release_alpha)
		            {
		                if(current_alpha > NOISE_ALPHA)
		                {
		                	pixel->current.min_release_alpha = current_alpha;
		               		pixel->current.release_end = frame_id;
		               	}
		               	
		                if(current_alpha < NOISE_ALPHA)
		                {
		            		pixel->in_envelope = NOT_IN_ENVELOPE;
		               		pixel->current.release_end = frame_id;
		                	pixel->current.min_release_alpha = 0;
		                   PRINT_IF("Process frame: exiting envelope, %d\n", frame_id);
		                }
		            }

		            if (current_alpha > pixel->current.min_release_alpha)
		            {
						check_it_again = 1;
		            	pixel->in_envelope = IN_SUSTAIN;
		            }

	left_envelope:
		            if(pixel->in_envelope == NOT_IN_ENVELOPE
		            && (pixel->current.max_alpha > NOISE_ALPHA) )
		            {
						pixel->current.attack_end = max_i32(pixel->current.attack_start, pixel->current.attack_end);
						//pixel->current.release_start = max_i32(pixel->current.attack_start+1, pixel->current.release_start);
						//pixel->current.release_end = max_i32(pixel->current.release_start, pixel->current.release_end);
		            
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
						PRINT_IF("pixel->in_envelope = %d\npixel->current.release_end = %d\npixel->current.attack_start = %d\npixel->current.max_alpha=%d\n",
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
		printf("total frames: %d\n", total_frames);
        ImageData * last_frame = MakeImage(builder->width, builder->height, 1);
       
        // reset key so that black frame is consistent
        builder->key.c = 0;
        
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
    
        if (builder->pixels[i].has_best) 
        {
			int duration = builder->pixels[i].best.release_end - builder->pixels[i].best.attack_start;
			
			if((duration / (float)(total_frames)) < g_NoiseFramePercent)
			{
				builder->pixels[i].has_best = 0;
			}
		}   
		    
        if (builder->pixels[i].has_best) {
           update_minmax(&m.start_attack_frame,    builder->pixels[i].best.attack_start);
           update_minmax(&m.end_attack_frame,    builder->pixels[i].best.attack_end);
           
           update_minmax(&m.start_release_frame,    builder->pixels[i].best.release_start);
           update_minmax(&m.end_release_frame,    builder->pixels[i].best.release_end);
        }
    }

	if(out) *out = m;

	// Count how many pixels have envelopes
	int envelope_count = 0;
	int in_envelope_count = 0;
	for (int i = 0; i < builder->width * builder->height; i++) {
	    if (builder->pixels[i].has_best) {
	        envelope_count++;
	    }
	    else if(builder->pixels[i].in_envelope)
	    {
			in_envelope_count++;
	    }
	}
	printf("DEBUG: Found %d pixels with valid envelopes (out of %d total)\n",
	       envelope_count, builder->width * builder->height);
	
	if(in_envelope_count)
		printf("DEBUG: Found %d pixels with unfinished envelopes (out of %d total)\n",
			   in_envelope_count, builder->width * builder->height);

    if(m.start_attack_frame.min > m.start_attack_frame.max
    && m.start_release_frame.min > m.start_release_frame.max)
    {
        printf("Build failed (no envelopes)\n");
  		return -1;
    }

	printf("DEBUG: Bounds - attack:[%d,%d] release:[%d,%d]\n",
	       m.start_attack_frame.min, m.end_attack_frame.max,
	       m.start_release_frame.min, m.end_release_frame.max);

	return 0;
}

int e_NormalizeBuilder(float * dst, EnvelopeBuilder * builder, EnvelopeMetadata * m)
{
	static const char * layer_names[4] =
	{
		"attack start", "attack end", "release start", "release end"
	};

	MinMax ranges[2];
	ranges[0] = minmax_i32(m->start_attack_frame, m->end_attack_frame);
	ranges[1] = minmax_i32(m->start_release_frame, m->end_release_frame);
	int durations[4];
	float error_bars[4];

	uint32_t N = builder->width*builder->height;
	SmartBlurContext* blur = 0L;
	InterpolateQuantizedCmd interpolate_quantized;

	// Test pixel coordinates
	int test_x = TEST_X1;
	int test_y = TEST_Y1;
	int test_idx = TEST_IDX1;
	int did_crackle = 0;

	printf("\n=== e_NormalizeBuilder: Test pixel (%d, %d) idx=%d ===\n", test_x, test_y, test_idx);
	if(builder->pixels[test_idx].has_best) {
		struct Envelope* env = &builder->pixels[test_idx].best;
		printf("Test pixel HAS envelope: attack[%d-%d] release[%d-%d] max_alpha=%d\n",
		       env->attack_start, env->attack_end, env->release_start, env->release_end, env->max_alpha);
	} else {
		printf("Test pixel has NO envelope (has_best=0)\n");
	}

	int min_duration = INT_MAX;
	
	for(int i = 0; i < 4; ++i)
	{
		MinMax * range = &ranges[i/2];
		durations[i]  = max_i32(1, (range->max) - range->min);
		error_bars[i] =  (1.0 / durations[i]);
		
		if(durations[i] >= 1)
		{
			min_duration = min_i32(min_duration, durations[i]);
		}
	}	
	
	if(min_duration == INT_MAX)
		min_duration = 0;
		
	int banding_estimate = max_i32(builder->width, builder->height) / min_duration;
		
	for(int i = 0; i < 4; ++i)
	{
		MinMax * range = &ranges[i/2];
// amp a tiny bit so that things that appear late don't get slammed to 0. 
		float inv = (1.0 - 2.0 / 256.0) / max_f32(1, durations[i]);

		printf("Layer %d (%s): range=[%d,%d], duration=%d, inv=%f, error=%f\n", i, layer_names[i], range->min, range->max, durations[i], inv, error_bars[i]);
				
		if(banding_estimate <= 1) // DISABLED SMART BLUR FOR DEBUGGING
		{
			for(uint32_t j = 0u; j < N; ++j)
			{
				if(builder->pixels[j].has_best)
				{
					// Normalize by subtracting min and dividing by duration
					int frame_value = (&(builder->pixels[j].best.attack_start))[i] - range->min;
					dst[j*4 + i] = (frame_value) * inv + 0.004;
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
		    {
				blur = sb_Initialize(builder->width, builder->height);
			}

			sb_Setup(blur);
			
			int W = builder->width;

			for(uint32_t j = 0u; j < N; ++j)
			{
				int px = j % W;
				int py = j / W;
				int is_debug = (i == 3 && py == 73 && px >= 39 && px <= 41);

				if(builder->pixels[j].has_best)
				{
					// Normalize by subtracting min and dividing by duration
					int frame_value = (&(builder->pixels[j].best.attack_start))[i] - (range->min);

					if(i < 2)
						frame_value = (range->max) - frame_value;

					if(is_debug) {
						struct Envelope *env = &builder->pixels[j].best;
						printf("DEBUG layer3 (%d,%d): has_best=1, release_end=%d, range.min=%d, frame_value=%d\n",
							px, py, env->release_end, range->min, frame_value);
					}

                   sb_SetValue(blur, j % W, j / W, frame_value);
                }
                else
                {
					if(is_debug) {
						printf("DEBUG layer3 (%d,%d): has_best=0, setting -1\n", px, py);
					}
                    // No envelope - interpret as always fully transparent.
                    sb_SetValue(blur, j % W, j / W, -1);
                }
			}
			
			if(banding_estimate < 4)
			{
				int iters = sb_RunUntilConverged(blur, 0.01f, 32);
				printf("Smart blur converged: %s in %d iterations\n", layer_names[i], iters);
			}
			else
			{
				did_crackle = 1;

				iq_Initialize(
					&interpolate_quantized,
					blur->input,
					builder->width,
					builder->height,
					0);
					
				iq_Execute(&interpolate_quantized);
				memcpy(blur->output, interpolate_quantized.output, sizeof(float)*N);
				iq_Free(&interpolate_quantized);
			}
			
			for(uint32_t j = 0u; j < N; ++j)
			{
				int px = j % W;
				int py = j / W;
				int is_debug = (i == 3 && py == 73 && px >= 39 && px <= 41);

				float frame_value = blur->output[j];

				if(is_debug) {
					printf("DEBUG layer3 output (%d,%d): blur->input=%d, blur->output=%f, inv=%f\n",
						px, py, blur->input[j], frame_value, inv);
				}

				if(frame_value < 0)
					dst[j*4+i] = 0;
				else
					dst[j*4 + i] = blur->output[j] * inv + 0.004;

				if(is_debug) {
					printf("DEBUG layer3 final (%d,%d): dst=%f\n", px, py, dst[j*4+i]);
				}
			}
		}
	}
	
	if(blur)
		sb_Free(blur);

	return did_crackle;	
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
		.width=builder->width,
		.height=builder->height,
		.data=malloc(sizeof(float)*4*builder->width*builder->height),
		.original = 0L
	};
	
	int did_crackle = e_NormalizeBuilder(image.data, builder, &m);

	if(did_crackle)
	{
		ResizingImage upsampled = {
			.width=next_pow2(image.width),
			.height=next_pow2(image.height),
			.data=0L,
			.original = 0L
		};
		
#if 1
		if(fft_ResizeImage(&upsampled, &image))
		{
			free(image.data);
			free(image.original);

			image = upsampled;
#if 1
			FFTBlurContext context;
			memset(&context, 0, sizeof(context));
			if(fft_Initialize(&context, image.width, image.height) == 0)
			{
				for(int channel = 0; channel < 4; ++channel)
				{
					fft_LoadChannel(&context, &image, 4, channel);
					fft_LowPassFilter(&context, 0.15, 0);
					fft_CopyBackToImage(&image, &context, 4, channel);
				}

				fft_Free(&context);
			}
#endif
		}
#endif
		
		FluidSolver solver;
		memset(&solver, 0, sizeof(solver));
		solver.width = image.width;
		solver.height = image.height;
		solver.height_interlaced0to4 = image.data;
		fs_Setup(&solver);
		fs_debug_export_all("/debug.png", &solver);
		fs_Free(&solver);
		
		
	}

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
	
    // Test pixel coordinates
	int test_idx = TEST_IDX1;

    printf("\n=== e_Build: Writing output texture ===\n");

    // Write texture using normalized values from e_NormalizeBuilder
    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            int idx = (y * W + x);

            int dst_idx = (y * dst->width + x) * 4;

            {
                // Get normalized values from tmp array
                // tmp[idx*4 + 0] = attack_start (normalized)
                // tmp[idx*4 + 1] = attack_end (normalized)
                // tmp[idx*4 + 2] = release_start (normalized)
                // tmp[idx*4 + 3] = release_end (normalized)
                
                float attack_start = 1.0 - image.data[idx*4+0];
                float attack_end = 1.0 - image.data[idx*4+1];
                float release_start = image.data[idx*4+2];
                float release_end = image.data[idx*4+3];
                
                if(release_end <= 0.0)
                {
					*(uint32_t*)(&dst->data[dst_idx]) = 0xFF000000;
					if(idx == test_idx) {
						printf("Test pixel (%d,%d): NO ENVELOPE - writing black (0xFF000000)\n", x, y);
					}
					
					continue;
                }
                
                float texEffect_r = 1.0 - attack_start; // works
                float texEffect_g = release_end; // works

                // B: Edge hardness based on attack/release speed

				// Attack
				float attack_speed = 1.0 / max_f32(1.0f, attack_end - attack_start);
				
				// Release
				float release_speed = 1.0 / max_f32(1.0f, release_end - release_start);
				
				float attack_softness = 1.0f - (attack_speed * (m.end_attack_frame.max - m.start_attack_frame.min) / (15.0f * m.total_frames));
				float release_softness = 1.0f - (release_speed * (m.end_release_frame.max - m.start_release_frame.min) / (15.0f * m.total_frames));
				
				float texEffect_b = 0; //min_f32(attack_softness, release_softness);

                if(idx == test_idx) {
                    printf("Test pixel (%d,%d): HAS ENVELOPE\n", x, y);
                    printf("  attack_norm=%f, release_norm=%f\n", 1.0 - texEffect_r, texEffect_g);
                    printf("  attack_speed=%f, release_speed=%f\n", attack_speed, release_speed);
                    printf("  attack_softness=%f, release_softness=%f, final softness=%f\n",
                           attack_softness, release_softness, texEffect_b);
                }

                // R: Inverted normalized attack timing (for shader)
                uint8_t r = (uint8_t)clamp_i32(texEffect_r * 255, 0, 255);
                // G: Normalized release timing
                uint8_t g = (uint8_t)clamp_i32(texEffect_g * 255, 1, 255);
                // B: Edge softness
                uint8_t b = (uint8_t)clamp_i32(texEffect_b * 255, 0, 255);
                // A: Full opacity for valid pixels
                uint8_t a = 255;

                dst->data[dst_idx + 0] = r;
                dst->data[dst_idx + 1] = g;
                dst->data[dst_idx + 2] = b;
                dst->data[dst_idx + 3] = a;

                if(idx == test_idx) {
                    printf("  Final RGBA written: R=%d G=%d B=%d A=%d\n", r, g, b, a);
                }
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
