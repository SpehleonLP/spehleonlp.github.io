#ifndef HESSIAN_CMD_H
#define HESSIAN_CMD_H

#include <stdint.h>

/*
 * Hessian matrix (2x2 symmetric) at a single pixel
 *
 * Represents second derivatives:
 *   H = [xx  xy]
 *       [xy  yy]
 */
typedef struct {
    float xx;  // ∂²f/∂x²
    float xy;  // ∂²f/∂x∂y
    float yy;  // ∂²f/∂y²
} Hessian2D;

/*
 * Border policy for sampling outside image bounds
 */
typedef enum {
    HESSIAN_BORDER_UNDEFINED,    // Output zero Hessian at edges (default)
    HESSIAN_BORDER_CLAMP_EDGE,   // Clamp coordinates to valid range
    HESSIAN_BORDER_REPEAT,       // Wrap around (modulo)
    HESSIAN_BORDER_MIRROR        // Mirror at edges
} HessianBorderPolicy;

/*
 * HessianCmd - Compute Hessian matrix field from heightmap
 *
 * Uses central finite differences with configurable kernel size.
 * Border policy controls how edge pixels are handled.
 */
typedef struct {
    /* Input */
    const float* heightmap;
    uint32_t W, H;
    int kernel_size;              // 3 or 5 (for 3x3 or 5x5 stencil)
    HessianBorderPolicy border;   // How to handle edges (default UNDEFINED)
    float undefined_value;        // Value to treat as "no data" (exact match)

    /* Output (allocated by caller or by hessian_Execute) */
    Hessian2D* hessian;  // W*H array of Hessian matrices
} HessianCmd;

/*
 * Compute Hessian field.
 * If cmd->hessian is NULL, allocates it internally.
 * Returns 0 on success, -1 on error.
 */
int hessian_Execute(HessianCmd* cmd);

/*
 * Free allocated memory.
 */
void hessian_Free(HessianCmd* cmd);

#endif /* HESSIAN_CMD_H */
