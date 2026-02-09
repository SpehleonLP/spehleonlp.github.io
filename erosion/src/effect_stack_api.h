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

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT
#endif

/* =========================================================================
 * Stack type constants
 * ========================================================================= */

#define STACK_GRADIENT  0
#define STACK_EROSION   1

/* =========================================================================
 * Catalog generation
 *
 * Called once at startup. Each effect module writes its JSON descriptor to
 * the file. The coordinator assembles the full catalog structure.
 *
 * After this call, /effect_catalog.json exists on the emscripten VFS.
 * JS reads it with FS.readFile('/effect_catalog.json').
 * ========================================================================= */

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
EXPORT void     push_effect(int effect_id, const float* params, int float_count);
EXPORT uint8_t* stack_end(int* out_w, int* out_h);

/* =========================================================================
 * Source analysis and auto-configuration (WASM -> JS)
 *
 * analyze_source examines the loaded source (frame count, banding estimate,
 * etc.) and calls back into JS to recommend effects. The callbacks fire
 * synchronously from C's perspective; the worker's JS implementations
 * forward them via postMessage.
 *
 * Call after set_source_path + set_source_changed(1).
 * ========================================================================= */

EXPORT void analyze_source(int stack_type);

/* =========================================================================
 * JS callbacks (imported functions)
 *
 * These are implemented in JS (worker.js) and linked via emscripten imports.
 * C code calls them synchronously during analyze_source().
 *
 * js_clear_auto_effects: Remove all auto-placed effects from the UI stack.
 * js_push_auto_effect:   Insert a recommended effect at position 1 (after
 *                        source) with the given params. Marked data-auto
 *                        in the DOM.
 * ========================================================================= */

extern void js_clear_auto_effects(int stack_type);
extern void js_push_auto_effect(int stack_type, int effect_id,
                                const float* params, int param_count);

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
