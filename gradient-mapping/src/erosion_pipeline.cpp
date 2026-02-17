#include "erosion_pipeline.h"
#include "commands/hessian_flow_dbg.h"
#include "commands/constrained_poisson_cmd.h"
#include "commands/laminarize_cmd.h"
#include "commands/sdf_layered.h"
#include "image_memo/image_memo.h"
#include "commands/fft_blur.h"
#include "commands/interp_quantized.h"
#include "commands/lic_debug_cmd.h"
#include "commands/debug_png.h"
#include "commands/label_regions.h"
#include "commands/laplacian_cmd.h"
#include "commands/ridge_mesh_cmd.h"
#include "commands/heightmap_ops.h"
#include "debug_output.h"
#include "utility.h"
#include "pipeline_memo.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <memory>

static PipelineMemo g_erosion_memo = {};

void erosion_memo_clear(void) {
    memo_clear(&g_erosion_memo);
}

/* =============================================================================
 * Box Blur
 * ============================================================================= */

/* Single-channel box blur (3x3 kernel), iterated */
static void box_blur_channel(float *data, uint32_t W, uint32_t H, int iterations) {

	std::unique_ptr<float[]> temp(new float[W * H]());

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
		memcpy(data, temp.get(), W * H * sizeof(float));
	}
}

/* Box blur on planar height field (3 channels stored as separate planes) */
static void box_blur_heights_planar(float *heights, uint32_t W, uint32_t H, int iterations) {
	size_t N = (size_t)W * H;
	for (int c = 0; c < 3; c++) {
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
		vec3 n(nx[i], ny[i], nz[i]);
		float len = glm::length(n);
		if (len > 0.0001f) {
			n /= len;
		} else {
			n = vec3(0, 0, 1);
		}
		nx[i] = n.x;
		ny[i] = n.y;
		nz[i] = n.z;
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

	/* Extract back from centered position, clamping to valid range.
	 * Preserve zeros: if the input was 0, force the output to 0 so
	 * the FFT ringing doesn't bleed into empty regions. */
	for (uint32_t y = 0; y < H; y++) {
		for (uint32_t x = 0; x < W; x++) {
			uint32_t idx = y * W + x;
			if (data[idx] == 0.0f) continue;
			float val = ctx.real[(y + offY) * fftW + (x + offX)];
			data[idx] = fmaxf(0.0f, fminf(1.0f, val));
		}
	}

	fft_Free(&ctx);
}

/* FFT filter on planar height field (3 channels stored as separate planes) */
static void fft_filter_heights_planar(float *heights, uint32_t W, uint32_t H, float low_pass, float Hgh_pass) {
	if (W <= 0 || H <= 0) return;
	size_t N = (size_t)W * H;

	for (int c = 0; c < 3; c++) {
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
		vec3 n(nx[i], ny[i], nz[i]);
		float len = glm::length(n);
		if (len > 0.0001f) {
			n /= len;
		} else {
			n = vec3(0, 0, 1);
		}
		nx[i] = n.x;
		ny[i] = n.y;
		nz[i] = n.z;
	}
}

/* =============================================================================
 * Normal Map Conversions (Planar Format)
 *
 * Normal maps are stored as planar data with 3 components per height channel:
 *   Layout: [ch0_nx][ch0_ny][ch0_nz][ch1_nx][ch1_ny][ch1_nz][ch2_nx]...
 *   Total size: 3 channels * 3 components * W * H = 9 * W * H floats
 * ============================================================================= */

/* Convert one height channel to planar normal map (nx, ny, nz as separate planes).
 * Out-of-bounds samples are 0 (GL_CLAMP_TO_BORDER). */
static void height_to_normals_planar(float *nx, float *ny, float *nz,
                                      const float *heights, uint32_t W, uint32_t H, float scale) {
	/* height_normal() uses scale as the z-component before normalization,
	 * while the old code scaled the gradient by `scale` with z=1.
	 * normalize(-gx*s, -gy*s, 1) == normalize(-gx, -gy, 1/s) */
	float z_scale = 1.0f / scale;
	for (uint32_t y = 0; y < H; y++) {
		for (uint32_t x = 0; x < W; x++) {
			size_t idx = y * W + x;
			vec3 n = height_normal(heights, x, y, W, H, z_scale);
			nx[idx] = n.x;
			ny[idx] = n.y;
			nz[idx] = n.z;
		}
	}
}

/* Convert planar normal map back to height field using constrained Poisson solve.
 * original_heights provides the constraints (zeros stay zero, positives stay positive). */
static void normals_to_height_planar(float *heights, const float *original_heights,
                                      const float *nx, const float *ny, const float *nz,
                                      uint32_t W, uint32_t H, int iterations) {
	if (W == 0 || H == 0) return;
	size_t N = (size_t)W * H;

	/* Pack planar normals into vec3 array for constrained_poisson */
	std::unique_ptr<vec3[]> normals(new vec3[N]);
	for (size_t i = 0; i < N; i++) {
		normals[i] = vec3(nx[i], ny[i], nz[i]);
	}

	ConstrainedPoissonCmd cmd{};
	cmd.original_height = original_heights;
	cmd.target_normals = normals.get();
	cmd.W = W;
	cmd.H = H;
	cmd.max_iterations = iterations > 0 ? iterations : 1000;
	cmd.tolerance = 1e-5f;
	cmd.zero_threshold = 1e-6f;

	if (constrained_poisson_Execute(&cmd) == 0 && cmd.result_height) {
		memcpy(heights, cmd.result_height.get(), N * sizeof(float));
	}
}

/* Convert planar heights (3 channels) to planar normals (3 channels * 3 components) */
static float* heights_to_normalmap_planar(const float *heights, uint32_t W, uint32_t H, float scale) {
	if (W == 0 || H == 0) return NULL;
	size_t N = (size_t)W * H;

	/* Allocate 9 planes: 3 height channels * 3 normal components */
	float *normals = new (std::nothrow) float[9 * N]();
	if (!normals) return NULL;

	for (int c = 0; c < 3; c++) {
		const float *h_channel = &heights[c * N];
		float *nx = &normals[(c * 3 + 0) * N];
		float *ny = &normals[(c * 3 + 1) * N];
		float *nz = &normals[(c * 3 + 2) * N];
		height_to_normals_planar(nx, ny, nz, h_channel, W, H, scale);
	}

	return normals;
}

/* Convert planar normals back to planar heights (original_heights provides constraints) */
static void normalmap_to_heights_planar(float *heights, const float *original_heights,
                                         const float *normals, uint32_t W, uint32_t H, int iterations) {
	if (W == 0 || H == 0) return;
	size_t N = (size_t)W * H;

	for (int c = 0; c < 3; c++) {
		float *h_channel = &heights[c * N];
		const float *orig_channel = &original_heights[c * N];
		const float *nx = &normals[(c * 3 + 0) * N];
		const float *ny = &normals[(c * 3 + 1) * N];
		const float *nz = &normals[(c * 3 + 2) * N];
		normals_to_height_planar(h_channel, orig_channel, nx, ny, nz, W, H, iterations);
	}
}

/* Process effects in gradient/normal space (planar normals: 9 planes for 3 channels * 3 components) */
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
			for (int c = 0; c < 3; c++) {
				float *nm_channel = &normals[c * 3 * N];
				box_blur_normals_planar(nm_channel, W, H, iters);
			}
			break;
		}

		case EFFECT_FOURIER_CLAMP: {
			float low_pass = effects[i].params.fourier_clamp.Maximum;
			float Hgh_pass = effects[i].params.fourier_clamp.Minimum;
			for (int c = 0; c < 3; c++) {
				float *nm_channel = &normals[c * 3 * N];
				fft_filter_normals_planar(nm_channel, W, H, low_pass, Hgh_pass);
			}
			break;
		}

		case EFFECT_LAMINARIZE: {
			LaminarizeParams const* lp = &effects[i].params.laminarize;
			for (int c = 0; c < 3; c++) {
				float *nm = &normals[c * 3 * N];
				/* Convert planar (nx[], ny[], nz[]) to vec3 array */
				std::unique_ptr<vec3[]> input(new vec3[N]);
				for (size_t j = 0; j < N; j++) {
					input[j] = vec3(nm[j], nm[N + j], nm[2 * N + j]);
				}
				LaminarizeCmd lam{};
				lam.normals = input.get();
				lam.W = W;
				lam.H = H;
				lam.scale = lp->scale;
				lam.strength = lp->strength;
				lam.blur_sigma = lp->blur_sigma;
				lam.max_iterations = 1000;
				lam.tolerance = 1e-5f;
				if (laminarize_Execute(&lam) == 0 && lam.result_normals) {
					for (size_t j = 0; j < N; j++) {
						nm[j]         = lam.result_normals[j].x;
						nm[N + j]     = lam.result_normals[j].y;
						nm[2 * N + j] = lam.result_normals[j].z;
					}
				}
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

		case EFFECT_DEBUG_HESSIAN_FLOW:
		case EFFECT_DEBUG_LIC:
		case EFFECT_DEBUG_LAPLACIAN:
		case EFFECT_DEBUG_RIDGE_MESH:

		case EFFECT_DEBUG_SPLIT_CHANNELS: {
			/* Export 3 normal maps (one per height channel) */
			for (int c = 0; c < 3; c++) {
				float *nx = &normals[(c * 3 + 0) * N];
				float *ny = &normals[(c * 3 + 1) * N];
				float *nz = &normals[(c * 3 + 2) * N];
				std::unique_ptr<vec3[]> packed(new vec3[N]);
				for (size_t j = 0; j < N; j++) {
					packed[j] = scale_normal(vec3(nx[j], ny[j], nz[j]), 0.1f);
				}
				char buf[512], filename[64];
				snprintf(filename, sizeof(filename), "normal_ch%d.png", c);
				PngVec3Cmd nc = {
					.path = debug_path(filename, buf, sizeof(buf)),
					.data = packed.get(),
					.width = W,
					.height = H,
				};
				png_ExportVec3(&nc);
			}
			break;
		}
		}
	}

	return i;
}

static int process_erosion_gradientify(float *height, uint32_t W, uint32_t H,
                                            Effect const* effects, int effect_count, int i)
{
	/* Convert to planar normal map */
	float scale = 1.0f;
	int start = i;
	if (i < effect_count && effects[i].effect_id == EFFECT_GRADIENTIFY) {
		scale = effects[i].params.gradientify.scale;
		if (scale <= 0.0f) scale = 1.0f;
		start = (i += 1); /* skip past the gradientify itself */
	}
	/* Otherwise entered implicitly (e.g. from LAMINARIZE) — start at i */

	/* Save original heights for constrained Poisson solve */
	size_t N = (size_t)W * H;
	std::unique_ptr<float[]> original_heights(new float[3 * N]);
	memcpy(original_heights.get(), height, 3 * N * sizeof(float));

	std::unique_ptr<float[]> normals(heights_to_normalmap_planar(height, W, H, scale));
	if (!normals) return i;

	/* Process effects in gradient space */
	i = process_erosion_gradient_planar(normals.get(), W, H, effects, effect_count, start);

	/* Poisson solve back to heights - get iterations from next effect if it's POISSON_SOLVE */
	int iterations = 1000;
	if (i < effect_count && effects[i].effect_id == EFFECT_POISSON_SOLVE) {
		iterations = (int)effects[i].params.poisson_solve.iterations;
		if (iterations < 1) iterations = 1000;
	}
	normalmap_to_heights_planar(height, original_heights.get(), normals.get(), W, H, iterations);

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
		uint32_t *regions = (uint32_t *)calloc(N * 3, sizeof(uint32_t));
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

/* Process effects on planar height field (3 channels stored as separate planes) */
static void process_erosion_height_planar(float *dst, uint32_t W, uint32_t H, Effect const* effects, int effect_count)
{
	size_t buffer_bytes = (size_t)W * H * 3 * sizeof(float);

	/* Find where we can resume from the memo cache */
	MemoResumePoint resume = memo_find_resume(&g_erosion_memo, effects, effect_count, W, H);

	/* Restore snapshot if we have a valid one */
	if (resume.snapshot_idx >= 0) {
		memcpy(dst, g_erosion_memo.layers[resume.snapshot_idx].buffer_snapshot, buffer_bytes);
	}

	/* Discard stale layers beyond the resume point */
	memo_truncate(&g_erosion_memo, resume.resume_from);
	g_erosion_memo.source_W = W;
	g_erosion_memo.source_H = H;

	for (int i = resume.resume_from; i < effect_count; ++i)
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

		case EFFECT_GRADIENTIFY:
		case EFFECT_LAMINARIZE: {
			int start_i = i;
			i = process_erosion_gradientify(dst, W, H, effects, effect_count, i);
			/* Save memo entries for all consumed effects in the sub-loop.
			 * Intermediates get no snapshot; final effect gets one if expensive. */
			for (int j = start_i; j <= i; j++) {
				bool do_snap = (j == i) && should_memoize(effects[j].effect_id);
				memo_save_layer(&g_erosion_memo, j, &effects[j],
				                do_snap ? dst : NULL,
				                do_snap ? buffer_bytes : 0);
			}
			continue; /* skip generic memo_save_layer at end of loop */
		}

		case EFFECT_POISSON_SOLVE:
			/* Orphaned poisson solve - ignore */
			break;

		case EFFECT_DEBUG_HESSIAN_FLOW: {
			HessianFlowDebugCmd flow{};
			flow.heightmap = &dst[W*H*1]; /* channel 1 (G) */
			flow.W = W;
			flow.H = H;
			flow.kernel_size = effects[i].params.debug_hessian_flow.kernel_size;
			flow.border = HESSIAN_BORDER_CLAMP_EDGE;
			flow.undefined_value = -1.f;
			flow.major_weight = 1.0;
			flow.minor_weight = 1.0;
			char dbg_path_buf[512];
			flow.output_path = debug_path("hessian.png", dbg_path_buf, sizeof(dbg_path_buf));
			hessian_flow_debug_Execute(&flow);
			break;
		}

		case EFFECT_DEBUG_SPLIT_CHANNELS: {
			size_t N = (size_t)W * H;
			char buf[512];
			PngFloatCmd fc = { .path = NULL, .data = NULL,
				.width = W, .height = H, .min_val = 0, .max_val = 1, .auto_range = 0 };
			fc.data = &dst[0*N];
			fc.path = debug_path("fadeIn.png", buf, sizeof(buf));
			png_ExportFloat(&fc);
			fc.data = &dst[1*N];
			fc.path = debug_path("fadeOut.png", buf, sizeof(buf));
			png_ExportFloat(&fc);
			fc.data = &dst[2*N];
			fc.path = debug_path("softness.png", buf, sizeof(buf));
			png_ExportFloat(&fc);
			break;
		}

		case EFFECT_DEBUG_LIC: {
			LicDebugCmd lic = {0};
			lic.heights = dst;
			lic.W = W;
			lic.H = H;
			lic.vector_field = effects[i].params.debug_lic.vector_field;
			lic.kernel_length = effects[i].params.debug_lic.kernel_length;
			lic.step_size = effects[i].params.debug_lic.step_size;
			lic_debug_Execute(&lic);
			break;
		}

		case EFFECT_DEBUG_RIDGE_MESH: {
			RidgeMeshCmd rm{};
			rm.heightmap = &dst[W*H*1]; /* channel 1 (G) */
			rm.W = W;
			rm.H = H;
			rm.normal_scale = effects[i].params.debug_ridge_mesh.normal_scale;
			rm.high_threshold = effects[i].params.debug_ridge_mesh.high_threshold;
			rm.low_threshold = effects[i].params.debug_ridge_mesh.low_threshold;
			ridge_mesh_Execute(&rm);
			ridge_mesh_DebugRender(&rm);
			break;
		}

		case EFFECT_DEBUG_LAPLACIAN: {
			size_t N = (size_t)W * H;
			char buf[512];
			for (int c = 0; c < 3; c++) {
				LaplacianCmd lap{};
				lap.heightmap = &dst[c * N];
				lap.W = W;
				lap.H = H;
				lap.kernel_size = effects[i].params.debug_laplacian.kernel_size;
				lap.border = HESSIAN_BORDER_CLAMP_EDGE;
				lap.undefined_value = -1.f;
				if (laplacian_Execute(&lap) == 0) {
					char filename[64];
					snprintf(filename, sizeof(filename), "laplacian_ch%d.png", c);
					PngFloatCmd fc{};
					fc.path = debug_path(filename, buf, sizeof(buf));
					fc.data = lap.laplacian;
					fc.width = W;
					fc.height = H;
					fc.auto_range = 1;
					png_ExportFloat(&fc);
				}
				laplacian_Free(&lap);
			}
			break;
		}

		case EFFECT_COLOR_RAMP:
		case EFFECT_BLEND_MODE:
			/* Invalid in erosion pipeline */
			break;
		}

		/* Save memo layer after each effect */
		{
			bool do_snapshot = should_memoize(effects[i].effect_id);
			memo_save_layer(&g_erosion_memo, i, &effects[i],
			                do_snapshot ? dst : NULL,
			                do_snapshot ? buffer_bytes : 0);
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

	std::unique_ptr<float[]> working_image(new float[N * 3]);

// deinterleave (RGB only)
	for (uint32_t c = 0; c < 3; ++c)
	{
		for (uint32_t i = 0; i < N; ++i)
			working_image[c * N + i] = memo->image.deinterleaved[c * N + i] / 255.f;
	}

	process_erosion_height_planar(working_image.get(), (uint32_t)W, (uint32_t)H, effects, effect_count);

	static u8vec4 * retn = 0L;
	retn = (u8vec4*)realloc(retn, N * sizeof(u8vec4));

// interleave (RGB + opaque alpha)
	for (uint32_t  i = 0; i < N; ++i)
	{
		retn[i].x = clamp_i32(working_image[0*N + i] * 255 + 0.5, 0, 255);
		retn[i].y = clamp_i32(working_image[1*N + i] * 255 + 0.5, 0, 255);
		retn[i].z = clamp_i32(working_image[2*N + i] * 255 + 0.5, 0, 255);
		retn[i].w = 255;
	}
	

    return (uint8_t*)retn;
}

