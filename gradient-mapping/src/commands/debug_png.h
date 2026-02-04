#ifndef DEBUG_PNG_H
#define DEBUG_PNG_H

#include <stdint.h>
#include "../effect_stack_api.h"

#define DEBUG_IMG_OUT 1

// don't include stb_png in release
#if DEBUG_IMG_OUT == 1

/*
 * Debug PNG Export Commands
 *
 * Command-pattern structs for exporting various data types to PNG files.
 * Useful for debugging and visualization of intermediate processing results.
 */

/*
 * PngFloatCmd - Export float array as grayscale PNG
 *
 * Values are normalized to [0, 255] based on min/max range.
 * If auto_range is set, min/max are computed from data.
 */
typedef struct PngFloatCmd {
	const char* path;
	const float* data;
	uint32_t width;
	uint32_t height;
	float min_val;
	float max_val;
	int auto_range;  // if nonzero, compute min/max automatically
} PngFloatCmd;

int png_ExportFloat(PngFloatCmd* cmd);

/*
 * PngVec2Cmd - Export vec2 array as RGB PNG (normal map style)
 *
 * Maps XY to RG channels, Z is derived from magnitude for proper normals.
 * scale controls the range: values are expected in [-scale, scale].
 */
typedef struct PngVec2Cmd {
	const char* path;
	const vec2* data;
	uint32_t width;
	uint32_t height;
	float scale;       // expected range of values (default 1.0)
	float z_bias;      // Z component bias for normal map (default 0.005)
} PngVec2Cmd;

int png_ExportVec2(PngVec2Cmd* cmd);

/*
 * PngVec3Cmd - Export vec3 array as RGB PNG
 *
 * Assumes normalized vectors in [-1, 1] range, maps to [0, 255].
 */
typedef struct PngVec3Cmd {
	const char* path;
	const vec3* data;
	uint32_t width;
	uint32_t height;
} PngVec3Cmd;

int png_ExportVec3(PngVec3Cmd* cmd);

/*
 * PngInterleavedCmd - Export one channel from interlaced float data
 *
 * Extracts channel 'channel' from data with stride 'stride',
 * then exports as grayscale PNG.
 */
typedef struct PngInterleavedCmd {
	const char* path;
	const float* data;
	uint32_t width;
	uint32_t height;
	int channel;
	int stride;
	float min_val;
	float max_val;
	int auto_range;
} PngInterleavedCmd;

int png_ExportInterleaved(PngInterleavedCmd* cmd);

/*
 * PngGridCmd - Export multiple images as a grid (for comparison)
 *
 * Creates a single PNG with tiles arranged in a grid.
 * Each tile must be the same size.
 */
typedef enum {
	PNG_TILE_GRAYSCALE,       // float* data, auto-ranged
	PNG_TILE_VEC2,            // vec2* data
	PNG_TILE_VEC3,            // vec3* data
	PNG_TILE_INTERLEAVED,     // float* data with channel/stride
} PngTileType;

typedef struct PngGridTile {
	PngTileType type;
	const void* data;
	int channel;              // for INTERLEAVED type
	int stride;               // for INTERLEAVED type
} PngGridTile;

typedef struct PngGridCmd {
	const char* path;
	uint32_t tile_width;
	uint32_t tile_height;
	uint32_t cols;
	uint32_t rows;
	const PngGridTile* tiles; // array of cols*rows tiles, row-major order
} PngGridCmd;

int png_ExportGrid(PngGridCmd* cmd);

#endif
#endif
