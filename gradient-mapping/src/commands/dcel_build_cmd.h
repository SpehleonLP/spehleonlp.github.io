#ifndef DCEL_BUILD_CMD_H
#define DCEL_BUILD_CMD_H

#include "half_edge.h"
#include <vector>

struct UndirectedEdge;  // forward decl from skeleton_graph_cmd.h

struct DCELBuildCmd {
    /* Input */
    std::vector<DCELVertex> vertices;           // moved in
    const std::vector<UndirectedEdge>* edges;   // borrowed

    /* Output */
    DCELMesh mesh;      // vertices + half_edges populated, features empty
};

int dcel_build_Execute(DCELBuildCmd* cmd);

#endif
