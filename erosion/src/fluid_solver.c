#include "fluid_solver.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/*
 * FluidSolver - Computes flow from two height layers
 *
 * Simple approach: velocity = layer0 * rotate90(grad(layer3))
 * - Flow direction is along layer3 contours (perpendicular to its gradient)
 * - Flow magnitude is scaled by layer0 value
 * - Helmholtz decomposition extracts incompressible (divergence-free) component
 */

#define POISSON_ITERATIONS 40

// Compute gradient at a pixel from interlaced data
static vec2 compute_gradient_interlaced(const float* data, uint32_t w, uint32_t h,
                                        uint32_t x, uint32_t y, int channel, int stride) {
	uint32_t idx = y * w + x;
	vec2 g = {0, 0};

	float center = data[idx * stride + channel];
	if (center <= 0.0f) {
		return g;  // Invalid pixel
	}

	float left  = (x > 0)      ? data[(idx - 1) * stride + channel] : center;
	float right = (x < w - 1)  ? data[(idx + 1) * stride + channel] : center;
	float up    = (y > 0)      ? data[(idx - w) * stride + channel] : center;
	float down  = (y < h - 1)  ? data[(idx + w) * stride + channel] : center;

	// Handle invalid neighbors
	if (left <= 0.0f) left = center;
	if (right <= 0.0f) right = center;
	if (up <= 0.0f) up = center;
	if (down <= 0.0f) down = center;

	g.x = (right - left) * 0.5f;
	g.y = (down - up) * 0.5f;

	return g;
}

void fs_Setup(FluidSolver* solver) {
	if (!solver || !solver->height_interlaced0to4)
		return;

	uint32_t N = solver->width * solver->height;
	uint32_t w = solver->width;
	uint32_t h = solver->height;
	const float* data = solver->height_interlaced0to4;

	// Allocate all vec2 fields in one block (4 fields)
	if (!solver->velocity) {
		solver->velocity = malloc(sizeof(vec2) * N * 4);
		if (!solver->velocity) return;

		solver->incompressible = solver->velocity + N;
		solver->curl_free = solver->incompressible + N;
		solver->swirl = solver->curl_free + N;
	}

	// Compute flow: layer0 * rotate90(grad3)
	// Flow along layer3 contours, scaled by layer0 value
	for (uint32_t y = 0; y < h; y++) {
		for (uint32_t x = 0; x < w; x++) {
			uint32_t idx = y * w + x;

			float h0 = data[idx * 4 + 0];
			float h3 = data[idx * 4 + 3];

			// Need both layers valid
			if (h0 <= 0.0f || h3 <= 0.0f) {
				solver->velocity[idx].x = 0.0f;
				solver->velocity[idx].y = 0.0f;
				continue;
			}

			// Compute gradient of layer3
			vec2 grad3 = compute_gradient_interlaced(data, w, h, x, y, 3, 4);

			// Rotate 90° CCW to get tangent to layer3 contours
			// rot90(grad3) = (-grad3.y, grad3.x)
			// Scale by layer0 value
			solver->velocity[idx].x = -grad3.y * h0;
			solver->velocity[idx].y = grad3.x * h0;
		}
	}

	// Helmholtz decomposition of the contour flow
	{
		HelmholtzCmd cmd = {
			.width = w,
			.height = h,
			.velocity = solver->velocity,
			.mask = data,
			.mask_channel = 0,
			.mask_stride = 4,
			.mask_zero = 0.0f,
			.iterations = POISSON_ITERATIONS,
			.incompressible = solver->incompressible,
			.gradient = solver->curl_free
		};
		helmholtz_Execute(&cmd);
		free(cmd._divergence);
		free(cmd._potential);
	}

	// Swirl field
	{
		SwirlCmd cmd = {
			.width = w,
			.height = h,
			.velocity = solver->velocity,
			.divergence = NULL,
			.mask = data,
			.mask_channel = 0,
			.mask_stride = 4,
			.mask_zero = 0.0f,
			.strength = 1.0f,
			.swirl = solver->swirl
		};
		swirl_Execute(&cmd);
		if (cmd._owns_divergence)
			free(cmd._divergence);
	}
}

void fs_Free(FluidSolver* solver) {
	if (!solver) return;

	free(solver->velocity);

	solver->velocity = NULL;
	solver->incompressible = NULL;
	solver->curl_free = NULL;
	solver->swirl = NULL;
}

#ifdef DEBUG_IMG_OUT

void fs_debug_export_all(const char* path, const FluidSolver* fs) {
	if (!fs || !fs->height_interlaced0to4)
		return;

	// 2 rows x 3 cols
	// Row 0: layer0, layer3, velocity (contour flow)
	// Row 1: incompressible, curl_free, swirl
	PngGridTile tiles[6] = {
		{ PNG_TILE_INTERLEAVED, fs->height_interlaced0to4, 0, 4 },
		{ PNG_TILE_INTERLEAVED, fs->height_interlaced0to4, 3, 4 },
		{ PNG_TILE_VEC2, fs->velocity, 0, 0 },
		{ PNG_TILE_VEC2, fs->incompressible, 0, 0 },
		{ PNG_TILE_VEC2, fs->curl_free, 0, 0 },
		{ PNG_TILE_VEC2, fs->swirl, 0, 0 },
	};

	PngGridCmd cmd = {
		.path = path,
		.tile_width = fs->width,
		.tile_height = fs->height,
		.cols = 3,
		.rows = 2,
		.tiles = tiles
	};
	png_ExportGrid(&cmd);
}

#endif
