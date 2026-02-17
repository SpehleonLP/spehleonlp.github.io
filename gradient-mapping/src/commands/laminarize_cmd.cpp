#include "laminarize_cmd.h"
#include "../utility.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <memory>

#define MAG_EPSILON 1e-6f

/*
 * Separable Gaussian blur on a float array (clamp-to-edge boundary)
 */
static void gaussian_blur(const float* input, float* output,
                           uint32_t W, uint32_t H, float sigma) {
	int radius = (int)ceilf(3.0f * sigma);
	if (radius < 1) radius = 1;
	int ksize = 2 * radius + 1;

	// Build 1D kernel
	auto kernel = std::unique_ptr<float[]>(new float[ksize]);
	float sum = 0.0f;
	for (int i = 0; i < ksize; i++) {
		float x = (float)(i - radius);
		kernel[i] = expf(-0.5f * x * x / (sigma * sigma));
		sum += kernel[i];
	}
	for (int i = 0; i < ksize; i++) kernel[i] /= sum;

	auto temp = std::unique_ptr<float[]>(new float[W * H]);

	// Horizontal pass
	for (uint32_t y = 0; y < H; y++) {
		for (uint32_t x = 0; x < W; x++) {
			float acc = 0.0f;
			for (int k = -radius; k <= radius; k++) {
				int sx = (int)x + k;
				if (sx < 0) sx = 0;
				if (sx >= (int)W) sx = (int)W - 1;
				acc += input[y * W + sx] * kernel[k + radius];
			}
			temp[y * W + x] = acc;
		}
	}

	// Vertical pass
	for (uint32_t y = 0; y < H; y++) {
		for (uint32_t x = 0; x < W; x++) {
			float acc = 0.0f;
			for (int k = -radius; k <= radius; k++) {
				int sy = (int)y + k;
				if (sy < 0) sy = 0;
				if (sy >= (int)H) sy = (int)H - 1;
				acc += temp[sy * W + x] * kernel[k + radius];
			}
			output[y * W + x] = acc;
		}
	}
}

/*
 * Compute divergence of a 2D vector field using central differences
 */
static void compute_divergence(const float* fx, const float* fy,
                                uint32_t W, uint32_t H, float* div_out) {
	for (uint32_t y = 0; y < H; y++) {
		for (uint32_t x = 0; x < W; x++) {
			uint32_t idx = y * W + x;

			uint32_t xm = (x > 0) ? x - 1 : 0;
			uint32_t xp = (x < W - 1) ? x + 1 : W - 1;
			uint32_t ym = (y > 0) ? y - 1 : 0;
			uint32_t yp = (y < H - 1) ? y + 1 : H - 1;

			float dFx_dx = (fx[y * W + xp] - fx[y * W + xm]) * 0.5f;
			float dFy_dy = (fy[yp * W + x] - fy[ym * W + x]) * 0.5f;

			div_out[idx] = dFx_dx + dFy_dy;
		}
	}
}

/*
 * Gauss-Seidel solver for nabla^2 phi = rhs with Neumann BC
 */
static float poisson_solve(float* phi, const float* rhs,
                            uint32_t W, uint32_t H,
                            int max_iter, float tolerance,
                            int* iterations_used) {
	float residual = 0.0f;
	int iter;

	for (iter = 0; iter < max_iter; iter++) {
		float max_change = 0.0f;

		for (uint32_t y = 0; y < H; y++) {
			for (uint32_t x = 0; x < W; x++) {
				uint32_t idx = y * W + x;

				// Neumann BC: mirror at boundaries
				float left  = (x > 0)     ? phi[idx - 1] : phi[idx + 1];
				float right = (x < W - 1) ? phi[idx + 1] : phi[idx - 1];
				float up    = (y > 0)     ? phi[idx - W] : phi[idx + W];
				float down  = (y < H - 1) ? phi[idx + W] : phi[idx - W];

				float new_val = (left + right + up + down - rhs[idx]) * 0.25f;
				float change = fabsf(new_val - phi[idx]);
				if (change > max_change) max_change = change;
				phi[idx] = new_val;
			}
		}

		if ((iter + 1) % 50 == 0 || max_change < tolerance) {
			float sum_sq = 0.0f;
			for (uint32_t y = 1; y < H - 1; y++) {
				for (uint32_t x = 1; x < W - 1; x++) {
					uint32_t idx = y * W + x;
					float lap = phi[idx-1] + phi[idx+1] + phi[idx-W] + phi[idx+W] - 4.0f*phi[idx];
					float r = lap - rhs[idx];
					sum_sq += r * r;
				}
			}
			residual = sqrtf(sum_sq / ((W - 2) * (H - 2)));

			if (max_change < tolerance) {
				iter++;
				break;
			}
		}
	}

	*iterations_used = iter;
	return residual;
}

int laminarize_Execute(LaminarizeCmd* cmd) {
	if (!cmd || !cmd->normals) {
		fprintf(stderr, "[laminarize] Error: NULL input\n");
		return -1;
	}

	uint32_t W = cmd->W;
	uint32_t H = cmd->H;
	uint32_t size = W * H;

	float scale = cmd->scale > 0.0f ? cmd->scale : 1.0f;
	float strength = cmd->strength;
	float blur_sigma = cmd->blur_sigma;

	// Step 1: Extract raw (-nx, -ny) field and compute magnitude
	auto mag = std::unique_ptr<float[]>(new float[size]);
	auto dir_x = std::unique_ptr<float[]>(new float[size]);
	auto dir_y = std::unique_ptr<float[]>(new float[size]);

	for (uint32_t i = 0; i < size; i++) {
		vec2 f(-cmd->normals[i].x, -cmd->normals[i].y);
		mag[i] = glm::length(f);
		dir_x[i] = f.x;
		dir_y[i] = f.y;
	}

	// Step 2: Compute divergence of raw (-nx, -ny) field
	auto L_orig = std::unique_ptr<float[]>(new float[size]);
	compute_divergence(dir_x.get(), dir_y.get(), W, H, L_orig.get());

	// Step 3: Compute target divergence from scaled normals
	auto scaled_fx = std::unique_ptr<float[]>(new float[size]);
	auto scaled_fy = std::unique_ptr<float[]>(new float[size]);
	auto L_target = std::unique_ptr<float[]>(new float[size]);

	for (uint32_t i = 0; i < size; i++) {
		vec3 n(cmd->normals[i].x, cmd->normals[i].y, cmd->normals[i].z * scale);
		float len = glm::length(n);
		if (len > 1e-8f) {
			scaled_fx[i] = -n.x / len;
			scaled_fy[i] = -n.y / len;
		} else {
			scaled_fx[i] = 0.0f;
			scaled_fy[i] = 0.0f;
		}
	}

	compute_divergence(scaled_fx.get(), scaled_fy.get(), W, H, L_target.get());
	scaled_fx.reset();
	scaled_fy.reset();

	// Step 4: Normalize direction field to unit vectors for Helmholtz correction
	for (uint32_t i = 0; i < size; i++) {
		if (mag[i] > MAG_EPSILON) {
			dir_x[i] /= mag[i];
			dir_y[i] /= mag[i];
		} else {
			dir_x[i] = 0.0f;
			dir_y[i] = 0.0f;
		}
	}

	// Step 5: Blur magnitude field
	auto blurred_mag = std::unique_ptr<float[]>(new float[size]);

	if (blur_sigma > 0.0f) {
		gaussian_blur(mag.get(), blurred_mag.get(), W, H, blur_sigma);
	} else {
		memcpy(blurred_mag.get(), mag.get(), size * sizeof(float));
	}
	mag.reset();

	// Poisson RHS = strength * (L_orig - L_target)
	auto rhs = std::unique_ptr<float[]>(new float[size]);
	auto phi = std::unique_ptr<float[]>(new float[size]());

	for (uint32_t i = 0; i < size; i++) {
		rhs[i] = strength * (L_orig[i] - L_target[i]);
	}
	L_orig.reset();
	L_target.reset();

	// Step 4: Solve nabla^2 phi = rhs
	int max_iter = cmd->max_iterations > 0 ? cmd->max_iterations : 1000;
	float tol = cmd->tolerance > 0.0f ? cmd->tolerance : 1e-5f;

	int iters;
	float residual = poisson_solve(phi.get(), rhs.get(), W, H, max_iter, tol, &iters);
	rhs.reset();

	cmd->iterations_used = iters;
	cmd->final_residual = residual;

	printf("[laminarize] Poisson converged in %d iterations, residual=%.2e\n",
		iters, residual);

	// Step 5: Correct unit direction field and reapply blurred magnitude
	if (!cmd->result_normals) {
		cmd->result_normals = std::unique_ptr<vec3[]>(new vec3[size]);
		if (!cmd->result_normals) {
			return -1;
		}
	}

	for (uint32_t y = 0; y < H; y++) {
		for (uint32_t x = 0; x < W; x++) {
			uint32_t idx = y * W + x;

			// grad phi via central differences
			float dphi_dx, dphi_dy;
			if (x == 0)       dphi_dx = phi[idx + 1] - phi[idx];
			else if (x == W-1) dphi_dx = phi[idx] - phi[idx - 1];
			else               dphi_dx = (phi[idx + 1] - phi[idx - 1]) * 0.5f;

			if (y == 0)       dphi_dy = phi[idx + W] - phi[idx];
			else if (y == H-1) dphi_dy = phi[idx] - phi[idx - W];
			else               dphi_dy = (phi[idx + W] - phi[idx - W]) * 0.5f;

			// Correct unit direction: dir' = dir - grad phi
			float cx = dir_x[idx] - dphi_dx;
			float cy = dir_y[idx] - dphi_dy;

			// Renormalize to unit direction
			float clen = sqrtf(cx * cx + cy * cy);
			if (clen > MAG_EPSILON) {
				cx /= clen;
				cy /= clen;
			}

			// Reapply blurred magnitude: F' = dir' * blurred_mag
			float fx = cx * blurred_mag[idx];
			float fy = cy * blurred_mag[idx];

			// Reconstruct normal: n = (-fx, -fy, nz)
			float xy_sq = fx * fx + fy * fy;
			float nz = (xy_sq < 1.0f) ? sqrtf(1.0f - xy_sq) : 0.0f;
			vec3 n(-fx, -fy, nz);
			float len = glm::length(n);

			if (len > 1e-8f) {
				cmd->result_normals[idx] = n / len;
			} else {
				cmd->result_normals[idx] = vec3(0, 0, 1);
			}
		}
	}

	return 0;
}

