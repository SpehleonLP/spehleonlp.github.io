#include "helmholtz.h"
#include <stdlib.h>
#include <string.h>

/*
 * Helmholtz-Hodge Decomposition
 *
 * Any vector field can be decomposed into:
 *   F = F_incomp + F_grad
 *
 * Where F_incomp is divergence-free and F_grad is curl-free.
 *
 * We solve: ∇²φ = ∇·F
 * Then: F_grad = ∇φ
 *       F_incomp = F - F_grad
 */

#define DEFAULT_ITERATIONS 40

static inline int is_valid(const float* mask, int idx, int ch, int stride, float zero_val) {
	if (!mask) return 1;
	return mask[idx * stride + ch] != zero_val;
}

// Compute divergence: div = ∂u/∂x + ∂v/∂y
int helmholtz_ComputeDivergence(
	const vec2* velocity,
	const float* mask, int mask_ch, int mask_stride, float mask_zero,
	uint32_t w, uint32_t h,
	float* div_out)
{
	if (!velocity || !div_out || w == 0 || h == 0)
		return -1;

	for (uint32_t y = 0; y < h; y++) {
		for (uint32_t x = 0; x < w; x++) {
			uint32_t idx = y * w + x;

			if (!is_valid(mask, idx, mask_ch, mask_stride, mask_zero)) {
				div_out[idx] = 0.0f;
				continue;
			}

			// Gather neighbor velocities
			float u_right = 0.0f, u_left = 0.0f;
			float v_down = 0.0f, v_up = 0.0f;
			int count_x = 0, count_y = 0;

			if (x > 0 && is_valid(mask, idx - 1, mask_ch, mask_stride, mask_zero)) {
				u_left = velocity[idx - 1].x;
				count_x++;
			}
			if (x < w - 1 && is_valid(mask, idx + 1, mask_ch, mask_stride, mask_zero)) {
				u_right = velocity[idx + 1].x;
				count_x++;
			}
			if (y > 0 && is_valid(mask, idx - w, mask_ch, mask_stride, mask_zero)) {
				v_up = velocity[idx - w].y;
				count_y++;
			}
			if (y < h - 1 && is_valid(mask, idx + w, mask_ch, mask_stride, mask_zero)) {
				v_down = velocity[idx + w].y;
				count_y++;
			}

			// Central differences where possible, else forward/backward
			float dudx = 0.0f, dvdy = 0.0f;

			if (count_x == 2) {
				dudx = (u_right - u_left) * 0.5f;
			} else if (x < w - 1 && is_valid(mask, idx + 1, mask_ch, mask_stride, mask_zero)) {
				dudx = u_right - velocity[idx].x;
			} else if (x > 0 && is_valid(mask, idx - 1, mask_ch, mask_stride, mask_zero)) {
				dudx = velocity[idx].x - u_left;
			}

			if (count_y == 2) {
				dvdy = (v_down - v_up) * 0.5f;
			} else if (y < h - 1 && is_valid(mask, idx + w, mask_ch, mask_stride, mask_zero)) {
				dvdy = v_down - velocity[idx].y;
			} else if (y > 0 && is_valid(mask, idx - w, mask_ch, mask_stride, mask_zero)) {
				dvdy = velocity[idx].y - v_up;
			}

			div_out[idx] = dudx + dvdy;
		}
	}

	return 0;
}

// Solve Poisson equation: ∇²φ = div using Gauss-Seidel iteration
static void solve_poisson(
	float* phi,
	const float* div,
	const float* mask, int mask_ch, int mask_stride, float mask_zero,
	uint32_t w, uint32_t h,
	int iterations)
{
	memset(phi, 0, w * h * sizeof(float));

	for (int iter = 0; iter < iterations; iter++) {
		for (uint32_t y = 0; y < h; y++) {
			for (uint32_t x = 0; x < w; x++) {
				uint32_t idx = y * w + x;

				if (!is_valid(mask, idx, mask_ch, mask_stride, mask_zero))
					continue;

				float sum = 0.0f;
				int count = 0;

				if (x > 0 && is_valid(mask, idx - 1, mask_ch, mask_stride, mask_zero)) {
					sum += phi[idx - 1];
					count++;
				}
				if (x < w - 1 && is_valid(mask, idx + 1, mask_ch, mask_stride, mask_zero)) {
					sum += phi[idx + 1];
					count++;
				}
				if (y > 0 && is_valid(mask, idx - w, mask_ch, mask_stride, mask_zero)) {
					sum += phi[idx - w];
					count++;
				}
				if (y < h - 1 && is_valid(mask, idx + w, mask_ch, mask_stride, mask_zero)) {
					sum += phi[idx + w];
					count++;
				}

				if (count > 0)
					phi[idx] = (sum - div[idx]) / (float)count;
			}
		}
	}
}

// Compute gradient of scalar field: grad = ∇φ
static void compute_potential_gradient(
	const float* phi,
	const float* mask, int mask_ch, int mask_stride, float mask_zero,
	uint32_t w, uint32_t h,
	vec2* grad_out)
{
	for (uint32_t y = 0; y < h; y++) {
		for (uint32_t x = 0; x < w; x++) {
			uint32_t idx = y * w + x;

			if (!is_valid(mask, idx, mask_ch, mask_stride, mask_zero)) {
				grad_out[idx].x = 0.0f;
				grad_out[idx].y = 0.0f;
				continue;
			}

			float phi_left = phi[idx], phi_right = phi[idx];
			float phi_up = phi[idx], phi_down = phi[idx];

			if (x > 0 && is_valid(mask, idx - 1, mask_ch, mask_stride, mask_zero))
				phi_left = phi[idx - 1];
			if (x < w - 1 && is_valid(mask, idx + 1, mask_ch, mask_stride, mask_zero))
				phi_right = phi[idx + 1];
			if (y > 0 && is_valid(mask, idx - w, mask_ch, mask_stride, mask_zero))
				phi_up = phi[idx - w];
			if (y < h - 1 && is_valid(mask, idx + w, mask_ch, mask_stride, mask_zero))
				phi_down = phi[idx + w];

			grad_out[idx].x = (phi_right - phi_left) * 0.5f;
			grad_out[idx].y = (phi_down - phi_up) * 0.5f;
		}
	}
}

int helmholtz_Execute(HelmholtzCmd* cmd) {
	if (!cmd || !cmd->velocity || cmd->width == 0 || cmd->height == 0)
		return -1;

	uint32_t w = cmd->width;
	uint32_t h = cmd->height;
	uint32_t N = w * h;
	int iterations = cmd->iterations > 0 ? cmd->iterations : DEFAULT_ITERATIONS;

	// Allocate outputs if needed
	if (!cmd->incompressible) {
		cmd->incompressible = malloc(sizeof(vec2) * N);
		if (!cmd->incompressible) return -2;
	}
	if (!cmd->gradient) {
		cmd->gradient = malloc(sizeof(vec2) * N);
		if (!cmd->gradient) return -2;
	}

	// Allocate scratch buffers
	if (!cmd->_divergence) {
		cmd->_divergence = malloc(sizeof(float) * N);
		if (!cmd->_divergence) return -2;
	}
	if (!cmd->_potential) {
		cmd->_potential = malloc(sizeof(float) * N);
		if (!cmd->_potential) return -2;
	}

	// Step 1: Compute divergence
	helmholtz_ComputeDivergence(
		cmd->velocity,
		cmd->mask, cmd->mask_channel, cmd->mask_stride, cmd->mask_zero,
		w, h,
		cmd->_divergence);

	// Step 2: Solve Poisson equation
	solve_poisson(
		cmd->_potential,
		cmd->_divergence,
		cmd->mask, cmd->mask_channel, cmd->mask_stride, cmd->mask_zero,
		w, h,
		iterations);

	// Step 3: Compute gradient of potential
	compute_potential_gradient(
		cmd->_potential,
		cmd->mask, cmd->mask_channel, cmd->mask_stride, cmd->mask_zero,
		w, h,
		cmd->gradient);

	// Step 4: incompressible = velocity - gradient
	for (uint32_t i = 0; i < N; i++) {
		cmd->incompressible[i].x = cmd->velocity[i].x - cmd->gradient[i].x;
		cmd->incompressible[i].y = cmd->velocity[i].y - cmd->gradient[i].y;
	}

	return 0;
}

void helmholtz_Free(HelmholtzCmd* cmd) {
	if (!cmd) return;

	free(cmd->incompressible);
	free(cmd->gradient);
	free(cmd->_divergence);
	free(cmd->_potential);

	cmd->incompressible = NULL;
	cmd->gradient = NULL;
	cmd->_divergence = NULL;
	cmd->_potential = NULL;
}
