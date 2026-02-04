#include "erosion_pipeline.h"
#include "commands/hessian_flow_dbg.h"
#include "commands/sdf_layered.h"
#include "image_memo/image_memo.h"
#include "commands/fft_blur.h"
#include "commands/interp_quantized.h"
#include "commands/label_regions.h"
#include "utility.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* =============================================================================
 * Box Blur
 * ============================================================================= */

/* Single-channel box blur (3x3 kernel), iterated */
static void box_blur_channel(float *data, uint32_t W, uint32_t H, int iterations) {
	
	float *temp = (float *)calloc(W * H, sizeof(float));

	for (int iter = 0; iter < iterations; iter++) {
		for (uint32_t y = 0; y < H; y++) {
			for (uint32_t x = 0; x < W; x++) {
				float sum = 0.0f;
				int count = 0;

				/* 3x3 neighborhood */
				for (int dy = -1; dy <= 1; dy++) {
					for (int dx = -1; dx <= 1; dx++) {
						int nx = x + dx;
						int ny = y + dy;
						if ((uint32_t)nx < W && (uint32_t)ny < H) {
							sum += data[ny * W + nx];
							count++;
						}
					}
				}
				temp[y * W + x] = sum / count;
			}
		}
		memcpy(data, temp, W * H * sizeof(float));
	}

	free(temp);
}

/* Box blur on planar height field (4 channels stored as separate planes) */
static void box_blur_heights_planar(float *heights, uint32_t W, uint32_t H, int iterations) {
	size_t N = (size_t)W * H;
	for (int c = 0; c < 4; c++) {
		box_blur_channel(&heights[c * N], W, H, iterations);
	}
}

/* Box blur on planar normal map (nx, ny, nz as separate planes), then renormalize */
static void box_blur_normals_planar(float *normals, uint32_t W, uint32_t H, int iterations) {
	size_t N = (size_t)W * H;
	float *nx = &normals[0 * N];
	float *ny = &normals[1 * N];
	float *nz = &normals[2 * N];

	/* Blur x, y, z components separately */
	box_blur_channel(nx, W, H, iterations);
	box_blur_channel(ny, W, H, iterations);
	box_blur_channel(nz, W, H, iterations);

	/* Renormalize */
	for (size_t i = 0; i < N; i++) {
		float x = nx[i], y = ny[i], z = nz[i];
		float len = sqrtf(x * x + y * y + z * z);
		if (len > 0.0001f) {
			nx[i] = x / len;
			ny[i] = y / len;
			nz[i] = z / len;
		} else {
			nx[i] = 0;
			ny[i] = 0;
			nz[i] = 1;
		}
	}
}

/* =============================================================================
 * FFT Low-Pass Filter (Wth padding to power of 2)
 * ============================================================================= */

/* FFT filter on single channel - pads to power of 2 (centered), filters, extracts */
static void fft_filter_channel(float *data, uint32_t W, uint32_t H, float low_pass, float Hgh_pass) {
	
	if (W <= 0 || H <= 0) return;
	
	if(Hgh_pass <= 0 && low_pass >= 1)
		return;

	int fftW = next_pow2(W);
	int fftH = next_pow2(H);

	/* Offset to center the image in the padded buffer */
	int offX = (fftW - W) / 2;
	int offY = (fftH - H) / 2;

	FFTBlurContext ctx;
	if (fft_Initialize(&ctx, fftW, fftH) != 0) return;

	/* Clear buffer and load centered */
	memset(ctx.real, 0, (size_t)fftW * (size_t)fftH * sizeof(float));
	memset(ctx.imag, 0, (size_t)fftW * (size_t)fftH * sizeof(float));
	for (uint32_t y = 0; y < H; y++) {
		for (uint32_t x = 0; x < W; x++) {
			ctx.real[(y + offY) * fftW + (x + offX)] = data[y * W + x];
		}
	}

	/* Forward FFT, apply filters, inverse FFT */
	fft_2d(ctx.real, ctx.imag, fftW, fftH, 0, 1.0f);

	if (Hgh_pass > 0.0f) {
		fft_HighPassFilter(&ctx, Hgh_pass);
	}
	if (low_pass < 1.0f) {
		fft_LowPassFilter(&ctx, low_pass, 0);
	}

	fft_2d(ctx.real, ctx.imag, fftW, fftH, 1, 1.0f / (fftW * fftH));

	/* Extract back from centered position, clamping to valid range */
	for (uint32_t y = 0; y < H; y++) {
		for (uint32_t x = 0; x < W; x++) {
			float val = ctx.real[(y + offY) * fftW + (x + offX)];
			data[y * W + x] = fmaxf(0.0f, fminf(1.0f, val));
		}
	}

	fft_Free(&ctx);
}

/* FFT filter on planar height field (4 channels stored as separate planes) */
static void fft_filter_heights_planar(float *heights, uint32_t W, uint32_t H, float low_pass, float Hgh_pass) {
	if (W <= 0 || H <= 0) return;
	size_t N = (size_t)W * H;

	for (int c = 0; c < 4; c++) {
		fft_filter_channel(&heights[c * N], W, H, low_pass, Hgh_pass);
	}
}

/* FFT filter on planar normal map (nx, ny, nz as separate planes), then renormalize */
static void fft_filter_normals_planar(float *normals, uint32_t W, uint32_t H, float low_pass, float Hgh_pass) {
	if (W <= 0 || H <= 0) return;
	size_t N = (size_t)W * H;
	float *nx = &normals[0 * N];
	float *ny = &normals[1 * N];
	float *nz = &normals[2 * N];

	/* Filter x, y, z components separately */
	fft_filter_channel(nx, W, H, low_pass, Hgh_pass);
	fft_filter_channel(ny, W, H, low_pass, Hgh_pass);
	fft_filter_channel(nz, W, H, low_pass, Hgh_pass);

	/* Renormalize */
	for (size_t i = 0; i < N; i++) {
		float x = nx[i], y = ny[i], z = nz[i];
		float len = sqrtf(x * x + y * y + z * z);
		if (len > 0.0001f) {
			nx[i] = x / len;
			ny[i] = y / len;
			nz[i] = z / len;
		} else {
			nx[i] = 0;
			ny[i] = 0;
			nz[i] = 1;
		}
	}
}

/* =============================================================================
 * Normal Map Conversions (Planar Format)
 *
 * Normal maps are stored as planar data with 3 components per height channel:
 *   Layout: [ch0_nx][ch0_ny][ch0_nz][ch1_nx][ch1_ny][ch1_nz][ch2_nx]...
 *   Total size: 4 channels * 3 components * W * H = 12 * W * H floats
 * ============================================================================= */

/* Convert one height channel to planar normal map (nx, ny, nz as separate planes) */
static void height_to_normals_planar(float *nx, float *ny, float *nz,
                                      const float *heights, uint32_t W, uint32_t H, float scale) {
	for (uint32_t y = 0; y < H; y++) {
		for (uint32_t x = 0; x < W; x++) {
			size_t idx = y * W + x;

			/* Sample neighbors with edge clamping */
			size_t iL = (x > 0) ? idx - 1 : idx;
			size_t iR = (x < W - 1) ? idx + 1 : idx;
			size_t iD = (y > 0) ? idx - W : idx;
			size_t iU = (y < H - 1) ? idx + W : idx;

			float hL = heights[iL];
			float hR = heights[iR];
			float hD = heights[iD];
			float hU = heights[iU];

			/* Gradient via central differences */
			float gx = (hR - hL) * 0.5f * scale;
			float gy = (hU - hD) * 0.5f * scale;

			/* Normal from gradient: n = normalize(-gx, -gy, 1) */
			float len = sqrtf(gx * gx + gy * gy + 1.0f);
			nx[idx] = -gx / len;
			ny[idx] = -gy / len;
			nz[idx] = 1.0f / len;
		}
	}
}

/* Convert planar normal map back to height field via Poisson solve (Jacobi iteration) */
static void normals_to_height_planar(float *heights, const float *nx, const float *ny, const float *nz,
                                      uint32_t W, uint32_t H, int iterations) {
	if (W == 0 || H == 0) return;
	size_t N = (size_t)W * H;

	float *temp = (float *)calloc(N, sizeof(float));
	if (!temp) return;

	/* Initialize heights to zero */
	memset(heights, 0, N * sizeof(float));

	/* Jacobi iteration */
	for (int iter = 0; iter < iterations; iter++) {
		for (uint32_t y = 0; y < H; y++) {
			for (uint32_t x = 0; x < W; x++) {
				size_t idx = y * W + x;

				/* Get gradient from normal: g = -n.xy / n.z */
				float n_z = nz[idx];
				if (n_z < 0.001f) n_z = 0.001f;  /* avoid div by zero */
				float gx = -nx[idx] / n_z;
				float gy = -ny[idx] / n_z;

				/* Neighbor indices with edge clamping */
				size_t iL = (x > 0) ? idx - 1 : idx;
				size_t iR = (x < W - 1) ? idx + 1 : idx;
				size_t iD = (y > 0) ? idx - W : idx;
				size_t iU = (y < H - 1) ? idx + W : idx;

				float hL = heights[iL];
				float hR = heights[iR];
				float hD = heights[iD];
				float hU = heights[iU];

				/* Poisson update */
				float hNew = (hL + hR + hD + hU) * 0.25f;
				hNew += (hR - hL) * 0.125f - gx * 0.5f;
				hNew += (hU - hD) * 0.125f - gy * 0.5f;

				temp[idx] = hNew;
			}
		}

		/* Swap buffers */
		float *swap = heights;
		heights = temp;
		temp = swap;
	}

	/* If odd iterations, result is in temp, need to copy back */
	if (iterations % 2 == 1) {
		memcpy(heights, temp, N * sizeof(float));
	}

	free(temp);
}

/* Convert planar heights (4 channels) to planar normals (4 channels * 3 components) */
static float* heights_to_normalmap_planar(const float *heights, uint32_t W, uint32_t H, float scale) {
	if (W == 0 || H == 0) return NULL;
	size_t N = (size_t)W * H;

	/* Allocate 12 planes: 4 height channels * 3 normal components */
	float *normals = (float *)calloc(12 * N, sizeof(float));
	if (!normals) return NULL;

	for (int c = 0; c < 4; c++) {
		const float *h_channel = &heights[c * N];
		float *nx = &normals[(c * 3 + 0) * N];
		float *ny = &normals[(c * 3 + 1) * N];
		float *nz = &normals[(c * 3 + 2) * N];
		height_to_normals_planar(nx, ny, nz, h_channel, W, H, scale);
	}

	return normals;
}

/* Convert planar normals back to planar heights */
static void normalmap_to_heights_planar(float *heights, const float *normals, uint32_t W, uint32_t H, int iterations) {
	if (W == 0 || H == 0) return;
	size_t N = (size_t)W * H;

	for (int c = 0; c < 4; c++) {
		float *h_channel = &heights[c * N];
		const float *nx = &normals[(c * 3 + 0) * N];
		const float *ny = &normals[(c * 3 + 1) * N];
		const float *nz = &normals[(c * 3 + 2) * N];
		normals_to_height_planar(h_channel, nx, ny, nz, W, H, iterations);
	}
}

/* Process effects in gradient/normal space (planar normals: 12 planes for 4 channels * 3 components) */
static int process_erosion_gradient_planar(float *normals, uint32_t W, uint32_t H,
                                            Effect const* effects, int effect_count, int i)
{
	size_t N = (size_t)W * H;

	for (; i < effect_count; ++i)
	{
		switch ((EffectId)effects[i].effect_id)
		{
		case EFFECT_SOURCE_GRADIENT:
		case EFFECT_SOURCE_WORLEY:
		case EFFECT_SOURCE_PERLIN:
		case EFFECT_SOURCE_CURL:
		case EFFECT_SOURCE_NOISE:
			/* TODO: apply effect to normal map */
		case EFFECT_DIJKSTRA:
			/* DIJKSTRA -- inapplicable to gradients */
			break;

		case EFFECT_BOX_BLUR: {
			int iters = (int)effects[i].params.box_blur.iterations;
			if (iters < 1) iters = 1;
			/* Apply to all 4 height channel normal sets */
			for (int c = 0; c < 4; c++) {
				/* Each height channel has 3 normal components stored contiguously */
				float *nm_channel = &normals[c * 3 * N];
				box_blur_normals_planar(nm_channel, W, H, iters);
			}
			break;
		}

		case EFFECT_FOURIER_CLAMP: {
			float low_pass = effects[i].params.fourier_clamp.Maximum;
			float Hgh_pass = effects[i].params.fourier_clamp.Minimum;
			/* Apply to all 4 height channel normal sets */
			for (int c = 0; c < 4; c++) {
				float *nm_channel = &normals[c * 3 * N];
				fft_filter_normals_planar(nm_channel, W, H, low_pass, Hgh_pass);
			}
			break;
		}

		case EFFECT_GRADIENTIFY:
			/* Nested gradientify - ignore */
			break;

		case EFFECT_POISSON_SOLVE:
			/* Exit gradient state */
			return i;

		case EFFECT_COLOR_RAMP:
		case EFFECT_BLEND_MODE:
			/* Invalid in erosion pipeline */
			break;
		}
	}

	return i;
}

/* Dijkstra distance transform on planar height data */
static void run_dijkstra_planar(float *dst, uint32_t W, uint32_t H, SDFDistanceParams params)
{
	uint32_t N = W*H;
	ErosionImageMemo* memo = memo_get_erosion_mutable();
	if (!memo || !memo->image.deinterleaved) {
		return;
	}

	/* Compute and cache regions if not already done */
	if (memo->regions == NULL)
	{
		/* Allocate space for 3 channels worth of region labels */
		uint32_t *regions = calloc(N * 3, sizeof(uint32_t));
		if (!regions) return;

		/* Run label_regions for each channel (RGB, not A) */
		for (uint32_t i = 0; i < 3; ++i)
		{
			LabelRegionsCmd label_cmd = {
				.src = &memo->image.deinterleaved[N * i],
				.W = W,
				.H = H,
				.connectivity = LABEL_CONNECT_8,
				.labels = &regions[N * i],
				.num_regions = 0,
			};

			if (label_regions(&label_cmd) < 0)
			{
				free(regions);
				return;
			}
			memo->region_count[i] = label_cmd.num_regions;
		}
		memo->region_count[3] = 0;  /* Alpha channel unused */
		memo->regions = regions;
	}

	/* Run interpolation for each channel */
	for (int i = 0; i < 3; ++i)
	{
		uint8_t *channel_data = &memo->image.deinterleaved[N * i];
		uint8_t colors_used[256];

		for (int j = 0; j < 256; ++j)
		{
			colors_used[j] = ((uint8_t *)(&memo->colors_used[j].x))[i];
		}

		/* Set up command with pre-computed labels from memo */
		InterpolateQuantizedCmd iq = {0};
		iq.labels = (uint32_t *)&memo->regions[N * i];
		iq.num_regions = memo->region_count[i];

		if (iq_Initialize(&iq, channel_data, colors_used, W, H, &params, i==0) != 0) {
			/* Don't call iq_Free - labels is owned by memo */
			free(iq.pixels);
			free(iq.regions);
			free(iq.output);
			return;
		}

		if (iq_Execute(&iq) != 0) {
			/* Don't call iq_Free - labels is owned by memo */
			free(iq.pixels);
			free(iq.regions);
			free(iq.output);
			return;
		}

		/* Copy interpolated results to output */
		for (uint32_t j = 0; j < N; ++j)
		{
			dst[N * i + j] = iq.output[j] / 255.f;
		}
		
		/* Free per-channel allocations but NOT labels (owned by memo) */
		free(iq.pixels);
		free(iq.regions);
		free(iq.output);
	}

	/* Copy alpha channel unchanged */
	for (uint32_t i = 0; i < N; ++i) {
		dst[N * 3 + i] = memo->image.deinterleaved[N * 3 + i] / 255.0f;
	}
}

#if 0
/* =============================================================================
 * Reference: old interleaved dijkstra implementation
 * ============================================================================= */
static void run_dijkstra_reference(vec4 *dst, uint32_t W, uint32_t H, SDFDistanceParams dist_params)
{

	

	/* Execute */
	if (ed_Execute(&ed) == 0) {
		/* Copy results back to working image (interleaved) */
		uint32_t N = W * H;
		for (uint32_t j = 0; j < N; j++) {
			dst[j].x = ed.output[j * 4 + 0] / 255.0f;
			dst[j].y = ed.output[j * 4 + 1] / 255.0f;
			dst[j].z = ed.output[j * 4 + 2] / 255.0f;
			dst[j].w = ed.output[j * 4 + 3] / 255.0f;
		}
	}

	ed_Free(&ed);
}
#endif

/* Process effects on planar height field (4 channels stored as separate planes) */
static void process_erosion_height_planar(float *dst, uint32_t W, uint32_t H, Effect const* effects, int effect_count)
{
	for (int i = 0; i < effect_count; ++i)
	{
		switch ((EffectId)effects[i].effect_id)
		{
		case EFFECT_SOURCE_GRADIENT:
		case EFFECT_SOURCE_WORLEY:
		case EFFECT_SOURCE_PERLIN:
		case EFFECT_SOURCE_CURL:
		case EFFECT_SOURCE_NOISE:
			/* TODO: apply effect to height field */
		case EFFECT_DIJKSTRA: {
			/* DIJKSTRA -- only valid if first thing in the stack */
			if (i == 0)
			{
				SDFDistanceParams params;
				params.minkowski = exp2(effects[i].params.dijkstra.Minkowski);
				params.chebyshev = effects[i].params.dijkstra.Chebyshev;
				run_dijkstra_planar(dst, W, H, params);
			}
			break;
		}

		case EFFECT_BOX_BLUR: {
			int iters = (int)effects[i].params.box_blur.iterations;
			if (iters < 1) iters = 1;
			box_blur_heights_planar(dst, W, H, iters);
			break;
		}

		case EFFECT_FOURIER_CLAMP: {
			float low_pass = effects[i].params.fourier_clamp.Maximum;
			float Hgh_pass = effects[i].params.fourier_clamp.Minimum;
			fft_filter_heights_planar(dst, W, H, low_pass, Hgh_pass);
			break;
		}

		case EFFECT_GRADIENTIFY: {
			HessianFlowDebugCmd flow = {0};
			
			flow.heightmap = &dst[W*H*1];
			flow.W=W;
			flow.H=H;
			flow.output_path = "/debug.png";
			flow.kernel_size = 5;
			flow.border = HESSIAN_BORDER_CLAMP_EDGE;
			flow.undefined_value = -1.f;
			flow.normal_scale = 0.01f;
			flow.major_weight = 0.5;
			flow.minor_weight = 0.5;
			
			printf("executing heissan flow command.");
			hessian_flow_debug_Execute(&flow);
			hessian_flow_debug_Free(&flow);
		
#if 0
			/* Convert to planar normal map */
			float scale = effects[i].params.gradientify.scale;
			if (scale <= 0.0f) scale = 1.0f;
			float *normals = heights_to_normalmap_planar(dst, W, H, scale);
			if (!normals) break;

			/* Process effects in gradient space */
			i = process_erosion_gradient_planar(normals, W, H, effects, effect_count, i + 1);

			/* Poisson solve back to heights - get iterations from next effect if it's POISSON_SOLVE */
			int iterations = 100;
			if (i < effect_count && effects[i].effect_id == EFFECT_POISSON_SOLVE) {
				iterations = (int)effects[i].params.poisson_solve.iterations;
				if (iterations < 1) iterations = 100;
			}
			normalmap_to_heights_planar(dst, normals, W, H, iterations);

			free(normals);
#endif
			break;
		}

		case EFFECT_POISSON_SOLVE:
			/* Orphaned poisson solve - ignore */
			break;

		case EFFECT_COLOR_RAMP:
		case EFFECT_BLEND_MODE:
			/* Invalid in erosion pipeline */
			break;
		}
	}
}

uint8_t* process_erosion_stack(Effect const* effects, int effect_count, int* out_w, int* out_h)
{
	if(!out_w || !out_h)
		return NULL;
		
    /* Get the loaded erosion image from memo */
    const ErosionImageMemo* memo = memo_get_erosion();
    if (!memo || !memo->image.deinterleaved) {
        js_post_error(ERROR_NO_SOURCE, -1, -1, "erosion image not loaded");
        return NULL;
    }
    
    uint32_t W = *out_w = (int)memo->image.width;
    uint32_t H = *out_h = (int)memo->image.height;
	uint32_t N = W * H;

	float * working_image = malloc(N * sizeof(float) * 4);
	
// deinterleave
	for (uint32_t  i = 0; i < N*4; ++i)
	{
		working_image[i] = memo->image.deinterleaved[i] / 255.f;
	}

	process_erosion_height_planar(working_image, (uint32_t)W, (uint32_t)H, effects, effect_count);

	static u8vec4 * retn = 0L;
	retn = (u8vec4*)realloc(retn, N * sizeof(u8vec4));

// interleave
	for (uint32_t  i = 0; i < N; ++i)
	{
		retn[i].x = clamp_i32(working_image[0*N + i] * 255 + 0.5, 0, 255);
		retn[i].y = clamp_i32(working_image[1*N + i] * 255 + 0.5, 0, 255);
		retn[i].z = clamp_i32(working_image[2*N + i] * 255 + 0.5, 0, 255);
		retn[i].w = clamp_i32(working_image[3*N + i] * 255 + 0.5, 0, 255);
	}

	free(working_image);
	

    return (uint8_t*)retn;
}

