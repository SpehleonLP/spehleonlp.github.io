#include "effect_stack_api.h"
#include "erosion_pipeline.h"
#include "gradient_pipeline.h"
#include "image_memo/image_memo.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>  // DEBUG
#include <stdbool.h>

/* =========================================================================
 * Internal state
 * ========================================================================= */

static struct {
    Effect effects[MAX_STACK_SIZE];
    int count;
    int stack_type;
    char source_path[256];
    bool source_changed;
    bool source_path_changed;
    ErosionSourceTexture source_params;
} g_stacks[STACK_TYPE_TOTAL];  /* [STACK_GRADIENT] and [STACK_EROSION] */

static int g_current_stack = -1;


/* =========================================================================
 * Memory management for ColorRamp stops
 * ========================================================================= */

static void free_stack_color_ramps(int stack_idx) {
    for (int i = 0; i < g_stacks[stack_idx].count; i++) {
        Effect* e = &g_stacks[stack_idx].effects[i];
        if (e->effect_id == EFFECT_COLOR_RAMP && e->params.color_ramp.stops) {
            free(e->params.color_ramp.stops);
            e->params.color_ramp.stops = NULL;
        }
    }
}

/* =========================================================================
 * Param validation helpers
 * ========================================================================= */

static int validate_param_count(int effect_id, int got, int expected) {
    if (got != expected) {
        js_post_error(ERROR_PARAM_COUNT, effect_id, -1,
            "wrong number of parameters");
        return 0;
    }
    return 1;
}

/* validate_range removed - unpacking handles range conversion */

/* =========================================================================
 * Effect-specific validation and parsing
 * ========================================================================= */

/* --- Data Sources --- */

static int parse_gradient_source(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_SOURCE_GRADIENT, n, 3)) return 0;
    out->effect_id = EFFECT_SOURCE_GRADIENT;
    out->params.gradient_source.angle = unpack_angle(p[0]);
    out->params.gradient_source.scale = unpack_log_range(p[1], 0.1f, 10.0f);
    out->params.gradient_source.offset = unpack_linear_signed(p[2]);
    return 1;
}

static int parse_worley_source(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_SOURCE_WORLEY, n, 4)) return 0;
    out->effect_id = EFFECT_SOURCE_WORLEY;
    out->params.worley_source.scale = unpack_log_range(p[0], 1.0f, 100.0f);
    out->params.worley_source.jitter = unpack_linear01(p[1]);
    out->params.worley_source.metric = unpack_enum(p[2], 2);
    out->params.worley_source.mode = unpack_enum(p[3], 2);
    return 1;
}

static int parse_perlin_source(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_SOURCE_PERLIN, n, 4)) return 0;
    out->effect_id = EFFECT_SOURCE_PERLIN;
    out->params.perlin_source.scale = unpack_log_range(p[0], 1.0f, 100.0f);
    out->params.perlin_source.octaves = unpack_int_range(p[1], 1, 16);
    out->params.perlin_source.persistence = unpack_linear01(p[2]);
    out->params.perlin_source.lacunarity = unpack_linear_range(p[3], 1.0f, 4.0f);
    return 1;
}

static int parse_curl_source(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_SOURCE_CURL, n, 4)) return 0;
    out->effect_id = EFFECT_SOURCE_CURL;
    out->params.curl_source.scale = unpack_log_range(p[0], 1.0f, 100.0f);
    out->params.curl_source.octaves = unpack_int_range(p[1], 1, 16);
    out->params.curl_source.persistence = unpack_linear01(p[2]);
    out->params.curl_source.lacunarity = unpack_linear_range(p[3], 1.0f, 4.0f);
    return 1;
}

static int parse_noise_source(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_SOURCE_NOISE, n, 3)) return 0;
    out->effect_id = EFFECT_SOURCE_NOISE;
    out->params.noise_source.type = unpack_enum(p[0], 2);
    out->params.noise_source.scale = unpack_log_range(p[1], 1.0f, 100.0f);
    out->params.noise_source.seed = unpack_seed(p[2]);
    return 1;
}

/* --- Erosion Effects --- */

static int parse_dijkstra(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_DIJKSTRA, n, 2)) return 0;
    out->effect_id = EFFECT_DIJKSTRA;
    out->params.dijkstra.Minkowski = unpack_linear_range(p[0], -10.0f, 10.0f);
    out->params.dijkstra.Chebyshev = unpack_linear01(p[1]);
    return 1;
}

static int parse_fourier_clamp(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_FOURIER_CLAMP, n, 2)) return 0;
    out->effect_id = EFFECT_FOURIER_CLAMP;
    out->params.fourier_clamp.Minimum = unpack_linear01(p[0]);
    out->params.fourier_clamp.Maximum = unpack_linear01(p[1]);
    return 1;
}

static int parse_box_blur(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_BOX_BLUR, n, 2)) return 0;
    out->effect_id = EFFECT_BOX_BLUR;
    out->params.box_blur.iterations = unpack_log_range(p[0], 1.0f, 500.0f);
    out->params.box_blur.threshold = unpack_log_range(p[1], 0.001f, 1.0f);
    return 1;
}

static int parse_gradientify(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_GRADIENTIFY, n, 1)) return 0;
    out->effect_id = EFFECT_GRADIENTIFY;
    out->params.gradientify.scale = unpack_log_range(p[0], 0.1f, 10.0f);
    return 1;
}

static int parse_poisson_solve(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_POISSON_SOLVE, n, 1)) return 0;
    out->effect_id = EFFECT_POISSON_SOLVE;
    out->params.poisson_solve.iterations = (int)unpack_log_range(p[0], 1.0f, 1000.0f);
    return 1;
}

static int parse_laminarize(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_LAMINARIZE, n, 3)) return 0;
    out->effect_id = EFFECT_LAMINARIZE;
    out->params.laminarize.scale = unpack_log_range(p[0], 0.01f, 10.0f);
    out->params.laminarize.strength = unpack_linear01(p[1]);
    out->params.laminarize.blur_sigma = unpack_linear_range(p[2], 0.0f, 5.0f);
    return 1;
}

/* --- Debug Commands --- */

static int parse_debug_hessian_flow(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_DEBUG_HESSIAN_FLOW, n, 1)) return 0;
    out->effect_id = EFFECT_DEBUG_HESSIAN_FLOW;
    out->params.debug_hessian_flow.kernel_size = unpack_enum(p[0], 1) == 0 ? 3 : 5;
    return 1;
}

static int parse_debug_split_channels(const uint8_t* p, int n, Effect* out) {
    (void)p;
    if (!validate_param_count(EFFECT_DEBUG_SPLIT_CHANNELS, n, 0)) return 0;
    out->effect_id = EFFECT_DEBUG_SPLIT_CHANNELS;
    return 1;
}

static int parse_debug_lic(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_DEBUG_LIC, n, 3)) return 0;
    out->effect_id = EFFECT_DEBUG_LIC;
    out->params.debug_lic.vector_field = (LicVectorField)unpack_enum(p[0], 2);
    out->params.debug_lic.kernel_length = unpack_log_range(p[1], 1.0f, 50.0f);
    out->params.debug_lic.step_size = unpack_linear_range(p[2], 0.1f, 2.0f);
    return 1;
}

static int parse_debug_laplacian(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_DEBUG_LAPLACIAN, n, 1)) return 0;
    out->effect_id = EFFECT_DEBUG_LAPLACIAN;
    out->params.debug_laplacian.kernel_size = unpack_enum(p[0], 1) == 0 ? 3 : 5;
    return 1;
}

static int parse_debug_ridge_mesh(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_DEBUG_RIDGE_MESH, n, 3)) return 0;
    out->effect_id = EFFECT_DEBUG_RIDGE_MESH;
    out->params.debug_ridge_mesh.normal_scale = unpack_log_range(p[0], 0.001f, 100.0f);
    out->params.debug_ridge_mesh.high_threshold = unpack_linear01(p[1]);
    out->params.debug_ridge_mesh.low_threshold = unpack_linear01(p[2]);
    return 1;
}

/* --- Gradient Stack --- */

/*
 * ColorRamp params layout: [stop_count, pos0, r0, g0, b0, a0, pos1, r1, g1, b1, a1, ...]
 * So total uint8s = 1 + stop_count * 5
 */
static int parse_color_ramp(const uint8_t* p, int n, Effect* out) {
    if (n < 1) {
        js_post_error(ERROR_PARAM_COUNT, EFFECT_COLOR_RAMP, -1, "missing stop count");
        return 0;
    }
    int stop_count = p[0];
    int expected = 1 + stop_count * 5;
    if (!validate_param_count(EFFECT_COLOR_RAMP, n, expected)) return 0;
    if (stop_count < 1 || stop_count > 64) {
        js_post_error(ERROR_PARAM_RANGE, EFFECT_COLOR_RAMP, 0, "stop_count");
        return 0;
    }

    ColorStop* stops = (ColorStop*)malloc(sizeof(ColorStop) * stop_count);
    if (!stops) {
        js_post_error(ERROR_ALLOC, EFFECT_COLOR_RAMP, -1, "failed to allocate stops");
        return 0;
    }

    for (int i = 0; i < stop_count; i++) {
        int base = 1 + i * 5;
        stops[i].position = unpack_linear01(p[base]);
        stops[i].color.x = unpack_linear01(p[base + 1]);
        stops[i].color.y = unpack_linear01(p[base + 2]);
        stops[i].color.z = unpack_linear01(p[base + 3]);
        stops[i].color.w = unpack_linear01(p[base + 4]);
    }

    out->effect_id = EFFECT_COLOR_RAMP;
    out->params.color_ramp.stops = stops;
    out->params.color_ramp.length = stop_count;
    return 1;
}

static int parse_blend_mode(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_BLEND_MODE, n, 2)) return 0;
    out->effect_id = EFFECT_BLEND_MODE;
    out->params.blend_mode.mode = unpack_enum(p[0], 5);
    out->params.blend_mode.opacity = unpack_linear01(p[1]);
    return 1;
}

/* =========================================================================
 * API implementation
 * ========================================================================= */

EXPORT void init_catalog(void) {
    /* TODO: Write catalog JSON to /effect_catalog.json */
}

EXPORT void set_source_path(int stack_type, const char* vfs_path) {
    if (stack_type < 0 || stack_type > 1) return;
    strncpy(g_stacks[stack_type].source_path, vfs_path,
            sizeof(g_stacks[stack_type].source_path) - 1);
            
    g_stacks[stack_type].source_path[sizeof(g_stacks[stack_type].source_path) - 1] = '\0';
    g_stacks[stack_type].source_changed = 1;
    g_stacks[stack_type].source_path_changed = 1;
    if (stack_type == STACK_EROSION) erosion_memo_clear();
    if (stack_type == STACK_GRADIENT) gradient_memo_clear();
}

EXPORT void set_source_changed(int stack_type, int changed) {
    if (stack_type < 0 || stack_type > 1) return;
    g_stacks[stack_type].source_changed = changed;
    if (changed) {
        if (stack_type == STACK_EROSION) erosion_memo_clear();
        if (stack_type == STACK_GRADIENT) gradient_memo_clear();
    }
}

EXPORT void stack_begin(int stack_type) {
    if (stack_type < 0 || stack_type > 1) return;

    /* Free any malloc'd ColorRamp stops from previous run */
    free_stack_color_ramps(stack_type);

    g_current_stack = stack_type;
    g_stacks[stack_type].count = 0;
    g_stacks[stack_type].stack_type = stack_type;
}

EXPORT void push_effect(int effect_id, const uint8_t* params, int param_count) {
    if (g_current_stack < 0) {
        js_post_error(ERROR_UNKNOWN_EFFECT, effect_id, -1,
            "push_effect called before stack_begin");
        return;
    }

    int idx = g_current_stack;
    if (g_stacks[idx].count >= MAX_STACK_SIZE) {
        js_post_error(ERROR_STACK_FULL, effect_id, -1,
            "effect stack full");
        return;
    }

    Effect* e = &g_stacks[idx].effects[g_stacks[idx].count];
    int ok = 0;

    switch (effect_id) {
        /* Data sources */
        case EFFECT_SOURCE_GRADIENT: ok = parse_gradient_source(params, param_count, e); break;
        case EFFECT_SOURCE_WORLEY:   ok = parse_worley_source(params, param_count, e); break;
        case EFFECT_SOURCE_PERLIN:   ok = parse_perlin_source(params, param_count, e); break;
        case EFFECT_SOURCE_CURL:     ok = parse_curl_source(params, param_count, e); break;
        case EFFECT_SOURCE_NOISE:    ok = parse_noise_source(params, param_count, e); break;
        /* Erosion effects */
        case EFFECT_DIJKSTRA:        ok = parse_dijkstra(params, param_count, e); break;
        case EFFECT_FOURIER_CLAMP:   ok = parse_fourier_clamp(params, param_count, e); break;
        case EFFECT_BOX_BLUR:        ok = parse_box_blur(params, param_count, e); break;
        case EFFECT_GRADIENTIFY:     ok = parse_gradientify(params, param_count, e); break;
        case EFFECT_POISSON_SOLVE:   ok = parse_poisson_solve(params, param_count, e); break;
        case EFFECT_LAMINARIZE:      ok = parse_laminarize(params, param_count, e); break;
        /* Debug commands */
        case EFFECT_DEBUG_HESSIAN_FLOW:   ok = parse_debug_hessian_flow(params, param_count, e); break;
        case EFFECT_DEBUG_SPLIT_CHANNELS: ok = parse_debug_split_channels(params, param_count, e); break;
        case EFFECT_DEBUG_LIC:            ok = parse_debug_lic(params, param_count, e); break;
        case EFFECT_DEBUG_LAPLACIAN:      ok = parse_debug_laplacian(params, param_count, e); break;
        case EFFECT_DEBUG_RIDGE_MESH:    ok = parse_debug_ridge_mesh(params, param_count, e); break;
        /* Gradient stack */
        case EFFECT_COLOR_RAMP:      ok = parse_color_ramp(params, param_count, e); break;
        case EFFECT_BLEND_MODE:      ok = parse_blend_mode(params, param_count, e); break;
        default:
            js_post_error(ERROR_UNKNOWN_EFFECT, effect_id, -1,
                "unknown effect_id");
            return;
    }

    if (ok) {
        g_stacks[idx].count++;
    }
}



EXPORT uint8_t* stack_end(int* out_w, int* out_h) {
	*out_w = 0;
	*out_h = 0;
	
    if (g_current_stack < 0) {
        js_post_error(ERROR_NO_SOURCE, -1, -1, "no active stack");

        return NULL;
    }

	//debug_print_stack(g_current_stack);
	
    int idx = g_current_stack;
    g_current_stack = -1;

    if (idx == STACK_GRADIENT) {
        /* Gradient stack generates procedurally - no source file needed */
        return process_gradient_stack(&g_stacks[idx].effects[0], g_stacks[idx].count, out_w, out_h);
    }

    if (idx == STACK_EROSION) {
        return process_erosion_stack(&g_stacks[idx].effects[0], g_stacks[idx].count, out_w, out_h);
    }
    
    return NULL;
}

EXPORT void debug_print_effect(Effect * e)
{
	switch (e->effect_id) {
		case EFFECT_SOURCE_GRADIENT:
			printf("SOURCE_GRADIENT: angle=%.3f scale=%.3f offset=%.3f\n",
				   e->params.gradient_source.angle,
				   e->params.gradient_source.scale,
				   e->params.gradient_source.offset);
			break;
		case EFFECT_SOURCE_WORLEY:
			printf("SOURCE_WORLEY: scale=%.3f jitter=%.3f metric=%d mode=%d\n",
				   e->params.worley_source.scale,
				   e->params.worley_source.jitter,
				   e->params.worley_source.metric,
				   e->params.worley_source.mode);
			break;
		case EFFECT_SOURCE_PERLIN:
			printf("SOURCE_PERLIN: scale=%.3f octaves=%d persistence=%.3f lacunarity=%.3f\n",
				   e->params.perlin_source.scale,
				   e->params.perlin_source.octaves,
				   e->params.perlin_source.persistence,
				   e->params.perlin_source.lacunarity);
			break;
		case EFFECT_SOURCE_CURL:
			printf("SOURCE_CURL: scale=%.3f octaves=%d persistence=%.3f lacunarity=%.3f\n",
				   e->params.curl_source.scale,
				   e->params.curl_source.octaves,
				   e->params.curl_source.persistence,
				   e->params.curl_source.lacunarity);
			break;
		case EFFECT_SOURCE_NOISE:
			printf("SOURCE_NOISE: type=%d scale=%.3f seed=%u\n",
				   e->params.noise_source.type,
				   e->params.noise_source.scale,
				   e->params.noise_source.seed);
			break;
		case EFFECT_DIJKSTRA:
			printf("DIJKSTRA: Minkowski=%.3f Chebyshev=%.3f\n",
				   e->params.dijkstra.Minkowski,
				   e->params.dijkstra.Chebyshev);
			break;
		case EFFECT_FOURIER_CLAMP:
			printf("FOURIER_CLAMP: Minimum=%.3f Maximum=%.3f\n",
				   e->params.fourier_clamp.Minimum,
				   e->params.fourier_clamp.Maximum);
			break;
		case EFFECT_BOX_BLUR:
			printf("BOX_BLUR: iterations=%.3f threshold=%.3f\n",
				   e->params.box_blur.iterations,
				   e->params.box_blur.threshold);
			break;
		case EFFECT_GRADIENTIFY:
			printf("GRADIENTIFY: scale=%.3f\n",
				   e->params.gradientify.scale);
			break;
		case EFFECT_POISSON_SOLVE:
			printf("POISSON_SOLVE: iterations=%d\n",
				   e->params.poisson_solve.iterations);
			break;
		case EFFECT_LAMINARIZE:
			printf("LAMINARIZE: scale=%.3f strength=%.3f blur_sigma=%.3f\n",
				   e->params.laminarize.scale,
				   e->params.laminarize.strength,
				   e->params.laminarize.blur_sigma);
			break;
		case EFFECT_COLOR_RAMP:
			printf("COLOR_RAMP: %d stops\n", e->params.color_ramp.length);
			for (int j = 0; j < e->params.color_ramp.length; j++) {
				ColorStop* s = &e->params.color_ramp.stops[j];
				printf("    [%d] pos=%.3f rgba=(%.3f, %.3f, %.3f, %.3f)\n",
					   j, s->position, s->color.x, s->color.y, s->color.z, s->color.w);
			}
			break;
		case EFFECT_BLEND_MODE:
			printf("BLEND_MODE: mode=%d opacity=%.3f\n",
				   e->params.blend_mode.mode,
				   e->params.blend_mode.opacity);
			break;
		case EFFECT_DEBUG_HESSIAN_FLOW:
			printf("DEBUG_HESSIAN_FLOW: kernel_size=%d\n",
				   e->params.debug_hessian_flow.kernel_size);
			break;
		case EFFECT_DEBUG_SPLIT_CHANNELS:
			printf("DEBUG_SPLIT_CHANNELS\n");
			break;
		case EFFECT_DEBUG_LIC:
			printf("DEBUG_LIC: vector_field=%d kernel_length=%.3f step_size=%.3f\n",
				   e->params.debug_lic.vector_field,
				   e->params.debug_lic.kernel_length,
				   e->params.debug_lic.step_size);
			break;
		case EFFECT_DEBUG_LAPLACIAN:
			printf("DEBUG_LAPLACIAN: kernel_size=%d\n",
				   e->params.debug_laplacian.kernel_size);
			break;
		case EFFECT_DEBUG_RIDGE_MESH:
			printf("DEBUG_RIDGE_MESH: normal_scale=%.3f high_thresh=%.4f low_thresh=%.4f\n",
				   e->params.debug_ridge_mesh.normal_scale,
				   e->params.debug_ridge_mesh.high_threshold,
				   e->params.debug_ridge_mesh.low_threshold);
			break;
		default:
			printf("UNKNOWN\n");
			break;
	}
}

EXPORT void debug_print_stack(int stack_type) {
    if (stack_type < 0 || stack_type >= STACK_TYPE_TOTAL) {
        printf("debug_print_stack: invalid stack_type %d\n", stack_type);
        return;
    }

    int count = g_stacks[stack_type].count;
    printf("=== Stack %d (%s) - %d effects ===\n",
           stack_type,
           stack_type == STACK_GRADIENT ? "GRADIENT" : "EROSION",
           count);
    printf("source_path: \"%s\"\n", g_stacks[stack_type].source_path);
    printf("source_changed: %d\n", g_stacks[stack_type].source_changed);

    for (int i = 0; i < count; i++) {
        Effect* e = &g_stacks[stack_type].effects[i];
        printf("[%d] effect_id=0x%02X ", i, e->effect_id);
        debug_print_effect(e);
    }
    printf("=== End Stack %d ===\n\n", stack_type);
}

EXPORT void analyze_source(int stack_type, const float* params, int param_count) {
    if ((uint32_t)stack_type >= STACK_TYPE_TOTAL)
        return;

    const char* path = g_stacks[stack_type].source_path;
    if (!path || path[0] == '\0') {
        js_post_error(ERROR_NO_SOURCE, -1, -1, "no source path set");
        return;
    }

    /* Parse and store source parameters */
    ErosionSourceTexture new_params = {0};
    if (param_count >= 1) {
        new_params.quatization = params[0];
    }

    /* Check if params changed */
    int params_changed = memcmp(&new_params, &g_stacks[stack_type].source_params,
                                 sizeof(ErosionSourceTexture)) != 0;
                                 
    if (params_changed) 
    {    
        g_stacks[stack_type].source_params = new_params;        
        bool did_change = true;
        
		if(stack_type == STACK_EROSION)
		{
			float q = g_stacks[stack_type].source_params.quatization;
			ErosionImageMemo const* erosion = memo_get_erosion();
			
			if(erosion)
			{
				did_change = false;
				
				if(!(erosion->minimum_quantization <= q && q <= erosion->maximum_quantization))
				{
					did_change = true;
				}
			}
		}
    
        g_stacks[stack_type].source_changed = did_change;
    }

    /* Skip reload if nothing changed */
    if (g_stacks[stack_type].source_changed == 0)
        return;

    /* Load image into memo cache with target quantization */
    float target_quant = new_params.quatization;

    if (!memo_load_image(stack_type, path, target_quant)) {
        js_post_error(ERROR_SOURCE_READ, -1, -1, "failed to load source image");
        return;
    }

    g_stacks[stack_type].source_changed = 0;
	
	return;
	
    /* For erosion stack, send timing and recommend auto effects based on quantization */
    if (stack_type == STACK_EROSION
	&& g_stacks[stack_type].source_path_changed) {
		g_stacks[stack_type].source_path_changed = 0;
		
        const ErosionImageMemo* erosion = memo_get_erosion();
        if (erosion) {
            /* Send timing info to JS for UI slider updates */
            js_set_source_timing(stack_type,
                                 erosion->fade_in_time,
                                 erosion->fade_out_time,
                                 erosion->animation_duration);

            /* Calculate effective bit depth from quantization */
            /* quant 0.0 = 1 bit, quant 1.0 = 8 bits */
            /* bits = 1 + quant * 7 */
            float quant = erosion->maximum_quantization;
            float bits = 1.0f + quant * 7.0f;

            /* Clear existing auto effects first */
            js_clear_auto_effects(stack_type);
                
            /* Apply rules based on bit depth:
             * > 7 bits: no modification
             * > 5 bits: iterated blur (Box Blur)
             * <= 5 bits: dijkstra then low pass (Fourier Filter)
             *
             * Params are uint8 packed values:
             * - Box Blur: iterations=32 -> 142, threshold=0.01 -> 85 (log scale)
             * - Dijkstra: minkowski=1 -> 140, chebyshev=0 -> 0 (linear scale)
             * - Fourier: high_pass=0 -> 0, low_pass=0.15 -> 38 (linear scale)
             */
            if (bits > 7.0f) {
                /* High precision - no modification needed */
            } else if (bits > 5.0f) {
                /* Medium precision - use iterated blur */
                uint8_t blur_params[] = { 142, 85 };
                js_push_auto_effect(stack_type, EFFECT_BOX_BLUR, blur_params, 2);
            } else {
                /* Low precision - use dijkstra then low pass */
                uint8_t dijkstra_params[] = { 140, 0 };
                js_push_auto_effect(stack_type, EFFECT_DIJKSTRA, dijkstra_params, 2);

                uint8_t fourier_params[] = { 0, 38 };
                js_push_auto_effect(stack_type, EFFECT_FOURIER_CLAMP, fourier_params, 2);
            }
        }
    }
}
