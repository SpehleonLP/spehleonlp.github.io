#ifndef SWIRL_H
#define SWIRL_H

#include <stdint.h>

#ifndef VEC2_DEFINED
#define VEC2_DEFINED
typedef struct { float x, y; } vec2;
#endif

/*
 * SwirlCmd - Command pattern for generating divergence-driven swirl fields
 *
 * Creates rotational flow patterns based on local divergence of a velocity field.
 * Where divergence is high (sources/sinks), the velocity is rotated toward
 * perpendicular, creating swirling patterns around divergent regions.
 *
 * - Positive divergence (source/spreading): rotates one direction
 * - Negative divergence (sink/converging): rotates opposite direction
 *
 * Usage:
 *   SwirlCmd cmd = {0};
 *   cmd.width = w;
 *   cmd.height = h;
 *   cmd.velocity = my_velocity_field;
 *   cmd.divergence = my_divergence_field;  // or NULL to compute internally
 *   cmd.strength = 1.0f;
 *   swirl_Execute(&cmd);
 *   // use cmd.swirl
 *   swirl_Free(&cmd);
 */
typedef struct SwirlCmd {
	// Input parameters
	uint32_t width;
	uint32_t height;
	const vec2* velocity;      // input velocity field
	const float* divergence;   // pre-computed divergence (optional, computed if NULL)

	// Optional masking
	const float* mask;
	int mask_channel;
	int mask_stride;
	float mask_zero;

	// Swirl parameters
	float strength;            // blend strength (0-1, can be >1 for exaggerated effect)

	// Outputs
	vec2* swirl;               // rotational component, allocated by Execute

	// Internal
	float* _divergence;        // internal divergence if not provided
	int _owns_divergence;      // flag: did we allocate _divergence?
} SwirlCmd;

// Execute the swirl field generation
// Returns 0 on success, negative on error
int swirl_Execute(SwirlCmd* cmd);

// Free allocated outputs
void swirl_Free(SwirlCmd* cmd);

#endif
