#ifndef EFFECT_STACK_API_H
#define EFFECT_STACK_API_H

/*
 * Effect Stack API — WASM exports for the prototype UI.
 * See docs/plans/2026-01-24-effect-stack-architecture.md
 *
 * Each effect module implements two functions side-by-side:
 *   - xxx_write_catalog(FILE* f)   — writes this effect's JSON catalog entry
 *   - xxx_process(const float* params, int count, ...)  — unpacks and applies
 *
 * The JS worker calls these exports via cwrap. The stack_begin / push_effect /
 * stack_end sequence is the hot path; init_catalog and analyze_source are
 * called once per source load.
 */

#include <stdint.h>
#include <math.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT
#endif

#define VEC2_DEFINED
typedef struct { float x, y; } vec2;
#define VEC3_DEFINED
typedef struct { float x,y,z; } vec3;
typedef struct { float x,y,z,w; } vec4;

typedef struct { int16_t x, y; } i16vec2;
typedef struct { int16_t x,y,z; } i16vec3;
typedef struct { int16_t x,y,z,w; } i16vec4;
	
typedef struct { uint8_t x, y; } u8vec2;
typedef struct { uint8_t x,y,z; } u8vec3;
typedef struct { uint8_t x,y,z,w; } u8vec4;

typedef struct { float position; vec4 color; } ColorStop;

/* =========================================================================
 * Uint8 parameter unpacking helpers
 * All UI params are 0-255; these unpack to actual ranges.
 * ========================================================================= */

static inline float unpack_linear01(uint8_t u) {
    return u / 255.0f;
}

static inline float unpack_linear_signed(uint8_t u) {
    return (u / 255.0f) * 2.0f - 1.0f;
}

static inline float unpack_angle(uint8_t u) {
    return (u / 255.0f) * 6.28318530718f - 3.14159265359f;
}

static inline float unpack_linear_range(uint8_t u, float min_val, float max_val) {
    return min_val + (u / 255.0f) * (max_val - min_val);
}

static inline float unpack_log_range(uint8_t u, float min_val, float max_val) {
    return min_val * powf(max_val / min_val, u / 255.0f);
}

static inline int unpack_int_range(uint8_t u, int min_val, int max_val) {
    return min_val + (int)roundf((u / 255.0f) * (max_val - min_val));
}

static inline int unpack_enum(uint8_t u, int max_val) {
    return u <= max_val ? u : max_val;
}

static inline uint32_t unpack_seed(uint8_t u) {
    return (uint32_t)u * 3922u;
}

/* =========================================================================
 * Stack type constants
 * ========================================================================= */

typedef enum {
	STACK_GRADIENT,  
	STACK_EROSION,   
	STACK_TYPE_TOTAL
} STACK_TYPE;

/* =========================================================================
 * Error codes (passed to js_post_error)
 * ========================================================================= */

typedef enum {
    ERROR_NONE              = 0,
    ERROR_UNKNOWN_EFFECT    = 1,   /* effect_id not in catalog */
    ERROR_PARAM_COUNT       = 2,   /* wrong number of params for effect */
    ERROR_PARAM_RANGE       = 3,   /* param value outside [min, max] */
    ERROR_STACK_FULL        = 4,   /* too many effects pushed */
    ERROR_NO_SOURCE         = 5,   /* stack_end called with no source loaded */
    ERROR_SOURCE_READ       = 6,   /* failed to read source from VFS */
    ERROR_ALLOC             = 7,   /* memory allocation failed */
} ErrorCode;

/* =========================================================================
 * Effect IDs (must match catalog JSON)
 * ========================================================================= */

typedef enum {
    /* Data sources (produce scalar/vec2 fields, can domain-warp) */
    EFFECT_SOURCE_GRADIENT      = 0x10,
    EFFECT_SOURCE_WORLEY        = 0x11,
    EFFECT_SOURCE_PERLIN        = 0x12,
    EFFECT_SOURCE_CURL          = 0x13,
    EFFECT_SOURCE_NOISE         = 0x14,

    /* Erosion stack effects (image processing) */
    EFFECT_DIJKSTRA             = 0x20,
    EFFECT_FOURIER_CLAMP        = 0x21,
    EFFECT_BOX_BLUR             = 0x22,
    EFFECT_GRADIENTIFY          = 0x23,
    EFFECT_POISSON_SOLVE        = 0x24,
    EFFECT_LAMINARIZE           = 0x25,

    /* Gradient stack specifics */
    EFFECT_COLOR_RAMP           = 0x30,
    EFFECT_BLEND_MODE           = 0x31,

    /* Debug commands (CLI only, produce debug PNGs) */
    EFFECT_DEBUG_HESSIAN_FLOW   = 0x40,
    EFFECT_DEBUG_SPLIT_CHANNELS = 0x41,
    EFFECT_DEBUG_LIC            = 0x42,
} EffectId;

// parameters for erosion source
typedef struct
{
	float quatization; // how much we bit crush the source (e.g. jpeg artifacts can screw us up.)
} ErosionSourceTexture;

/* =========================================================================
 * Data source structs (produce scalar/vec2 fields, can domain-warp)
 * ========================================================================= */

typedef struct
{
	float angle;      // rotation in radians
	float scale;      // frequency
	float offset;     // phase shift
} GradientSource;

typedef struct
{
	float scale;      // cell size
	float jitter;     // randomness 0-1
	int   metric;     // 0=euclidean, 1=manhattan, 2=chebyshev
	int   mode;       // 0=F1, 1=F2, 2=F2-F1
} WorleySource;

typedef struct
{
	float scale;       // base frequency
	int   octaves;     // layers of detail
	float persistence; // amplitude falloff per octave
	float lacunarity;  // frequency multiplier per octave
} PerlinSource;

typedef struct
{
	float scale;       // base frequency
	int   octaves;
	float persistence;
	float lacunarity;
} CurlSource;

typedef struct
{
	int   type;       // 0=white, 1=blue, 2=value
	float scale;
	uint32_t seed;
} NoiseSource;

 
/* =========================================================================
 * Effect parameter structs (one per effect type)
 * ========================================================================= */

typedef struct
{
	float Minkowski; // -10 to +10, (use exp2(minkwoski))
	float Chebyshev;
} DijkstraParams;

typedef struct
{
	float Maximum; 
	float Minimum;
} FourierClamp; 

typedef struct {
    float iterations;
    float threshold;
} BoxBlurParams;

/* Converters */

/* Convert height field to normal map before perturbation */
typedef struct
{
	float scale;      // height-to-normal sensitivity
} GradientifyParams;

/* Convert normal map back to height (auto-inserted after Gradientify if needed) */
typedef struct
{
	int iterations;   // solver iterations
} PoissonSolveParams;

/* Helmholtz-style divergence redistribution on normal maps */
typedef struct
{
	float scale;      // normal Z scaling for target divergence (0.01-10)
	float strength;   // blend: 0=unchanged, 1=full correction
	float blur_sigma; // Gaussian sigma for magnitude blur (0-5)
} LaminarizeParams;


/* =========================================================================
 * Debug command parameter structs (CLI only)
 * ========================================================================= */

typedef enum { LIC_FIELD_NORMAL=0, LIC_FIELD_TANGENT=1, LIC_FIELD_BITANGENT=2 } LicVectorField;

typedef struct {
    int kernel_size;  // 3 or 5
} DebugHessianFlowParams;

/* No params needed for EFFECT_DEBUG_SPLIT_CHANNELS */

typedef struct {
    LicVectorField vector_field;
    float kernel_length;  // half-length in pixels
    float step_size;      // Euler step
} DebugLicParams;

/* =========================================================================
 * Gradient parameter structs (one per effect type)
 * ========================================================================= */

// gradient stacks go [data source]+ -> color ramp -> blend mode, and cycle that pattern.
// if one data source gets added to another it preturbs the input tex coordinate.

// special struct needs special memory handling..., maps [0,1] onto a color ramp.
typedef struct
{
	ColorStop* stops;
	int length;
} ColorRamp;

typedef struct
{
	int   mode;       // 0=normal, 1=multiply, 2=screen, 3=overlay, 4=add, 5=subtract
	float opacity;    // 0-1
} BlendModeParams;


/* =========================================================================
 * Tagged union for validated effects
 * ========================================================================= */

typedef struct {
    int effect_id;
    union {
        /* Image source (shared by both stacks) */
        ErosionSourceTexture erosion_source;
        /* Data sources */
        GradientSource      gradient_source;
        WorleySource        worley_source;
        PerlinSource        perlin_source;
        CurlSource          curl_source;
        NoiseSource         noise_source;
        /* Erosion effects */
        DijkstraParams      dijkstra;
        FourierClamp        fourier_clamp;
        BoxBlurParams       box_blur;
        GradientifyParams   gradientify;
        PoissonSolveParams  poisson_solve;
        LaminarizeParams    laminarize;
        /* Debug commands */
        DebugHessianFlowParams debug_hessian_flow;
        DebugLicParams         debug_lic;
        /* Gradient stack */
        ColorRamp           color_ramp;      /* note: stops is malloc'd */
        BlendModeParams     blend_mode;
    } params;
} Effect;

#define MAX_STACK_SIZE 32

/* =========================================================================
 * Catalog generation
 *
 * Called once at startup. Each effect module writes its JSON descriptor to
 * the file. The coordinator assembles the full catalog structure.
 *
 * After this call, /effect_catalog.json exists on the emscripten VFS.
 * JS reads it with FS.readFile('/effect_catalog.json').
 * ========================================================================= */

#ifdef __cplusplus
extern "C" {
#endif

EXPORT void init_catalog(void);

/* =========================================================================
 * Source management
 *
 * Sources are files on the emscripten VFS (written by JS via FS.writeFile).
 * set_source_path tells WASM where the file is. set_source_changed tells
 * WASM whether to re-read or use memoized data.
 * ========================================================================= */

EXPORT void set_source_path(int stack_type, const char* vfs_path);
EXPORT void set_source_changed(int stack_type, int changed);

/* =========================================================================
 * Stack execution (JS -> WASM)
 *
 * Called on every UI change. JS iterates enabled effects top-to-bottom,
 * calling push_effect for each. stack_end processes the chain and returns
 * a pointer to an RGBA image buffer.
 *
 * Usage:
 *   stack_begin(STACK_EROSION);
 *   push_effect(100, blur_params, 2);
 *   push_effect(101, fluid_params, 1);
 *   uint8_t* img = stack_end(&w, &h);
 *   // img points to w*h*4 bytes of RGBA data (owned by WASM, valid until
 *   // next stack_begin or shutdown)
 * ========================================================================= */

EXPORT void     stack_begin(int stack_type);
EXPORT void     push_effect(int effect_id, const uint8_t* params, int param_count);
EXPORT uint8_t* stack_end(int* out_w, int* out_h);
EXPORT void     debug_print_stack(int stack_type);

/* =========================================================================
 * Source analysis and auto-configuration (WASM -> JS)
 *
 * analyze_source loads and examines the source image (frame count, banding
 * estimate, etc.) and calls back into JS to recommend effects. The callbacks
 * fire synchronously from C's perspective; the worker's JS implementations
 * forward them via postMessage.
 *
 * Parameters for erosion stack (ErosionSourceTexture):
 *   params[0] = quantization (0.0 = no bit crushing)
 *
 * Parameters for gradient stack: none (pass NULL, 0)
 *
 * Call after set_source_path().
 * ========================================================================= */

EXPORT void analyze_source(int stack_type, const float* params, int param_count);

/* =========================================================================
 * JS callbacks (imported functions)
 *
 * These are implemented in JS (worker.js) and linked via emscripten imports.
 * C code calls them synchronously during analyze_source() or push_effect().
 *
 * js_clear_auto_effects: Remove all auto-placed effects from the UI stack.
 * js_push_auto_effect:   Insert a recommended effect at position 1 (after
 *                        source) with the given params. Marked data-auto
 *                        in the DOM.
 * js_post_error:         Report an error from C to JS. Called during
 *                        push_effect when validation fails. JS side has
 *                        context about which UI element triggered this.
 *
 *   error_code: One of the ERROR_* constants below
 *   effect_id:  Which effect caused the error (-1 if not effect-specific)
 *   param_idx:  Which parameter index failed (-1 if not param-specific)
 *   message:    Human-readable error description (static string, don't free)
 * ========================================================================= */

extern void js_clear_auto_effects(int stack_type);
extern void js_push_auto_effect(int stack_type, int effect_id,
                                const uint8_t* params, int param_count);
extern void js_post_error(int error_code, int effect_id, int param_idx,
                          const char* message);
extern void js_set_source_timing(int stack_type, float fade_in_time,
                                 float fade_out_time, float total_duration);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* =========================================================================
 * Per-module catalog writers
 *
 * Each effect module provides a function to write its JSON catalog entry.
 * Called by init_catalog(). The writer must output valid JSON (no trailing
 * commas). The coordinator handles array separators.
 *
 * Example implementation in smart_blur.c:
 *
 *   void smart_blur_write_catalog(FILE* f) {
 *       fprintf(f,
 *           "{\"id\":100,\"name\":\"Smart Blur\",\"params\":["
 *               "{\"name\":\"Iterations\",\"type\":\"float\","
 *                "\"min\":1,\"max\":500,\"default\":200,\"step\":1},"
 *               "{\"name\":\"Threshold\",\"type\":\"float\","
 *                "\"min\":0.001,\"max\":1,\"default\":0.01,\"step\":0.001}"
 *           "]}");
 *   }
 *
 * Add declarations here as modules are implemented.
 * ========================================================================= */

/* #include <stdio.h> */
/* void smart_blur_write_catalog(FILE* f); */
/* void fluid_interp_write_catalog(FILE* f); */
/* void brightness_contrast_write_catalog(FILE* f); */
/* void gradient_layer_write_catalog(FILE* f); */

#endif /* EFFECT_STACK_API_H */
