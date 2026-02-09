#ifndef HELMHOLTZ_H
#define HELMHOLTZ_H

#include <stdint.h>

#ifndef VEC2_DEFINED
#define VEC2_DEFINED
typedef struct { float x, y; } vec2;
#endif

/*
 * HelmholtzCmd - Command pattern for Helmholtz-Hodge decomposition
 *
 * Decomposes a vector field into divergence-free (incompressible) and
 * curl-free (gradient) components:
 *
 *   velocity = incompressible + gradient
 *
 * Where:
 *   - incompressible has zero divergence (∇·u = 0)
 *   - gradient is the gradient of a scalar potential (∇φ)
 *
 * Algorithm:
 *   1. Compute divergence of input: div = ∇·velocity
 *   2. Solve Poisson equation: ∇²φ = div
 *   3. Compute gradient component: grad = ∇φ
 *   4. Subtract to get incompressible: incomp = velocity - grad
 *
 * Usage:
 *   HelmholtzCmd cmd = {0};
 *   cmd.width = w;
 *   cmd.height = h;
 *   cmd.velocity = my_velocity_field;
 *   cmd.mask = my_height_data;  // optional, for masking invalid regions
 *   cmd.mask_channel = 0;
 *   cmd.mask_stride = 4;
 *   cmd.iterations = 40;
 *   helmholtz_Execute(&cmd);
 *   // use cmd.incompressible and cmd.gradient
 *   helmholtz_Free(&cmd);
 */
typedef struct HelmholtzCmd {
	// Input parameters
	uint32_t width;
	uint32_t height;
	const vec2* velocity;      // input velocity field

	// Optional masking (to skip "no data" regions)
	const float* mask;         // if non-NULL, pixels where mask==mask_zero are skipped
	int mask_channel;          // channel offset for interlaced mask data
	int mask_stride;           // interlacing stride for mask
	float mask_zero;           // value treated as "no data" (default 0)

	// Solver parameters
	int iterations;            // Poisson solver iterations (default 40)

	// Outputs (allocated by Execute)
	vec2* incompressible;      // divergence-free component
	vec2* gradient;            // curl-free (potential) component

	// Internal scratch buffers (allocated by Execute, freed by Free)
	float* _divergence;
	float* _potential;
} HelmholtzCmd;

// Execute the Helmholtz decomposition
// Allocates outputs if NULL
// Returns 0 on success, negative on error
int helmholtz_Execute(HelmholtzCmd* cmd);

// Free allocated outputs and scratch buffers
void helmholtz_Free(HelmholtzCmd* cmd);

// Standalone divergence computation
// div = ∂u/∂x + ∂v/∂y
int helmholtz_ComputeDivergence(
	const vec2* velocity,
	const float* mask, int mask_channel, int mask_stride, float mask_zero,
	uint32_t width, uint32_t height,
	float* div_out);

#endif
