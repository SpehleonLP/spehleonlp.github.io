# Effect Stack Architecture

## Problem

The erosion texture app needs configurable processing pipelines for both the gradient ramp and erosion texture. A full node graph (Blender-style) is too complex. A flat effect stack causes an options explosion because procedural texture operations (domain warping, noise layering) are inherently compositional. The solution is a thin UI wrapper around WASM-declared capabilities.

## Core Principle

**WASM is the authority.** The UI doesn't know what effects exist. WASM declares them via a catalog, JS generates the UI from that catalog, and the stack state gets pushed back to WASM as a sequence of calls. The JS side is a generic form generator and stack manager.

## Architecture

### 1. Effect Catalog

At startup, WASM's `init_catalog()` function constructs `/effect_catalog.json` on the emscripten virtual filesystem. Each effect module writes its own catalog entry, so the JSON descriptor and the buffer unpacking code live in the same `.c` file. JS then reads the file via `FS.readFile`.

**C side — each module self-describes:**

```c
// smart_blur.c

// Called during init_catalog() — writes this effect's JSON descriptor
void smart_blur_write_catalog(FILE* f) {
    fprintf(f,
        "{\"id\":100,\"name\":\"Smart Blur\",\"params\":["
            "{\"name\":\"Iterations\",\"type\":\"float\",\"min\":1,\"max\":500,\"default\":200,\"step\":1},"
            "{\"name\":\"Threshold\",\"type\":\"float\",\"min\":0.001,\"max\":1,\"default\":0.01,\"step\":0.001}"
        "]}");
}

// Called during stack processing — unpacks the same params from the buffer
void smart_blur_process(const float* buf, int count, /* ... */) {
    float iterations = buf[0];  // matches param 0: Iterations
    float threshold  = buf[1];  // matches param 1: Threshold
    // ...
}
```

**C side — coordinator builds the full catalog:**

```c
// effect_catalog.c

void init_catalog(void) {
    FILE* f = fopen("/effect_catalog.json", "w");

    fprintf(f, "{\"gradient\":{\"groups\":[");
    gradient_layer_write_catalog(f);   // writes Data Source + Color Ramp + Blend Mode group
    fprintf(f, "],\"standalone\":[");
    brightness_contrast_write_catalog(f);
    fprintf(f, "]},\"erosion\":{\"groups\":[");
    fprintf(f, "],\"standalone\":[");
    smart_blur_write_catalog(f);
    fprintf(f, ",");
    fluid_interp_write_catalog(f);
    fprintf(f, "]}}");

    fclose(f);
}
```

**Resulting JSON (example):**

```json
{
  "gradient": {
    "groups": [
      {
        "name": "Gradient Layer",
        "effects": [
          {
            "id": 10,
            "name": "Data Source",
            "params": [
              {"name": "Type", "type": "enum", "options": ["Radial", "Blue Noise", "Perlin"], "default": 0},
              {"name": "Scale", "type": "float", "min": 0.1, "max": 10, "default": 1, "step": 0.1}
            ]
          },
          {
            "id": 11,
            "name": "Color Ramp",
            "params": [
              {"name": "Stops", "type": "color_ramp", "default_stops": [
                {"pos": 0, "r": 0, "g": 0, "b": 0, "a": 1},
                {"pos": 1, "r": 1, "g": 1, "b": 1, "a": 1}
              ]}
            ]
          },
          {
            "id": 12,
            "name": "Blend Mode",
            "params": [
              {"name": "Mode", "type": "enum", "options": ["Normal", "Multiply", "Screen", "Overlay", "Add"], "default": 0},
              {"name": "Opacity", "type": "float", "min": 0, "max": 1, "default": 1, "step": 0.01}
            ]
          }
        ]
      }
    ],
    "standalone": [
      {
        "id": 20,
        "name": "Brightness / Contrast",
        "params": [
          {"name": "Brightness", "type": "float", "min": -1, "max": 1, "default": 0, "step": 0.01},
          {"name": "Contrast", "type": "float", "min": -1, "max": 1, "default": 0, "step": 0.01}
        ]
      }
    ]
  },
  "erosion": {
    "groups": [],
    "standalone": [
      {
        "id": 100,
        "name": "Smart Blur",
        "params": [
          {"name": "Iterations", "type": "float", "min": 1, "max": 500, "default": 200, "step": 1},
          {"name": "Threshold", "type": "float", "min": 0.001, "max": 1, "default": 0.01, "step": 0.001}
        ]
      },
      {
        "id": 101,
        "name": "Fluid Interpolation",
        "params": [
          {"name": "Strength", "type": "float", "min": 0, "max": 2, "default": 1, "step": 0.01}
        ]
      }
    ]
  }
}
```

**Key benefit:** The `_write_catalog` and `_process` functions sit adjacent in each module's `.c` file. When a param is added, renamed, or reordered, both the JSON output and the buffer unpacking are visible in the same context window.

**Param types:**
- `float` — rendered as a slider. Fields: `min`, `max`, `default`, `step`.
- `enum` — rendered as a dropdown. Fields: `options` (string array), `default` (index).
- `color_ramp` — rendered as a gradient stop editor. Fields: `default_stops` (array of `{pos, r, g, b, a}`). Variable-length in the buffer.

### 2. Stack Structure

Each stack (gradient and erosion) has the same structure:

```
[Source]          <- fixed at position 0, cannot be removed
[Auto Effect 1]  <- placed by WASM analysis, tagged data-auto="true"
[Auto Effect 2]  <- also auto-placed
[User Effect 1]  <- manually added by user
[User Effect 2]
...
```

**Source effect:**
- Always at the top, cannot be removed or reordered.
- Gradient ramp source defaults to solid white.
- Erosion texture source defaults to empty (must load a GIF/image).
- User can load an image or GIF. File goes to emscripten VFS.
- No parameters for now.

**Auto effects:**
- Placed by WASM after analyzing the source (see section 4).
- Inserted at position 1 (right after source).
- Tagged with `data-auto="true"` in the DOM.
- Visually distinguished (subtle badge or border).
- User can toggle, tweak params, or remove them. Removing clears the auto tag; the effect won't be swept on next analysis.

**User effects:**
- Added manually from the "Add Effect" menu.
- Appear below auto effects.
- Not touched by auto-config.

**Groups** (e.g., Gradient Layer) appear as a single collapsible card in the UI with sub-sections for each constituent effect. Under the hood, each sub-effect is a separate `push_effect` call. Groups are added/removed as a unit.

### 3. Stack Execution API (JS -> WASM)

When any effect parameter changes, or an effect is added/removed/reordered/toggled, JS sends the entire stack to WASM:

```c
// Set the source file (call before stack_begin if source changed)
void set_source_path(int stack_type, const char* vfs_path);
void set_source_changed(int stack_type, int changed);

// Process the stack
void stack_begin(int stack_type);  // 0 = gradient, 1 = erosion
void push_effect(int effect_id, const float* params, int float_count);
uint8_t* stack_end(int* out_w, int* out_h);  // returns RGBA image
```

**stack_type:** `0` for gradient, `1` for erosion.

**push_effect:** called once per enabled effect, in stack order (top to bottom). Disabled effects are skipped entirely. For groups, each sub-effect is a separate call.

**stack_end:** WASM processes the chain and returns a pointer to an RGBA image buffer. JS reads `out_w` and `out_h` to know dimensions, then uploads to a WebGL texture.

**Source handling:** WASM checks the `changed` flag. If false, uses memoized source data. If true, re-reads the file from VFS.

### 4. Auto-Configuration (WASM -> JS)

When a new source file is loaded, WASM analyzes it and recommends effects. This uses imported JS functions:

```c
// Imported from JS (implemented in JS, called from C)
extern void js_clear_auto_effects(int stack_type);
extern void js_push_auto_effect(int stack_type, int effect_id,
                                const float* params, int param_count);
```

**Flow:**

1. User loads a GIF into the erosion source.
2. JS writes file to VFS, calls `set_source_path(1, "/sources/erosion.gif")` and `set_source_changed(1, 1)`.
3. WASM triggers analysis (frame count, banding estimate, etc.).
4. WASM calls `js_clear_auto_effects(1)` — JS removes all DOM elements with `data-auto="true"` from the erosion stack.
5. WASM calls `js_push_auto_effect(1, 100, blur_params, 2)` — JS creates a Smart Blur effect at position 1 with `data-auto="true"` and the given param values.
6. WASM may call `js_push_auto_effect` additional times for more recommended effects.
7. JS re-renders the stack UI.

**Analysis logic (C side):**

```c
void analyze_source(int stack_type) {
    int frames = get_frame_count();
    float error_width = 256.0f / frames;

    js_clear_auto_effects(stack_type);

    if (error_width >= 2.0f) {
        float blur_params[] = {200.0f, 0.01f};
        js_push_auto_effect(stack_type, EFFECT_SMART_BLUR, blur_params, 2);
    }
    if (error_width > 4.0f) {
        float fluid_params[] = {1.0f};
        js_push_auto_effect(stack_type, EFFECT_FLUID_SOLVER, fluid_params, 1);
    }
}
```

### 5. Float Buffer Encoding

All parameter values are packed as `float32`. Each param type has a specific encoding:

**float params:** Direct value. One float per param.

**enum params:** Index as float. `0.0` = first option, `1.0` = second, etc.

**color_ramp params:** Variable-length. First float is stop count, then `count * 5` floats follow:

```
[stop_count, pos0, r0, g0, b0, a0, pos1, r1, g1, b1, a1, ...]
```

Example: 3-stop ramp from black to red to white:
```
[3.0,  0.0, 0.0, 0.0, 0.0, 1.0,  0.5, 1.0, 0.0, 0.0, 1.0,  1.0, 1.0, 1.0, 1.0, 1.0]
```

The `float_count` passed to `push_effect` is the total number of floats for that effect, including all params. For an effect with one float and one 3-stop color ramp, `float_count = 1 + (1 + 3*5) = 17`.

### 6. JS-Side Stack Manager

The JS stack manager is generic. It:

1. **On init:** calls `init_catalog()` in WASM, then reads the generated `/effect_catalog.json` from VFS via `FS.readFile`. Builds "Add Effect" menus for each stack from the catalog. Renders fixed Source effect at top of each stack.

2. **On user interaction:** generates appropriate controls per param type:
   - `float` -> slider with label, value display, min/max/step from catalog
   - `enum` -> dropdown with options from catalog
   - `color_ramp` -> gradient stop editor widget (library or custom)

3. **On any change:** iterates the stack DOM top-to-bottom, skips disabled effects, packs each enabled effect's params into a `Float32Array`, calls `push_effect` for each, calls `stack_end`, uploads returned image to WebGL texture.

4. **On auto-config callback:** receives `js_clear_auto_effects` and `js_push_auto_effect` calls, manipulates the stack DOM accordingly, marks auto effects with `data-auto="true"` attribute and visual indicator.

5. **On source load:** writes file to VFS via emscripten `FS.writeFile`, calls `set_source_path` and `set_source_changed`. WASM analysis triggers auto-config callbacks.

### 7. UI Rendering

**Group rendering:** A group (e.g., Gradient Layer) is a single collapsible card. Inside it, each sub-effect gets its own labeled section with its params. The whole group is added/removed/toggled as one unit.

**Standalone rendering:** Each standalone effect is its own collapsible card with an enable toggle, parameter controls, and a remove button.

**Auto effects:** Rendered identically to user effects but with a small "Auto" badge or a slightly different border color. User can interact with them normally. Removing an auto effect strips the auto tag (it won't be auto-cleared next time). Editing params keeps the auto tag (it will be cleared and re-created on next source load).

**Source effect:** Rendered as a card at the top of the stack with no remove button. Shows the loaded file name and thumbnail, or "Default (white)" / "No source" if nothing loaded. Has a load button or accepts drag-and-drop.

## 8. Threading Model

All WASM execution happens in a Web Worker (`prototype/worker.js`). The main thread handles UI and WebGL only. Communication is via `postMessage` with `id` fields for request/response correlation.

### Three Layers

```
┌─────────────────────────────────────────────────────────────┐
│ Main Thread (UI + WebGL)                                    │
│   script.js — DOM, sliders, effect stack UI, WebGL canvas   │
│   Communicates with worker via postMessage                  │
└──────────────────────┬──────────────────────────────────────┘
                       │ postMessage (structured clone / transfer)
┌──────────────────────▼──────────────────────────────────────┐
│ Worker Thread (prototype/worker.js)                         │
│   Owns the WASM module instance                             │
│   Translates messages into WASM calls                       │
│   Forwards WASM callbacks back to main thread               │
└──────────────────────┬──────────────────────────────────────┘
                       │ direct function calls
┌──────────────────────▼──────────────────────────────────────┐
│ WASM (C)                                                    │
│   Effect processing, analysis, catalog generation           │
│   Calls imported JS functions for auto-config callbacks     │
└─────────────────────────────────────────────────────────────┘
```

### Messages: Main Thread -> Worker

**`init`**
Sent once at startup. Worker initializes the WASM module, calls `init_catalog()`, reads `/effect_catalog.json` from VFS, and returns it.

```js
// Main sends:
worker.postMessage({ id, type: 'init' });

// Worker responds:
{ id, type: 'ready', catalog: { /* parsed JSON */ } }
```

**`loadSource`**
Sent when the user loads a file (image/GIF). Includes the raw file bytes and target stack. Worker writes the file to VFS, calls `set_source_path` and `set_source_changed`, then triggers WASM analysis. During analysis, WASM auto-config callbacks fire synchronously inside the worker — the worker forwards each as a separate message to the main thread. After analysis completes, worker sends `sourceLoaded`.

```js
// Main sends:
worker.postMessage(
    { id, type: 'loadSource', stackType: 1, fileName: 'erosion.gif', fileData: arrayBuffer },
    [arrayBuffer]  // transfer, not copy
);

// Worker sends (during analysis, before response):
{ type: 'clearAutoEffects', stackType: 1 }
{ type: 'pushAutoEffect', stackType: 1, effectId: 100, params: [200.0, 0.01] }
{ type: 'pushAutoEffect', stackType: 1, effectId: 101, params: [1.0] }

// Worker responds (after analysis complete):
{ id, type: 'sourceLoaded', stackType: 1 }
```

**`processStack`**
Sent when any effect parameter changes, or effects are added/removed/toggled. Includes the full stack configuration as a serialized array of effects. Worker calls `stack_begin` / `push_effect` x N / `stack_end`, then transfers the resulting image back.

```js
// Main sends:
worker.postMessage({
    id,
    type: 'processStack',
    stackType: 0,  // gradient
    effects: [
        { effectId: 10, params: new Float32Array([0.0, 1.0]) },
        { effectId: 11, params: new Float32Array([2.0, 0.0,0.0,0.0,0.0,1.0, 1.0,1.0,1.0,1.0,1.0]) },
        { effectId: 12, params: new Float32Array([0.0, 1.0]) },
    ]
});

// Worker responds:
{ id, type: 'stackResult', stackType: 0, width: 256, height: 256, imageData: Uint8Array }
// imageData transferred (not copied) via Transferable
```

### Messages: Worker -> Main Thread (unsolicited)

These are sent by the worker when WASM auto-config callbacks fire. They have no `id` because they aren't responses to a request — they're events initiated by WASM during source analysis.

```js
{ type: 'clearAutoEffects', stackType: 1 }
{ type: 'pushAutoEffect', stackType: 1, effectId: 100, params: Float32Array }
```

The main thread handles these by manipulating the effect stack DOM (clearing auto effects, inserting new ones with `data-auto="true"`).

### WASM Callback Wiring

In the worker, the emscripten module is configured with imported functions that forward to `postMessage`:

```js
// worker.js — when creating the WASM module
createModule({
    js_clear_auto_effects: function(stackType) {
        self.postMessage({ type: 'clearAutoEffects', stackType });
    },
    js_push_auto_effect: function(stackType, effectId, paramsPtr, paramCount) {
        const params = new Float32Array(
            Module.HEAPF32.buffer, paramsPtr, paramCount
        ).slice();  // copy out of WASM heap
        self.postMessage({ type: 'pushAutoEffect', stackType, effectId, params });
    }
}).then(Module => { /* ... */ });
```

On the C side, these are declared as imports:

```c
// In the C code, declared with EMSCRIPTEN_KEEPALIVE or extern
extern void js_clear_auto_effects(int stack_type);
extern void js_push_auto_effect(int stack_type, int effect_id,
                                const float* params, int param_count);
```

These execute synchronously from C's perspective. The worker's JS implementation sends the message and returns immediately. The main thread processes the messages asynchronously.

### Debounce and Stale Result Handling

When the user drags a slider, `processStack` can fire many times per second. Two mechanisms prevent wasted work:

**1. Debounce on main thread.** The main thread debounces slider `input` events — only sends `processStack` after a brief pause (e.g., 50ms of no changes). This reduces message volume.

**2. Sequence numbers.** Each `processStack` message has an `id`. The main thread tracks the latest `id` it sent. When a `stackResult` arrives, main thread checks: if `result.id < latestSentId`, the result is stale and gets discarded. Only the freshest result is uploaded to WebGL.

```js
// Main thread
let nextId = 0;
let latestSentId = 0;

function sendProcessStack(stackType, effects) {
    const id = nextId++;
    latestSentId = id;
    worker.postMessage({ id, type: 'processStack', stackType, effects });
}

worker.onmessage = (e) => {
    const msg = e.data;
    if (msg.type === 'stackResult') {
        if (msg.id < latestSentId) return;  // stale, discard
        uploadToWebGL(msg);
    }
};
```

**Note:** WASM cannot be interrupted mid-execution. If a stale `processStack` is already running in the worker, it will complete and the result will be discarded by the main thread. The worker processes messages sequentially from its queue — if multiple `processStack` messages pile up, they execute in order but only the last result matters. A future optimization could have the worker check a flag between effects to bail out early, but this is not necessary for v1.

### Worker Lifecycle

```
Main Thread                     Worker Thread
    │                               │
    ├── new Worker('worker.js') ───>│
    │                               ├── importScripts('video_processor.js')
    ├── { type: 'init' } ─────────>│
    │                               ├── createModule(imports)
    │                               ├── init_catalog()
    │                               ├── FS.readFile('/effect_catalog.json')
    │<── { type: 'ready', catalog } ┤
    │                               │
    │   [user loads GIF]            │
    ├── { type: 'loadSource' } ────>│
    │                               ├── FS.writeFile('/sources/erosion.gif')
    │                               ├── set_source_path(1, '/sources/erosion.gif')
    │                               ├── set_source_changed(1, 1)
    │                               ├── analyze_source(1)
    │<── { clearAutoEffects }       ├──── js_clear_auto_effects(1)
    │<── { pushAutoEffect }         ├──── js_push_auto_effect(1, 100, ...)
    │<── { pushAutoEffect }         ├──── js_push_auto_effect(1, 101, ...)
    │<── { type: 'sourceLoaded' } ──┤
    │                               │
    │   [UI rebuilds, user tweaks]  │
    ├── { type: 'processStack' } ──>│
    │                               ├── stack_begin(1)
    │                               ├── push_effect(100, buf, 2)
    │                               ├── push_effect(101, buf, 1)
    │                               ├── stack_end(&w, &h)
    │<── { type: 'stackResult' } ───┤
    │                               │
    ├── upload to WebGL             │
    │                               │
```

## Data Flow Summary

```
┌──────────────────────────────────────────────────────┐
│ Main Thread                                          │
│                                                      │
│  UI: effect stacks, timeline, sliders                │
│  WebGL: renders erosion + gradient textures          │
│                                                      │
│  postMessage ──────────> worker.js                   │
│  onmessage  <────────── worker.js                    │
└──────────────────────────────────────────────────────┘
         │                    ▲
         │ postMessage        │ postMessage
         │ (structured clone  │ (transfer image
         │  + transfer)       │  buffers)
         ▼                    │
┌──────────────────────────────────────────────────────┐
│ Worker Thread (prototype/worker.js)                  │
│                                                      │
│  Owns WASM module instance                           │
│  Translates messages ──> WASM calls                  │
│  Forwards WASM callbacks ──> postMessage             │
└──────────────────────────────────────────────────────┘
         │                    ▲
         │ cwrap calls        │ imported JS functions
         ▼                    │
┌──────────────────────────────────────────────────────┐
│ WASM (C)                                             │
│                                                      │
│  init_catalog() writes /effect_catalog.json to VFS   │
│  Source files on VFS (memoized)                      │
│  Effect processing chain                             │
│  Analysis ──> js_clear_auto_effects()                │
│           ──> js_push_auto_effect() x N              │
└──────────────────────────────────────────────────────┘
```
