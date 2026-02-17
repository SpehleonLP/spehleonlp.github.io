# Pipeline Effect Memoization — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Cache per-layer buffer snapshots so that when only a later effect changes, we skip reprocessing the unchanged prefix.

**Architecture:** Each pipeline file keeps a static `PipelineMemo` with up to `MAX_STACK_SIZE` layer entries. On each `process_*` call, walk forward comparing the incoming effect array against the memo via `memcmp` (with a special case for ColorRamp). Resume processing from the last valid snapshot. A `should_memoize(EffectId)` function controls which effects get snapshots (cheap ones are skipped).

**Tech Stack:** C++20, GLM, same build system (CMake GLOB picks up new headers automatically).

**Design doc:** `docs/plans/2026-02-17-pipeline-memoization-design.md`

---

### Task 1: Create `src/pipeline_memo.h`

**Files:**
- Create: `src/pipeline_memo.h`

This header defines all memo data structures and inline helper functions. It is header-only (no `.cpp`).

**Step 1: Write the header**

```cpp
#ifndef PIPELINE_MEMO_H
#define PIPELINE_MEMO_H

#include "effect_stack_api.h"
#include <string.h>
#include <stdlib.h>
#include <memory>

/* Per-layer memo entry */
struct EffectMemo {
    Effect effect;              /* copy of the effect config at this layer */
    float* buffer_snapshot;     /* full buffer copy after this effect ran (NULL if skipped) */
    size_t buffer_size;         /* size in bytes of buffer_snapshot */
    void* effect_state;         /* optional effect-specific reusable state */
    void (*free_state)(void*);  /* cleanup for effect_state (NULL if none) */
};

/* Per-pipeline memo */
struct PipelineMemo {
    EffectMemo layers[MAX_STACK_SIZE];
    int count;                  /* number of memoized layers */
    uint32_t source_W, source_H; /* dimensions when memo was built */
};

/* Returns true if this effect type is worth snapshotting the buffer for.
 * Cheap effects return false — they still get compared in walk-forward
 * but don't store a buffer snapshot. */
static inline bool should_memoize(int effect_id) {
    switch (effect_id) {
    /* Erosion effects worth memoizing (expensive) */
    case EFFECT_DIJKSTRA:
    case EFFECT_FOURIER_CLAMP:
    case EFFECT_BOX_BLUR:
    case EFFECT_LAMINARIZE:
    case EFFECT_POISSON_SOLVE:
        return true;

    /* Cheap or structural — not worth snapshotting */
    case EFFECT_GRADIENTIFY:
    case EFFECT_BLEND_MODE:
    case EFFECT_COLOR_RAMP:
    case EFFECT_SOURCE_GRADIENT:
    case EFFECT_SOURCE_WORLEY:
    case EFFECT_SOURCE_PERLIN:
    case EFFECT_SOURCE_CURL:
    case EFFECT_SOURCE_NOISE:
    /* Debug effects — not worth memoizing */
    case EFFECT_DEBUG_HESSIAN_FLOW:
    case EFFECT_DEBUG_SPLIT_CHANNELS:
    case EFFECT_DEBUG_LIC:
    case EFFECT_DEBUG_LAPLACIAN:
    case EFFECT_DEBUG_RIDGE_MESH:
    default:
        return false;
    }
}

/* Compare two Effect structs. Returns true if they are equivalent.
 * Uses memcmp for most effects; special-cases ColorRamp (has malloc'd stops). */
static inline bool effects_equal(const Effect* a, const Effect* b) {
    if (a->effect_id != b->effect_id) return false;

    if (a->effect_id == EFFECT_COLOR_RAMP) {
        if (a->params.color_ramp.length != b->params.color_ramp.length)
            return false;
        if (a->params.color_ramp.length == 0)
            return true;
        return memcmp(a->params.color_ramp.stops, b->params.color_ramp.stops,
                      a->params.color_ramp.length * sizeof(ColorStop)) == 0;
    }

    /* For everything else, bitwise compare the params union */
    return memcmp(&a->params, &b->params, sizeof(a->params)) == 0;
}

/* Free all resources held by a single memo layer */
static inline void memo_layer_free(EffectMemo* layer) {
    free(layer->buffer_snapshot);
    layer->buffer_snapshot = NULL;
    layer->buffer_size = 0;
    if (layer->free_state && layer->effect_state) {
        layer->free_state(layer->effect_state);
    }
    layer->effect_state = NULL;
    layer->free_state = NULL;
}

/* Clear the entire memo, freeing all snapshots and effect state */
static inline void memo_clear(PipelineMemo* memo) {
    for (int i = 0; i < memo->count; i++) {
        memo_layer_free(&memo->layers[i]);
    }
    memo->count = 0;
    memo->source_W = 0;
    memo->source_H = 0;
}

/* Truncate memo to `new_count` layers, freeing layers beyond that */
static inline void memo_truncate(PipelineMemo* memo, int new_count) {
    for (int i = new_count; i < memo->count; i++) {
        memo_layer_free(&memo->layers[i]);
    }
    memo->count = new_count;
}

/* Save a buffer snapshot for a layer. Copies the effect and buffer.
 * `layer_idx` must equal memo->count (append only).
 * Pass buffer=NULL and buffer_bytes=0 if should_memoize() returned false. */
static inline void memo_save_layer(PipelineMemo* memo, int layer_idx,
                                    const Effect* effect, const float* buffer,
                                    size_t buffer_bytes) {
    if (layer_idx < 0 || layer_idx >= MAX_STACK_SIZE) return;

    EffectMemo* layer = &memo->layers[layer_idx];
    layer->effect = *effect;

    /* Deep-copy ColorRamp stops */
    if (effect->effect_id == EFFECT_COLOR_RAMP && effect->params.color_ramp.stops) {
        size_t stops_size = effect->params.color_ramp.length * sizeof(ColorStop);
        layer->effect.params.color_ramp.stops = (ColorStop*)malloc(stops_size);
        if (layer->effect.params.color_ramp.stops) {
            memcpy(layer->effect.params.color_ramp.stops,
                   effect->params.color_ramp.stops, stops_size);
        }
    }

    /* Store buffer snapshot */
    free(layer->buffer_snapshot);
    if (buffer && buffer_bytes > 0) {
        layer->buffer_snapshot = (float*)malloc(buffer_bytes);
        if (layer->buffer_snapshot) {
            memcpy(layer->buffer_snapshot, buffer, buffer_bytes);
            layer->buffer_size = buffer_bytes;
        } else {
            layer->buffer_size = 0;
        }
    } else {
        layer->buffer_snapshot = NULL;
        layer->buffer_size = 0;
    }

    layer->effect_state = NULL;
    layer->free_state = NULL;

    if (layer_idx >= memo->count)
        memo->count = layer_idx + 1;
}

/* Walk the incoming effect array against the memo. Returns:
 *   resume_from: index of the first effect to re-process (0 = start from scratch)
 *   snapshot_idx: index of the memo layer whose buffer_snapshot to restore (-1 = none)
 *   reusable_state: if effect_id matches at resume_from, the cached effect_state (else NULL)
 *
 * Caller should:
 *   1. If snapshot_idx >= 0, memcpy memo->layers[snapshot_idx].buffer_snapshot into working buffer
 *   2. Process effects[resume_from .. effect_count-1]
 *   3. After processing, call memo_truncate(memo, effect_count) and save new layers
 */
struct MemoResumePoint {
    int resume_from;      /* first effect index that needs reprocessing */
    int snapshot_idx;     /* memo layer index to restore buffer from (-1 = none) */
    void* reusable_state; /* effect_state from memo if type matches at mismatch point */
};

static inline MemoResumePoint memo_find_resume(const PipelineMemo* memo,
                                                const Effect* effects, int effect_count,
                                                uint32_t W, uint32_t H) {
    MemoResumePoint result = { 0, -1, NULL };

    /* Dimension change invalidates everything */
    if (memo->source_W != W || memo->source_H != H || memo->count == 0) {
        return result;
    }

    int walk_limit = (effect_count < memo->count) ? effect_count : memo->count;

    for (int i = 0; i < walk_limit; i++) {
        if (!effects_equal(&effects[i], &memo->layers[i].effect)) {
            /* Mismatch — check if at least the effect type matches for state reuse */
            if (effects[i].effect_id == memo->layers[i].effect.effect_id
                && memo->layers[i].effect_state) {
                result.reusable_state = memo->layers[i].effect_state;
            }
            break;
        }
        /* Match — track latest snapshot */
        result.resume_from = i + 1;
        if (memo->layers[i].buffer_snapshot) {
            result.snapshot_idx = i;
        }
    }

    return result;
}

#endif /* PIPELINE_MEMO_H */
```

**Step 2: Build to verify header compiles**

Run: `cmake -S src -B src/build_cli && make -C src/build_cli -j$(nproc) 2>&1 | tail -5`
Expected: Build succeeds (header is included by pipeline files in next tasks, but GLOBbed headers mean no error now).

**Step 3: Commit**

```bash
git add src/pipeline_memo.h
git commit -m "Add pipeline_memo.h: memoization structs and helpers"
```

---

### Task 2: Integrate memoization into erosion pipeline

**Files:**
- Modify: `src/erosion_pipeline.cpp`
- Modify: `src/erosion_pipeline.h`

The erosion pipeline's `process_erosion_height_planar` currently loops over all effects unconditionally. We add a static `PipelineMemo`, use `memo_find_resume` to skip already-computed prefix, and save new snapshots after each effect.

**Step 1: Add memo include and static state to erosion_pipeline.cpp**

At the top of `erosion_pipeline.cpp` (after existing includes, around line 16), add:

```cpp
#include "pipeline_memo.h"

static PipelineMemo g_erosion_memo = {};
```

**Step 2: Add clear function declaration to erosion_pipeline.h**

Add before the `#endif`:

```cpp
void erosion_memo_clear(void);
```

**Step 3: Add clear function to erosion_pipeline.cpp**

After the static PipelineMemo declaration, add:

```cpp
void erosion_memo_clear(void) {
    memo_clear(&g_erosion_memo);
}
```

**Step 4: Modify `process_erosion_height_planar` signature and body**

The current function signature (line 540):
```cpp
static void process_erosion_height_planar(float *dst, uint32_t W, uint32_t H, Effect const* effects, int effect_count)
```

Replace the entire function body with the memoized version. The key changes:
1. Call `memo_find_resume` at the top
2. If we can resume, restore the snapshot into `dst`
3. Loop starting from `resume_from` instead of 0
4. After each effect, save a new memo layer (with or without snapshot depending on `should_memoize`)
5. Truncate any stale memo layers at the end

```cpp
static void process_erosion_height_planar(float *dst, uint32_t W, uint32_t H, Effect const* effects, int effect_count)
{
    size_t buffer_bytes = (size_t)W * H * 3 * sizeof(float);

    /* Find how far we can skip using the memo */
    MemoResumePoint resume = memo_find_resume(&g_erosion_memo, effects, effect_count, W, H);

    /* Restore from snapshot if available */
    if (resume.snapshot_idx >= 0) {
        memcpy(dst, g_erosion_memo.layers[resume.snapshot_idx].buffer_snapshot, buffer_bytes);
    }

    /* Truncate memo beyond where we're resuming — those layers are stale */
    memo_truncate(&g_erosion_memo, resume.resume_from);
    g_erosion_memo.source_W = W;
    g_erosion_memo.source_H = H;

    for (int i = resume.resume_from; i < effect_count; ++i)
    {
        switch ((EffectId)effects[i].effect_id)
        {
        // ... (existing switch body unchanged) ...
        }

        /* Save memo layer after this effect */
        bool do_snapshot = should_memoize(effects[i].effect_id);
        memo_save_layer(&g_erosion_memo, i, &effects[i],
                        do_snapshot ? dst : NULL,
                        do_snapshot ? buffer_bytes : 0);
    }
}
```

The switch body inside the loop stays exactly the same as the current code. Only the loop bounds and the memo save/restore around it change.

**Important caveat — gradientify sub-loop:** The `EFFECT_GRADIENTIFY` and `EFFECT_LAMINARIZE` cases call `process_erosion_gradientify` which advances `i` past a sub-sequence of effects. These sub-effects are consumed as a group. The memo should treat the whole gradientify..poisson_solve span as a single unit — save one memo layer at the poisson_solve exit point. Implementation detail: after `process_erosion_gradientify` returns the new `i`, save memo for each consumed effect without a snapshot, then save the final one (at returned `i`) with a snapshot if the returned effect was POISSON_SOLVE.

Alternatively, for the initial version: just save the effect at position `i` (after the switch) with a snapshot. The sub-effects consumed by gradientify won't have individual memo entries, which means any change inside the gradientify block restarts from before it. This is simpler and still correct — the gradientify block is fast relative to dijkstra/FFT.

**Step 5: Build and test**

Run: `bash run_debug_test.sh`
Expected: Build succeeds, test runs identically to before (first run builds full memo, no resume yet).

**Step 6: Commit**

```bash
git add src/erosion_pipeline.cpp src/erosion_pipeline.h src/pipeline_memo.h
git commit -m "Integrate effect memoization into erosion pipeline"
```

---

### Task 3: Integrate memoization into gradient pipeline

**Files:**
- Modify: `src/gradient_pipeline.cpp`
- Modify: `src/gradient_pipeline.h`

The gradient pipeline is different — it has front/back/output buffers and cycles through source->ramp->blend patterns. For the initial version, `should_memoize` returns false for all gradient effects, so we add the machinery but no actual snapshots.

**Step 1: Add memo include and static state to gradient_pipeline.cpp**

After existing includes (around line 18), add:

```cpp
#include "pipeline_memo.h"

static PipelineMemo g_gradient_memo = {};
```

**Step 2: Add clear function declaration to gradient_pipeline.h**

Add before the `#endif`:

```cpp
void gradient_memo_clear(void);
```

**Step 3: Add clear function to gradient_pipeline.cpp**

```cpp
void gradient_memo_clear(void) {
    memo_clear(&g_gradient_memo);
}
```

**Step 4: Add memo walk-forward to `process_gradient_stack`**

In `process_gradient_stack` (line 357), after the early-return checks and pipeline reset, add memo logic around the main effect loop (lines 408-432). Since no gradient effects are memoized yet, this just:
1. Calls `memo_find_resume` (will always return resume_from=N for matching prefixes, snapshot_idx=-1)
2. Saves layers without snapshots
3. On matching prefix, skips re-processing those effects

The effect loop needs to distinguish "replaying matched prefix" (just skip the effect) from "processing new effects" (run them). When `should_memoize` returns false for all gradient effects, the resume point tells us how many effects to skip, but since there's no snapshot to restore from, we can't actually skip — the pipeline state (front/back/output) isn't captured.

**For the initial version:** Just do the memo comparison and save layers (no snapshots, no skipping). This sets up the data structure so that when we later enable memoization for expensive gradient effects, the walk-forward will work. The only runtime cost is the memcmp per layer, which is negligible.

```cpp
/* In process_gradient_stack, after pipeline reset and backdrop setup,
 * before the main effect loop: */

/* Update memo dimensions */
g_gradient_memo.source_W = W;
g_gradient_memo.source_H = H;

/* For now, just track effect entries for future memoization.
 * No snapshots are stored (should_memoize returns false for all gradient effects). */
int memo_match_count = 0;
{
    int walk_limit = (effect_count < g_gradient_memo.count) ? effect_count : g_gradient_memo.count;
    for (int i = 0; i < walk_limit; i++) {
        if (!effects_equal(&effects[i], &g_gradient_memo.layers[i].effect))
            break;
        memo_match_count = i + 1;
    }
}
memo_truncate(&g_gradient_memo, memo_match_count);

/* ... existing effect loop (unchanged) ... */

/* After the loop, save memo layers for all effects */
for (int i = memo_match_count; i < effect_count; i++) {
    memo_save_layer(&g_gradient_memo, i, &effects[i], NULL, 0);
}
```

**Step 5: Build and test**

Run: `bash run_debug_test.sh`
Expected: Build succeeds, test output identical.

**Step 6: Commit**

```bash
git add src/gradient_pipeline.cpp src/gradient_pipeline.h
git commit -m "Add memoization scaffolding to gradient pipeline (no-op for now)"
```

---

### Task 4: Wire up memo clearing on source change

**Files:**
- Modify: `src/effect_stack_api.cpp`

When the source image changes, the memo for that stack must be invalidated.

**Step 1: Add includes**

At the top of `effect_stack_api.cpp`, add:

```cpp
#include "erosion_pipeline.h"
#include "gradient_pipeline.h"
```

Note: `erosion_pipeline.h` is likely already included. Just ensure `gradient_pipeline.h` is there for `gradient_memo_clear`.

**Step 2: Add clearing calls**

In `set_source_path` (line 256), after setting `source_changed = 1`, add:

```cpp
if (stack_type == STACK_EROSION) erosion_memo_clear();
if (stack_type == STACK_GRADIENT) gradient_memo_clear();
```

In `set_source_changed` (line 266), when `changed` is true, add the same:

```cpp
if (changed) {
    if (stack_type == STACK_EROSION) erosion_memo_clear();
    if (stack_type == STACK_GRADIENT) gradient_memo_clear();
}
```

**Step 3: Build and test**

Run: `bash run_debug_test.sh`
Expected: Build succeeds, test output identical.

**Step 4: Commit**

```bash
git add src/effect_stack_api.cpp
git commit -m "Clear pipeline memos when source image changes"
```

---

### Task 5: Verify end-to-end with CLI

**Files:** (none — verification only)

**Step 1: Run the debug test twice**

Run `bash run_debug_test.sh` twice in succession. On the second run, the memo should be populated from the first run's stack_end, but since the CLI builds a fresh stack each invocation (process exits), this won't demonstrate caching. This is expected — the memo lives in process memory and benefits the WASM context where stack_begin/push_effect/stack_end is called repeatedly without process restart.

**Step 2: Verify no memory leaks with valgrind**

Run: `valgrind --leak-check=full src/build_cli/effect_stack_cli <same args as run_debug_test.sh>`
Expected: 0 bytes definitely lost. The memo's static buffers will show as "still reachable" (same as the existing output buffer), which is fine.

**Step 3: Final commit (if any fixups needed)**

```bash
git add -A
git commit -m "Pipeline memoization: fix any valgrind findings"
```

---

## Summary of files touched

| File | Action | Purpose |
|------|--------|---------|
| `src/pipeline_memo.h` | Create | Memo structs, helpers, should_memoize() |
| `src/erosion_pipeline.h` | Modify | Add `erosion_memo_clear()` declaration |
| `src/erosion_pipeline.cpp` | Modify | Static memo, walk-forward in process loop |
| `src/gradient_pipeline.h` | Modify | Add `gradient_memo_clear()` declaration |
| `src/gradient_pipeline.cpp` | Modify | Static memo, scaffolding (no-op initially) |
| `src/effect_stack_api.cpp` | Modify | Call memo clear on source change |
