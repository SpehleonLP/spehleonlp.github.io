#include "hessian_flow_dbg.h"
#include "split_normals_cmd.h"
#include "constrained_poisson_cmd.h"
#include "debug_png.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

/*
 * Compute divergence of tangent and bitangent fields from a normal map.
 * Tangent = direction of steepest ascent = (-nx/nz, -ny/nz) ≈ (-nx, -ny) for visualization
 * Bitangent = perpendicular to tangent = (ny, -nx)
 * Divergence = ∂Fx/∂x + ∂Fy/∂y
 */
static void compute_divergence_from_normals(const vec3* normals, uint32_t W, uint32_t H,
                                             float** div_tangent_out, float** div_bitangent_out) {
    uint32_t size = W * H;

    float* div_tangent = (float*)malloc(size * sizeof(float));
    float* div_bitangent = (float*)malloc(size * sizeof(float));

    if (!div_tangent || !div_bitangent) {
        free(div_tangent);
        free(div_bitangent);
        *div_tangent_out = NULL;
        *div_bitangent_out = NULL;
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
            // We use -nx, -ny directly (proportional to gradient)
            float Tx_xm = -normals[y * W + xm].x;
            float Tx_xp = -normals[y * W + xp].x;
            float Ty_ym = -normals[ym * W + x].y;
            float Ty_yp = -normals[yp * W + x].y;

            // div(T) = ∂Tx/∂x + ∂Ty/∂y
            float dTx_dx = (Tx_xp - Tx_xm) * 0.5f;
            float dTy_dy = (Ty_yp - Ty_ym) * 0.5f;
            div_tangent[idx] = dTx_dx + dTy_dy;

            // Bitangent field: B = (ny, -nx) - perpendicular to tangent (90° CCW)
            float Bx_xm = normals[y * W + xm].y;
            float Bx_xp = normals[y * W + xp].y;
            float By_ym = -normals[ym * W + x].x;
            float By_yp = -normals[yp * W + x].x;

            // div(B) = ∂Bx/∂x + ∂By/∂y
            float dBx_dx = (Bx_xp - Bx_xm) * 0.5f;
            float dBy_dy = (By_yp - By_ym) * 0.5f;
            div_bitangent[idx] = dBx_dx + dBy_dy;
        }
    }

    *div_tangent_out = div_tangent;
    *div_bitangent_out = div_bitangent;
}

/*
 * Compute normal map from heightmap using central differences
 */
static vec3* compute_normals_from_height(const float* height, uint32_t W, uint32_t H, float scale) {
    vec3* normals = (vec3*)malloc(W * H * sizeof(vec3));
    if (!normals) return NULL;

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

    float normal_scale = cmd->normal_scale;
    if (normal_scale <= 0.0f) normal_scale = 1.0f;

    printf( "[hessian_flow_init] Processing %ux%u heightmap with kernel_size=%d\n",
            W, H, cmd->kernel_size);

    // Step 1: Compute original normals from heightmap
    printf( "[hessian_flow_init] Step 1: Computing original normals...\n");
    cmd->original_normals = compute_normals_from_height(cmd->heightmap, W, H, normal_scale);
    if (!cmd->original_normals) {
        printf( "[hessian_flow_init] Error: failed to compute original normals\n");
        return -1;
    }

    // Step 2: Use split_normals to get major/minor normal maps
    SplitNormalsCmd split_cmd = {
        .heightmap = cmd->heightmap,
        .W = W,
        .H = H,
        .kernel_size = cmd->kernel_size ? cmd->kernel_size : 3,
        .border = cmd->border,
        .undefined_value = cmd->undefined_value,
        .normal_scale = normal_scale,
        .major_normals = NULL,
        .minor_normals = NULL,
        .major_ratio = NULL
    };

    printf( "[hessian_flow_init] Step 2: Computing split normal maps...\n");
    if (split_normals_Execute(&split_cmd) != 0) {
        printf( "[hessian_flow_init] Error: split_normals_Execute failed\n");
        hessian_flow_debug_Free(cmd);
        return -1;
    }

    // Transfer ownership from split_cmd to cmd
    cmd->major_normals = split_cmd.major_normals;
    cmd->minor_normals = split_cmd.minor_normals;
    cmd->anisotropy_ratio = split_cmd.major_ratio;  // Keep for visualization

    // Step 2b: Compute major_height via constrained Poisson from major_normals
    printf( "[hessian_flow_init] Step 2b: Computing major height...\n");
    {
        ConstrainedPoissonCmd major_poisson = {
            .original_height = cmd->heightmap,
            .target_normals = cmd->major_normals,
            .W = W,
            .H = H,
            .max_iterations = cmd->poisson_iterations > 0 ? cmd->poisson_iterations : 1000,
            .tolerance = cmd->poisson_tolerance > 0 ? cmd->poisson_tolerance : 1e-5f,
            .zero_threshold = 1e-6f,
            .result_height = NULL
        };
        if (constrained_poisson_Execute(&major_poisson) != 0) {
            printf( "[hessian_flow_init] Error: major height Poisson failed\n");
            hessian_flow_debug_Free(cmd);
            return -1;
        }
        cmd->major_height = major_poisson.result_height;
    }

    // Step 2c: Compute minor_height via constrained Poisson from minor_normals
    printf( "[hessian_flow_init] Step 2c: Computing minor height...\n");
    {
        ConstrainedPoissonCmd minor_poisson = {
            .original_height = cmd->heightmap,
            .target_normals = cmd->minor_normals,
            .W = W,
            .H = H,
            .max_iterations = cmd->poisson_iterations > 0 ? cmd->poisson_iterations : 1000,
            .tolerance = cmd->poisson_tolerance > 0 ? cmd->poisson_tolerance : 1e-5f,
            .zero_threshold = 1e-6f,
            .result_height = NULL
        };
        if (constrained_poisson_Execute(&minor_poisson) != 0) {
            printf( "[hessian_flow_init] Error: minor height Poisson failed\n");
            hessian_flow_debug_Free(cmd);
            return -1;
        }
        cmd->minor_height = minor_poisson.result_height;
    }

    // Step 3: Remix normals with custom weights
    float major_w = cmd->major_weight;
    float minor_w = cmd->minor_weight;
    if (major_w == 0.0f && minor_w == 0.0f) {
        major_w = 1.0f;
        minor_w = 1.0f;
    }

    printf( "[hessian_flow_init] Step 3: Remixing normals (major=%.2f, minor=%.2f)...\n",
            major_w, minor_w);

    cmd->remixed_normals = (vec3*)malloc(size * sizeof(vec3));
    if (!cmd->remixed_normals) {
        printf( "[hessian_flow_init] Error: failed to allocate remixed_normals\n");
        hessian_flow_debug_Free(cmd);
        return -1;
    }

    for (uint32_t i = 0; i < size; i++) {
        // Blend the XY components (gradients), keep Z normalized
        float nx = major_w * cmd->major_normals[i].x + minor_w * cmd->minor_normals[i].x;
        float ny = major_w * cmd->major_normals[i].y + minor_w * cmd->minor_normals[i].y;
        float nz = normal_scale;  // Use same scale as individual normals

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

    ConstrainedPoissonCmd poisson_cmd = {
        .original_height = cmd->heightmap,
        .target_normals = cmd->remixed_normals,
        .W = W,
        .H = H,
        .max_iterations = max_iter,
        .tolerance = tol,
        .zero_threshold = 1e-6f,
        .result_height = NULL
    };

    if (constrained_poisson_Execute(&poisson_cmd) != 0) {
        printf( "[hessian_flow_init] Error: constrained_poisson_Execute failed\n");
        hessian_flow_debug_Free(cmd);
        return -1;
    }
    cmd->reconstructed_height = poisson_cmd.result_height;

    printf( "[hessian_flow_init] Poisson converged in %d iterations, residual=%.2e\n",
            poisson_cmd.iterations_used, poisson_cmd.final_residual);

    // Step 5: Compute divergence fields for all normal maps
    printf( "[hessian_flow_init] Step 5: Computing divergence fields...\n");

    compute_divergence_from_normals(cmd->original_normals, W, H,
                                    &cmd->original_div_tangent, &cmd->original_div_bitangent);
    compute_divergence_from_normals(cmd->major_normals, W, H,
                                    &cmd->major_div_tangent, &cmd->major_div_bitangent);
    compute_divergence_from_normals(cmd->minor_normals, W, H,
                                    &cmd->minor_div_tangent, &cmd->minor_div_bitangent);
    compute_divergence_from_normals(cmd->remixed_normals, W, H,
                                    &cmd->remixed_div_tangent, &cmd->remixed_div_bitangent);

    if (!cmd->original_div_tangent || !cmd->major_div_tangent ||
        !cmd->minor_div_tangent || !cmd->remixed_div_tangent) {
        printf( "[hessian_flow_init] Error: failed to compute divergence fields\n");
        hessian_flow_debug_Free(cmd);
        return -1;
    }

    return 0;
}

/*
 * Generate debug PNG grid (5x4):
 *   Row 1: [original_h]       [major_h]       [minor_h]       [remixed_h]
 *   Row 2: [original_n]       [major_n]       [minor_n]       [remixed_n]
 *   Row 3: [original_div_t]   [major_div_t]   [minor_div_t]   [remixed_div_t]
 *   Row 4: [original_div_b]   [major_div_b]   [minor_div_b]   [remixed_div_b]
 *   Row 5: [anisotropy]       [anisotropy]    [anisotropy]    [anisotropy]
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

    printf( "[hessian_flow_debug_Output] Generating 5x4 PNG grid...\n");

    PngGridTile tiles[20] = {
        // Row 1: Heights
        {PNG_TILE_GRAYSCALE, cmd->heightmap, 0, 0},            // original height
        {PNG_TILE_GRAYSCALE, cmd->major_height, 0, 0},         // major height
        {PNG_TILE_GRAYSCALE, cmd->minor_height, 0, 0},         // minor height
        {PNG_TILE_GRAYSCALE, cmd->reconstructed_height, 0, 0}, // remixed height
        // Row 2: Normals
        {PNG_TILE_VEC3, cmd->original_normals, 0, 0},          // original normals
        {PNG_TILE_VEC3, cmd->major_normals, 0, 0},             // major normals
        {PNG_TILE_VEC3, cmd->minor_normals, 0, 0},             // minor normals
        {PNG_TILE_VEC3, cmd->remixed_normals, 0, 0},           // remixed normals
        // Row 3: Divergence of tangent (steepest ascent direction)
        {PNG_TILE_GRAYSCALE, cmd->original_div_tangent, 0, 0},
        {PNG_TILE_GRAYSCALE, cmd->major_div_tangent, 0, 0},
        {PNG_TILE_GRAYSCALE, cmd->minor_div_tangent, 0, 0},
        {PNG_TILE_GRAYSCALE, cmd->remixed_div_tangent, 0, 0},
        // Row 4: Divergence of bitangent (perpendicular to tangent)
        {PNG_TILE_GRAYSCALE, cmd->original_div_bitangent, 0, 0},
        {PNG_TILE_GRAYSCALE, cmd->major_div_bitangent, 0, 0},
        {PNG_TILE_GRAYSCALE, cmd->minor_div_bitangent, 0, 0},
        {PNG_TILE_GRAYSCALE, cmd->remixed_div_bitangent, 0, 0},
        // Row 5: Anisotropy ratio (0.5=isotropic, 1.0=fully anisotropic)
        {PNG_TILE_GRAYSCALE, cmd->anisotropy_ratio, 0, 0},
        {PNG_TILE_GRAYSCALE, cmd->anisotropy_ratio, 0, 0},
        {PNG_TILE_GRAYSCALE, cmd->anisotropy_ratio, 0, 0},
        {PNG_TILE_GRAYSCALE, cmd->anisotropy_ratio, 0, 0}
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

void hessian_flow_debug_Free(HessianFlowDebugCmd* cmd) {
    if (cmd) {
        if (cmd->original_normals) {
            free(cmd->original_normals);
            cmd->original_normals = NULL;
        }
        if (cmd->major_normals) {
            free(cmd->major_normals);
            cmd->major_normals = NULL;
        }
        if (cmd->minor_normals) {
            free(cmd->minor_normals);
            cmd->minor_normals = NULL;
        }
        if (cmd->remixed_normals) {
            free(cmd->remixed_normals);
            cmd->remixed_normals = NULL;
        }
        if (cmd->major_height) {
            free(cmd->major_height);
            cmd->major_height = NULL;
        }
        if (cmd->minor_height) {
            free(cmd->minor_height);
            cmd->minor_height = NULL;
        }
        if (cmd->reconstructed_height) {
            free(cmd->reconstructed_height);
            cmd->reconstructed_height = NULL;
        }
        // Free divergence fields
        if (cmd->original_div_tangent) {
            free(cmd->original_div_tangent);
            cmd->original_div_tangent = NULL;
        }
        if (cmd->major_div_tangent) {
            free(cmd->major_div_tangent);
            cmd->major_div_tangent = NULL;
        }
        if (cmd->minor_div_tangent) {
            free(cmd->minor_div_tangent);
            cmd->minor_div_tangent = NULL;
        }
        if (cmd->remixed_div_tangent) {
            free(cmd->remixed_div_tangent);
            cmd->remixed_div_tangent = NULL;
        }
        if (cmd->original_div_bitangent) {
            free(cmd->original_div_bitangent);
            cmd->original_div_bitangent = NULL;
        }
        if (cmd->major_div_bitangent) {
            free(cmd->major_div_bitangent);
            cmd->major_div_bitangent = NULL;
        }
        if (cmd->minor_div_bitangent) {
            free(cmd->minor_div_bitangent);
            cmd->minor_div_bitangent = NULL;
        }
        if (cmd->remixed_div_bitangent) {
            free(cmd->remixed_div_bitangent);
            cmd->remixed_div_bitangent = NULL;
        }
        if (cmd->anisotropy_ratio) {
            free(cmd->anisotropy_ratio);
            cmd->anisotropy_ratio = NULL;
        }
    }
}
