#ifndef MORSE_SMALE_CMD_H
#define MORSE_SMALE_CMD_H

#include <stdint.h>
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
};

struct MorseSmaleCmd {
	/* Input */
	const float* heightmap;
	uint32_t W, H;

	/* Output */
	std::vector<MorseSmaleCriticalPoint> critical_points;
	/* Skeleton mask: 1 = ridge/valley pixel, 0 = background.
	 * Separate masks for ridges (descending from saddle to min)
	 * and valleys (ascending from saddle to max). */
	std::vector<uint8_t> ridge_mask;  /* [W*H] */
	std::vector<uint8_t> valley_mask; /* [W*H] */
};

int morse_smale_Execute(MorseSmaleCmd* cmd);
int morse_smale_DebugRender(const MorseSmaleCmd* cmd);

#endif /* MORSE_SMALE_CMD_H */
