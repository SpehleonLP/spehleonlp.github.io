#ifndef RIDGE_ENERGY_CMD_H
#define RIDGE_ENERGY_CMD_H

#include "half_edge.h"
#include <stdint.h>

struct RidgeEnergyCmd {
	/* Input/Output: sets energy on half-edges */
	DCELMesh* mesh;

	/* Input for debug rendering (optional, only used if DEBUG_IMG_OUT) */
	const float* heightmap;
	uint32_t W, H;
};

int ridge_energy_Execute(RidgeEnergyCmd* cmd);

#endif
