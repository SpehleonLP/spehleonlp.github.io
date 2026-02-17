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
  effect_stack_api.h  # Public API (types, enums, WASM exports)
  effect_stack_api.cpp # Catalog generation + stack execution
  erosion_pipeline.cpp # Erosion stack processing
  gradient_pipeline.cpp # Gradient stack processing
  utility.h           # Shared scalar/vector helpers
  sources/            # Noise generators (Perlin, Worley, Curl)
  commands/           # Processing commands (one struct+function per file)
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

## Command Pattern (`src/commands/`)

Commands are **functions with named arguments**. The struct is just the argument list + outputs, not an abstraction:

```cpp
// laminarize_cmd.h — one command per file for token optimization
typedef struct {
    /* Input (borrowed pointers, caller owns) */
    const vec3* normals;
    uint32_t W, H;
    float scale;

    /* Output (RAII, allocated by the function) */
    std::unique_ptr<vec3[]> result_normals;
} LaminarizeCmd;

int laminarize_Execute(LaminarizeCmd* cmd);
```

Key rules:
- Input fields are **raw borrowed pointers** (caller owns the data)
- Output fields are **`std::unique_ptr`** (RAII, freed when struct goes out of scope)
- One `.h`/`.cpp` pair per command — keeps context windows small
- Function naming: `modulename_Execute(ModuleNameCmd* cmd)`
- Shared utilities (e.g. `heightmap_ops.h`) go in `commands/` as header-only inlines

## Code Style

- C++20, `-Wall -Wextra -Wpedantic -fno-exceptions`
- GLM for all vector math (`using glm::vec3;` etc. in `effect_stack_api.h`)
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
- `reference/BUGS_FOUND.md` - Known issues
- `reference/cli_interface.md` - **CLI effect IDs, param encoding, and usage** (read before invoking the CLI or modifying effect parsing; update when effects are added/changed)
