#include "lic_debug_cmd.h"
#include "heightmap_ops.h"
#include "../utility.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <memory>

/*
 * Bilinear sample a scalar field with clamp-to-edge boundary.
 */
static float lic_dbg_bilinear_scalar(const float* field, uint32_t W, uint32_t H,
                                      float fx, float fy) {
	fx = clamp_f32(fx, 0.0f, (float)(W - 1));
	fy = clamp_f32(fy, 0.0f, (float)(H - 1));

	uint32_t x0 = (uint32_t)fx;
	uint32_t y0 = (uint32_t)fy;
	uint32_t x1 = x0 < W - 1 ? x0 + 1 : x0;
	uint32_t y1 = y0 < H - 1 ? y0 + 1 : y0;

	float sx = fx - (float)x0;
	float sy = fy - (float)y0;

	float v00 = field[y0 * W + x0];
	float v10 = field[y0 * W + x1];
	float v01 = field[y1 * W + x0];
	float v11 = field[y1 * W + x1];

	float top    = v00 + sx * (v10 - v00);
	float bottom = v01 + sx * (v11 - v01);
	return top + sy * (bottom - top);
}

/*
 * Bilinear sample a 2D vector field (stored as two separate arrays).
 */
static void lic_dbg_bilinear_vec2(const float* field_x, const float* field_y,
                                   uint32_t W, uint32_t H,
                                   float fx, float fy,
                                   float* out_x, float* out_y) {
	fx = clamp_f32(fx, 0.0f, (float)(W - 1));
	fy = clamp_f32(fy, 0.0f, (float)(H - 1));

	uint32_t x0 = (uint32_t)fx;
	uint32_t y0 = (uint32_t)fy;
	uint32_t x1 = x0 < W - 1 ? x0 + 1 : x0;
	uint32_t y1 = y0 < H - 1 ? y0 + 1 : y0;

	float sx = fx - (float)x0;
	float sy = fy - (float)y0;

	float vx00 = field_x[y0 * W + x0];
	float vx10 = field_x[y0 * W + x1];
	float vx01 = field_x[y1 * W + x0];
	float vx11 = field_x[y1 * W + x1];
	float top_x    = vx00 + sx * (vx10 - vx00);
	float bottom_x = vx01 + sx * (vx11 - vx01);
	*out_x = top_x + sy * (bottom_x - top_x);

	float vy00 = field_y[y0 * W + x0];
	float vy10 = field_y[y0 * W + x1];
	float vy01 = field_y[y1 * W + x0];
	float vy11 = field_y[y1 * W + x1];
	float top_y    = vy00 + sx * (vy10 - vy00);
	float bottom_y = vy01 + sx * (vy11 - vy01);
	*out_y = top_y + sy * (bottom_y - top_y);
}

/* Simple xorshift32 PRNG for white noise generation */
static uint32_t xorshift32(uint32_t* state) {
	uint32_t x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

/*
 * Run LIC on a single height channel.
 * Uses the height gradient to derive the vector field, generates white noise,
 * then convolves the noise along streamlines to produce visible streaks.
 * Kernel length adapts per-pixel based on gradient magnitude.
 *
 * heights: pointer to single channel (W*H floats), replaced with LIC output
 */
static int lic_channel(float* heights, uint32_t W, uint32_t H,
                        LicVectorField vector_field,
                        float kernel_length, float step_size,
                        uint32_t noise_seed) {
	uint32_t N = W * H;

	auto noise    = std::unique_ptr<float[]>(new float[N]);
	auto flow_x   = std::unique_ptr<float[]>(new float[N]);
	auto flow_y   = std::unique_ptr<float[]>(new float[N]);
	auto grad_mag = std::unique_ptr<float[]>(new float[N]);
	auto lic_out  = std::unique_ptr<float[]>(new float[N]);

	/* Generate white noise */
	uint32_t rng = noise_seed;
	for (uint32_t i = 0; i < N; i++) {
		noise[i] = (float)xorshift32(&rng) / (float)0xFFFFFFFFu;
	}

	/* Compute gradient from heights, store magnitude, derive vector field */
	float max_mag = 0.0f;
	for (uint32_t y = 0; y < H; y++) {
		for (uint32_t x = 0; x < W; x++) {
			uint32_t idx = y * W + x;

			glm::vec2 g = height_gradient(heights, x, y, W, H);
			float mag = glm::length(g);
			grad_mag[idx] = mag;
			if (mag > max_mag) max_mag = mag;

			float vx, vy;
			switch (vector_field) {
			case LIC_FIELD_NORMAL:
				vx = -g.x;
				vy = -g.y;
				break;
			case LIC_FIELD_TANGENT:
				vx = g.x;
				vy = g.y;
				break;
			case LIC_FIELD_BITANGENT:
				vx = -g.y;
				vy = g.x;
				break;
			default:
				vx = -g.x;
				vy = -g.y;
				break;
			}

			if (mag > 1e-8f) {
				flow_x[idx] = vx / mag;
				flow_y[idx] = vy / mag;
			} else {
				flow_x[idx] = 0.0f;
				flow_y[idx] = 0.0f;
			}
		}
	}

	/* Normalize gradient magnitudes for adaptive kernel scaling.
	 * Use sqrt for non-linear mapping: spreads low-magnitude gradients
	 * so moderate slopes still produce visible streaks. */
	float inv_max = (max_mag > 1e-8f) ? (1.0f / max_mag) : 0.0f;

	/* LIC: convolve white noise along streamlines with per-pixel kernel length */
	for (uint32_t y = 0; y < H; y++) {
		for (uint32_t x = 0; x < W; x++) {
			uint32_t idx = y * W + x;

			/* Zero height with zero gradient = empty area -> black */
			if (heights[idx] == 0.0f && grad_mag[idx] < 1e-8f) {
				lic_out[idx] = 0.0f;
				continue;
			}

			/* sqrt scaling: mag_norm 0.01 -> 0.1, 0.1 -> 0.32, 0.5 -> 0.71 */
			float mag_norm = sqrtf(grad_mag[idx] * inv_max);
			float local_kernel = kernel_length * mag_norm;
			int local_steps = (int)(local_kernel / step_size + 0.5f);

			if (local_steps < 1) {
				lic_out[idx] = noise[idx];
				continue;
			}

			float accum = 0.0f;
			float weight_sum = 0.0f;

			/* Center sample */
			accum += noise[idx];
			weight_sum += 1.0f;

			/* Forward trace */
			float px = (float)x;
			float py = (float)y;
			for (int s = 1; s <= local_steps; s++) {
				float fx, fy;
				lic_dbg_bilinear_vec2(flow_x.get(), flow_y.get(), W, H, px, py, &fx, &fy);
				px += fx * step_size;
				py += fy * step_size;

				float t = (float)s * step_size;
				float w = 0.75f * (1.0f + cosf(3.14159265f * t / local_kernel));

				accum += lic_dbg_bilinear_scalar(noise.get(), W, H, px, py) * w;
				weight_sum += w;
			}

			/* Backward trace */
			px = (float)x;
			py = (float)y;
			for (int s = 1; s <= local_steps; s++) {
				float fx, fy;
				lic_dbg_bilinear_vec2(flow_x.get(), flow_y.get(), W, H, px, py, &fx, &fy);
				px -= fx * step_size;
				py -= fy * step_size;

				float t = (float)s * step_size;
				float w = 0.75f * (1.0f + cosf(3.14159265f * t / local_kernel));

				accum += lic_dbg_bilinear_scalar(noise.get(), W, H, px, py) * w;
				weight_sum += w;
			}

			lic_out[idx] = accum / weight_sum;
		}
	}

	/* Replace channel with LIC result */
	for (uint32_t i = 0; i < N; i++) {
		heights[i] = lic_out[i];
	}

	return 0;
}

int lic_debug_Execute(LicDebugCmd* cmd) {
	if (!cmd || !cmd->heights) {
		fprintf(stderr, "[lic_debug] Error: NULL input\n");
		return -1;
	}

	uint32_t W = cmd->W;
	uint32_t H = cmd->H;
	uint32_t N = W * H;

	float kernel_length = cmd->kernel_length > 0.0f ? cmd->kernel_length : 10.0f;
	float step_size     = cmd->step_size > 0.0f ? cmd->step_size : 0.5f;

	printf("[lic_debug] %ux%u, field=%d, kernel=%.1f, step=%.2f\n",
	       W, H, cmd->vector_field, kernel_length, step_size);

	for (int c = 0; c < 3; c++) {
		uint32_t seed = 0x12345678u ^ ((uint32_t)c * 2654435761u);
		if (lic_channel(&cmd->heights[c * N], W, H,
		                cmd->vector_field, kernel_length, step_size, seed) != 0) {
			return -1;
		}
	}

	printf("[lic_debug] Done.\n");
	return 0;
}
