#ifndef HEIGHTMAP_OPS_H
#define HEIGHTMAP_OPS_H

#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

/*
 * Shared height map utilities.
 *
 * All gradient/normal/divergence computations use clamp-to-border
 * (out-of-bounds samples read as 0.0f) and central differences.
 */

/* Compute height gradient via central differences (clamp-to-border). */
static inline glm::vec2 height_gradient(const float* height,
                                         uint32_t x, uint32_t y,
                                         uint32_t W, uint32_t H)
{
    uint32_t idx = y * W + x;
    float hL = (x > 0)     ? height[idx - 1] : 0.0f;
    float hR = (x < W - 1) ? height[idx + 1] : 0.0f;
    float hD = (y > 0)     ? height[idx - W] : 0.0f;
    float hU = (y < H - 1) ? height[idx + W] : 0.0f;
    return glm::vec2((hR - hL) * 0.5f, (hU - hD) * 0.5f);
}

/*
 * Compute surface normal from height gradient.
 * scale controls z-component magnitude before renormalization.
 * Larger scale = flatter normals; smaller = sharper features.
 */
static inline glm::vec3 height_normal(const float* height,
                                       uint32_t x, uint32_t y,
                                       uint32_t W, uint32_t H,
                                       float scale)
{
    glm::vec2 g = height_gradient(height, x, y, W, H);
    glm::vec3 n(-g.x, -g.y, scale);
    float len = glm::length(n);
    return len > 1e-12f ? n / len : glm::vec3(0, 0, 1);
}

/*
 * Compute divergence of a 2D vector field (fx, fy) via central differences.
 * Clamp-to-border: out-of-bounds reads as 0.0f.
 */
static inline float divergence_2d(const float* fx, const float* fy,
                                   uint32_t x, uint32_t y,
                                   uint32_t W, uint32_t H)
{
    float fx_L = (x > 0)     ? fx[y * W + (x - 1)] : 0.0f;
    float fx_R = (x < W - 1) ? fx[y * W + (x + 1)] : 0.0f;
    float fy_D = (y > 0)     ? fy[(y - 1) * W + x]  : 0.0f;
    float fy_U = (y < H - 1) ? fy[(y + 1) * W + x]  : 0.0f;
    return (fx_R - fx_L + fy_U - fy_D) * 0.5f;
}

#endif /* HEIGHTMAP_OPS_H */
