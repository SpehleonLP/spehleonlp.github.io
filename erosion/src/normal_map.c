#include "normal_map.h"
#include <stdlib.h>
#include <math.h>

/*
 * Normal map computation from height field
 *
 * For a height field h(x,y), the surface normal is:
 *   n = normalize((-dh/dx, -dh/dy, 1/scale))
 *
 * The scale parameter controls steepness - larger scale = flatter normals.
 * Unlike truncated 2D gradient approaches, this preserves the full 3D normal
 * including proper Z component for lighting calculations.
 */

static inline float get_height(const float* data, int idx, int channel, int stride, float zero_val) {
	float v = data[idx * stride + channel];
	return v;
}

static inline int is_valid(float v, float zero_val) {
	return v != zero_val;
}

int nm_Execute(NormalMapCmd* cmd) {
	if (!cmd || !cmd->height_data || cmd->width == 0 || cmd->height == 0)
		return -1;

	uint32_t w = cmd->width;
	uint32_t h = cmd->height;
	int ch = cmd->channel;
	int stride = cmd->stride > 0 ? cmd->stride : 1;
	float scale = cmd->scale > 0 ? cmd->scale : 1.0f;
	float zero_val = cmd->zero_value;
	const float* data = cmd->height_data;

	if (!cmd->normals) {
		cmd->normals = malloc(sizeof(vec3) * w * h);
		if (!cmd->normals)
			return -2;
	}

	for (uint32_t y = 0; y < h; y++) {
		for (uint32_t x = 0; x < w; x++) {
			uint32_t idx = y * w + x;
			float center = get_height(data, idx, ch, stride, zero_val);

			// Default normal pointing up
			vec3 normal = {0.0f, 0.0f, 1.0f};

			if (!is_valid(center, zero_val)) {
				cmd->normals[idx] = normal;
				continue;
			}

			// Sample neighbors
			float left  = (x > 0)     ? get_height(data, idx - 1, ch, stride, zero_val) : zero_val;
			float right = (x < w - 1) ? get_height(data, idx + 1, ch, stride, zero_val) : zero_val;
			float up    = (y > 0)     ? get_height(data, idx - w, ch, stride, zero_val) : zero_val;
			float down  = (y < h - 1) ? get_height(data, idx + w, ch, stride, zero_val) : zero_val;

			// Compute gradients using available neighbors
			float dhdx = 0.0f, dhdy = 0.0f;

			if (is_valid(left, zero_val) && is_valid(right, zero_val)) {
				dhdx = (right - left) * 0.5f;
			} else if (is_valid(right, zero_val)) {
				dhdx = right - center;
			} else if (is_valid(left, zero_val)) {
				dhdx = center - left;
			}

			if (is_valid(up, zero_val) && is_valid(down, zero_val)) {
				dhdy = (down - up) * 0.5f;
			} else if (is_valid(down, zero_val)) {
				dhdy = down - center;
			} else if (is_valid(up, zero_val)) {
				dhdy = center - up;
			}

			// Build normal: n = (-dh/dx, -dh/dy, 1/scale)
			normal.x = -dhdx;
			normal.y = -dhdy;
			normal.z = 1.0f / scale;

			// Normalize
			float mag = sqrtf(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
			if (mag > 1e-6f) {
				float inv_mag = 1.0f / mag;
				normal.x *= inv_mag;
				normal.y *= inv_mag;
				normal.z *= inv_mag;
			}

			cmd->normals[idx] = normal;
		}
	}

	return 0;
}

void nm_Free(NormalMapCmd* cmd) {
	if (cmd && cmd->normals) {
		free(cmd->normals);
		cmd->normals = NULL;
	}
}

/*
 * Gradient computation (2D only, for velocity fields)
 * Flow goes from high to low, so gradient is negated.
 */
int grad_Execute(GradientCmd* cmd) {
	if (!cmd || !cmd->height_data || cmd->width == 0 || cmd->height == 0)
		return -1;

	uint32_t w = cmd->width;
	uint32_t h = cmd->height;
	int ch = cmd->channel;
	int stride = cmd->stride > 0 ? cmd->stride : 1;
	float zero_val = cmd->zero_value;
	const float* data = cmd->height_data;

	if (!cmd->gradient) {
		cmd->gradient = malloc(sizeof(vec2) * w * h);
		if (!cmd->gradient)
			return -2;
	}

	for (uint32_t y = 0; y < h; y++) {
		for (uint32_t x = 0; x < w; x++) {
			uint32_t idx = y * w + x;
			float center = get_height(data, idx, ch, stride, zero_val);

			if (!is_valid(center, zero_val)) {
				cmd->gradient[idx].x = 0.0f;
				cmd->gradient[idx].y = 0.0f;
				continue;
			}

			float left  = (x > 0)     ? get_height(data, idx - 1, ch, stride, zero_val) : zero_val;
			float right = (x < w - 1) ? get_height(data, idx + 1, ch, stride, zero_val) : zero_val;
			float up    = (y > 0)     ? get_height(data, idx - w, ch, stride, zero_val) : zero_val;
			float down  = (y < h - 1) ? get_height(data, idx + w, ch, stride, zero_val) : zero_val;

			float dhdx = 0.0f, dhdy = 0.0f;

			if (is_valid(left, zero_val) && is_valid(right, zero_val)) {
				dhdx = (right - left) * 0.5f;
			} else if (is_valid(right, zero_val)) {
				dhdx = right - center;
			} else if (is_valid(left, zero_val)) {
				dhdx = center - left;
			}

			if (is_valid(up, zero_val) && is_valid(down, zero_val)) {
				dhdy = (down - up) * 0.5f;
			} else if (is_valid(down, zero_val)) {
				dhdy = down - center;
			} else if (is_valid(up, zero_val)) {
				dhdy = center - up;
			}

			// Flow from high to low: negate gradient
			cmd->gradient[idx].x = -dhdx;
			cmd->gradient[idx].y = -dhdy;
		}
	}

	return 0;
}

void grad_Free(GradientCmd* cmd) {
	if (cmd && cmd->gradient) {
		free(cmd->gradient);
		cmd->gradient = NULL;
	}
}

/*
 * Height from Normals - Poisson reconstruction
 *
 * Given normals n = (nx, ny, nz), recover gradients:
 *   dh/dx = -nx/nz
 *   dh/dy = -ny/nz
 *
 * Then solve Poisson equation:
 *   ∇²h = ∂(dh/dx)/∂x + ∂(dh/dy)/∂y
 *
 * Using Gauss-Seidel iteration.
 */

#define DEFAULT_POISSON_ITERATIONS 100

int height_from_normals_Execute(HeightFromNormalsCmd* cmd) {
	if (!cmd || cmd->width == 0 || cmd->height == 0)
		return -1;
	if (!cmd->normals && (!cmd->nx || !cmd->ny))
		return -2;

	uint32_t w = cmd->width;
	uint32_t h = cmd->height;
	uint32_t N = w * h;
	int iterations = cmd->iterations > 0 ? cmd->iterations : DEFAULT_POISSON_ITERATIONS;
	float scale = cmd->scale > 0 ? cmd->scale : 1.0f;

	// Allocate output
	if (!cmd->heightmap) {
		cmd->heightmap = malloc(sizeof(float) * N);
		if (!cmd->heightmap) return -3;
	}

	// Allocate gradient fields
	float* gx = malloc(sizeof(float) * N);
	float* gy = malloc(sizeof(float) * N);
	float* div = malloc(sizeof(float) * N);
	if (!gx || !gy || !div) {
		free(gx);
		free(gy);
		free(div);
		return -3;
	}

	// Extract gradients from normals: dh/dx = -nx/nz, dh/dy = -ny/nz
	if (cmd->normals) {
		for (uint32_t i = 0; i < N; i++) {
			float nz = cmd->normals[i].z;
			if (fabsf(nz) < 1e-6f) {
				gx[i] = 0.0f;
				gy[i] = 0.0f;
			} else {
				gx[i] = -cmd->normals[i].x / nz;
				gy[i] = -cmd->normals[i].y / nz;
			}
		}
	} else {
		// Use provided gradient channels directly
		for (uint32_t i = 0; i < N; i++) {
			gx[i] = cmd->nx[i];
			gy[i] = cmd->ny[i];
		}
	}

	// Compute divergence of gradient field: div = ∂gx/∂x + ∂gy/∂y
	for (uint32_t y = 0; y < h; y++) {
		for (uint32_t x = 0; x < w; x++) {
			uint32_t idx = y * w + x;

			if (cmd->mask && cmd->mask[idx] == 0) {
				div[idx] = 0.0f;
				continue;
			}

			float dgx_dx = 0.0f, dgy_dy = 0.0f;

			// ∂gx/∂x
			if (x > 0 && x < w - 1) {
				dgx_dx = (gx[idx + 1] - gx[idx - 1]) * 0.5f;
			} else if (x < w - 1) {
				dgx_dx = gx[idx + 1] - gx[idx];
			} else if (x > 0) {
				dgx_dx = gx[idx] - gx[idx - 1];
			}

			// ∂gy/∂y
			if (y > 0 && y < h - 1) {
				dgy_dy = (gy[idx + w] - gy[idx - w]) * 0.5f;
			} else if (y < h - 1) {
				dgy_dy = gy[idx + w] - gy[idx];
			} else if (y > 0) {
				dgy_dy = gy[idx] - gy[idx - w];
			}

			div[idx] = dgx_dx + dgy_dy;
		}
	}

	// Initialize height to zero
	for (uint32_t i = 0; i < N; i++)
		cmd->heightmap[i] = 0.0f;

	// Gauss-Seidel iteration: solve ∇²h = div
	for (int iter = 0; iter < iterations; iter++) {
		for (uint32_t y = 0; y < h; y++) {
			for (uint32_t x = 0; x < w; x++) {
				uint32_t idx = y * w + x;

				if (cmd->mask && cmd->mask[idx] == 0)
					continue;

				float sum = 0.0f;
				int count = 0;

				if (x > 0 && (!cmd->mask || cmd->mask[idx - 1])) {
					sum += cmd->heightmap[idx - 1];
					count++;
				}
				if (x < w - 1 && (!cmd->mask || cmd->mask[idx + 1])) {
					sum += cmd->heightmap[idx + 1];
					count++;
				}
				if (y > 0 && (!cmd->mask || cmd->mask[idx - w])) {
					sum += cmd->heightmap[idx - w];
					count++;
				}
				if (y < h - 1 && (!cmd->mask || cmd->mask[idx + w])) {
					sum += cmd->heightmap[idx + w];
					count++;
				}

				if (count > 0) {
					cmd->heightmap[idx] = (sum - div[idx]) / (float)count;
				}
			}
		}
	}

	// Apply scale
	if (scale != 1.0f) {
		for (uint32_t i = 0; i < N; i++)
			cmd->heightmap[i] *= scale;
	}

	free(gx);
	free(gy);
	free(div);

	return 0;
}

void height_from_normals_Free(HeightFromNormalsCmd* cmd) {
	if (cmd && cmd->heightmap) {
		free(cmd->heightmap);
		cmd->heightmap = NULL;
	}
}
