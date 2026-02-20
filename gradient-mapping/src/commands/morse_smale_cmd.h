#ifndef MORSE_SMALE_CMD_H
#define MORSE_SMALE_CMD_H

#include <stdint.h>
#include <map>
#include <vector>

/*
 * Morse-Smale Complex Extraction
 *
 * Computes the discrete Morse gradient and extracts critical points
 * (maxima, minima, saddles) plus separatrices (ridge/valley lines)
 * from a 2D heightmap using the diamorse library.
 */

struct MorseSmaleCriticalPoint {
	float x, y;
	int dimension;  /* 0=extremum(max/min), 1=saddle, 2=face-critical */
	float value;    /* heightmap value at this point */
	float persistence; /* |h(creator) - h(destroyer)|; INFINITY if unpaired */
};

struct MorseSmaleCmd {
	/* Input */
	const float* heightmap;
	uint32_t W, H;
	float normal_scale;

	/* Output */
	std::vector<MorseSmaleCriticalPoint> critical_points;
	/* Skeleton persistence mask: persistence of the saddle that spawned
	 * each separatrix pixel. 0 = no separatrix, >0 = separatrix.
	 * When multiple separatrices overlap, keeps the max persistence. */
	std::vector<float> ridge_mask;  /* [W*H] */
	std::vector<float> valley_mask; /* [W*H] */

	/* Internal: Cell → persistence for tagging critical points.
	 * Key is diamorse cell_id_type (uint64_t). */
	std::map<uint64_t, float> persistence_map;
};

int morse_smale_Execute(MorseSmaleCmd* cmd);
int morse_smale_DebugRender(const MorseSmaleCmd* cmd);

#endif /* MORSE_SMALE_CMD_H */
