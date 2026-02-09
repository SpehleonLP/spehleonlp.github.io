#ifndef UTILITY_H
#define UTILITY_H

#include "effect_stack_api.h"

/* Scalar min/max/clamp */
static inline int min_i32(int a, int b) { return a < b? a : b; }
static inline int max_i32(int a, int b) { return a > b? a : b; }
static inline float min_f32(float a, float b) { return a < b? a : b; }
static inline float max_f32(float a, float b) { return a > b? a : b; }
static inline int clamp_i32(int a, int b, int c) { return max_i32(b, min_i32(a, c)); }
static inline float clamp_f32(float a, float b, float c) { return max_f32(b, min_f32(a, c)); }
static inline float lerp_f32(float a, float b, float t) { return a + t * (b - a); }
static inline float unlerp_f32(float a, float b, float v) { return (v - a) / (b - a); }

/* Template macros for vec2 types */
#define DEFINE_VEC2_MINMAX(TYPE, SUFFIX, COMP) \
    static inline TYPE min_##SUFFIX(TYPE a, TYPE b) { \
        return (TYPE){ a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y }; \
    } \
    static inline TYPE max_##SUFFIX(TYPE a, TYPE b) { \
        return (TYPE){ a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y }; \
    } \
    static inline TYPE clamp_##SUFFIX(TYPE v, TYPE lo, TYPE hi) { \
        return max_##SUFFIX(lo, min_##SUFFIX(v, hi)); \
    }

DEFINE_VEC2_MINMAX(vec2, vec2, float)
DEFINE_VEC2_MINMAX(i16vec2, i16vec2, int16_t)
DEFINE_VEC2_MINMAX(u8vec2, u8vec2, uint8_t)

#undef DEFINE_VEC2_MINMAX

/* Template macros for vec3 types */
#define DEFINE_VEC3_MINMAX(TYPE, SUFFIX, COMP) \
    static inline TYPE min_##SUFFIX(TYPE a, TYPE b) { \
        return (TYPE){ a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z }; \
    } \
    static inline TYPE max_##SUFFIX(TYPE a, TYPE b) { \
        return (TYPE){ a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z }; \
    } \
    static inline TYPE clamp_##SUFFIX(TYPE v, TYPE lo, TYPE hi) { \
        return max_##SUFFIX(lo, min_##SUFFIX(v, hi)); \
    }

DEFINE_VEC3_MINMAX(vec3, vec3, float)
DEFINE_VEC3_MINMAX(i16vec3, i16vec3, int16_t)
DEFINE_VEC3_MINMAX(u8vec3, u8vec3, uint8_t)

#undef DEFINE_VEC3_MINMAX

/* Template macros for vec4 types */
#define DEFINE_VEC4_MINMAX(TYPE, SUFFIX, COMP) \
    static inline TYPE min_##SUFFIX(TYPE a, TYPE b) { \
        return (TYPE){ a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, \
                       a.z < b.z ? a.z : b.z, a.w < b.w ? a.w : b.w }; \
    } \
    static inline TYPE max_##SUFFIX(TYPE a, TYPE b) { \
        return (TYPE){ a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, \
                       a.z > b.z ? a.z : b.z, a.w > b.w ? a.w : b.w }; \
    } \
    static inline TYPE clamp_##SUFFIX(TYPE v, TYPE lo, TYPE hi) { \
        return max_##SUFFIX(lo, min_##SUFFIX(v, hi)); \
    }

DEFINE_VEC4_MINMAX(vec4, vec4, float)
DEFINE_VEC4_MINMAX(i16vec4, i16vec4, int16_t)
DEFINE_VEC4_MINMAX(u8vec4, u8vec4, uint8_t)

#undef DEFINE_VEC4_MINMAX

/* Vec3 arithmetic helpers */
static inline vec3 vec3_add(vec3 a, vec3 b) { return (vec3){ a.x+b.x, a.y+b.y, a.z+b.z }; }
static inline vec3 vec3_sub(vec3 a, vec3 b) { return (vec3){ a.x-b.x, a.y-b.y, a.z-b.z }; }
static inline vec3 vec3_scale(vec3 a, float s) { return (vec3){ a.x*s, a.y*s, a.z*s }; }
static inline float vec3_dot(vec3 a, vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static inline vec3 vec3_cross(vec3 a, vec3 b) {
    return (vec3){ a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}
static inline vec3 vec3_normalize(vec3 a) {
    float len = sqrtf(vec3_dot(a, a));
    return len > 1e-12f ? vec3_scale(a, 1.0f / len) : (vec3){0, 0, 0};
}

static inline vec3 scale_normal(vec3 a, float scale) {
	float nx = a.x;
	float ny = a.y;
	float nz = a.z * scale;
	float len = sqrtf(nx * nx + ny * ny + nz * nz);
	if (len > 1e-8f) { nx /= len; ny /= len; nz /= len; }
	return (vec3){nx, ny, nz};				
}

/* Tangent space conversions.
 * reference: the surface normal (unit vector)
 * surface:   the normal to transform (unit vector)
 *
 * to_tangent:   rotates surface into a frame where reference -> (0,0,1)
 * from_tangent: rotates surface from tangent frame back using reference as the new Z
 */
static inline vec3 to_tangent(vec3 reference, vec3 surface) {
    /* Build TBN basis: T, B, N = reference */
    vec3 n = reference;
    /* Pick a non-parallel axis for the cross product */
    vec3 up = fabsf(n.z) < 0.999f ? (vec3){0,0,1} : (vec3){1,0,0};
    vec3 t = vec3_normalize(vec3_cross(up, n));
    vec3 b = vec3_cross(n, t);
    /* Transpose of TBN rotates world -> tangent */
    return (vec3){
        vec3_dot(t, surface),
        vec3_dot(b, surface),
        vec3_dot(n, surface)
    };
}

static inline vec3 from_tangent(vec3 reference, vec3 surface) {
    vec3 n = reference;
    vec3 up = fabsf(n.z) < 0.999f ? (vec3){0,0,1} : (vec3){1,0,0};
    vec3 t = vec3_normalize(vec3_cross(up, n));
    vec3 b = vec3_cross(n, t);
    /* TBN matrix * surface rotates tangent -> world */
    return (vec3){
        t.x*surface.x + b.x*surface.y + n.x*surface.z,
        t.y*surface.x + b.y*surface.y + n.y*surface.z,
        t.z*surface.x + b.z*surface.y + n.z*surface.z
    };
}

/* Unsigned normalized conversions (0-255 <-> 0.0-1.0) */
static inline vec4 vec4_from_u8vec4_unorm(u8vec4 v) {
    return (vec4){
        v.x / 255.0f,
        v.y / 255.0f,
        v.z / 255.0f,
        v.w / 255.0f
    };
}

static inline u8vec4 u8vec4_from_vec4_unorm(vec4 v) {
    return (u8vec4){
        (uint8_t)(clamp_i32(v.x*255 + 0.5f, 0, 255)),
        (uint8_t)(clamp_i32(v.y*255 + 0.5f, 0, 255)),
        (uint8_t)(clamp_i32(v.z*255 + 0.5f, 0, 255)),
        (uint8_t)(clamp_i32(v.w*255 + 0.5f, 0, 255))
    };
}

#endif // UTILITY_H
