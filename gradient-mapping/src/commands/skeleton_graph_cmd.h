#ifndef SKELETON_GRAPH_CMD_H
#define SKELETON_GRAPH_CMD_H

#include "half_edge.h"
#include <stdint.h>
#include <memory>
#include <vector>

struct UndirectedEdge {
    int32_t v0, v1;
    DCELEdgeType type;
    std::vector<uint32_t> pixels;   // pixel indices along the edge (construction-time data)
};

struct SkeletonGraphCmd {
    /* Input (borrowed) */
    const float* heightmap;
    const float* divergence;
    const uint8_t* walkable;        // |div| > low_threshold
    const std::vector<DCELVertex>* vertices;
    uint32_t W, H;

    /* Output (owned) */
    std::vector<DCELVertex> expanded_vertices;  // original + path pixel vertices
    std::vector<UndirectedEdge> edges;           // pixel-step edges
};

int skeleton_graph_Execute(SkeletonGraphCmd* cmd);

#endif
