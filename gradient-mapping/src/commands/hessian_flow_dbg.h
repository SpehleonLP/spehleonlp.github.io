#ifndef HESSIAN_FLOW_DBG_H
#define HESSIAN_FLOW_DBG_H

#include <stdint.h>
#include "hessian_cmd.h"
#include "../effect_stack_api.h"  // for vec3

/*
 * HessianFlowDebugCmd - Full pipeline with debug grid output
 *
 * Chains: heightmap → split_normals → remix → constrained Poisson → PNG grid
 *
 * Grid layout (5×4):
 *   [original_h]       [major_h]       [minor_h]       [remixed_h]
 *   [original_n]       [major_n]       [minor_n]       [remixed_n]
 *   [original_div_t]   [major_div_t]   [minor_div_t]   [remixed_div_t]   (tangent divergence)
 *   [original_div_b]   [major_div_b]   [minor_div_b]   [remixed_div_b]   (bitangent divergence = -curl)
 *   [anisotropy]       [anisotropy]    [anisotropy]    [anisotropy]      (0.5=isotropic, 1.0=anisotropic)
 */
typedef struct {
    /* Input */
    const float* heightmap;
    uint32_t W, H;
    int kernel_size;              // 3 or 5 for Hessian computation
    HessianBorderPolicy border;   // How to handle edges (default UNDEFINED)
    float undefined_value;        // Value to treat as "no data" (exact match)
    float normal_scale;           // Normal map Z scale (default 1.0, higher = flatter)
    const char* output_path;      // where to save the debug PNG grid

    /* Remix parameters */
    float major_weight;           // Weight for major curvature (default 1.0)
    float minor_weight;           // Weight for minor curvature (default 1.0)
    int poisson_iterations;       // Max iterations for constrained Poisson (default 1000)
    float poisson_tolerance;      // Convergence tolerance (default 1e-5)

    /* Output (allocated internally) */
    vec3* original_normals;       // W*H normal map from original heightmap
    vec3* major_normals;          // W*H normal map from major curvature
    vec3* minor_normals;          // W*H normal map from minor curvature
    vec3* remixed_normals;        // W*H remixed normal map (weighted major+minor)
    float* major_height;          // W*H height from major curvature
    float* minor_height;          // W*H height from minor curvature
    float* reconstructed_height;  // W*H height from constrained Poisson

    /* Divergence fields (allocated internally) */
    float* original_div_tangent;  // W*H divergence of tangent (steepest ascent direction)
    float* major_div_tangent;
    float* minor_div_tangent;
    float* remixed_div_tangent;
    float* original_div_bitangent; // W*H divergence of bitangent (perpendicular to tangent)
    float* major_div_bitangent;
    float* minor_div_bitangent;
    float* remixed_div_bitangent;

    /* Anisotropy ratio: |λ_major|/(|λ_major|+|λ_minor|), 0.5=isotropic, 1.0=fully anisotropic */
    float* anisotropy_ratio;
} HessianFlowDebugCmd;

/*
 * Execute full pipeline and generate debug PNG grid.
 * Returns 0 on success, -1 on error.
 */
int hessian_flow_debug_Execute(HessianFlowDebugCmd* cmd);

/*
 * Execute full pipeline (without PNG output).
 * Returns 0 on success, -1 on error.
 */
int hessian_flow_init(HessianFlowDebugCmd* cmd);

/*
 * Generate debug PNG grid (5×4).
 * Returns 0 on success, -1 on error.
 */
int hessian_flow_debug_Output(HessianFlowDebugCmd* cmd);

/*
 * Free allocated memory.
 */
void hessian_flow_debug_Free(HessianFlowDebugCmd* cmd);

#endif /* HESSIAN_FLOW_DBG_H */
