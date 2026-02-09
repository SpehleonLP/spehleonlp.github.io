#ifndef LIC_STYLIZE_CMD_H
#define LIC_STYLIZE_CMD_H

#include <stdint.h>
#include <memory>
#include "../effect_stack_api.h"

/*
 * LicStylizeCmd - Line Integral Convolution stylization
 *
 * Advects the minor's signed scalar (tangent-space X component, perpendicular
 * to major flow) along the major flow direction using LIC. This creates
 * streaky detail that follows the major curvature contours.
 *
 * Input:
 *   major_normals  - World-space normals defining the flow field
 *   minor_normals  - Tangent-space minor normals (relative to major)
 *
 * Output:
 *   result_normals - Stylized tangent-space minor normals:
 *                    (lic_value, 0, minor.z)
 */
typedef struct {
	/* Input */
	const vec3* major_normals;    // Flow field (world-space normals)
	const vec3* minor_normals;    // Tangent-space minor normals (relative to major)
	uint32_t W, H;
	float kernel_length;          // Half-length of LIC kernel in pixels (default 10)
	float step_size;              // Euler integration step (default 0.5)

	/* Output (allocated internally if NULL) */
	std::unique_ptr<vec3[]> result_normals;  // Stylized minor normals (tangent space)
} LicStylizeCmd;

int lic_stylize_Execute(LicStylizeCmd* cmd);

#endif /* LIC_STYLIZE_CMD_H */
