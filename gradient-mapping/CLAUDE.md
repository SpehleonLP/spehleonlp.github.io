# Gradient Mapping Texture Tool

Web-based tool for creating animated dissolve effects using per-pixel timing textures instead of sprite sheets.

## Quick Start

```bash
# Build WASM module (requires Emscripten SDK)
cd src
emcmake cmake . -G Ninja
ninja
# Output: ../scripts/effect_stack.js + effect_stack.wasm

# Serve locally (any static server works)
python3 -m http.server 8000
```

## Architecture

Two-stack processing model:
- **Gradient Stack (0)**: Generates 2D color ramps (256×64 RGBA)
- **Erosion Stack (1)**: Processes source images into timing data (R=reveal, G=dissolve, B=edge softness)

```
index.html          # Single-page app entry
script.js           # Main UI & WebGL (2100+ lines)
worker.js           # WASM bridge & async processing
shaders/
  erosion.frag      # Main visualization shader
  erosion.vert
scripts/
  effect_stack.js   # Emscripten module wrapper
  effect_stack.wasm # Compiled C processing (155KB)
  effect_catalog.json
src/
  effect_stack_api.h  # Public C API (all effect definitions)
  effect_stack_api.c  # Catalog generation + stack execution
  erosion_pipeline.c  # Erosion stack processing
  gradient_pipeline.c # Gradient stack processing
  smart_blur.c        # Key: constraint-based interpolation
  sources/            # Noise generators (Perlin, Worley, Curl)
  commands/           # Image processing effects (10+ filters)
  image_memo/         # Frame caching + envelope detection
```

## Key C API (effect_stack_api.h)

```c
init_catalog()                              // Generate JSON effect definitions
set_source_path(int stack, const char* p)   // Set source file in Emscripten VFS
stack_begin(int stack)                      // Start building effect chain
push_effect(int id, float* params, int n)   // Add effect to chain
stack_end(int* w, int* h)                   // Process stack, return RGBA
```

## Code Style

- C code: C99, `-Wall -Wextra -Wpedantic`
- JS: ES6+, no build step, vanilla (no framework)
- Shaders: GLSL ES 3.0 (WebGL 2)

## Gotchas

- Worker uses Emscripten VFS: JS writes files via `FS.writeFile()`, C reads via fopen
- Message queue in worker.js coalesces rapid updates - uses latest ID tracking
- CMake has two modes: Emscripten (WASM) vs Native (for IDE debugging)
- MAX_STACK_SIZE = 32 effects per stack
- Memory config: 512MB max with `ALLOW_MEMORY_GROWTH=1`

## Testing

No automated tests. Manual testing via:
- Live WebGL preview with timeline scrubbing
- Demo images in `demo-images/` and `reference/alex_test_frames/`
- Export/download for external validation

## Documentation

- `landing.md` - Public-facing introduction
- `about.md` - Artist-friendly guide
- `reference/STATUS.md` - Development roadmap
- `reference/BUGS_FOUND.md` - Known issues
