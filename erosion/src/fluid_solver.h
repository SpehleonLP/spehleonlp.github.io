#ifndef FLUID_SOLVER_H
#define FLUID_SOLVER_H

#include <stdint.h>
#include "normal_map.h"
#include "helmholtz.h"
#include "swirl.h"
#include "debug_png.h"

#define DEBUG_IMG_OUT 1

/*
 * FluidSolver - Computes flow from two height layers
 *
 * velocity = layer0 * rotate90(grad(layer3))
 * - Flow along layer3 contours, scaled by layer0 value
 * - Helmholtz decomposition extracts divergence-free component
 * - Swirl field from divergence-driven rotation
 */

typedef struct FluidSolver {
	uint32_t width;
	uint32_t height;

	// Input: 4-channel interlaced height data (channels 0 and 3 are used)
	float* height_interlaced0to4;

	// Flow: layer0 * rotate90(grad(layer3))
	vec2* velocity;

	// Helmholtz decomposition: velocity = incompressible + gradient
	vec2* incompressible;  // Divergence-free component
	vec2* curl_free;       // Curl-free (potential flow) component

	// Swirl field (divergence-driven rotation)
	vec2* swirl;

} FluidSolver;

// Setup and compute all fields from the interlaced height data
void fs_Setup(FluidSolver*);

// Free all allocated fields
void fs_Free(FluidSolver*);

#ifdef DEBUG_IMG_OUT
// Export all fields as a grid image
void fs_debug_export_all(const char* path, const FluidSolver* fs);
#endif

#endif
