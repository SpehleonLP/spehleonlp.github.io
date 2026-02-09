#ifndef CONTOUR_FLOW_H
#define CONTOUR_FLOW_H

#include <stdint.h>

#ifndef VEC2_DEFINED
#define VEC2_DEFINED
typedef struct { float x, y; } vec2;
#endif

/*
 * ContourFlowCmd - Compute directional flow along contour lines
 *
 * Given a height field, computes flow that runs ALONG contours (iso-lines)
 * rather than across them. This is the gradient rotated 90°, but with
 * consistent direction choice (CW vs CCW) propagated from seeds.
 *
 * Algorithm:
 *   1. Compute gradient of height field
 *   2. Detect ridges (optional) as natural seed points
 *   3. From seeds, flood-fill propagate the direction choice
 *   4. At each pixel: tangent = rotate(gradient, chosen_direction)
 *
 * The result is a flow field that follows contours with consistent
 * directionality - things flow from one ridge point to another.
 */

// Seed for direction propagation
typedef struct {
	int x, y;
	int direction;    // +1 = CCW (rotate left), -1 = CW (rotate right)
	float priority;   // lower = processed first
} CFSeed;

// Ridge detection mode
typedef enum {
	CF_RIDGE_NONE,        // don't auto-detect, use provided seeds only
	CF_RIDGE_PEAKS,       // local maxima (bright ridges)
	CF_RIDGE_VALLEYS,     // local minima (dark ridges)
	CF_RIDGE_BOTH,        // both peaks and valleys
	CF_RIDGE_SADDLES      // saddle points (where direction naturally splits)
} CFRidgeMode;

typedef struct ContourFlowCmd {
	// Input
	uint32_t width;
	uint32_t height;
	const float* heightmap;      // input height field

	// Seeds (optional - can also auto-detect from ridges)
	const CFSeed* seeds;
	uint32_t seed_count;

	// Ridge detection
	CFRidgeMode ridge_mode;
	float ridge_threshold;       // how prominent a ridge must be (0-1)

	// Flow parameters
	float influence_falloff;     // how quickly influence decreases with distance
	float min_gradient;          // below this, flow is zero (flat regions)

	// Blending with original gradient (0 = pure tangent, 1 = pure gradient)
	float gradient_blend;

	// Outputs (allocated by Execute if NULL)
	vec2* flow;                  // the tangent flow field
	float* influence;            // influence strength at each pixel (0-1)
	int8_t* direction;           // chosen direction at each pixel (+1/-1)

	// Internal
	vec2* _gradient;
	float* _ridge_strength;
} ContourFlowCmd;

int cf_Execute(ContourFlowCmd* cmd);
void cf_Free(ContourFlowCmd* cmd);

/*
 * Utility: detect ridges in height field
 * Returns strength of "ridgeness" at each pixel (0 = not ridge, 1 = strong ridge)
 */
int cf_DetectRidges(
	const float* heightmap,
	uint32_t width, uint32_t height,
	CFRidgeMode mode,
	float* ridge_strength_out);

/*
 * Utility: rotate vector 90 degrees
 * direction > 0: CCW (x,y) -> (-y, x)
 * direction < 0: CW  (x,y) -> (y, -x)
 */
static inline vec2 cf_rotate90(vec2 v, int direction) {
	vec2 result;
	if (direction >= 0) {
		// CCW: (x,y) -> (-y, x)
		result.x = -v.y;
		result.y = v.x;
	} else {
		// CW: (x,y) -> (y, -x)
		result.x = v.y;
		result.y = -v.x;
	}
	return result;
}

#endif
