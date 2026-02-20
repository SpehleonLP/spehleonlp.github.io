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

/* Simplified mesh vertex with classification */
struct MSVertex {
	float x, y;          /* sub-pixel position */
	float divergence;     /* signed divergence at this point */
	int type;             /* 0=saddle, 1=maximum, 2=minimum, 3=branch, 4=boundary */
};

/* Simplified mesh edge (polyline between junction vertices) */
struct MSEdge {
	std::vector<int> vertex_indices;  /* ordered polyline vertices including endpoints */
	bool ascending;                   /* true=ridge, false=valley */
};

struct MorseSmaleCmd {
	/* Input */
	const float* heightmap;
	uint32_t W, H;

	float normal_scale;  /* F factor for divergence (0 = skip) */

	/* Output */
	std::vector<MorseSmaleCriticalPoint> critical_points;
	/* Per-pixel masks (additive channels for debug render):
	 * R = div_mask:    0-255 |divergence| at this skeleton pixel
	 * G = ridge_mask:  ascending separatrices (saddle → maxima)
	 * B = valley_mask: descending separatrices (saddle → minima) */
	std::vector<uint8_t> ridge_mask;   /* [W*H] */
	std::vector<uint8_t> valley_mask;  /* [W*H] */
	std::vector<uint8_t> div_mask;     /* [W*H] 0-255 |divergence| */
	std::vector<float>   div_signed;   /* [W*H] signed divergence [-1,+1] */

	/* Individual separatrices as polylines in spatial path order.
	 * Each saddle produces up to 2 ascending + 2 descending branches. */
	struct Separatrix {
		std::vector<uint32_t> pixels;  /* pixel indices for mask painting */
		std::vector<float> path_x;     /* sub-pixel x coords, path order */
		std::vector<float> path_y;     /* sub-pixel y coords, path order */
		bool ascending; /* true=ridge, false=valley */
	};
	std::vector<Separatrix> separatrices;

	/* Simplified mesh: junction graph after merge/split/RDP */
	std::vector<MSVertex> mesh_vertices;
	std::vector<MSEdge> mesh_edges;
};

int morse_smale_Execute(MorseSmaleCmd* cmd);
int morse_smale_DebugRender(const MorseSmaleCmd* cmd);

#endif /* MORSE_SMALE_CMD_H */
