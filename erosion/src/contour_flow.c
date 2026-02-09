#include "contour_flow.h"
#include "flood_fill.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
 * Contour Flow - directional flow along iso-lines
 *
 * The key insight: gradient points ACROSS contours (uphill).
 * Rotating 90° gives flow ALONG contours.
 * But we need consistent direction choice to avoid chaos.
 *
 * We use flood-fill from ridges/seeds to propagate direction choice.
 * Each pixel inherits direction from its nearest seed, ensuring
 * smooth, consistent flow patterns.
 */

// Compute gradient at a pixel
static vec2 compute_gradient(const float* h, uint32_t w, uint32_t height,
                             uint32_t x, uint32_t y) {
	uint32_t idx = y * w + x;
	vec2 g = {0, 0};

	float left  = (x > 0)          ? h[idx - 1] : h[idx];
	float right = (x < w - 1)      ? h[idx + 1] : h[idx];
	float up    = (y > 0)          ? h[idx - w] : h[idx];
	float down  = (y < height - 1) ? h[idx + w] : h[idx];

	g.x = (right - left) * 0.5f;
	g.y = (down - up) * 0.5f;

	return g;
}

// Detect ridges using eigenvalues of Hessian
int cf_DetectRidges(
	const float* heightmap,
	uint32_t w, uint32_t h,
	CFRidgeMode mode,
	float* ridge_out)
{
	if (!heightmap || !ridge_out || w == 0 || h == 0)
		return -1;

	for (uint32_t y = 0; y < h; y++) {
		for (uint32_t x = 0; x < w; x++) {
			uint32_t idx = y * w + x;
			float strength = 0.0f;

			// Need 1-pixel border for Hessian
			if (x < 1 || x >= w - 1 || y < 1 || y >= h - 1) {
				ridge_out[idx] = 0.0f;
				continue;
			}

			// Compute Hessian components
			float center = heightmap[idx];
			float left   = heightmap[idx - 1];
			float right  = heightmap[idx + 1];
			float up     = heightmap[idx - w];
			float down   = heightmap[idx + w];
			float ul     = heightmap[idx - w - 1];
			float ur     = heightmap[idx - w + 1];
			float dl     = heightmap[idx + w - 1];
			float dr     = heightmap[idx + w + 1];

			// Second derivatives
			float hxx = right - 2.0f * center + left;
			float hyy = down - 2.0f * center + up;
			float hxy = (dr - dl - ur + ul) * 0.25f;

			// Eigenvalues of Hessian: λ = (hxx + hyy)/2 ± sqrt(((hxx-hyy)/2)² + hxy²)
			float trace = hxx + hyy;
			float det = hxx * hyy - hxy * hxy;
			float disc = trace * trace * 0.25f - det;
			if (disc < 0) disc = 0;
			disc = sqrtf(disc);

			float lambda1 = trace * 0.5f + disc;  // larger eigenvalue
			float lambda2 = trace * 0.5f - disc;  // smaller eigenvalue

			// Ridge detection based on mode
			switch (mode) {
			case CF_RIDGE_PEAKS:
				// Ridge: one eigenvalue very negative (curves down sharply in one direction)
				// but other eigenvalue near zero (flat along ridge)
				if (lambda2 < -0.01f && fabsf(lambda1) < fabsf(lambda2) * 0.5f) {
					strength = -lambda2;
				}
				break;

			case CF_RIDGE_VALLEYS:
				// Valley: one eigenvalue very positive
				if (lambda1 > 0.01f && fabsf(lambda2) < fabsf(lambda1) * 0.5f) {
					strength = lambda1;
				}
				break;

			case CF_RIDGE_BOTH:
				// Either ridge or valley
				if (lambda2 < -0.01f && fabsf(lambda1) < fabsf(lambda2) * 0.5f) {
					strength = -lambda2;
				} else if (lambda1 > 0.01f && fabsf(lambda2) < fabsf(lambda1) * 0.5f) {
					strength = lambda1;
				}
				break;

			case CF_RIDGE_SADDLES:
				// Saddle: eigenvalues have opposite signs
				if (lambda1 * lambda2 < -0.001f) {
					strength = fabsf(lambda1 * lambda2);
				}
				break;

			default:
				break;
			}

			ridge_out[idx] = strength;
		}
	}

	// Normalize ridge strength to [0, 1]
	float max_strength = 0.0f;
	for (uint32_t i = 0; i < w * h; i++) {
		if (ridge_out[i] > max_strength)
			max_strength = ridge_out[i];
	}
	if (max_strength > 1e-6f) {
		for (uint32_t i = 0; i < w * h; i++) {
			ridge_out[i] /= max_strength;
		}
	}

	return 0;
}

// Custom flood fill rule for direction propagation
// Propagates direction from nearest seed, with influence falloff
typedef struct {
	const int8_t* seed_directions;
	const float* heightmap;
	uint32_t width;
	float falloff;
} DirectionPropagateData;

static float direction_propagate_rule(const FFRuleContext* ctx) {
	// Simply propagate distance - direction comes from seed
	float min_dist = INFINITY;
	for (int i = 0; i < ctx->neighbor_count; i++) {
		float d = ctx->neighbors[i].value + ctx->neighbors[i].distance;
		if (d < min_dist) min_dist = d;
	}
	return min_dist;
}

int cf_Execute(ContourFlowCmd* cmd) {
	if (!cmd || !cmd->heightmap || cmd->width == 0 || cmd->height == 0)
		return -1;

	uint32_t w = cmd->width;
	uint32_t h = cmd->height;
	uint32_t N = w * h;

	float min_grad = cmd->min_gradient > 0 ? cmd->min_gradient : 0.001f;
	float falloff = cmd->influence_falloff > 0 ? cmd->influence_falloff : 0.1f;
	float grad_blend = cmd->gradient_blend;
	if (grad_blend < 0) grad_blend = 0;
	if (grad_blend > 1) grad_blend = 1;

	// Allocate outputs
	if (!cmd->flow) {
		cmd->flow = malloc(sizeof(vec2) * N);
		if (!cmd->flow) return -2;
	}
	if (!cmd->influence) {
		cmd->influence = malloc(sizeof(float) * N);
		if (!cmd->influence) return -2;
	}
	if (!cmd->direction) {
		cmd->direction = malloc(sizeof(int8_t) * N);
		if (!cmd->direction) return -2;
	}

	// Compute gradient field
	cmd->_gradient = malloc(sizeof(vec2) * N);
	if (!cmd->_gradient) return -2;

	for (uint32_t y = 0; y < h; y++) {
		for (uint32_t x = 0; x < w; x++) {
			cmd->_gradient[y * w + x] = compute_gradient(cmd->heightmap, w, h, x, y);
		}
	}

	// Detect ridges if requested
	float* ridge_strength = NULL;
	if (cmd->ridge_mode != CF_RIDGE_NONE) {
		ridge_strength = malloc(sizeof(float) * N);
		cmd->_ridge_strength = ridge_strength;
		if (!ridge_strength) return -2;
		cf_DetectRidges(cmd->heightmap, w, h, cmd->ridge_mode, ridge_strength);
	}

	// Build seed list: user seeds + detected ridges
	uint32_t max_seeds = cmd->seed_count + (ridge_strength ? N / 100 : 0);
	FFSeed* all_seeds = malloc(sizeof(FFSeed) * (max_seeds + 1));
	int8_t* seed_dir_map = calloc(N, sizeof(int8_t));  // direction at each seed
	if (!all_seeds || !seed_dir_map) {
		free(all_seeds);
		free(seed_dir_map);
		return -2;
	}

	uint32_t num_seeds = 0;

	// Add user-provided seeds
	for (uint32_t i = 0; i < cmd->seed_count && num_seeds < max_seeds; i++) {
		int x = cmd->seeds[i].x;
		int y = cmd->seeds[i].y;
		if (x >= 0 && x < (int)w && y >= 0 && y < (int)h) {
			all_seeds[num_seeds].x = x;
			all_seeds[num_seeds].y = y;
			all_seeds[num_seeds].value = cmd->seeds[i].priority;
			seed_dir_map[y * w + x] = (int8_t)cmd->seeds[i].direction;
			num_seeds++;
		}
	}

	// Add ridge points as seeds (alternating direction for interesting patterns)
	if (ridge_strength) {
		float threshold = cmd->ridge_threshold > 0 ? cmd->ridge_threshold : 0.5f;

		// Find local maxima in ridge strength
		for (uint32_t y = 2; y < h - 2 && num_seeds < max_seeds; y += 4) {
			for (uint32_t x = 2; x < w - 2 && num_seeds < max_seeds; x += 4) {
				uint32_t idx = y * w + x;
				float rs = ridge_strength[idx];

				if (rs < threshold) continue;

				// Check if local maximum in 3x3
				int is_max = 1;
				for (int dy = -1; dy <= 1 && is_max; dy++) {
					for (int dx = -1; dx <= 1 && is_max; dx++) {
						if (dx == 0 && dy == 0) continue;
						if (ridge_strength[(y + dy) * w + (x + dx)] > rs)
							is_max = 0;
					}
				}

				if (is_max) {
					all_seeds[num_seeds].x = x;
					all_seeds[num_seeds].y = y;
					all_seeds[num_seeds].value = 1.0f - rs;  // stronger ridges = higher priority

					// Alternate direction based on position for variety
					// Could also base this on gradient direction or other heuristics
					int dir = ((x / 4 + y / 4) % 2) ? 1 : -1;
					seed_dir_map[idx] = (int8_t)dir;
					num_seeds++;
				}
			}
		}
	}

	// If no seeds, create a default at center
	if (num_seeds == 0) {
		all_seeds[0].x = w / 2;
		all_seeds[0].y = h / 2;
		all_seeds[0].value = 0.0f;
		seed_dir_map[(h / 2) * w + (w / 2)] = 1;
		num_seeds = 1;
	}

	// Flood fill to propagate distance from seeds
	FloodFillCmd ff = {
		.width = w,
		.height = h,
		.seeds = all_seeds,
		.seed_count = num_seeds,
		.rule = ff_rule_distance,
		.connectivity = FF_CONNECT_8,
		.max_value = INFINITY
	};

	int ff_result = ff_Execute(&ff);
	if (ff_result < 0) {
		free(all_seeds);
		free(seed_dir_map);
		return -3;
	}

	// For each pixel, find which seed it's closest to and inherit direction
	// (We need to trace back through the distance field)
	// Simpler approach: another flood fill that propagates direction

	// Initialize direction from seeds
	for (uint32_t i = 0; i < N; i++) {
		cmd->direction[i] = 0;
	}
	for (uint32_t i = 0; i < num_seeds; i++) {
		int x = all_seeds[i].x;
		int y = all_seeds[i].y;
		cmd->direction[y * w + x] = seed_dir_map[y * w + x];
	}

	// Propagate direction by following gradient of distance field toward seeds
	// Multiple passes to ensure propagation
	for (int pass = 0; pass < (int)(w + h); pass++) {
		for (uint32_t y = 0; y < h; y++) {
			for (uint32_t x = 0; x < w; x++) {
				uint32_t idx = y * w + x;
				if (cmd->direction[idx] != 0) continue;

				// Find neighbor with smallest distance
				float min_d = ff.output[idx];
				int8_t best_dir = 0;

				for (int dy = -1; dy <= 1; dy++) {
					for (int dx = -1; dx <= 1; dx++) {
						if (dx == 0 && dy == 0) continue;
						int nx = (int)x + dx;
						int ny = (int)y + dy;
						if (nx < 0 || nx >= (int)w || ny < 0 || ny >= (int)h) continue;

						uint32_t nidx = ny * w + nx;
						if (ff.output[nidx] < min_d && cmd->direction[nidx] != 0) {
							min_d = ff.output[nidx];
							best_dir = cmd->direction[nidx];
						}
					}
				}

				cmd->direction[idx] = best_dir;
			}
		}
	}

	// Default any remaining to +1
	for (uint32_t i = 0; i < N; i++) {
		if (cmd->direction[i] == 0)
			cmd->direction[i] = 1;
	}

	// Compute influence (inverse of distance, with falloff)
	float max_dist = 0.0f;
	for (uint32_t i = 0; i < N; i++) {
		if (isfinite(ff.output[i]) && ff.output[i] > max_dist)
			max_dist = ff.output[i];
	}
	if (max_dist < 1.0f) max_dist = 1.0f;

	for (uint32_t i = 0; i < N; i++) {
		if (!isfinite(ff.output[i])) {
			cmd->influence[i] = 0.0f;
		} else {
			// Exponential falloff from seeds
			cmd->influence[i] = expf(-ff.output[i] * falloff);
		}
	}

	// Compute final flow field
	for (uint32_t i = 0; i < N; i++) {
		vec2 g = cmd->_gradient[i];
		float mag = sqrtf(g.x * g.x + g.y * g.y);

		if (mag < min_grad) {
			cmd->flow[i].x = 0.0f;
			cmd->flow[i].y = 0.0f;
			continue;
		}

		// Normalize gradient
		g.x /= mag;
		g.y /= mag;

		// Rotate 90° based on direction
		vec2 tangent = cf_rotate90(g, cmd->direction[i]);

		// Blend with original gradient if requested
		if (grad_blend > 0) {
			cmd->flow[i].x = tangent.x * (1.0f - grad_blend) + g.x * grad_blend;
			cmd->flow[i].y = tangent.y * (1.0f - grad_blend) + g.y * grad_blend;
			// Re-normalize
			float m = sqrtf(cmd->flow[i].x * cmd->flow[i].x + cmd->flow[i].y * cmd->flow[i].y);
			if (m > 1e-6f) {
				cmd->flow[i].x /= m;
				cmd->flow[i].y /= m;
			}
		} else {
			cmd->flow[i] = tangent;
		}

		// Scale by influence and original gradient magnitude
		cmd->flow[i].x *= mag * cmd->influence[i];
		cmd->flow[i].y *= mag * cmd->influence[i];
	}

	// Cleanup
	ff_Free(&ff);
	free(all_seeds);
	free(seed_dir_map);

	return 0;
}

void cf_Free(ContourFlowCmd* cmd) {
	if (!cmd) return;

	free(cmd->flow);
	free(cmd->influence);
	free(cmd->direction);
	free(cmd->_gradient);
	free(cmd->_ridge_strength);

	cmd->flow = NULL;
	cmd->influence = NULL;
	cmd->direction = NULL;
	cmd->_gradient = NULL;
	cmd->_ridge_strength = NULL;
}
