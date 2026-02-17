#ifndef DCEL_FEATURES_CMD_H
#define DCEL_FEATURES_CMD_H

#include "half_edge.h"

struct DCELFeaturesCmd {
	/* Input/Output: modifies face fields on half-edges, populates features */
	DCELMesh* mesh;
};

int dcel_features_Execute(DCELFeaturesCmd* cmd);

#endif
