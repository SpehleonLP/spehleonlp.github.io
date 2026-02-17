#ifndef PIPELINE_MEMO_H
#define PIPELINE_MEMO_H

#include "effect_stack_api.h"
#include <string.h>
#include <stdlib.h>

/* =========================================================================
 * Pipeline memoization — cache intermediate results so that editing a
 * late effect in the stack doesn't re-run the entire pipeline.
 * ========================================================================= */

struct MemoResumePoint {
	int   resume_from;      // first effect index that needs reprocessing
	int   snapshot_idx;     // memo layer index to restore buffer from (-1 = none)
	void* reusable_state;   // effect_state if type matches at mismatch point
};

struct EffectMemo {
	Effect  effect;           // copy of the effect config at this layer
	float*  buffer_snapshot;  // full buffer copy after this effect ran (NULL if skipped)
	size_t  buffer_size;      // size in bytes of buffer_snapshot
	void*   effect_state;     // optional effect-specific reusable state
	void  (*free_state)(void*); // cleanup for effect_state (NULL if none)
};

struct PipelineMemo {
	EffectMemo layers[MAX_STACK_SIZE];
	int        count;        // number of memoized layers
	uint32_t   source_W, source_H; // dimensions when memo was built
};

/* =========================================================================
 * Inline helpers
 * ========================================================================= */

static inline bool should_memoize(int effect_id)
{
	switch (effect_id) {
	case EFFECT_DIJKSTRA:
	case EFFECT_FOURIER_CLAMP:
	case EFFECT_BOX_BLUR:
	case EFFECT_LAMINARIZE:
	case EFFECT_POISSON_SOLVE:
		return true;
	default:
		return false;
	}
}

static inline bool effects_equal(const Effect* a, const Effect* b)
{
	if (a->effect_id != b->effect_id)
		return false;

	if (a->effect_id == EFFECT_COLOR_RAMP) {
		const ColorRamp* ra = &a->params.color_ramp;
		const ColorRamp* rb = &b->params.color_ramp;
		if (ra->length != rb->length)
			return false;
		if (ra->length == 0)
			return true;
		return memcmp(ra->stops, rb->stops,
		              (size_t)ra->length * sizeof(ColorStop)) == 0;
	}

	return memcmp(&a->params, &b->params, sizeof(a->params)) == 0;
}

static inline void memo_layer_free(EffectMemo* layer)
{
	if (layer->buffer_snapshot) {
		free(layer->buffer_snapshot);
		layer->buffer_snapshot = nullptr;
	}
	if (layer->effect_state && layer->free_state) {
		layer->free_state(layer->effect_state);
	}
	layer->effect_state = nullptr;
	layer->free_state   = nullptr;
	layer->buffer_size  = 0;

	/* Free deep-copied ColorRamp stops */
	if (layer->effect.effect_id == EFFECT_COLOR_RAMP) {
		free(layer->effect.params.color_ramp.stops);
		layer->effect.params.color_ramp.stops  = nullptr;
		layer->effect.params.color_ramp.length = 0;
	}
}

static inline void memo_clear(PipelineMemo* memo)
{
	for (int i = 0; i < memo->count; i++)
		memo_layer_free(&memo->layers[i]);
	memo->count    = 0;
	memo->source_W = 0;
	memo->source_H = 0;
}

static inline void memo_truncate(PipelineMemo* memo, int new_count)
{
	if (new_count < 0)
		new_count = 0;
	for (int i = new_count; i < memo->count; i++)
		memo_layer_free(&memo->layers[i]);
	if (new_count < memo->count)
		memo->count = new_count;
}

static inline void memo_save_layer(PipelineMemo* memo, int layer_idx,
                                   const Effect* effect, const float* buffer,
                                   size_t buffer_bytes)
{
	if (layer_idx < 0 || layer_idx >= MAX_STACK_SIZE)
		return;

	/* If overwriting an existing layer, free it first */
	if (layer_idx < memo->count)
		memo_layer_free(&memo->layers[layer_idx]);

	EffectMemo* layer = &memo->layers[layer_idx];

	/* Shallow copy the Effect struct */
	layer->effect = *effect;

	/* Deep-copy ColorRamp stops if applicable */
	if (effect->effect_id == EFFECT_COLOR_RAMP && effect->params.color_ramp.stops
	    && effect->params.color_ramp.length > 0) {
		size_t stops_bytes = (size_t)effect->params.color_ramp.length * sizeof(ColorStop);
		layer->effect.params.color_ramp.stops = (ColorStop*)malloc(stops_bytes);
		memcpy(layer->effect.params.color_ramp.stops,
		       effect->params.color_ramp.stops, stops_bytes);
	}

	/* Copy buffer if non-NULL */
	if (buffer && buffer_bytes > 0) {
		layer->buffer_snapshot = (float*)malloc(buffer_bytes);
		memcpy(layer->buffer_snapshot, buffer, buffer_bytes);
		layer->buffer_size = buffer_bytes;
	} else {
		layer->buffer_snapshot = nullptr;
		layer->buffer_size     = 0;
	}

	layer->effect_state = nullptr;
	layer->free_state   = nullptr;

	/* Update count to include this layer */
	if (layer_idx >= memo->count)
		memo->count = layer_idx + 1;
}

static inline MemoResumePoint memo_find_resume(const PipelineMemo* memo,
                                               const Effect* effects,
                                               int effect_count,
                                               uint32_t W, uint32_t H)
{
	MemoResumePoint result = { 0, -1, nullptr };

	/* Dimension mismatch or empty memo: reprocess everything */
	if (memo->count == 0 || memo->source_W != W || memo->source_H != H)
		return result;

	int limit = memo->count < effect_count ? memo->count : effect_count;

	for (int i = 0; i < limit; i++) {
		if (!effects_equal(&memo->layers[i].effect, &effects[i])) {
			/* Mismatch — check if effect_id matches for state reuse */
			if (memo->layers[i].effect.effect_id == effects[i].effect_id)
				result.reusable_state = memo->layers[i].effect_state;
			break;
		}

		/* Match — advance resume point past this effect */
		result.resume_from = i + 1;

		/* Track latest snapshot */
		if (memo->layers[i].buffer_snapshot)
			result.snapshot_idx = i;
	}

	return result;
}

#endif /* PIPELINE_MEMO_H */
