#ifndef HESSIAN_FLOW_DBG_H
#define HESSIAN_FLOW_DBG_H

#include <stdint.h>
#include <memory>
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
 *   [anisotropy]       [dot(maj,orig)] [dot(min,orig)] [dot(mix,orig)]   (normal similarity)
 */
typedef struct {
    /* Input */
    const float* heightmap;
    uint32_t W, H;
    int kernel_size;              // 3 or 5 for Hessian computation
    HessianBorderPolicy border;   // How to handle edges (default UNDEFINED)
    float undefined_value;        // Value to treat as "no data" (exact match)
    const char* output_path;      // where to save the debug PNG grid

    /* Remix parameters */
    float major_weight;           // Weight for major curvature (default 1.0)
    float minor_weight;           // Weight for minor curvature (default 1.0)
    int poisson_iterations;       // Max iterations for constrained Poisson (default 1000)
    float poisson_tolerance;      // Convergence tolerance (default 1e-5)

    /* Output (allocated internally, RAII) */
    std::unique_ptr<vec3[]> original_normals;       // W*H normal map from original heightmap
    std::unique_ptr<vec3[]> major_normals;          // W*H normal map from major curvature
    std::unique_ptr<vec3[]> minor_normals;          // W*H normal map from minor curvature
    std::unique_ptr<vec3[]> remixed_normals;        // W*H remixed normal map (weighted major+minor)
    std::unique_ptr<float[]> major_height;          // W*H height from major curvature
    std::unique_ptr<float[]> minor_height;          // W*H height from minor curvature
    std::unique_ptr<float[]> reconstructed_height;  // W*H height from constrained Poisson

    /* Divergence fields (allocated internally) */
    std::unique_ptr<float[]> original_div_tangent;  // W*H divergence of tangent (steepest ascent direction)
    std::unique_ptr<float[]> major_div_tangent;
    std::unique_ptr<float[]> minor_div_tangent;
    std::unique_ptr<float[]> remixed_div_tangent;
    std::unique_ptr<float[]> original_div_bitangent; // W*H divergence of bitangent (perpendicular to tangent)
    std::unique_ptr<float[]> major_div_bitangent;
    std::unique_ptr<float[]> minor_div_bitangent;
    std::unique_ptr<float[]> remixed_div_bitangent;

    /* Anisotropy ratio: |λ_major|/(|λ_major|+|λ_minor|), 0.5=isotropic, 1.0=fully anisotropic */
    std::unique_ptr<float[]> anisotropy_ratio;

    /* Dot products of column normals with original normals (1.0=identical, 0=perpendicular) */
    std::unique_ptr<float[]> dot_major_original;    // dot(major_normals, original_normals)
    std::unique_ptr<float[]> dot_minor_original;    // dot(minor_normals, original_normals)
    std::unique_ptr<float[]> dot_remixed_original;  // dot(remixed_normals, original_normals)
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

/* RAII: memory is freed automatically when cmd goes out of scope */

#endif /* HESSIAN_FLOW_DBG_H */
