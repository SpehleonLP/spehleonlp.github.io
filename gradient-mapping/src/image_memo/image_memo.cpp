/*
 * Image Memoization Implementation
 *
 * Uses stb_image for image loading. Handles:
 *   - Static images (PNG, JPG, BMP, etc.)
 *   - Animated GIFs (multi-frame for erosion)
 */

#include "create_envelopes.h"
#include "../utility.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#include "stb_image.h"

#include "image_memo.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* =========================================================================
 * Internal state
 * ========================================================================= */

static GradientImageMemo g_gradient_memo = {0};
static ErosionImageMemo g_erosion_memo = {0};

static char g_gradient_path[256] = {0};
static char g_erosion_path[256] = {0};

/* =========================================================================
 * Helper functions
 * ========================================================================= */

static void free_loaded_image(LoadedImage *img) {
    if (!img) return;
    free(img->colors);
    img->colors = NULL;
    img->width = 0;
    img->height = 0;
}

static void free_gradient_memo(void) {
    free_loaded_image(&g_gradient_memo.image);
    g_gradient_path[0] = '\0';
}

static void free_erosion_memo(void) {
    free_loaded_image(&g_erosion_memo.image);
    free((void*)g_erosion_memo.regions);
    g_erosion_memo.regions = NULL;
	g_erosion_memo.region_count[0] = 0;
	g_erosion_memo.region_count[1] = 0;
	g_erosion_memo.region_count[2] = 0;
	g_erosion_memo.region_count[3] = 0;
    g_erosion_memo.fade_in_time = -1;
    g_erosion_memo.fade_out_time = -1;
    g_erosion_memo.animation_duration = -1;
    g_erosion_memo.maximum_quantization = 0;
    g_erosion_memo.minimum_quantization = 0;
    g_erosion_memo.min_rgba = (u8vec4){0, 0, 0, 0};
    g_erosion_memo.max_rgba = (u8vec4){0, 0, 0, 0};
    memset(g_erosion_memo.colors_used, 255, sizeof(g_erosion_memo.colors_used));
    g_erosion_path[0] = '\0';
}

/* Load file from VFS into memory buffer */
static uint8_t *load_file_to_memory(const char *path, int *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        return NULL;
    }

    uint8_t *buffer = (uint8_t *)malloc(size);
    if (!buffer) {
        fclose(f);
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, size, f);
    fclose(f);

    if ((long)bytes_read != size) {
        free(buffer);
        return NULL;
    }

    *out_size = (int)size;
    return buffer;
}

/* Check if file data looks like a GIF */
static int is_gif_data(const uint8_t *data, int size) {
    if (size < 6) return 0;
    return (data[0] == 'G' && data[1] == 'I' && data[2] == 'F' &&
            data[3] == '8' && (data[4] == '7' || data[4] == '9') && data[5] == 'a');
}

/* =========================================================================
 * Gradient loading - single frame to vec4
 * ========================================================================= */

static int load_gradient_static(const uint8_t *file_data, int file_size) {
    int w, h, channels;
    uint8_t *pixels = stbi_load_from_memory(file_data, file_size, &w, &h, &channels, 4);
    if (!pixels) return 0;

    uint32_t pixel_count = w * h;
    g_gradient_memo.image.colors = (u8vec4 *)calloc(sizeof(u8vec4), pixel_count);
    if (!g_gradient_memo.image.colors) {
        stbi_image_free(pixels);
        return 0;
    }

    g_gradient_memo.image.width = w;
    g_gradient_memo.image.height = h;

    /* Convert u8 RGBA to vec4 */
    for (uint32_t i = 0; i < pixel_count; i++) {
        g_gradient_memo.image.colors[i].x = pixels[i * 4 + 0];
        g_gradient_memo.image.colors[i].y = pixels[i * 4 + 1];
        g_gradient_memo.image.colors[i].z = pixels[i * 4 + 2];
        g_gradient_memo.image.colors[i].w = pixels[i * 4 + 3];
    }

    stbi_image_free(pixels);
    return 1;
}

static int load_gradient_gif(const uint8_t *file_data, int file_size) {
    int *delays = NULL;
    int w, h, frame_count, channels;

    /* Load all frames but we only use the first */
    uint8_t *pixels = stbi_load_gif_from_memory(
        file_data, file_size,
        &delays, &w, &h, &frame_count, &channels, 4
    );

    if (!pixels || frame_count <= 0) {
        /* Note: if pixels is NULL, delays may already be freed by stbi internally */
        if (pixels) {
            stbi_image_free(pixels);
            if (delays) stbi_image_free(delays);
        }
        return 0;
    }

    int pixel_count = w * h;
    g_gradient_memo.image.colors = (u8vec4 *)calloc(sizeof(u8vec4), pixel_count);
    if (!g_gradient_memo.image.colors) {
        stbi_image_free(pixels);
        if (delays) stbi_image_free(delays);
        return 0;
    }

    g_gradient_memo.image.width = w;
    g_gradient_memo.image.height = h;

    /* Convert first frame only */
    for (int i = 0; i < pixel_count; i++) {
        g_gradient_memo.image.colors[i].x = pixels[i * 4 + 0];
        g_gradient_memo.image.colors[i].y = pixels[i * 4 + 1];
        g_gradient_memo.image.colors[i].z = pixels[i * 4 + 2];
        g_gradient_memo.image.colors[i].w = pixels[i * 4 + 3];
    }

    stbi_image_free(pixels);
    if (delays) stbi_image_free(delays);
    return 1;
}

/* =========================================================================
 * Erosion loading - analyze all frames for GIFs
 * ========================================================================= */


static i16vec4 used_color_flags_to_indices(ErosionImageMemo *ememo)
{
    /* Pass 2: Convert usage markers to "previous used value" links */
    /* Find minimum gap between adjacent used values (per channel) */
    int min_gap[4] = {256, 256, 256, 256};
    int prev_used[4] = {-1, -1, -1, -1};

    for (uint32_t i = 0; i < 256; i++) {
        uint8_t *u = (uint8_t*)&ememo->colors_used[i];

        for (uint32_t j = 0; j < 4; j++) {
            if (u[j] == 1) {
                /* This value is used */
                if (prev_used[j] >= 0) {
                    int gap = i - prev_used[j];
                    if (gap < min_gap[j]) min_gap[j] = gap;
                }
                u[j] = (prev_used[j] >= 0) ? (uint8_t)prev_used[j] : (uint8_t)i;
                prev_used[j] = i;
            } else {
                /* Not used */
                u[j] = 255;
            }
        }
    }
    
    return (i16vec4){(int16_t)min_gap[0], (int16_t)min_gap[1], (int16_t)min_gap[2], (int16_t)min_gap[3]};
}

/*
 * Apply quantization and analyze the result.
 *
 * target_quant: 0.0 = 1 bit (2 levels), 1.0 = 8 bits (256 levels)
 *
 * Modifies ememo->image.colors in place with quantized values.
 * Sets min/max_quantization to the range where memo remains valid.
 */
static void analyze_quantization_single(ErosionImageMemo *ememo, float target_quant) {
    u8vec4 *colors = ememo->image.colors;
    int pixel_count = ememo->image.width * ememo->image.height;

    /* Convert target_quant to bit depth and compute quantization parameters */
    float bits = 1.0f + target_quant * 7.0f;  /* 1 to 8 bits */
    int levels = (int)(powf(2.0f, bits) + 0.5f);
    if (levels < 2) levels = 2;
    if (levels > 256) levels = 256;

    float step = 255.0f / (levels - 1);

    /* Initialize tracking */
    ememo->min_rgba = (u8vec4){255, 255, 255, 255};
    ememo->max_rgba = (u8vec4){0, 0, 0, 0};
    memset(ememo->colors_used, 0, sizeof(ememo->colors_used));

    /* Pass 1: Quantize pixels and mark used values */
    for (int i = 0; i < pixel_count; i++) {
        uint8_t *c = &colors[i].x;

        if (target_quant < 1) {
            for (int j = 0; j < 4; j++) {
                /* Quantize: round to nearest level, then map back to 0-255 */
                int level = (int)((c[j] / step) + 0.5f);
                if (level >= levels) level = levels - 1;
                uint8_t quantized = (uint8_t)(level * step + 0.5f);
                c[j] = quantized;
            }
        }

        /* Always mark colors as used (after potential quantization) */
        for (int j = 0; j < 4; j++) {
            (&ememo->colors_used[c[j]].x)[j] = 1;
        }

        ememo->min_rgba = min_u8vec4(ememo->min_rgba, *(u8vec4*)c);
        ememo->max_rgba = max_u8vec4(ememo->max_rgba, *(u8vec4*)c);
    }

    /* Pass 2: Convert usage markers to "previous used value" links */
    /* Find minimum gap between adjacent used values (per channel) */
	i16vec4 min_gap = used_color_flags_to_indices(ememo);

    /* Compute quantization range from minimum gap across all channels */
    int overall_min_gap = 256;
    for (int j = 0; j < 4; j++) {
        if ((&min_gap.x)[j] < overall_min_gap) overall_min_gap = (&min_gap.x)[j];
    }

    if (overall_min_gap >= 256) {
        /* Only one value used per channel - any quantization works */
        ememo->minimum_quantization = 0.0f;
        ememo->maximum_quantization = 1.0f;
    } else {
        /* minimum_quantization: coarsest that keeps values distinct */
        /* If min_gap = 1, we need 8 bits (quant=1.0) */
        /* If min_gap = 128, we need 2 bits (quant=0.143) */
        float min_bits_needed = 8.0f - log2f((float)overall_min_gap);
        if (min_bits_needed < 1.0f) min_bits_needed = 1.0f;
        ememo->minimum_quantization = (min_bits_needed - 1.0f) / 7.0f;

        /* maximum_quantization: the precision we actually have (target) */
        ememo->maximum_quantization = target_quant;
    }
}

static int load_erosion_static(const uint8_t *file_data, int file_size, float target_quant) {
    int w, h, channels;
    uint8_t *pixels = stbi_load_from_memory(file_data, file_size, &w, &h, &channels, 4);
    if (!pixels) return 0;

    int pixel_count = w * h;
    g_erosion_memo.image.colors = (u8vec4 *)malloc(sizeof(u8vec4) * pixel_count);
    if (!g_erosion_memo.image.colors) {
        stbi_image_free(pixels);
        return 0;
    }

    g_erosion_memo.image.width = w;
    g_erosion_memo.image.height = h;

    /* Copy pixel data directly */
    memcpy(g_erosion_memo.image.colors, pixels, pixel_count * 4);
    stbi_image_free(pixels);

    /* Apply quantization and analyze */
    analyze_quantization_single(&g_erosion_memo, target_quant);

    /* No timing info for static image */
    g_erosion_memo.fade_in_time = -1;
    g_erosion_memo.fade_out_time = -1;
    g_erosion_memo.animation_duration = -1;

    return 1;
}

static int load_erosion_gif(const uint8_t *file_data, int file_size, float target_quant) {
    int *delays = NULL;
    int w, h, frame_count, channels;

    uint8_t *pixels = stbi_load_gif_from_memory(
        file_data, file_size,
        &delays, &w, &h, &frame_count, &channels, 4
    );

    if (!pixels || frame_count <= 0) {
        /* Note: if pixels is NULL, delays may already be freed by stbi internally */
        if (pixels) {
            stbi_image_free(pixels);
            if (delays) stbi_image_free(delays);
        }
        return 0;
    }

    /* Calculate total duration from frame delays (stb_image returns ms) */
    int total_duration_ms = 0;
    for (int f = 0; f < frame_count; f++) {
        total_duration_ms += delays ? delays[f] : 100;
    }
	
	EnvelopeBuilder * builder = e_Initialize(w, h);
	
    for (int f = 0; f < frame_count; f++) {
		ImageData data;
		data.width = w;
		data.height = h;
		data.depth = 1;
		data.pad00 = 0;
		data.data = &pixels[w*h*4*f];  /* 4 bytes per pixel (RGBA) */

        e_ProcessFrame(builder, &data, f, target_quant);
    }
    
    stbi_image_free(pixels);
    if (delays) stbi_image_free(delays);

	ImageData image = {0};
	image.width = w;
	image.height = h;
	image.depth = 1;
	image.pad00 = 0;
	image.data = (uint8_t*)malloc(w*h*sizeof(u8vec4));  /* 4 bytes per pixel (RGBA) */
	
	EnvelopeMetadata metadata = {0};
	e_Build(builder, &image, &metadata, frame_count);
	e_Free(builder);
	
    /* Copy image */
    g_erosion_memo.image.colors = (u8vec4*)image.data; // GL_8888 so we can just cast.
    g_erosion_memo.image.width = w;
    g_erosion_memo.image.height = h;
    
    /* Set timing - all as percentages (0-1) of total duration */
    g_erosion_memo.animation_duration = total_duration_ms / 1000.0f;
    /* fade_in_time: percentage of animation that is fade-in */
    g_erosion_memo.fade_in_time = metadata.end_attack_frame.max / (float)frame_count;
    /* fade_out_time: percentage of animation that is fade-out */
    g_erosion_memo.fade_out_time = (frame_count - metadata.start_release_frame.min) / (float)frame_count;
		
	/* Set Quantization based on actual gif data */
	g_erosion_memo.minimum_quantization = metadata.min_quantization;
	g_erosion_memo.maximum_quantization = metadata.max_quantization;
	
	/* Set min/max rgba and colors_used based on the actual envelope output image */
	g_erosion_memo.min_rgba = (u8vec4){255, 255, 255, 255};
	g_erosion_memo.max_rgba = (u8vec4){0, 0, 0, 0};
	memset(g_erosion_memo.colors_used, 0, sizeof(g_erosion_memo.colors_used));

	uint32_t N = w*h;
	for(uint32_t i = 0; i < N; ++i)
	{
		u8vec4 c = g_erosion_memo.image.colors[i];
		g_erosion_memo.min_rgba = min_u8vec4(g_erosion_memo.min_rgba, c);
		g_erosion_memo.max_rgba = max_u8vec4(g_erosion_memo.max_rgba, c);

		/* Mark actual pixel values as used (same as analyze_quantization_single) */
		g_erosion_memo.colors_used[c.x].x = 1;
		g_erosion_memo.colors_used[c.y].y = 1;
		g_erosion_memo.colors_used[c.z].z = 1;
		g_erosion_memo.colors_used[c.w].w = 1;
	}

	used_color_flags_to_indices(&g_erosion_memo);

	g_erosion_memo.regions = 0;
	g_erosion_memo.region_count[0] = 0;
	g_erosion_memo.region_count[1] = 0;
	g_erosion_memo.region_count[2] = 0;
	g_erosion_memo.region_count[3] = 0;
	
    return 1;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

int memo_load_image(int stack_type, const char *vfs_path, float target_quant) {
    if (stack_type < 0 || stack_type >= MEMO_STACK_COUNT) return 0;
    if (!vfs_path || !vfs_path[0]) return 0;

    /* Free existing */
    memo_free(stack_type);

    /* Load file */
    int file_size = 0;
    uint8_t *file_data = load_file_to_memory(vfs_path, &file_size);
    if (!file_data) return 0;

    int result = 0;
    int is_gif = is_gif_data(file_data, file_size);

    if (stack_type == MEMO_STACK_GRADIENT) {
        /* Gradient stack doesn't use quantization */
        result = is_gif ? load_gradient_gif(file_data, file_size)
                        : load_gradient_static(file_data, file_size);
        if (result) {
            strncpy(g_gradient_path, vfs_path, sizeof(g_gradient_path) - 1);
        }
    } else {
        /* Erosion stack applies quantization */
        result = is_gif ? load_erosion_gif(file_data, file_size, target_quant)
                        : load_erosion_static(file_data, file_size, target_quant);
        if (result) {
            strncpy(g_erosion_path, vfs_path, sizeof(g_erosion_path) - 1);
        }
        
        if(g_erosion_memo.image.colors)
        {
			uint32_t N = g_erosion_memo.image.width*g_erosion_memo.image.height;
			uint8_t * deinterleaved = (uint8_t *)malloc(N*4*sizeof(uint8_t));
			
			for(uint32_t i = 0; i < N; ++i)
			{
				deinterleaved[0*N + i] = g_erosion_memo.image.colors[i].x;
				deinterleaved[1*N + i] = g_erosion_memo.image.colors[i].y;
				deinterleaved[2*N + i] = g_erosion_memo.image.colors[i].z;
				deinterleaved[3*N + i] = g_erosion_memo.image.colors[i].w;
			}
			
			free(g_erosion_memo.image.colors);
			g_erosion_memo.image.colors = NULL;  /* Mark as freed */
			g_erosion_memo.image.deinterleaved = deinterleaved;
        }
    }
    

    free(file_data);
    return result;
}

const GradientImageMemo *memo_get_gradient(void) {
    return g_gradient_memo.image.colors ? &g_gradient_memo : NULL;
}

const ErosionImageMemo *memo_get_erosion(void) {
    /* Check either colors or deinterleaved - deinterleaved is used after loading */
    return (g_erosion_memo.image.colors || g_erosion_memo.image.deinterleaved) ? &g_erosion_memo : NULL;
}

ErosionImageMemo *memo_get_erosion_mutable(void) {
    return (g_erosion_memo.image.colors || g_erosion_memo.image.deinterleaved) ? &g_erosion_memo : NULL;
}

void memo_free(int stack_type) {
    if (stack_type == MEMO_STACK_GRADIENT) {
        free_gradient_memo();
    } else if (stack_type == MEMO_STACK_EROSION) {
        free_erosion_memo();
    }
}

void memo_free_all(void) {
    free_gradient_memo();
    free_erosion_memo();
}
