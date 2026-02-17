#include "divergence_field_cmd.h"
#include "heightmap_ops.h"
#include <math.h>
#include <stdio.h>
#include <memory>
#include <glm/glm.hpp>

/* =========================================================================
 * Compute divergence of F-scaled normal field
 *
 * For each pixel:
 *   gradient = (df/dx, df/dy)  via central differences
 *   normal = normalize(-gx, -gy, 1)
 *   scaled = normalize(nx, ny, nz * F)
 *   field = (-scaled.x, -scaled.y)   (projected gradient direction)
 *   divergence = d(field.x)/dx + d(field.y)/dy
 *
 * Positive divergence = valley (flow converges)
 * Negative divergence = ridge (flow diverges)
 * ========================================================================= */

static void compute_scaled_divergence(const float* height, uint32_t W, uint32_t H,
                                       float F, float* div_out)
{
	uint32_t N = W * H;

	/* Compute the projected gradient field: (-nx_scaled, -ny_scaled) */
	auto fx = std::unique_ptr<float[]>(new float[N]);
	auto fy = std::unique_ptr<float[]>(new float[N]);

	for (uint32_t y = 0; y < H; y++) {
		for (uint32_t x = 0; x < W; x++) {
			uint32_t idx = y * W + x;

			/* Central differences for gradient (clamp to border = 0) */
			glm::vec2 g = height_gradient(height, x, y, W, H);

			/* Normal = normalize(-gx, -gy, 1), then scale z by F */
			glm::vec3 n(-g.x, -g.y, F);
			float len = glm::length(n);
			if (len > 1e-12f) {
				/* Field = (-nx_scaled, -ny_scaled) = gradient direction normalized */
				fx[idx] = g.x / len;  /* -(-nx/len) = nx/len = gx/len */
				fy[idx] = g.y / len;
			} else {
				fx[idx] = 0.0f;
				fy[idx] = 0.0f;
			}
		}
	}

	/* Compute divergence via central differences (clamp to border = 0) */
	for (uint32_t y = 0; y < H; y++) {
		for (uint32_t x = 0; x < W; x++) {
			uint32_t idx = y * W + x;
			div_out[idx] = divergence_2d(fx.get(), fy.get(), x, y, W, H);
		}
	}
}

static void normalize_divergence(float* div, uint32_t N)
{
	float max_abs = 0.0f;
	for (uint32_t i = 0; i < N; i++) {
		float a = fabsf(div[i]);
		if (a > max_abs) max_abs = a;
	}

	if (max_abs > 1e-12f) {
		float inv = 1.0f / max_abs;
		for (uint32_t i = 0; i < N; i++) {
			div[i] *= inv;
		}
	}

	printf("[ridge_mesh] Divergence range normalized: max_abs was %.6f\n", max_abs);
}

int divergence_field_Execute(DivergenceFieldCmd* cmd)
{
	if (!cmd || !cmd->heightmap) {
		fprintf(stderr, "[divergence_field] Error: NULL input\n");
		return -1;
	}

	uint32_t W = cmd->W;
	uint32_t H = cmd->H;
	uint32_t N = W * H;

	float F = cmd->normal_scale > 0.0f ? cmd->normal_scale : 1.0f;

	cmd->divergence = std::unique_ptr<float[]>(new float[N]);
	compute_scaled_divergence(cmd->heightmap, W, H, F, cmd->divergence.get());
	normalize_divergence(cmd->divergence.get(), N);

	return 0;
}
