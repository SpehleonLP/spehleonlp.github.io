#ifndef MESH_SIMPLIFY_CMD_H
#define MESH_SIMPLIFY_CMD_H

#include "half_edge.h"

struct MeshSimplifyCmd {
    DCELMesh* mesh;       // in/out
    float min_area;       // collapse faces with |area| < this (default: 4.0)
};

int mesh_simplify_Execute(MeshSimplifyCmd* cmd);

#endif
