#include "hessian_flow_dbg.h"
#include "laminarize_cmd.h"
#include "lic_stylize_cmd.h"
#include "split_normals_cmd.h"
#include "constrained_poisson_cmd.h"
#include "debug_png.h"
#include "../utility.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <memory>

/*
 * Compute divergence of tangent and bitangent fields from a normal map.
 * Tangent = direction of steepest ascent = (-nx/nz, -ny/nz) ≈ (-nx, -ny) for visualization
 * Bitangent = perpendicular to tangent = (ny, -nx)
 * Divergence = ∂Fx/∂x + ∂Fy/∂y
 */
static void compute_divergence_from_normals(const vec3* normals, uint32_t W, uint32_t H,
                                             std::unique_ptr<float[]>& div_tangent_out,
                                             std::unique_ptr<float[]>& div_bitangent_out) {
    uint32_t size = W * H;

    auto div_tangent = std::unique_ptr<float[]>(new (std::nothrow) float[size]);
    auto div_bitangent = std::unique_ptr<float[]>(new (std::nothrow) float[size]);

    if (!div_tangent || !div_bitangent) {
        div_tangent_out.reset();
        div_bitangent_out.reset();
        return;
    }

    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            uint32_t idx = y * W + x;

            // Get neighboring normals with boundary clamping
            uint32_t xm = (x > 0) ? x - 1 : 0;
            uint32_t xp = (x < W - 1) ? x + 1 : W - 1;
            uint32_t ym = (y > 0) ? y - 1 : 0;
            uint32_t yp = (y < H - 1) ? y + 1 : H - 1;

            // Tangent field: T = (-nx, -ny) - direction of steepest ascent
            float Tx_xm = -normals[y * W + xm].x;
            float Tx_xp = -normals[y * W + xp].x;
            float Ty_ym = -normals[ym * W + x].y;
            float Ty_yp = -normals[yp * W + x].y;

            float dTx_dx = (Tx_xp - Tx_xm) * 0.5f;
            float dTy_dy = (Ty_yp - Ty_ym) * 0.5f;
            div_tangent[idx] = dTx_dx + dTy_dy;

            // Bitangent field: B = (ny, -nx) - perpendicular to tangent (90° CCW)
            float Bx_xm = normals[y * W + xm].y;
            float Bx_xp = normals[y * W + xp].y;
            float By_ym = -normals[ym * W + x].x;
            float By_yp = -normals[yp * W + x].x;

            float dBx_dx = (Bx_xp - Bx_xm) * 0.5f;
            float dBy_dy = (By_yp - By_ym) * 0.5f;
            div_bitangent[idx] = dBx_dx + dBy_dy;
        }
    }

    div_tangent_out = std::move(div_tangent);
    div_bitangent_out = std::move(div_bitangent);
}

/*
 * Compute normal map from heightmap using central differences
 */
static std::unique_ptr<vec3[]> compute_normals_from_height(const float* height, uint32_t W, uint32_t H, float scale) {
    auto normals = std::unique_ptr<vec3[]>(new (std::nothrow) vec3[W * H]);
    if (!normals) return nullptr;

    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            uint32_t idx = y * W + x;

            // Central differences with boundary clamping
            float hL = (x > 0) ? height[idx - 1] : height[idx];
            float hR = (x < W - 1) ? height[idx + 1] : height[idx];
            float hD = (y > 0) ? height[idx - W] : height[idx];
            float hU = (y < H - 1) ? height[idx + W] : height[idx];

            float gx = (hR - hL) * 0.5f;
            float gy = (hU - hD) * 0.5f;

            // Normal = normalize(-gx, -gy, scale)
            float nx = -gx;
            float ny = -gy;
            float nz = scale;
            float len = sqrtf(nx * nx + ny * ny + nz * nz);

            if (len > 1e-8f) {
                normals[idx].x = nx / len;
                normals[idx].y = ny / len;
                normals[idx].z = nz / len;
            } else {
                normals[idx].x = 0.0f;
                normals[idx].y = 0.0f;
                normals[idx].z = 1.0f;
            }
        }
    }

    return normals;
}

int hessian_flow_debug_Execute(HessianFlowDebugCmd* cmd)
{
	int r = hessian_flow_init(cmd);
	if(r < 0) return r;
	return hessian_flow_debug_Output(cmd);
}

int hessian_flow_init(HessianFlowDebugCmd* cmd) {
    if (!cmd) {
        printf( "[hessian_flow_init] Error: cmd is NULL\n");
        return -1;
    }

    if (!cmd->heightmap) {
        printf( "[hessian_flow_init] Error: heightmap is NULL\n");
        return -1;
    }

    if (!cmd->output_path) {
        printf( "[hessian_flow_init] Error: output_path is NULL\n");
        return -1;
    }

    uint32_t W = cmd->W;
    uint32_t H = cmd->H;
    uint32_t size = W * H;

    printf( "[hessian_flow_init] Processing %ux%u heightmap with kernel_size=%d\n",
            W, H, cmd->kernel_size);

    // Step 1: Compute original normals from heightmap
    printf( "[hessian_flow_init] Step 1: Computing original normals...\n");
    cmd->original_normals = compute_normals_from_height(cmd->heightmap, W, H, 1.0f);
    if (!cmd->original_normals) {
        printf( "[hessian_flow_init] Error: failed to compute original normals\n");
        return -1;
    }

    // Step 2: Use split_normals to get major/minor normal maps
    SplitNormalsCmd split_cmd{};
    split_cmd.heightmap = cmd->heightmap;
    split_cmd.W = W;
    split_cmd.H = H;
    split_cmd.kernel_size = cmd->kernel_size ? cmd->kernel_size : 3;
    split_cmd.border = cmd->border;
    split_cmd.undefined_value = cmd->undefined_value;

    printf( "[hessian_flow_init] Step 2: Computing split normal maps...\n");
    if (split_normals_Execute(&split_cmd) != 0) {
        fprintf(stderr, "[hessian_flow_init] Error: split_normals_Execute failed\n");
        return -1;
    }

    // Transfer ownership from split_cmd to cmd
    cmd->major_normals = std::move(split_cmd.major_normals);
    cmd->minor_normals = std::move(split_cmd.minor_normals);
    cmd->anisotropy_ratio = std::move(split_cmd.major_ratio);

	//laminerize

	#if 1
	{
		LaminarizeCmd lam_cmd{};
		lam_cmd.normals = cmd->major_normals.get();
		lam_cmd.W = W;
		lam_cmd.H = H;
		lam_cmd.scale = 0.1f;
		lam_cmd.strength = 1.0f;
		lam_cmd.blur_sigma = 2.0f;
		lam_cmd.max_iterations = 1000;
		lam_cmd.tolerance = 1e-5f;

		if (laminarize_Execute(&lam_cmd) != 0) {
			fprintf(stderr, "[hessian_flow_init] Error: laminarize_Execute failed\n");
			return -1;
		}

		cmd->major_normals = std::move(lam_cmd.result_normals);
	}
	#endif

    // Step 2b: Compute major_height via constrained Poisson from major_normals
    printf( "[hessian_flow_init] Step 2b: Computing major height...\n");
    {
        ConstrainedPoissonCmd major_poisson{};
        major_poisson.original_height = cmd->heightmap;
        major_poisson.target_normals = cmd->major_normals.get();
        major_poisson.W = W;
        major_poisson.H = H;
        major_poisson.max_iterations = cmd->poisson_iterations > 0 ? cmd->poisson_iterations : 1000;
        major_poisson.tolerance = cmd->poisson_tolerance > 0 ? cmd->poisson_tolerance : 1e-5f;
        major_poisson.zero_threshold = 1e-6f;

        if (constrained_poisson_Execute(&major_poisson) != 0) {
            printf( "[hessian_flow_init] Error: major height Poisson failed\n");
            return -1;
        }
        cmd->major_height = std::move(major_poisson.result_height);
    }


	// orthoganalize major/minor
	for (uint32_t i = 0; i < size; i++)
	{
		vec3 m = vec3_sub(cmd->original_normals[i], cmd->major_normals[i]);

		// this broke round tripping but the output is visually uninterpretable without it.. how to fix?
		m.z = sqrtf(1.f - clamp_f32(m.x*m.x + m.y*m.y, 0, 1));

		cmd->minor_normals[i] = m;
	//	cmd->minor_normals[i] = to_tangent(cmd->major_normals[i], cmd->minor_normals[i]);
	}

	#if 1
	{
		LaminarizeCmd lam_cmd{};
		lam_cmd.normals = cmd->minor_normals.get();
		lam_cmd.W = W;
		lam_cmd.H = H;
		lam_cmd.scale = 0.5f;
		lam_cmd.strength = 1.0f;
		lam_cmd.blur_sigma = 2.0f;
		lam_cmd.max_iterations = 1000;
		lam_cmd.tolerance = 1e-5f;

		if (laminarize_Execute(&lam_cmd) != 0) {
			fprintf(stderr, "[hessian_flow_init] Error: laminarize_Execute failed\n");
			return -1;
		}

		cmd->minor_normals = std::move(lam_cmd.result_normals);
	}
	#endif

    // Step 2c: Compute minor_height via constrained Poisson from minor_normals
    printf( "[hessian_flow_init] Step 2c: Computing minor height...\n");
    {
        ConstrainedPoissonCmd minor_poisson{};
        minor_poisson.original_height = cmd->heightmap;
        minor_poisson.target_normals = cmd->minor_normals.get();
        minor_poisson.W = W;
        minor_poisson.H = H;
        minor_poisson.max_iterations = cmd->poisson_iterations > 0 ? cmd->poisson_iterations : 1000;
        minor_poisson.tolerance = cmd->poisson_tolerance > 0 ? cmd->poisson_tolerance : 1e-5f;
        minor_poisson.zero_threshold = 1e-6f;

        if (constrained_poisson_Execute(&minor_poisson) != 0) {
            printf( "[hessian_flow_init] Error: minor height Poisson failed\n");
            return -1;
        }
        //cmd->minor_height = std::move(minor_poisson.result_height);
    }

	// LIC stylize: advect minor detail along major flow
	#if 0
	{
		LicStylizeCmd lic_cmd{};
		lic_cmd.major_normals = cmd->major_normals.get();
		lic_cmd.minor_normals = cmd->minor_normals.get();
		lic_cmd.W = W;
		lic_cmd.H = H;
		lic_cmd.kernel_length = 10.0f;
		lic_cmd.step_size = 0.5f;

		if (lic_stylize_Execute(&lic_cmd) != 0) {
			fprintf(stderr, "[hessian_flow_init] Error: lic_stylize_Execute failed\n");
			return -1;
		}


		for (uint32_t i = 0; i < size; i++)
		{
			vec3 m = cmd->minor_normals[i];
			m = to_tangent(cmd->major_normals[i], m);
			m.x = lic_cmd.result_normals[i].x;
			m.z = sqrtf(1.f - clamp_f32(m.x*m.x + m.y*m.y, 0, 1));

			lic_cmd.result_normals[i] = m;
		}

		cmd->minor_normals = std::move(lic_cmd.result_normals);
	}
	#endif

    // Step 3: Remix normals with custom weights
    float major_w = cmd->major_weight;
    float minor_w = cmd->minor_weight;
    if (major_w == 0.0f && minor_w == 0.0f) {
        major_w = 1.0f;
        minor_w = 1.0f;
    }

    printf( "[hessian_flow_init] Step 3: Remixing normals (major=%.2f, minor=%.2f)...\n",
            major_w, minor_w);

    cmd->remixed_normals = std::unique_ptr<vec3[]>(new (std::nothrow) vec3[size]);
    if (!cmd->remixed_normals) {
        printf( "[hessian_flow_init] Error: failed to allocate remixed_normals\n");
        return -1;
    }

    for (uint32_t i = 0; i < size; i++) {

		vec3 minor = cmd->minor_normals[i];
		minor = from_tangent(cmd->major_normals[i], minor);

        // Blend all three components to preserve proportionality
        float nx = major_w * cmd->major_normals[i].x + minor_w * minor.x;
        float ny = major_w * cmd->major_normals[i].y + minor_w * minor.y;
        float nz = major_w * cmd->major_normals[i].z;

        // Renormalize
        float len = sqrtf(nx * nx + ny * ny + nz * nz);
        if (len > 1e-8f) {
            cmd->remixed_normals[i].x = nx / len;
            cmd->remixed_normals[i].y = ny / len;
            cmd->remixed_normals[i].z = nz / len;
        } else {
            cmd->remixed_normals[i].x = 0.0f;
            cmd->remixed_normals[i].y = 0.0f;
            cmd->remixed_normals[i].z = 1.0f;
        }
    }

    // Step 4: Run constrained Poisson solver
    int max_iter = cmd->poisson_iterations > 0 ? cmd->poisson_iterations : 1000;
    float tol = cmd->poisson_tolerance > 0 ? cmd->poisson_tolerance : 1e-5f;

    printf( "[hessian_flow_init] Step 4: Constrained Poisson solve (max_iter=%d)...\n", max_iter);

    {
        ConstrainedPoissonCmd poisson_cmd{};
        poisson_cmd.original_height = cmd->heightmap;
        poisson_cmd.target_normals = cmd->remixed_normals.get();
        poisson_cmd.W = W;
        poisson_cmd.H = H;
        poisson_cmd.max_iterations = max_iter;
        poisson_cmd.tolerance = tol;
        poisson_cmd.zero_threshold = 1e-6f;

        if (constrained_poisson_Execute(&poisson_cmd) != 0) {
            printf( "[hessian_flow_init] Error: constrained_poisson_Execute failed\n");
            return -1;
        }
        cmd->reconstructed_height = std::move(poisson_cmd.result_height);

        printf( "[hessian_flow_init] Poisson converged in %d iterations, residual=%.2e\n",
                poisson_cmd.iterations_used, poisson_cmd.final_residual);
    }

    // Step 5: Compute divergence fields for all normal maps
    printf( "[hessian_flow_init] Step 5: Computing divergence fields...\n");

    compute_divergence_from_normals(cmd->original_normals.get(), W, H,
                                    cmd->original_div_tangent, cmd->original_div_bitangent);
    compute_divergence_from_normals(cmd->major_normals.get(), W, H,
                                    cmd->major_div_tangent, cmd->major_div_bitangent);
    compute_divergence_from_normals(cmd->minor_normals.get(), W, H,
                                    cmd->minor_div_tangent, cmd->minor_div_bitangent);
    compute_divergence_from_normals(cmd->remixed_normals.get(), W, H,
                                    cmd->remixed_div_tangent, cmd->remixed_div_bitangent);

    if (!cmd->original_div_tangent || !cmd->major_div_tangent ||
        !cmd->minor_div_tangent || !cmd->remixed_div_tangent) {
        printf( "[hessian_flow_init] Error: failed to compute divergence fields\n");
        return -1;
    }

    // Step 6: Compute dot products of each normal map with original
    printf( "[hessian_flow_init] Step 6: Computing normal dot products...\n");
    cmd->dot_major_original = std::unique_ptr<float[]>(new (std::nothrow) float[size]);
    cmd->dot_minor_original = std::unique_ptr<float[]>(new (std::nothrow) float[size]);
    cmd->dot_remixed_original = std::unique_ptr<float[]>(new (std::nothrow) float[size]);

    if (!cmd->dot_major_original || !cmd->dot_minor_original || !cmd->dot_remixed_original) {
        printf( "[hessian_flow_init] Error: failed to allocate dot product fields\n");
        return -1;
    }

    for (uint32_t i = 0; i < size; i++) {
        vec3 o = cmd->original_normals[i];
        cmd->dot_major_original[i] =
            o.x * cmd->major_normals[i].x +
            o.y * cmd->major_normals[i].y;
        cmd->dot_minor_original[i] = cmd->minor_normals[i].y;
        cmd->dot_remixed_original[i] =
            o.x * cmd->remixed_normals[i].x +
            o.y * cmd->remixed_normals[i].y +
            o.z * cmd->remixed_normals[i].z;
    }

    return 0;
}

/*
 * Generate debug PNG grid (5x4):
 *   Row 1: [original_h]       [major_h]       [minor_h]       [remixed_h]
 *   Row 2: [original_n]       [major_n]       [minor_n]       [remixed_n]
 *   Row 3: [original_div_t]   [major_div_t]   [minor_div_t]   [remixed_div_t]
 *   Row 4: [original_div_b]   [major_div_b]   [minor_div_b]   [remixed_div_b]
 *   Row 5: [anisotropy]       [dot(maj,orig)] [dot(min,orig)] [dot(mix,orig)]
 */
int hessian_flow_debug_Output(HessianFlowDebugCmd* cmd) {
    if (!cmd) {
        printf( "[hessian_flow_debug_Output] Error: cmd is NULL\n");
        return -1;
    }

    if (!cmd->heightmap) {
        printf( "[hessian_flow_debug_Output] Error: heightmap is NULL\n");
        return -1;
    }

    if (!cmd->output_path) {
        printf( "[hessian_flow_debug_Output] Error: output_path is NULL\n");
        return -1;
    }

    uint32_t W = cmd->W;
    uint32_t H = cmd->H;

    // display scale for normal visualization (lower = steeper/more contrast)
    float display_scale = 0.1f;

    printf( "[hessian_flow_debug_Output] Generating 5x4 PNG grid...\n");

    PngGridTile tiles[20] = {
        // Row 1: Heights
        {PNG_TILE_GRAYSCALE, cmd->heightmap, 0, 0, 1},                    // original height
        {PNG_TILE_GRAYSCALE, cmd->major_height.get(), 0, 0, 1},           // major height
        {PNG_TILE_GRAYSCALE, cmd->minor_height.get(), 0, 0, 1},           // minor height
        {PNG_TILE_GRAYSCALE, cmd->reconstructed_height.get(), 0, 0, 0},   // remixed height
        // Row 2: Normals
        {PNG_TILE_VEC3, cmd->original_normals.get(), 0, 0, display_scale},    // original normals
        {PNG_TILE_VEC3, cmd->major_normals.get(), 0, 0, display_scale},       // major normals
        {PNG_TILE_VEC3, cmd->minor_normals.get(), 0, 0, display_scale},       // minor normals
        {PNG_TILE_VEC3, cmd->remixed_normals.get(), 0, 0, display_scale},     // remixed normals
        // Row 3: Divergence of tangent (steepest ascent direction)
        {PNG_TILE_GRAYSCALE, cmd->original_div_tangent.get(), 0, 0, 0},
        {PNG_TILE_GRAYSCALE, cmd->major_div_tangent.get(), 0, 0, 0},
        {PNG_TILE_GRAYSCALE, cmd->minor_div_tangent.get(), 0, 0, 0},
        {PNG_TILE_GRAYSCALE, cmd->remixed_div_tangent.get(), 0, 0, 0},
        // Row 4: Divergence of bitangent (perpendicular to tangent)
        {PNG_TILE_GRAYSCALE, cmd->original_div_bitangent.get(), 0, 0, 0},
        {PNG_TILE_GRAYSCALE, cmd->major_div_bitangent.get(), 0, 0, 0},
        {PNG_TILE_GRAYSCALE, cmd->minor_div_bitangent.get(), 0, 0, 0},
        {PNG_TILE_GRAYSCALE, cmd->remixed_div_bitangent.get(), 0, 0, 0},
        // Row 5: Anisotropy + dot products with original normals
        {PNG_TILE_GRAYSCALE, cmd->anisotropy_ratio.get(), 0, 0, 0},
        {PNG_TILE_GRAYSCALE, cmd->dot_major_original.get(), 0, 0, 0},
        {PNG_TILE_GRAYSCALE, cmd->dot_minor_original.get(), 0, 0, 0},
        {PNG_TILE_GRAYSCALE, cmd->dot_remixed_original.get(), 0, 0, 0}
    };

    PngGridCmd grid_cmd = {
        .path = cmd->output_path,
        .tile_width = W,
        .tile_height = H,
        .cols = 4,
        .rows = 5,
        .tiles = tiles
    };

    int result = png_ExportGrid(&grid_cmd);

    if (result != 0) {
        printf( "[hessian_flow_debug_Output] Error: png_ExportGrid failed\n");
    } else {
        printf( "[hessian_flow_debug_Output] Success! Output written to %s\n",
                cmd->output_path);
    }

    return result;
}
