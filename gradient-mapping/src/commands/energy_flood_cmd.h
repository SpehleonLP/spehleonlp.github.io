#ifndef ENERGY_FLOOD_CMD_H
#define ENERGY_FLOOD_CMD_H

#include "half_edge.h"
#include <memory>
#include <stdint.h>

struct EnergyFloodCmd {
	/* Input (borrowed) */
	const DCELMesh* mesh;
	const float* heightmap;
	uint32_t W, H;

	/* Output (owned) — vec2 per pixel (W*H*2 floats) */
	std::unique_ptr<float[]> ridge_energy;
	std::unique_ptr<float[]> valley_energy;

	/* Output (owned) — min distance per pixel (W*H floats) */
	std::unique_ptr<float[]> ridge_dist;
	std::unique_ptr<float[]> valley_dist;
};

int energy_flood_Execute(EnergyFloodCmd* cmd);

#endif
