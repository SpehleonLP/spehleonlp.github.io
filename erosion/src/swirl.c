#include "swirl.h"
#include "helmholtz.h"  // for helmholtz_ComputeDivergence
#include <stdlib.h>
#include <math.h>

/*
 * Swirl Field Generation
 *
 * Creates rotational motion based on divergence. At divergent regions
 * (sources/sinks), the flow is rotated perpendicular to create swirling
 * patterns. The sign of divergence determines rotation direction.
 */

static inline int is_valid(const float* mask, int idx, int ch, int stride, float zero_val) {
	if (!mask) return 1;
	return mask[idx * stride + ch] != zero_val;
}

int swirl_Execute(SwirlCmd* cmd) {
	if (!cmd || !cmd->velocity || cmd->width == 0 || cmd->height == 0)
		return -1;

	uint32_t w = cmd->width;
	uint32_t h = cmd->height;
	uint32_t N = w * h;
	float strength = cmd->strength;

	// Allocate output
	if (!cmd->swirl) {
		cmd->swirl = malloc(sizeof(vec2) * N);
		if (!cmd->swirl) return -2;
	}

	// Get or compute divergence
	const float* div = cmd->divergence;
	if (!div) {
		cmd->_divergence = malloc(sizeof(float) * N);
		if (!cmd->_divergence) return -2;
		cmd->_owns_divergence = 1;

		helmholtz_ComputeDivergence(
			cmd->velocity,
			cmd->mask, cmd->mask_channel, cmd->mask_stride, cmd->mask_zero,
			w, h,
			cmd->_divergence);

		div = cmd->_divergence;
	}

	// Find max divergence for normalization
	float max_div = 0.0f;
	for (uint32_t i = 0; i < N; i++) {
		float d = fabsf(div[i]);
		if (d > max_div) max_div = d;
	}
	if (max_div < 1e-6f) max_div = 1.0f;

	// Generate swirl field
	for (uint32_t y = 0; y < h; y++) {
		for (uint32_t x = 0; x < w; x++) {
			uint32_t idx = y * w + x;

			if (!is_valid(cmd->mask, idx, cmd->mask_channel, cmd->mask_stride, cmd->mask_zero)) {
				cmd->swirl[idx].x = 0.0f;
				cmd->swirl[idx].y = 0.0f;
				continue;
			}

			float vx = cmd->velocity[idx].x;
			float vy = cmd->velocity[idx].y;
			float d = div[idx];

			// Normalize divergence and apply smoothstep
			float norm_div = fabsf(d) / max_div;
			float blend = norm_div * norm_div * (3.0f - 2.0f * norm_div);
			blend *= strength;
			if (blend > 1.0f) blend = 1.0f;

			// Perpendicular vector, direction based on sign of divergence
			float sign = (d >= 0.0f) ? 1.0f : -1.0f;
			float perp_x = -vy * sign;
			float perp_y =  vx * sign;

			// Output the perpendicular component scaled by blend
			cmd->swirl[idx].x = perp_x * blend;
			cmd->swirl[idx].y = perp_y * blend;
		}
	}

	return 0;
}

void swirl_Free(SwirlCmd* cmd) {
	if (!cmd) return;

	free(cmd->swirl);
	cmd->swirl = NULL;

	if (cmd->_owns_divergence) {
		free(cmd->_divergence);
		cmd->_divergence = NULL;
		cmd->_owns_divergence = 0;
	}
}
