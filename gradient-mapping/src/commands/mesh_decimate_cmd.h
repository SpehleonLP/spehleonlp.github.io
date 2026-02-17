#ifndef MESH_DECIMATE_CMD_H
#define MESH_DECIMATE_CMD_H

#include "half_edge.h"

struct MeshDecimateCmd {
    DCELMesh* mesh;       // in/out
    float epsilon;        // max perpendicular deviation in pixels (default: 1.0)
};

int mesh_decimate_Execute(MeshDecimateCmd* cmd);

#endif
