#ifndef EDGE_DIJKSTRA_CMD_H
#define EDGE_DIJKSTRA_CMD_H

#include "half_edge.h"
#include <memory>
#include <stdint.h>

struct EdgeDijkstraCmd {
	/* Input (borrowed) */
	const DCELMesh* mesh;
	const float* heightmap;
	uint32_t W, H;
	float height_bias;  /* how much height matters vs distance (0 = pure distance) */

	/* Output: per-pixel direction from uphill memo (W*H*2 floats) */
	std::unique_ptr<float[]> uphill_dir;
	/* Output: per-pixel cost from uphill memo (W*H floats) */
	std::unique_ptr<float[]> uphill_cost;

	/* Output: per-pixel direction from downhill memo (W*H*2 floats) */
	std::unique_ptr<float[]> downhill_dir;
	/* Output: per-pixel cost from downhill memo (W*H floats) */
	std::unique_ptr<float[]> downhill_cost;

	/* Output: per-pixel half-edge index from winning seed (W*H, -1 if unreached) */
	std::unique_ptr<int32_t[]> uphill_edge_id;
	std::unique_ptr<int32_t[]> downhill_edge_id;

	/* Diagnostic: per-pixel seed origin position (W*H*2 floats) */
	std::unique_ptr<float[]> uphill_seed_xy;
	std::unique_ptr<float[]> downhill_seed_xy;
	/* Diagnostic: per-pixel flag — seed was from a terminal edge (W*H) */
	std::unique_ptr<uint8_t[]> uphill_terminal;
	std::unique_ptr<uint8_t[]> downhill_terminal;
};

int edge_dijkstra_Execute(EdgeDijkstraCmd* cmd);

#endif
