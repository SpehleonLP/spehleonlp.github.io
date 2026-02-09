#ifndef LIC_DEBUG_CMD_H
#define LIC_DEBUG_CMD_H

#include <stdint.h>
#include "../effect_stack_api.h"

/*
 * LicDebugCmd - Per-channel LIC debug command
 *
 * For each of the 3 height channels:
 *   1. Compute gradient -> derive vector field (normal/tangent/bitangent)
 *   2. Apply Line Integral Convolution
 *   3. Write debug PNG: debug_lic_ch{0,1,2}.png
 *   4. Replace channel in-place with LIC result
 */
typedef struct {
    float* heights;       /* planar height data (3 channels * W*H), modified in-place */
    uint32_t W, H;
    LicVectorField vector_field;
    float kernel_length;  /* half-length of LIC kernel in pixels */
    float step_size;      /* Euler integration step */
} LicDebugCmd;

int lic_debug_Execute(LicDebugCmd* cmd);

#endif /* LIC_DEBUG_CMD_H */
