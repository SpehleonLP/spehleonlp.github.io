# Pipeline Effect Memoization

## Problem

Processing the effect stack is slow because every UI change reprocesses the entire chain from scratch, even when only the last effect's parameters changed.

## Solution

Each pipeline maintains a file-local memo of the last processed effect stack. On re-execution, walk forward comparing effects until mismatch, then resume from the last cached buffer snapshot.

## Data Structures

```cpp
// Per-layer memo entry
struct EffectMemo {
    Effect effect;              // copy of the effect config
    float* buffer_snapshot;     // full 3*N float copy (NULL if should_memoize() == false)
    void* effect_state;         // optional effect-specific reusable state
    void (*free_state)(void*);  // cleanup for effect_state
};

// Per-pipeline memo
struct PipelineMemo {
    EffectMemo layers[MAX_STACK_SIZE];
    int count;                  // number of memoized layers
    uint32_t source_W, source_H; // dimensions when memo was built
};
```

One `static PipelineMemo` per pipeline file (file-local).

## Comparison

- Default: `memcmp(&a.effect, &b.effect, sizeof(Effect))`
- Special case for `EFFECT_COLOR_RAMP`: compare `length` first, then `memcmp` on the stops arrays

## Skip Function

`should_memoize(EffectId id)` returns whether a given effect type is worth snapshotting. Cheap effects (gradientify, blend_mode, etc.) return false. We still compare them in the walk-forward but skip storing a buffer snapshot.

When a cheap effect doesn't match, we fall back to the last memoized snapshot before it.

## Walk-Forward Algorithm

```
1. Walk effects[0..N] comparing against memo.layers[0..M]
2. For each matching effect:
   - If it has a buffer_snapshot, update "last_good_snapshot" index
   - Continue to next
3. At first mismatch:
   - If effect_id matches memo but params differ -> offer effect_state to the command
   - Restore working buffer from last_good_snapshot
   - Process from this effect onward
4. When source changes -> clear the entire memo
```

## Effect-Specific State Reuse

At the first mismatched layer, if the `effect_id` matches the memo's `effect_id`, the pipeline can pass `memo.layers[i].effect_state` to the command. Each command decides internally whether to use it. Examples:

- Dijkstra: cache region labels (reusable when only minkowski/chebyshev params change)
- FFT: cache plans (reusable when image dimensions match)

This is opt-in per command; most commands ignore it.

## Clearing

- `set_source_path()` or `set_source_changed()` triggers memo clear for that stack
- Dimension change clears (checked by comparing source_W/source_H)

## Gradient Pipeline

The gradient pipeline has different state (front/back/output buffers). The memo stores pipeline state snapshots. Initially, `should_memoize()` returns false for all gradient effects, so the machinery exists but no snapshots are stored yet.

## Memory Budget

For erosion on a 1024x1024 image: ~12MB per memoized layer (3 channels x 1024 x 1024 x 4 bytes). With 5 memoized layers that's ~60MB. Well within the 512MB WASM limit.

## Files to Touch

1. **New**: `src/pipeline_memo.h` -- memo structs, `should_memoize()`, comparison helpers
2. **Edit**: `src/erosion_pipeline.cpp` -- add static `PipelineMemo`, integrate walk-forward in `process_erosion_height_planar`
3. **Edit**: `src/gradient_pipeline.cpp` -- add static `PipelineMemo`, integrate in `process_gradient_stack`
4. **Edit**: `src/effect_stack_api.cpp` -- call memo clear on source change
