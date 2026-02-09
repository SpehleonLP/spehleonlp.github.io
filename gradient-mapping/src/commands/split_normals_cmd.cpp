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
        fprintf(stderr, "[split_normals_Execute] Error: cmd is NULL\n");
        return -1;
    }

    if (!cmd->heightmap) {
        fprintf(stderr, "[split_normals_Execute] Error: heightmap is NULL\n");
        return -1;
    }

    uint32_t W = cmd->W;
    uint32_t H = cmd->H;

    // Step 1: Compute Hessian
    HessianCmd hess_cmd{};
    hess_cmd.heightmap = cmd->heightmap;
    hess_cmd.W = W;
    hess_cmd.H = H;
    hess_cmd.kernel_size = cmd->kernel_size ? cmd->kernel_size : 3;
    hess_cmd.border = cmd->border;
    hess_cmd.undefined_value = cmd->undefined_value;

    if (hessian_Execute(&hess_cmd) != 0) {
        fprintf(stderr, "[split_normals_Execute] Error: hessian_Execute failed\n");
        return -1;
    }

    // Step 2: Eigendecomposition
    EigenDecomposeCmd eigen_cmd{};
    eigen_cmd.hessian = hess_cmd.hessian.get();
    eigen_cmd.W = W;
    eigen_cmd.H = H;

    if (eigen_Execute(&eigen_cmd) != 0) {
        fprintf(stderr, "[split_normals_Execute] Error: eigen_Execute failed\n");
        return -1;
    }

    // Free original Hessian (no longer needed after eigen)
    hess_cmd.hessian.reset();

    // Step 3: Reconstruct separate Hessians from eigendecomposition
    HessianFromEigenCmd major_hess_cmd{};
    major_hess_cmd.eigen = eigen_cmd.major.get();
    major_hess_cmd.W = W;
    major_hess_cmd.H = H;

    if (hessian_from_eigen_Execute(&major_hess_cmd) != 0) {
        fprintf(stderr, "[split_normals_Execute] Error: hessian_from_eigen (major) failed\n");
        return -1;
    }

    HessianFromEigenCmd minor_hess_cmd{};
    minor_hess_cmd.eigen = eigen_cmd.minor.get();
    minor_hess_cmd.W = W;
    minor_hess_cmd.H = H;

    if (hessian_from_eigen_Execute(&minor_hess_cmd) != 0) {
        fprintf(stderr, "[split_normals_Execute] Error: hessian_from_eigen (minor) failed\n");
        return -1;
    }

    // Step 4: Coupled constraint solve for both major and minor
    NormalFromHessianCmd norm_cmd{};
    norm_cmd.H1 = major_hess_cmd.hessian.get();
    norm_cmd.H2 = minor_hess_cmd.hessian.get();
    norm_cmd.height = cmd->heightmap;
    norm_cmd.W = W;
    norm_cmd.H = H;
    norm_cmd.max_iterations = 100;
    norm_cmd.tolerance = 1e-5f;
    norm_cmd.sor_omega = 1.7f;

    if (normal_from_hessian_Execute(&norm_cmd) != 0) {
        fprintf(stderr, "[split_normals_Execute] Error: normal_from_hessian failed\n");
        return -1;
    }

    cmd->major_normals = std::move(norm_cmd.major_normals);
    cmd->minor_normals = std::move(norm_cmd.minor_normals);

    // Intermediate height fields freed automatically when norm_cmd goes out of scope

    // Step 5: Compute major_ratio before eigen_cmd goes out of scope
    uint32_t size = W * H;
    cmd->major_ratio = std::unique_ptr<float[]>(new float[size]);

    const float epsilon = 1e-8f;
    for (uint32_t i = 0; i < size; i++) {
        float major_abs = fabsf(eigen_cmd.major[i].value);
        float minor_abs = fabsf(eigen_cmd.minor[i].value);
        float total = major_abs + minor_abs;
        cmd->major_ratio[i] = (total > epsilon) ? (major_abs / total) : 0.5f;
    }

    // eigen_cmd, hess_cmds freed automatically on scope exit
    return 0;
}
