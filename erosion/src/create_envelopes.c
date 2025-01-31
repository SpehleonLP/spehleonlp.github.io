#include "create_envelopes.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

int min_i32(int a, int b) { return a < b? a : b; }
int max_i32(int a, int b) { return a > b? a : b; }
float min_f32(float a, float b) { return a < b? a : b; }
float max_f32(float a, float b) { return a > b? a : b; }


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
    // Convert colors to [0,1) range
    float kr = key->r / 255.0f;
    float kg = key->g / 255.0f;
    float kb = key->b / 255.0f;

    float sr = sample->r / 255.0f;
    float sg = sample->g / 255.0f;
    float sb = sample->b / 255.0f;

    // Normalize vectors
    float k_len = sqrtf(kr*kr + kg*kg + kb*kb);
    float s_len = sqrtf(sr*sr + sg*sg + sb*sb);

    // Avoid division by zero
    if (k_len < 0.001f || s_len < 0.001f) {
        return 255;
    }

    // Get normalized dot product
    float dot = (kr*sr + kg*sg + kb*sb) / (k_len * s_len);

    // Convert similarity to alpha (1 = identical colors, 0 = orthogonal colors)
    float alpha = 1.0f - dot;

    // Scale to 0-255 range and clamp
    uint8_t alpha_8 = (uint8_t)(fminf(fmaxf(alpha * 255.0f, 0.0f), 255.0f));

    return alpha_8 < 128? 0 : alpha_8;
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
		               		pixel->current.release_end = frame_id-1;
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

int e_Build(EnvelopeBuilder * builder, ImageData * dst, EnvelopeMetadata * out, int total_frames)
{
/*
#if LOOP == 0
    int idx = (TEST_Y * builder->width + TEST_X);
  	PrintEnvelope( &builder->pixels[idx].best);

#else
#define IDX(x, y) ((y) * builder->width + (x))
  	PrintEnvelope( &builder->pixels[IDX(TEST_X, TEST_Y)].best);
  	PrintEnvelope( &builder->pixels[IDX(TEST_X-1, TEST_Y)].best);
  	PrintEnvelope( &builder->pixels[IDX(TEST_X+1, TEST_Y)].best);
  	PrintEnvelope( &builder->pixels[IDX(TEST_X, TEST_Y-1)].best);
  	PrintEnvelope( &builder->pixels[IDX(TEST_X, TEST_Y+1)].best);

#endif
*/




	float delta_alpha;

	if (!builder || !dst || !dst->data)
    {
        printf("Build failed (invalid argument)\n");
    	return -1;
    }

// add black frame at the end to finish any unifinished business.
    {
        ImageData * last_frame = MakeImage(builder->width, builder->height, 1);
        e_ProcessFrame(builder, last_frame, total_frames);
        free(last_frame->data);
        free(last_frame);
    }

    // First pass: find global timing bounds
    EnvelopeMetadata m;

    m.total_frames = total_frames;
    m.min_attack_frame = total_frames;
    m.max_attack_frame = 0;
    m.min_release_frame = total_frames;
    m.max_release_frame = 0;
    m.key = builder->key;

    for (int i = 0; i < builder->width * builder->height; i++) {
        if (builder->pixels[i].has_best) {
            m.min_attack_frame = min_i32(m.min_attack_frame,    builder->pixels[i].best.attack_start);
            m.max_attack_frame = max_i32(m.max_attack_frame,  builder->pixels[i].best.attack_end);

            m.min_release_frame = min_i32(m.min_release_frame,    builder->pixels[i].best.release_start);
            m.max_release_frame = max_i32(m.max_release_frame,  builder->pixels[i].best.release_end);
        }
    }

    if(m.min_attack_frame > m.max_attack_frame
    && m.min_release_frame > m.max_release_frame)
    {
        printf("Build failed (no envelopes)\n");
  		return -1;
    }

#if LOOP == 0
	printf("attack range %d to %d", m.min_attack_frame, m.max_attack_frame);
	printf("release range %d to %d", m.min_release_frame, m.max_release_frame);
#endif
    if(out)
    {
        *out = m;
    }

    float attack_duration = max_i32(1, m.max_attack_frame - m.min_attack_frame);
    float release_duration = max_i32(1, m.max_release_frame - m.min_release_frame);

    int H = min_i32(builder->height, dst->height);
    int W = min_i32(builder->width, dst->width);

    // Second pass: build texture
     for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int idx = (y * builder->width + x);
            struct PixelState* pixel = &builder->pixels[idx];

            int dst_idx = (y * dst->width + x) * 4;

            if (pixel->has_best == 0)
            	*(uint32_t*)(&dst->data[dst_idx]) = 0xFF000000;
            else
            {
                // R: Normalized attack start (inverted as per shader)
                float attack_norm = (float)(pixel->best.attack_start - m.min_attack_frame) / attack_duration;
                float release_norm = (float)(pixel->best.release_end -  m.min_release_frame) / release_duration;

                // B: Edge hardness based on attack/release speed
                delta_alpha = (pixel->best.max_alpha - pixel->best.min_attack_alpha);
                float attack_speed  = delta_alpha / (pixel->best.attack_end - pixel->best.attack_start);

                delta_alpha = (pixel->best.max_alpha - pixel->best.min_release_alpha);
                float release_speed = delta_alpha / (pixel->best.release_end - pixel->best.release_start);

                float hardness = min_f32(attack_speed, release_speed);

                dst->data[dst_idx + 0] = (uint8_t)((1.0f - attack_norm) * 255);
                dst->data[dst_idx + 1] = (uint8_t)(release_norm * 255);
                dst->data[dst_idx + 2] = 0; //TEST_X == x || TEST_Y == y? 255 : 0; //(uint8_t)(hardness * 255);

                // A: Full opacity for valid pixels
                dst->data[dst_idx + 3] = 255;
            }
        }
   }

    return 0;
}

void e_Free(EnvelopeBuilder* builder)
{
    if (builder) {
        free(builder->pixels);
        free(builder);
    }
}
