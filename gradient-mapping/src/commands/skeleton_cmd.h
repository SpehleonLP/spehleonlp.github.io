#ifndef SKELETON_CMD_H
#define SKELETON_CMD_H

#include "half_edge.h"
#include <stdint.h>
#include <memory>
#include <vector>

struct SkeletonCmd {
    /* Input (borrowed) */
    const float* heightmap;
    const float* divergence;
    uint32_t W, H;
    float high_threshold;
    int min_branch_length;

    /* Output (owned) */
    std::unique_ptr<uint8_t[]> skeleton;       // combined skeleton mask
    std::unique_ptr<uint8_t[]> ridge_skel;     // ridge-only skeleton
    std::unique_ptr<uint8_t[]> valley_skel;    // valley-only skeleton
    std::vector<DCELVertex> vertices;
    std::unique_ptr<int32_t[]> vertex_map;     // pixel -> vertex index, or -1
};

int skeleton_Execute(SkeletonCmd* cmd);

/* Utility: count 8-connected skeleton neighbors (used by skeleton_graph_cmd too) */
int skeleton_count_neighbors(const uint8_t* skel, uint32_t W, uint32_t H,
                              uint32_t x, uint32_t y);

#endif
