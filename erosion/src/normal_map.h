#ifndef NORMAL_MAP_H
#define NORMAL_MAP_H

#include <stdint.h>

#ifndef VEC3_DEFINED
#define VEC3_DEFINED
typedef struct { float x, y, z; } vec3;
#endif

/*
 * NormalMapCmd - Command pattern for computing normal maps from height fields
 *
 * Computes surface normals from a height field using finite differences.
 * Unlike truncated 2D gradients, this produces proper 3D normals with
 * accurate Z components based on gradient magnitude and a configurable scale.
 *
 * Usage:
 *   NormalMapCmd cmd = {0};
 *   cmd.width = w;
 *   cmd.height = h;
 *   cmd.height_data = my_height_field;
 *   cmd.channel = 0;  // which channel of interlaced data
 *   cmd.stride = 4;   // interlacing stride (4 for RGBA-style)
 *   cmd.scale = 1.0f; // height scale factor
 *   nm_Execute(&cmd);
 *   // use cmd.normals
 *   nm_Free(&cmd);
 */
typedef struct NormalMapCmd {
	// Input parameters
	uint32_t width;
	uint32_t height;
	const float* height_data;  // source height field
	int channel;               // channel offset for interlaced data
	int stride;                // interlacing stride (1 for non-interlaced, 4 for vec4)
	float scale;               // height scale factor (affects normal steepness)
	float zero_value;          // value treated as "no data" (default 0)

	// Output
	vec3* normals;             // allocated by Execute, size = width * height
} NormalMapCmd;

// Execute the normal map computation
// Allocates cmd->normals if NULL
// Returns 0 on success, negative on error
int nm_Execute(NormalMapCmd* cmd);

// Free allocated output
void nm_Free(NormalMapCmd* cmd);

// Compute gradient only (2D, for velocity fields)
// Returns vec2-compatible output (z ignored)
#ifndef VEC2_DEFINED
#define VEC2_DEFINED
typedef struct { float x, y; } vec2;
#endif

typedef struct GradientCmd {
	uint32_t width;
	uint32_t height;
	const float* height_data;
	int channel;
	int stride;
	float zero_value;

	vec2* gradient;  // output, allocated by Execute
} GradientCmd;

int grad_Execute(GradientCmd* cmd);
void grad_Free(GradientCmd* cmd);

/*
 * HeightFromNormalsCmd - Reconstruct height map from normal map (Poisson solve)
 *
 * Given surface normals, recovers the height field by integrating gradients.
 * Uses Poisson equation: ∇²h = ∇·(-nx/nz, -ny/nz)
 *
 * This is the inverse of NormalMapCmd.
 */
typedef struct HeightFromNormalsCmd {
	uint32_t width;
	uint32_t height;
	const vec3* normals;       // input normal map (or NULL if using nx/ny directly)
	const float* nx;           // alternative: separate X gradient channel
	const float* ny;           // alternative: separate Y gradient channel
	int iterations;            // Poisson solver iterations (default 100)
	float scale;               // output height scale factor

	// Optional mask: skip pixels where mask == 0
	const uint8_t* mask;

	// Output
	float* heightmap;          // allocated by Execute if NULL
} HeightFromNormalsCmd;

int height_from_normals_Execute(HeightFromNormalsCmd* cmd);
void height_from_normals_Free(HeightFromNormalsCmd* cmd);

#endif
