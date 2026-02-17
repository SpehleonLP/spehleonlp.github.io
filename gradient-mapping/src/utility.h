#ifndef UTILITY_H
#define UTILITY_H

#include "effect_stack_api.h"
#include <glm/glm.hpp>

/* Scalar min/max/clamp */
static inline int min_i32(int a, int b) { return a < b? a : b; }
static inline int max_i32(int a, int b) { return a > b? a : b; }
static inline float min_f32(float a, float b) { return a < b? a : b; }
static inline float max_f32(float a, float b) { return a > b? a : b; }
static inline int clamp_i32(int a, int b, int c) { return max_i32(b, min_i32(a, c)); }
static inline float clamp_f32(float a, float b, float c) { return max_f32(b, min_f32(a, c)); }
static inline float lerp_f32(float a, float b, float t) { return a + t * (b - a); }
static inline float unlerp_f32(float a, float b, float v) { return (v - a) / (b - a); }

static inline vec3 scale_normal(vec3 a, float scale) {
    a.z *= scale;
    float len = glm::length(a);
    return len > 1e-8f ? a / len : vec3(0);
}

/* Tangent space conversions.
 * reference: the surface normal (unit vector)
 * surface:   the normal to transform (unit vector)
 *
 * to_tangent:   rotates surface into a frame where reference -> (0,0,1)
 * from_tangent: rotates surface from tangent frame back using reference as the new Z
 */
static inline vec3 to_tangent(vec3 reference, vec3 surface) {
    vec3 n = reference;
    vec3 up = fabsf(n.z) < 0.999f ? vec3(0,0,1) : vec3(1,0,0);
    vec3 t = glm::normalize(glm::cross(up, n));
    vec3 b = glm::cross(n, t);
    return vec3(glm::dot(t, surface), glm::dot(b, surface), glm::dot(n, surface));
}

static inline vec3 from_tangent(vec3 reference, vec3 surface) {
    vec3 n = reference;
    vec3 up = fabsf(n.z) < 0.999f ? vec3(0,0,1) : vec3(1,0,0);
    vec3 t = glm::normalize(glm::cross(up, n));
    vec3 b = glm::cross(n, t);
    return t * surface.x + b * surface.y + n * surface.z;
}

/* Unsigned normalized conversions (0-255 <-> 0.0-1.0) */
static inline vec4 vec4_from_u8vec4_unorm(u8vec4 v) {
    return vec4(v) / 255.0f;
}

static inline u8vec4 u8vec4_from_vec4_unorm(vec4 v) {
    vec4 clamped = glm::clamp(v * 255.0f + 0.5f, vec4(0), vec4(255));
    return u8vec4(clamped);
}

#endif // UTILITY_H
