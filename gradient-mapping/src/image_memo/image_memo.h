#ifndef IMAGE_MEMO_H
#define IMAGE_MEMO_H

/*
 * Image Memoization System
 *
 * Loads and caches images from the Emscripten VFS. Images are loaded once
 * during analyze_source() and reused during stack processing.
 *
 * For animated GIFs:
 *   - Gradient stack: uses first frame only
 *   - Erosion stack: loads all frames for envelope detection
 */

#include "../effect_stack_api.h"
#include <stdint.h>

/* Stack type constants - must match STACK_TYPE enum in effect_stack_api.h */
#define MEMO_STACK_GRADIENT 0
#define MEMO_STACK_EROSION  1
#define MEMO_STACK_COUNT    2

typedef struct 
{
// do keep it in uint8 actually.. 
	union
	{
	u8vec4 * colors;
	uint8_t * deinterleaved;
	};
	uint32_t width;
	uint32_t height;
} LoadedImage;

typedef struct 
{
// interprets image as color data, that is floats are colors.
	LoadedImage image;
} GradientImageMemo;

typedef struct 
{
// interprets image as non-color data, that is floats are frame ids. 
	LoadedImage image;
	u8vec4 min_rgba;
	u8vec4 max_rgba;

	// can be computed from a gif, otherwise left at -1
	float fade_in_time;
	float fade_out_time;
	float animation_duration;
	
	// quantization basically maps onto bit depth.
	// for an animated gif each individual frame gets quantized, not the envelope result
	// 1.0 = 8 bits per channel
	// 0.0 = 1 bit per channel
	 
	// the maximum quantization we could have used on the source image to get this result
	float maximum_quantization; 
	// the minimum quantization we could have used to get this result
	float minimum_quantization;
	
	// if quantization is > 7 bits then we recommend no modification
	// if quantization is > 6 bits we recommend low pass filter
	// if quantization is > 5 bits we recommend iterated box blur with clamping and then low pass
	// if quantization is < 6 bits we recommend dijkstra and then low pass
	
	// if dijkstra was used this memo will be populated
	// otherwise left blank
	uint32_t const* regions;
	uint32_t region_count[4];
	
	// if a color is used then index to the previous color used.
	// e.g. if we use colors 6 and 8, then color[8] = 6
	// if a color is itself no prior color was used but it was
	// 255 for if unused.
	u8vec4 colors_used[256];
} ErosionImageMemo;

/* =========================================================================
 * API functions
 * ========================================================================= */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Load an image from VFS path into the memo cache for the given stack.
 *
 * For gradient stack: loads first frame as u8vec4 colors (target_quant ignored)
 * For erosion stack: loads image and applies quantization
 *
 * target_quant: 0.0 = 1 bit (2 levels), 1.0 = 8 bits (256 levels)
 *               Sets min/max_quantization to range where memo remains valid.
 *
 * Returns: 1 on success, 0 on failure
 */
int memo_load_image(int stack_type, const char *vfs_path, float target_quant);

/*
 * Get the gradient-specific memo (with vec4 colors).
 * Returns NULL if no gradient image loaded.
 */
const GradientImageMemo *memo_get_gradient(void);

/*
 * Get the erosion-specific memo (with analysis data).
 * Returns NULL if no erosion image loaded.
 */
const ErosionImageMemo *memo_get_erosion(void);

/*
 * Get mutable access to erosion memo for setting computed fields
 * (regions, region_count). Returns NULL if no erosion image loaded.
 */
ErosionImageMemo *memo_get_erosion_mutable(void);

/*
 * Free all resources for a stack's memo.
 */
void memo_free(int stack_type);

/*
 * Free all memoized images (call at shutdown).
 */
void memo_free_all(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* IMAGE_MEMO_H */
