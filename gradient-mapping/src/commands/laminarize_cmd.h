#ifndef LAMINARIZE_CMD_H
#define LAMINARIZE_CMD_H

#include <stdint.h>
#include <memory>
#include "../effect_stack_api.h"

/*
 * LaminarizeCmd - Helmholtz-style divergence redistribution
 *
 * Operates in magnitude-separated space:
 *   1. Extract raw (-nx,-ny) field, compute divergence (L_orig)
 *   2. Scale normals (nz *= scale), compute divergence of scaled field (L_target)
 *   3. Normalize (-nx,-ny) to unit directions, blur magnitude separately
 *   4. Helmholtz-correct unit directions: solve nabla^2 phi = strength*(L_orig - L_target)
 *   5. Reapply blurred magnitude, reconstruct normals
 *
 * Scale controls directional laminarization (divergence target pattern).
 * Blur sigma controls slope magnitude smoothing.
 * Strength blends between original and target divergence.
 *
 * Scale < 1: stronger Matthew effect (more divergence contrast)
 * Scale > 1: weaker (more uniform divergence)
 * Scale = 1: no directional change
 */
typedef struct {
	/* Input */
	const vec3* normals;          // Input normal map (scale=1)
	uint32_t W, H;
	float scale;                  // Normal Z scaling for target divergence
	float strength;               // Blend: 0 = unchanged, 1 = full target divergence
	float blur_sigma;             // Gaussian sigma for magnitude blur (0 = no blur)

	/* Poisson solver parameters */
	int max_iterations;           // default 1000
	float tolerance;              // default 1e-5

	/* Output (allocated internally if NULL) */
	std::unique_ptr<vec3[]> result_normals;  // Corrected normals

	/* Diagnostics */
	int iterations_used;
	float final_residual;
} LaminarizeCmd;

int laminarize_Execute(LaminarizeCmd* cmd);

#endif /* LAMINARIZE_CMD_H */
