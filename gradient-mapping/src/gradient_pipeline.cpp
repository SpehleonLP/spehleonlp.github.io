// gradient_pipeline.c
// Gradient ramp texture generation pipeline implementation
//
// This file coordinates the gradient pipeline, using the clean implementations
// from sources/. It provides thin wrappers that convert float array parameters
// to the struct-based interfaces expected by the source modules.

#include "gradient_pipeline.h"
#include "image_memo.h"
#include "sources/types.h"
#include "sources/linear_gradient.h"
#include "sources/perlin_noise.h"
#include "sources/curl_noise.h"
#include "sources/noise.h"
#include "sources/worley_noise.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>  // DEBUG

/* Gradient pipeline context */
static GradientPipeline g_gradient_ctx = {0};
static int g_gradient_initialized = 0;

// -----------------------------------------------------------------------------
// Color ramp sampling
// -----------------------------------------------------------------------------

static vec4 sample_ramp(const GPColorRamp *ramp, float t) {
    if (!ramp || ramp->stop_count == 0) {
        return (vec4){ t, t, t, 1.0f };
    }

    t = clampf(t, 0.0f, 1.0f);

    const ColorStop *stops = ramp->stops;
    int count = ramp->stop_count;

    // Before first stop
    if (t <= stops[0].position) {
        return stops[0].color;
    }

    // After last stop
    if (t >= stops[count - 1].position) {
        return stops[count - 1].color;
    }

    // Interpolate between stops
    for (int i = 0; i < count - 1; i++) {
        if (t >= stops[i].position && t <= stops[i+1].position) {
            float range = stops[i+1].position - stops[i].position;
            float local_t = (range > 0.0001f) ? (t - stops[i].position) / range : 0.0f;
            return (vec4){
                lerpf(stops[i].color.x, stops[i+1].color.x, local_t),
                lerpf(stops[i].color.y, stops[i+1].color.y, local_t),
                lerpf(stops[i].color.z, stops[i+1].color.z, local_t),
                lerpf(stops[i].color.w, stops[i+1].color.w, local_t)
            };
        }
    }

    // Fallback
    return (vec4){ t, t, t, 1.0f };
}

// -----------------------------------------------------------------------------
// Blend modes
// -----------------------------------------------------------------------------

static vec4 blend_colors(vec4 dst, vec4 src, int mode, float opacity) {
    // Pre-multiply source by opacity
    src.x *= opacity;
    src.y *= opacity;
    src.z *= opacity;
    src.w *= opacity;

    vec4 result;

    switch (mode) {
    case 0: // Normal
        result.x = src.x + dst.x * (1.0f - src.w);
        result.y = src.y + dst.y * (1.0f - src.w);
        result.z = src.z + dst.z * (1.0f - src.w);
        result.w = src.w + dst.w * (1.0f - src.w);
        break;

    case 1: // Multiply
        result.x = dst.x * src.x;
        result.y = dst.y * src.y;
        result.z = dst.z * src.z;
        result.w = src.w + dst.w * (1.0f - src.w);
        break;

    case 2: // Screen
        result.x = 1.0f - (1.0f - dst.x) * (1.0f - src.x);
        result.y = 1.0f - (1.0f - dst.y) * (1.0f - src.y);
        result.z = 1.0f - (1.0f - dst.z) * (1.0f - src.z);
        result.w = src.w + dst.w * (1.0f - src.w);
        break;

    case 3: // Overlay
        result.x = dst.x < 0.5f ? 2.0f * dst.x * src.x : 1.0f - 2.0f * (1.0f - dst.x) * (1.0f - src.x);
        result.y = dst.y < 0.5f ? 2.0f * dst.y * src.y : 1.0f - 2.0f * (1.0f - dst.y) * (1.0f - src.y);
        result.z = dst.z < 0.5f ? 2.0f * dst.z * src.z : 1.0f - 2.0f * (1.0f - dst.z) * (1.0f - src.z);
        result.w = src.w + dst.w * (1.0f - src.w);
        break;

    case 4: // Add
        result.x = clampf(dst.x + src.x, 0.0f, 1.0f);
        result.y = clampf(dst.y + src.y, 0.0f, 1.0f);
        result.z = clampf(dst.z + src.z, 0.0f, 1.0f);
        result.w = clampf(dst.w + src.w, 0.0f, 1.0f);
        break;

    case 5: // Subtract
        result.x = clampf(dst.x - src.x, 0.0f, 1.0f);
        result.y = clampf(dst.y - src.y, 0.0f, 1.0f);
        result.z = clampf(dst.z - src.z, 0.0f, 1.0f);
        result.w = dst.w;
        break;

    default:
        result = src;
    }

    return result;
}

// -----------------------------------------------------------------------------
// Pipeline lifecycle
// -----------------------------------------------------------------------------

int gp_init(GradientPipeline *ctx, int width, int height, uint32_t seed) {
    if (!ctx || width <= 0 || height <= 0) return -1;

    int size = width * height;

    ctx->front = (vec3 *)calloc(size, sizeof(vec3));
    ctx->back = (vec3 *)calloc(size, sizeof(vec3));
    ctx->output = (vec4 *)calloc(size, sizeof(vec4));

    if (!ctx->front || !ctx->back || !ctx->output) {
        gp_free(ctx);
        return -1;
    }

    ctx->width = width;
    ctx->height = height;
    ctx->seed = seed;
    ctx->has_output = 0;

    return 0;
}

void gp_free(GradientPipeline *ctx) {
    if (!ctx) return;
    free(ctx->front);
    free(ctx->back);
    free(ctx->output);
    ctx->front = NULL;
    ctx->back = NULL;
    ctx->output = NULL;
}

void gp_reset(GradientPipeline *ctx) {
    if (!ctx) return;
    int size = ctx->width * ctx->height;
    memset(ctx->front, 0, size * sizeof(vec3));
    memset(ctx->back, 0, size * sizeof(vec3));
    memset(ctx->output, 0, size * sizeof(vec4));
    ctx->has_output = 0;
}

// -----------------------------------------------------------------------------
// Source application
//
// Takes the Effect struct directly from effect_stack_api.c
// Maps API param structs to the sources/ param structs
// -----------------------------------------------------------------------------

void gp_apply_source(GradientPipeline *ctx, const Effect *e) {
    if (!ctx || !ctx->front || !ctx->back || !e) return;

    // Swap buffers: previous front becomes back (input for perturbation)
    vec3 *temp = ctx->back;
    ctx->back = ctx->front;
    ctx->front = temp;

    // Dispatch to appropriate source
    switch (e->effect_id) {
    case EFFECT_SOURCE_GRADIENT: {
        // GradientSource -> LinearGradientParams (same fields)
        LinearGradientParams p = {
            .angle = e->params.gradient_source.angle,
            .scale = e->params.gradient_source.scale,
            .offset = e->params.gradient_source.offset
        };
        linear_gradient(ctx->front, ctx->back, ctx->width, ctx->height, p, ctx->seed);
        break;
    }
    case EFFECT_SOURCE_WORLEY: {
        // WorleySource -> WorleyNoiseParams
        WorleyNoiseParams p = {
            .scale = e->params.worley_source.scale,
            .jitter = e->params.worley_source.jitter,
            .metric = (WorleyMetric)e->params.worley_source.metric,
            .mode = (WorleyMode)e->params.worley_source.mode
        };
        worley_noise(ctx->front, ctx->back, ctx->width, ctx->height, p, ctx->seed);
        break;
    }
    case EFFECT_SOURCE_PERLIN: {
        // PerlinSource -> PerlinNoiseParams
        PerlinNoiseParams p = {
            .scale = e->params.perlin_source.scale,
            .octaves = (float)e->params.perlin_source.octaves,
            .persistence = e->params.perlin_source.persistence,
            .lacunarity = e->params.perlin_source.lacunarity
        };
        perlin_noise(ctx->front, ctx->back, ctx->width, ctx->height, p, ctx->seed);
        break;
    }
    case EFFECT_SOURCE_CURL: {
        // CurlSource -> CurlNoiseParams
        CurlNoiseParams p = {
            .scale = e->params.curl_source.scale,
            .octaves = (float)e->params.curl_source.octaves,
            .persistence = e->params.curl_source.persistence,
            .lacunarity = e->params.curl_source.lacunarity
        };
        curl_noise(ctx->front, ctx->back, ctx->width, ctx->height, p, ctx->seed);
        break;
    }
    case EFFECT_SOURCE_NOISE: {
        // NoiseSource -> NoiseParams
        NoiseParams p = {
            .type = (NoiseType)e->params.noise_source.type,
            .scale = e->params.noise_source.scale,
            .seed = (float)e->params.noise_source.seed
        };
        noise_generate(ctx->front, ctx->back, ctx->width, ctx->height, p, ctx->seed);
        break;
    }
    default:
        // Unknown source, leave front unchanged
        break;
    }
}

// -----------------------------------------------------------------------------
// Color ramp application
// -----------------------------------------------------------------------------

void gp_apply_color_ramp(GradientPipeline *ctx, const GPColorRamp *ramp) {
    if (!ctx || !ctx->front || !ctx->output) {
        return;
    }

    int size = ctx->width * ctx->height;

    for (int i = 0; i < size; i++) {
        float t = ctx->front[i].z;  // Value channel
        vec4 color = sample_ramp(ramp, t);

            // Blend with existing output
            ctx->output[i] = blend_colors(ctx->output[i], color,
                                          ramp->blend_mode, ramp->opacity);
    }

    ctx->has_output = 1;

    // Clear front/back for next source chain
    memset(ctx->front, 0, size * sizeof(vec3));
    memset(ctx->back, 0, size * sizeof(vec3));
}

// -----------------------------------------------------------------------------
// Finalization
// -----------------------------------------------------------------------------

// Static buffer for uint8 output
static uint8_t *g_output_rgba = NULL;
static int g_output_size = 0;

uint8_t *gp_finalize(GradientPipeline *ctx) {
    if (!ctx) return NULL;

    int size = ctx->width * ctx->height;

    // Ensure output buffer is large enough
    if (g_output_size < size * 4) {
        free(g_output_rgba);
        g_output_rgba = (uint8_t *)malloc(size * 4);
        g_output_size = size * 4;
    }

    if (!g_output_rgba) return NULL;

    if (ctx->has_output == 2) {
        // Sources applied but no backdrop and no color ramp - convert front.z to greyscale
        for (int i = 0; i < size; i++) {
            uint8_t v = (uint8_t)(clampf(ctx->front[i].z, 0, 1) * 255.0f);
            g_output_rgba[i * 4 + 0] = v;
            g_output_rgba[i * 4 + 1] = v;
            g_output_rgba[i * 4 + 2] = v;
            g_output_rgba[i * 4 + 3] = 255;
        }
    }
    else if (ctx->has_output > 0)
    {
        // Has backdrop or color ramp applied - use output buffer
        for (int i = 0; i < size; i++) {
            g_output_rgba[i * 4 + 0] = (uint8_t)(clampf(ctx->output[i].x, 0, 1) * 255.0f);
            g_output_rgba[i * 4 + 1] = (uint8_t)(clampf(ctx->output[i].y, 0, 1) * 255.0f);
            g_output_rgba[i * 4 + 2] = (uint8_t)(clampf(ctx->output[i].z, 0, 1) * 255.0f);
            g_output_rgba[i * 4 + 3] = (uint8_t)(clampf(ctx->output[i].w, 0, 1) * 255.0f);
        }
    }
    else
    {
        // No output at all - return transparent black
        memset(g_output_rgba, 0, size * 4);
    }

    return g_output_rgba;
}

/* =========================================================================
 * Gradient pipeline helpers
 * ========================================================================= */

static int ensure_gradient_pipeline(int W, int H) {
    if (!g_gradient_initialized || g_gradient_ctx.width != W || g_gradient_ctx.height != H) {
        if (g_gradient_initialized) {
            gp_free(&g_gradient_ctx);
        }
        if (gp_init(&g_gradient_ctx, W, H, 42) != 0) {
            g_gradient_initialized = 0;
            return 0;
        }
        g_gradient_initialized = 1;
    }
    return 1;
}

/* Convert internal ColorRamp to pipeline GPColorRamp */
static GPColorRamp convert_color_ramp(const ColorRamp* src, int blend_mode, float opacity) {
    GPColorRamp ramp = {0};
    if (!src || !src->stops || src->length == 0) return ramp;

    /* GPColorRamp uses the same ColorStop type - just copy the pointer info */
    ramp.stops = src->stops;
    ramp.stop_count = src->length;
    ramp.blend_mode = blend_mode;
    ramp.opacity = opacity;

    return ramp;
}

uint8_t* process_gradient_stack(Effect const* effects, int effect_count, int* out_w, int* out_h) {
    int W = DEFAULT_GRADIENT_W;
    int H = DEFAULT_GRADIENT_H;

    /* Check if there's a memoized image source */
    const GradientImageMemo* memo = memo_get_gradient();
    int has_backdrop = (memo && memo->image.colors && memo->image.width > 0 && memo->image.height > 0);
    
    if (has_backdrop) {
        W = memo->image.width;
        H = memo->image.height;
	}

    /* Procedural generation path (no image source) */
    if (effect_count == 0 && !has_backdrop) {
        /* No effects and no image - return error */
        js_post_error(ERROR_NO_SOURCE, -1, -1, "no source - load image or add effects");
        *out_w = 0;
        *out_h = 0;
        return NULL;
    }

    /* Procedural generation path */

	/* Ensure pipeline is initialized */
	if (!ensure_gradient_pipeline(W, H)) {
		js_post_error(ERROR_ALLOC, -1, -1, "failed to allocate gradient pipeline");
		*out_w = 0;
		*out_h = 0;
		return NULL;
	}

	/* Reset pipeline for new generation */
	gp_reset(&g_gradient_ctx);

	if (has_backdrop) {
        int pixel_count = W * H;
        for (int i = 0; i < pixel_count; i++) {
            u8vec4 c = memo->image.colors[i];
            g_gradient_ctx.output[i].x = c.x / 255.f;
            g_gradient_ctx.output[i].y = c.y / 255.f;
            g_gradient_ctx.output[i].z = c.z / 255.f;
            g_gradient_ctx.output[i].w = c.w / 255.f;
        }
        g_gradient_ctx.has_output = 1;
    }
    
    /* Process effects in order */
    int current_blend_mode = 0;
    float current_opacity = 1.0f;

    for (int i = 0; i < effect_count; i++) {
        Effect const* e = &effects[i];

        /* Data sources (0x10-0x1F) */
        if (e->effect_id >= 0x10 && e->effect_id < 0x20) {
            gp_apply_source(&g_gradient_ctx, e);
            // Source is now the last thing on stack - show front buffer
            g_gradient_ctx.has_output = 2;
        }
        /* Blend mode - track for next color ramp */
        else if (e->effect_id == EFFECT_BLEND_MODE) {
            current_blend_mode = e->params.blend_mode.mode;
            current_opacity = e->params.blend_mode.opacity;
        }
        /* Color ramp - apply to current sources, blend to output */
        else if (e->effect_id == EFFECT_COLOR_RAMP) {
            GPColorRamp ramp = convert_color_ramp(&e->params.color_ramp,
                                                   current_blend_mode, current_opacity);
            gp_apply_color_ramp(&g_gradient_ctx, &ramp);
            /* Note: ramp.stops points to e->params.color_ramp.stops (not malloc'd here) */
            /* Reset blend mode for next chain */
            current_blend_mode = 0;
            current_opacity = 1.0f;
        }
    }

    /* Finalize and get output */
    uint8_t* result = gp_finalize(&g_gradient_ctx);

    *out_w = W;
    *out_h = H;
    return result;
}
