#include "split_normals_cmd.h"
#include "hessian_cmd.h"
#include "eigen_cmd.h"
#include "hessian_from_eigen_cmd.h"
#include "normal_from_hessian_cmd.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

int split_normals_Execute(SplitNormalsCmd* cmd) {
    if (!cmd) {
        printf( "[split_normals_Execute] Error: cmd is NULL\n");
        return -1;
    }

    if (!cmd->heightmap) {
        printf( "[split_normals_Execute] Error: heightmap is NULL\n");
        return -1;
    }

    uint32_t W = cmd->W;
    uint32_t H = cmd->H;

    float normal_scale = cmd->normal_scale;
    if (normal_scale <= 0.0f) normal_scale = 1.0f;

    // Step 1: Compute Hessian
    HessianCmd hess_cmd = {
        .heightmap = cmd->heightmap,
        .W = W,
        .H = H,
        .kernel_size = cmd->kernel_size ? cmd->kernel_size : 3,
        .border = cmd->border,
        .undefined_value = cmd->undefined_value,
        .hessian = NULL
    };

    if (hessian_Execute(&hess_cmd) != 0) {
        printf( "[split_normals_Execute] Error: hessian_Execute failed\n");
        return -1;
    }

    // Step 2: Eigendecomposition
    EigenDecomposeCmd eigen_cmd = {
        .hessian = hess_cmd.hessian,
        .W = W,
        .H = H,
        .major = NULL,
        .minor = NULL
    };

    if (eigen_Execute(&eigen_cmd) != 0) {
        printf( "[split_normals_Execute] Error: eigen_Execute failed\n");
        hessian_Free(&hess_cmd);
        return -1;
    }

    // Free original Hessian (no longer needed after eigen)
    hessian_Free(&hess_cmd);

    // Step 3a: Major curvature → Hessian → Normal
    HessianFromEigenCmd major_hess_cmd = {
        .eigen = eigen_cmd.major,
        .W = W,
        .H = H,
        .hessian = NULL
    };

    if (hessian_from_eigen_Execute(&major_hess_cmd) != 0) {
        printf( "[split_normals_Execute] Error: hessian_from_eigen (major) failed\n");
        eigen_Free(&eigen_cmd);
        return -1;
    }

    NormalFromHessianCmd major_norm_cmd = {
        .hessian = major_hess_cmd.hessian,
        .W = W,
        .H = H,
        .scale = normal_scale,
        .orig_height = cmd->heightmap,
        .normals = NULL,
        .height = NULL
    };

    if (normal_from_hessian_Execute(&major_norm_cmd) != 0) {
        printf( "[split_normals_Execute] Error: normal_from_hessian (major) failed\n");
        hessian_from_eigen_Free(&major_hess_cmd);
        eigen_Free(&eigen_cmd);
        return -1;
    }

    cmd->major_normals = major_norm_cmd.normals;
    free(major_norm_cmd.height);  // Don't need height
    hessian_from_eigen_Free(&major_hess_cmd);

    // Step 3b: Compute original normals from heightmap
    uint32_t size = W * H;
    vec3* orig_normals = (vec3*)malloc(size * sizeof(vec3));
    if (!orig_normals) {
        printf("[split_normals_Execute] Error: failed to allocate orig_normals\n");
        eigen_Free(&eigen_cmd);
        split_normals_Free(cmd);
        return -1;
    }

    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            uint32_t idx = y * W + x;

            uint32_t xm = (x > 0) ? x - 1 : 0;
            uint32_t xp = (x < W - 1) ? x + 1 : W - 1;
            uint32_t ym = (y > 0) ? y - 1 : 0;
            uint32_t yp = (y < H - 1) ? y + 1 : H - 1;

            float dx = (cmd->heightmap[y * W + xp] - cmd->heightmap[y * W + xm]) * 0.5f;
            float dy = (cmd->heightmap[yp * W + x] - cmd->heightmap[ym * W + x]) * 0.5f;

            float nx = -dx;
            float ny = -dy;
            float nz = normal_scale;

            float len = sqrtf(nx * nx + ny * ny + nz * nz);
            if (len > 1e-8f) {
                orig_normals[idx].x = nx / len;
                orig_normals[idx].y = ny / len;
                orig_normals[idx].z = nz / len;
            } else {
                orig_normals[idx].x = 0.0f;
                orig_normals[idx].y = 0.0f;
                orig_normals[idx].z = 1.0f;
            }
        }
    }

    // Step 3c: Minor normal = original - major (then renormalize)
    cmd->minor_normals = (vec3*)malloc(size * sizeof(vec3));
    if (!cmd->minor_normals) {
        printf("[split_normals_Execute] Error: failed to allocate minor_normals\n");
        free(orig_normals);
        eigen_Free(&eigen_cmd);
        split_normals_Free(cmd);
        return -1;
    }

    for (uint32_t i = 0; i < size; i++) {
        // Subtract tangent components (gradient encoded in nx, ny)
        float nx = orig_normals[i].x - cmd->major_normals[i].x;
        float ny = orig_normals[i].y - cmd->major_normals[i].y;
        float nz = sqrtf(1.f - nx * nx + ny * ny);

		cmd->minor_normals[i].x = nx;
		cmd->minor_normals[i].y = ny;
		cmd->minor_normals[i].z = nz;
    }

    free(orig_normals);

    // Step 4: Compute major_ratio before freeing eigen data
    cmd->major_ratio = (float*)malloc(size * sizeof(float));
    if (!cmd->major_ratio) {
        printf( "[split_normals_Execute] Error: failed to allocate major_ratio\n");
        eigen_Free(&eigen_cmd);
        split_normals_Free(cmd);
        return -1;
    }

    const float epsilon = 1e-8f;
    for (uint32_t i = 0; i < size; i++) {
        float major_abs = fabsf(eigen_cmd.major[i].value);
        float minor_abs = fabsf(eigen_cmd.minor[i].value);
        float total = major_abs + minor_abs;
        cmd->major_ratio[i] = (total > epsilon) ? (major_abs / total) : 0.5f;
    }

    // Free eigen data
    eigen_Free(&eigen_cmd);

    return 0;
}

void split_normals_Free(SplitNormalsCmd* cmd) {
    if (cmd) {
        if (cmd->major_normals) {
            free(cmd->major_normals);
            cmd->major_normals = NULL;
        }
        if (cmd->minor_normals) {
            free(cmd->minor_normals);
            cmd->minor_normals = NULL;
        }
        if (cmd->major_ratio) {
            free(cmd->major_ratio);
            cmd->major_ratio = NULL;
        }
    }
}
