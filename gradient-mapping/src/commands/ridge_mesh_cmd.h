#ifndef RIDGE_MESH_CMD_H
#define RIDGE_MESH_CMD_H

#include "half_edge.h"
#include <stdint.h>
#include <memory>

/*
 * Ridge/Valley Mesh Extraction — DCEL-based pipeline
 *
 * Extracts a half-edge mesh of ridges and valleys from a height map.
 * Pipeline: divergence → skeleton → graph → DCEL → features → energy
 */

struct RidgeMeshCmd {
	/* Input */
	const float* heightmap;
	uint32_t W, H;
	float normal_scale;
	float high_threshold;
	float low_threshold;

	/* Output */
	DCELMesh mesh;
	std::unique_ptr<float[]> divergence;

};

int ridge_mesh_Execute(RidgeMeshCmd* cmd);
int ridge_mesh_DebugRender(const RidgeMeshCmd* cmd);

#endif /* RIDGE_MESH_CMD_H */
