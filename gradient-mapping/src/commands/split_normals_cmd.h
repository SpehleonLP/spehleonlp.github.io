#ifndef SPLIT_NORMALS_CMD_H
#define SPLIT_NORMALS_CMD_H

#include <stdint.h>
#include <memory>
#include "hessian_cmd.h"
#include "../effect_stack_api.h"  // for vec3

/*
 * SplitNormalsCmd - Compute separated major/minor curvature normal maps
 *
 * Pipeline: heightmap → Hessian → eigen → split Hessians → normal maps
 *
 * Intermediate Hessian and eigen data are freed internally.
 */
typedef struct {
    /* Input */
    const float* heightmap;
    uint32_t W, H;
    int kernel_size;              // 3 or 5 for Hessian computation
    HessianBorderPolicy border;   // How to handle edges (default UNDEFINED)
    float undefined_value;        // Value to treat as "no data" (exact match)

    /* Output (allocated internally) */
    std::unique_ptr<vec3[]> major_normals;          // W*H normal map from major curvature
    std::unique_ptr<vec3[]> minor_normals;          // W*H normal map from minor curvature
    std::unique_ptr<float[]> major_ratio;           // W*H ratio: |λ_major| / (|λ_major| + |λ_minor|)
} SplitNormalsCmd;

/*
 * Compute split normal maps.
 * Returns 0 on success, -1 on error.
 */
int split_normals_Execute(SplitNormalsCmd* cmd);

/* RAII: memory is freed automatically when cmd goes out of scope */

#endif /* SPLIT_NORMALS_CMD_H */
