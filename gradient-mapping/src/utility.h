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
