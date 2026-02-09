#include "lic_stylize_cmd.h"
#include "../utility.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <memory>

/*
 * Bilinear sample a scalar field with clamp-to-edge boundary.
 */
static float bilinear_sample_scalar(const float* field, uint32_t W, uint32_t H,
                                     float fx, float fy) {
	/* Clamp to valid range */
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
 * Bilinear sample a 2D vector field (stored as two separate arrays)
 * with clamp-to-edge boundary.
 */
static void bilinear_sample_vec2(const float* field_x, const float* field_y,
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

	/* X component */
	float vx00 = field_x[y0 * W + x0];
	float vx10 = field_x[y0 * W + x1];
	float vx01 = field_x[y1 * W + x0];
	float vx11 = field_x[y1 * W + x1];
	float top_x    = vx00 + sx * (vx10 - vx00);
	float bottom_x = vx01 + sx * (vx11 - vx01);
	*out_x = top_x + sy * (bottom_x - top_x);

	/* Y component */
	float vy00 = field_y[y0 * W + x0];
	float vy10 = field_y[y0 * W + x1];
	float vy01 = field_y[y1 * W + x0];
	float vy11 = field_y[y1 * W + x1];
	float top_y    = vy00 + sx * (vy10 - vy00);
	float bottom_y = vy01 + sx * (vy11 - vy01);
	*out_y = top_y + sy * (bottom_y - top_y);
}

int lic_stylize_Execute(LicStylizeCmd* cmd) {
	if (!cmd) {
		fprintf(stderr, "[lic_stylize] Error: cmd is NULL\n");
		return -1;
	}
	if (!cmd->major_normals || !cmd->minor_normals) {
		fprintf(stderr, "[lic_stylize] Error: input normals are NULL\n");
		return -1;
	}

	uint32_t W = cmd->W;
	uint32_t H = cmd->H;
	uint32_t size = W * H;

	float kernel_length = cmd->kernel_length > 0.0f ? cmd->kernel_length : 10.0f;
	float step_size     = cmd->step_size > 0.0f ? cmd->step_size : 0.5f;
	int max_steps = (int)(kernel_length / step_size + 0.5f);

	printf("[lic_stylize] %ux%u, kernel=%.1f, step=%.2f, max_steps=%d\n",
	       W, H, kernel_length, step_size, max_steps);

	/* Allocate working buffers */
	auto scalar = std::unique_ptr<float[]>(new (std::nothrow) float[size]);
	auto flow_x = std::unique_ptr<float[]>(new (std::nothrow) float[size]);
	auto flow_y = std::unique_ptr<float[]>(new (std::nothrow) float[size]);
	auto lic_out = std::unique_ptr<float[]>(new (std::nothrow) float[size]);

	if (!scalar || !flow_x || !flow_y || !lic_out) {
		fprintf(stderr, "[lic_stylize] Error: allocation failed\n");
		return -1;
	}

	/* Extract scalar field and flow field */
	for (uint32_t i = 0; i < size; i++) {
		/* Scalar = minor tangent-space X (perpendicular to major flow) */
		scalar[i] = cmd->minor_normals[i].x;

		/* Flow = contour direction: 90° rotation of gradient (major.x, major.y) */
		float mx = cmd->major_normals[i].x;
		float my = cmd->major_normals[i].y;
		float len = sqrtf(mx * mx + my * my);
		if (len > 1e-8f) {
			flow_x[i] = -my / len;
			flow_y[i] =  mx / len;
		} else {
			flow_x[i] = 0.0f;
			flow_y[i] = 0.0f;
		}
	}

	/* LIC: for each pixel, trace streamline forward and backward */
	for (uint32_t y = 0; y < H; y++) {
		for (uint32_t x = 0; x < W; x++) {
			uint32_t idx = y * W + x;

			float accum = 0.0f;
			float weight_sum = 0.0f;

			/* Center sample (Hanning weight at t=0 is 1.0) */
			accum += scalar[idx];
			weight_sum += 1.0f;

			/* Forward trace */
			float px = (float)x;
			float py = (float)y;
			for (int s = 1; s <= max_steps; s++) {
				float fx, fy;
				bilinear_sample_vec2(flow_x.get(), flow_y.get(), W, H, px, py, &fx, &fy);
				px += fx * step_size;
				py += fy * step_size;

				/* Hanning window: 0.5 * (1 + cos(pi * t / kernel_length)) */
				float t = (float)s * step_size;
				float w = 0.5f * (1.0f + cosf(3.14159265f * t / kernel_length));

				accum += bilinear_sample_scalar(scalar.get(), W, H, px, py) * w;
				weight_sum += w;
			}

			/* Backward trace */
			px = (float)x;
			py = (float)y;
			for (int s = 1; s <= max_steps; s++) {
				float fx, fy;
				bilinear_sample_vec2(flow_x.get(), flow_y.get(), W, H, px, py, &fx, &fy);
				px -= fx * step_size;
				py -= fy * step_size;

				float t = (float)s * step_size;
				float w = 0.5f * (1.0f + cosf(3.14159265f * t / kernel_length));

				accum += bilinear_sample_scalar(scalar.get(), W, H, px, py) * w;
				weight_sum += w;
			}

			lic_out[idx] = (weight_sum > 1e-8f) ? (accum / weight_sum) : 0.0f;
		}
	}

	/* Allocate output */
	if (!cmd->result_normals) {
		cmd->result_normals = std::unique_ptr<vec3[]>(new vec3[size]);
		if (!cmd->result_normals) {
			fprintf(stderr, "[lic_stylize] Error: output allocation failed\n");
			return -1;
		}
	}

	/* Reconstruct tangent-space minor normals: (lic_value, 0, minor.z) */
	for (uint32_t i = 0; i < size; i++) {
		cmd->result_normals[i].x = lic_out[i];
		cmd->result_normals[i].y = 0.0f;
		cmd->result_normals[i].z = cmd->minor_normals[i].z;
	}

	printf("[lic_stylize] Done.\n");
	return 0;
}

