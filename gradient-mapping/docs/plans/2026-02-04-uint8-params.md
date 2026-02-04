# Uint8 Parameter Encoding Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Convert all effect parameters from float to uint8 (0-255) encoding for compact URL serialization.

**Architecture:** All UI sliders use 0-255 range. C code unpacks uint8 values to actual ranges using per-parameter formulas. Log scale for large ranges (iterations, scale), linear for bounded ranges.

**Tech Stack:** JavaScript (vanilla), C99/Emscripten WASM

---

## Unpack Formulas Reference

| Type | Formula | Example Params |
|------|---------|----------------|
| `linear01` | `u/255.0` | opacity, jitter, persistence, chebyshev, rgba |
| `linear_signed` | `(u/255.0)*2 - 1` | offset |
| `angle` | `(u/255.0)*2π - π` | gradient angle |
| `linear_range` | `min + (u/255.0)*(max-min)` | lacunarity (1-4), minkowski (-10 to +10) |
| `log_range` | `min * pow(max/min, u/255.0)` | scale (1-100), iterations (1-500), threshold (0.001-1) |
| `int_range` | `min + round((u/255.0)*(max-min))` | octaves (1-16) |
| `enum` | `u` (clamped to max) | metric, mode, blend mode, noise type |
| `seed` | `u * 3922` (maps 0-255 to 0-999999) | noise seed |

---

## Task 1: Add Unpack Helpers to C Header

**Files:**
- Modify: `src/effect_stack_api.h:40-45` (after vec types)

**Step 1: Add unpack macros**

Add after line 40 (after `typedef struct { float position; vec4 color; } ColorStop;`):

```c
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
```

**Step 2: Verify header compiles**

Run: `cd /home/anyuser/websites/spehleonlp.github.io/gradient-mapping/src && emcc -c effect_stack_api.c -I. -o /dev/null 2>&1 | head -20`

Expected: No errors (may need `#include <math.h>` for powf/roundf)

---

## Task 2: Change C API Signature

**Files:**
- Modify: `src/effect_stack_api.h:267`
- Modify: `src/effect_stack_api.c:253`

**Step 1: Update header declaration**

Change line 267 from:
```c
EXPORT void     push_effect(int effect_id, const float* params, int float_count);
```
to:
```c
EXPORT void     push_effect(int effect_id, const uint8_t* params, int param_count);
```

**Step 2: Update implementation signature**

Change line 253 in effect_stack_api.c from:
```c
EXPORT void push_effect(int effect_id, const float* params, int float_count) {
```
to:
```c
EXPORT void push_effect(int effect_id, const uint8_t* params, int param_count) {
```

Also update the variable usage (change `float_count` to `param_count` in the body).

---

## Task 3: Update All Parse Functions in C

**Files:**
- Modify: `src/effect_stack_api.c:69-217`

**Step 1: Update parse_gradient_source (lines 69-76)**

Change from:
```c
static int parse_gradient_source(const float* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_SOURCE_GRADIENT, n, 3)) return 0;
    out->effect_id = EFFECT_SOURCE_GRADIENT;
    out->params.gradient_source.angle = p[0];
    out->params.gradient_source.scale = p[1];
    out->params.gradient_source.offset = p[2];
    return 1;
}
```
to:
```c
static int parse_gradient_source(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_SOURCE_GRADIENT, n, 3)) return 0;
    out->effect_id = EFFECT_SOURCE_GRADIENT;
    out->params.gradient_source.angle = unpack_angle(p[0]);
    out->params.gradient_source.scale = unpack_log_range(p[1], 0.1f, 10.0f);
    out->params.gradient_source.offset = unpack_linear_signed(p[2]);
    return 1;
}
```

**Step 2: Update parse_worley_source (lines 78-87)**

```c
static int parse_worley_source(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_SOURCE_WORLEY, n, 4)) return 0;
    out->effect_id = EFFECT_SOURCE_WORLEY;
    out->params.worley_source.scale = unpack_log_range(p[0], 1.0f, 100.0f);
    out->params.worley_source.jitter = unpack_linear01(p[1]);
    out->params.worley_source.metric = unpack_enum(p[2], 2);
    out->params.worley_source.mode = unpack_enum(p[3], 2);
    return 1;
}
```

**Step 3: Update parse_perlin_source (lines 89-98)**

```c
static int parse_perlin_source(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_SOURCE_PERLIN, n, 4)) return 0;
    out->effect_id = EFFECT_SOURCE_PERLIN;
    out->params.perlin_source.scale = unpack_log_range(p[0], 1.0f, 100.0f);
    out->params.perlin_source.octaves = unpack_int_range(p[1], 1, 16);
    out->params.perlin_source.persistence = unpack_linear01(p[2]);
    out->params.perlin_source.lacunarity = unpack_linear_range(p[3], 1.0f, 4.0f);
    return 1;
}
```

**Step 4: Update parse_curl_source (lines 100-109)**

```c
static int parse_curl_source(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_SOURCE_CURL, n, 4)) return 0;
    out->effect_id = EFFECT_SOURCE_CURL;
    out->params.curl_source.scale = unpack_log_range(p[0], 1.0f, 100.0f);
    out->params.curl_source.octaves = unpack_int_range(p[1], 1, 16);
    out->params.curl_source.persistence = unpack_linear01(p[2]);
    out->params.curl_source.lacunarity = unpack_linear_range(p[3], 1.0f, 4.0f);
    return 1;
}
```

**Step 5: Update parse_noise_source (lines 111-119)**

```c
static int parse_noise_source(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_SOURCE_NOISE, n, 3)) return 0;
    out->effect_id = EFFECT_SOURCE_NOISE;
    out->params.noise_source.type = unpack_enum(p[0], 2);
    out->params.noise_source.scale = unpack_log_range(p[1], 1.0f, 100.0f);
    out->params.noise_source.seed = unpack_seed(p[2]);
    return 1;
}
```

**Step 6: Update parse_dijkstra (lines 123-131)**

```c
static int parse_dijkstra(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_DIJKSTRA, n, 2)) return 0;
    out->effect_id = EFFECT_DIJKSTRA;
    out->params.dijkstra.Minkowski = unpack_linear_range(p[0], -10.0f, 10.0f);
    out->params.dijkstra.Chebyshev = unpack_linear01(p[1]);
    return 1;
}
```

**Step 7: Update parse_fourier_clamp (lines 133-142)**

```c
static int parse_fourier_clamp(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_FOURIER_CLAMP, n, 2)) return 0;
    out->effect_id = EFFECT_FOURIER_CLAMP;
    out->params.fourier_clamp.Minimum = unpack_linear01(p[0]);
    out->params.fourier_clamp.Maximum = unpack_linear01(p[1]);
    return 1;
}
```

**Step 8: Update parse_box_blur (lines 144-152)**

```c
static int parse_box_blur(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_BOX_BLUR, n, 2)) return 0;
    out->effect_id = EFFECT_BOX_BLUR;
    out->params.box_blur.iterations = unpack_log_range(p[0], 1.0f, 500.0f);
    out->params.box_blur.threshold = unpack_log_range(p[1], 0.001f, 1.0f);
    return 1;
}
```

**Step 9: Update parse_gradientify (lines 154-159)**

```c
static int parse_gradientify(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_GRADIENTIFY, n, 1)) return 0;
    out->effect_id = EFFECT_GRADIENTIFY;
    out->params.gradientify.scale = unpack_log_range(p[0], 0.1f, 10.0f);
    return 1;
}
```

**Step 10: Update parse_poisson_solve (lines 161-167)**

```c
static int parse_poisson_solve(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_POISSON_SOLVE, n, 1)) return 0;
    out->effect_id = EFFECT_POISSON_SOLVE;
    out->params.poisson_solve.iterations = (int)unpack_log_range(p[0], 1.0f, 1000.0f);
    return 1;
}
```

**Step 11: Update parse_color_ramp (lines 175-207)**

```c
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

    ColorStop* stops = malloc(sizeof(ColorStop) * stop_count);
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
```

**Step 12: Update parse_blend_mode (lines 209-217)**

```c
static int parse_blend_mode(const uint8_t* p, int n, Effect* out) {
    if (!validate_param_count(EFFECT_BLEND_MODE, n, 2)) return 0;
    out->effect_id = EFFECT_BLEND_MODE;
    out->params.blend_mode.mode = unpack_enum(p[0], 5);
    out->params.blend_mode.opacity = unpack_linear01(p[1]);
    return 1;
}
```

**Step 13: Remove validation helpers that used float ranges**

Delete or comment out `validate_range()` function (lines 54-61) - no longer needed since unpacking handles ranges.

---

## Task 4: Update Worker JS to Send Uint8Array

**Files:**
- Modify: `worker.js:146-164`

**Step 1: Change handleProcessStack to use Uint8Array**

Find in worker.js the section that copies params to WASM heap and change from Float32Array to Uint8Array:

```javascript
function handleProcessStack(msg) {
    const { stackType, effects } = msg;

    cwrapped.stack_begin(stackType);

    for (const effect of effects) {
        const params = effect.params;  // Now Uint8Array
        const byteCount = params.length;

        // Allocate WASM heap space
        const ptr = Module._malloc(byteCount);

        // Copy Uint8Array into WASM HEAPU8
        Module.HEAPU8.set(params, ptr);

        // Call C: push_effect(effect_id, uint8_ptr, param_count)
        cwrapped.push_effect(effect.effectId, ptr, byteCount);

        Module._free(ptr);
    }
    // ... rest unchanged
}
```

**Step 2: Update cwrap signature**

Change push_effect cwrap from `['number', 'number', 'number']` - no change needed since pointers are still numbers.

---

## Task 5: Update Catalog JSON with Unpack Types

**Files:**
- Modify: `scripts/effect_catalog.json`

**Step 1: Add unpack field to each param**

Change each param from:
```json
{"name": "Scale", "type": "float", "min": 1, "max": 100, "default": 10, "step": 1}
```
to:
```json
{"name": "Scale", "type": "float", "min": 1, "max": 100, "default": 10, "unpack": "log"}
```

Full catalog update:

```json
{
  "stacks": {
    "gradient": {
      "id": 0,
      "name": "Gradient Ramp",
      "standalone": [
        {
          "id": 100,
          "name": "Linear Gradient",
          "category": "source",
          "params": [
            {"name": "Angle", "type": "float", "min": -3.14159, "max": 3.14159, "default": 0.785, "unpack": "angle"},
            {"name": "Scale", "type": "float", "min": 0.1, "max": 10, "default": 1, "unpack": "log"},
            {"name": "Offset", "type": "float", "min": -1, "max": 1, "default": 0, "unpack": "signed"}
          ]
        },
        {
          "id": 101,
          "name": "Worley Noise",
          "category": "source",
          "params": [
            {"name": "Scale", "type": "float", "min": 1, "max": 100, "default": 10, "unpack": "log"},
            {"name": "Jitter", "type": "float", "min": 0, "max": 1, "default": 1, "unpack": "linear"},
            {"name": "Metric", "type": "enum", "default": 0, "options": ["Euclidean", "Manhattan", "Chebyshev"]},
            {"name": "Mode", "type": "enum", "default": 0, "options": ["F1", "F2", "F2-F1"]}
          ]
        },
        {
          "id": 102,
          "name": "Perlin Noise",
          "category": "source",
          "params": [
            {"name": "Scale", "type": "float", "min": 1, "max": 100, "default": 10, "unpack": "log"},
            {"name": "Octaves", "type": "float", "min": 1, "max": 16, "default": 4, "unpack": "int"},
            {"name": "Persistence", "type": "float", "min": 0, "max": 1, "default": 0.5, "unpack": "linear"},
            {"name": "Lacunarity", "type": "float", "min": 1, "max": 4, "default": 2, "unpack": "linear"}
          ]
        },
        {
          "id": 103,
          "name": "Curl Noise",
          "category": "source",
          "params": [
            {"name": "Scale", "type": "float", "min": 1, "max": 100, "default": 10, "unpack": "log"},
            {"name": "Octaves", "type": "float", "min": 1, "max": 16, "default": 4, "unpack": "int"},
            {"name": "Persistence", "type": "float", "min": 0, "max": 1, "default": 0.5, "unpack": "linear"},
            {"name": "Lacunarity", "type": "float", "min": 1, "max": 4, "default": 2, "unpack": "linear"}
          ]
        },
        {
          "id": 104,
          "name": "Noise",
          "category": "source",
          "params": [
            {"name": "Type", "type": "enum", "default": 0, "options": ["White", "Blue", "Value"]},
            {"name": "Scale", "type": "float", "min": 1, "max": 100, "default": 1, "unpack": "log"},
            {"name": "Seed", "type": "float", "min": 0, "max": 255, "default": 3, "unpack": "seed"}
          ]
        }
      ],
      "groups": [
        {
          "name": "Color Ramp",
          "effects": [
            {
              "id": 301,
              "name": "Blend",
              "params": [
                {"name": "Mode", "type": "enum", "default": 0, "options": ["Normal", "Multiply", "Screen", "Overlay", "Add", "Subtract"]},
                {"name": "Opacity", "type": "float", "min": 0, "max": 1, "default": 1, "unpack": "linear"}
              ]
            },
            {
              "id": 300,
              "name": "Gradient",
              "params": [
                {"name": "Stops", "type": "color_ramp", "default": [
                  {"position": 0, "color": [0, 0, 0, 1]},
                  {"position": 1, "color": [1, 1, 1, 1]}
                ]}
              ]
            }
          ]
        }
      ]
    },
    "erosion": {
      "id": 1,
      "name": "Erosion Texture",
      "standalone": [
        {
          "id": 200,
          "name": "Dijkstra Distance",
          "category": "effect",
          "params": [
            {"name": "Minkowski", "type": "float", "min": -10, "max": 10, "default": 0, "unpack": "linear"},
            {"name": "Chebyshev", "type": "float", "min": 0, "max": 1, "default": 0, "unpack": "linear"}
          ]
        },
        {
          "id": 201,
          "name": "Fourier Filter",
          "category": "effect",
          "params": [
            {"name": "High Pass", "type": "float", "min": 0, "max": 1, "default": 0, "unpack": "linear"},
            {"name": "Low Pass", "type": "float", "min": 0, "max": 1, "default": 0.2, "unpack": "linear"}
          ]
        },
        {
          "id": 202,
          "name": "Box Blur",
          "category": "effect",
          "params": [
            {"name": "Iterations", "type": "float", "min": 1, "max": 500, "default": 200, "unpack": "log"},
            {"name": "Threshold", "type": "float", "min": 0.001, "max": 1, "default": 0.01, "unpack": "log"}
          ]
        },
        {
          "id": 203,
          "name": "Gradientify",
          "category": "converter",
          "params": [
            {"name": "Scale", "type": "float", "min": 0.1, "max": 10, "default": 1, "unpack": "log"}
          ]
        },
        {
          "id": 204,
          "name": "Poisson Solve",
          "category": "converter",
          "params": [
            {"name": "Iterations", "type": "float", "min": 1, "max": 1000, "default": 100, "unpack": "log"}
          ]
        }
      ],
      "groups": []
    }
  }
}
```

---

## Task 6: Update JS Slider Creation and Value Collection

**Files:**
- Modify: `script.js` (buildParamsHTML, collectParams functions)

**Step 1: Add pack/unpack functions to JS**

Add near top of script.js (after initial declarations):

```javascript
// Pack/unpack functions for uint8 params
const packFunctions = {
  linear: (val, min, max) => Math.round(((val - min) / (max - min)) * 255),
  log: (val, min, max) => Math.round((Math.log(val / min) / Math.log(max / min)) * 255),
  angle: (val) => Math.round(((val + Math.PI) / (2 * Math.PI)) * 255),
  signed: (val) => Math.round(((val + 1) / 2) * 255),
  int: (val, min, max) => Math.round(((val - min) / (max - min)) * 255),
  seed: (val) => Math.round(val / 3922),
  enum: (val) => val
};

const unpackFunctions = {
  linear: (u, min, max) => min + (u / 255) * (max - min),
  log: (u, min, max) => min * Math.pow(max / min, u / 255),
  angle: (u) => (u / 255) * 2 * Math.PI - Math.PI,
  signed: (u) => (u / 255) * 2 - 1,
  int: (u, min, max) => Math.round(min + (u / 255) * (max - min)),
  seed: (u) => u * 3922,
  enum: (u) => u
};
```

**Step 2: Update buildParamsHTML to create 0-255 sliders**

Change slider creation from:
```javascript
html += '<input type="range"' +
    ' min="' + p.min + '"' +
    ' max="' + p.max + '"' +
    ' step="' + p.step + '"' +
    ' value="' + p.default + '"' +
```
to:
```javascript
const defaultU8 = packFunctions[p.unpack || 'linear'](p.default, p.min, p.max);
html += '<input type="range"' +
    ' min="0"' +
    ' max="255"' +
    ' step="1"' +
    ' value="' + defaultU8 + '"' +
    ' data-unpack="' + (p.unpack || 'linear') + '"' +
    ' data-min="' + p.min + '"' +
    ' data-max="' + p.max + '"' +
```

**Step 3: Update slider display value formatting**

Add a function to format the display value for user-facing labels:

```javascript
function formatSliderValue(slider) {
  const u = parseInt(slider.value);
  const unpack = slider.getAttribute('data-unpack') || 'linear';
  const min = parseFloat(slider.getAttribute('data-min')) || 0;
  const max = parseFloat(slider.getAttribute('data-max')) || 1;
  const realValue = unpackFunctions[unpack](u, min, max);

  // Format based on range
  if (unpack === 'angle') {
    return (realValue * 180 / Math.PI).toFixed(1) + '°';
  } else if (unpack === 'int' || unpack === 'seed') {
    return Math.round(realValue).toString();
  } else if (Math.abs(realValue) < 0.01) {
    return realValue.toExponential(2);
  } else if (Math.abs(realValue) < 1) {
    return realValue.toFixed(3);
  } else {
    return realValue.toFixed(1);
  }
}
```

**Step 4: Update collectParams to return Uint8Array**

Change collectParams to return raw uint8 values:

```javascript
function collectParams(container) {
  if (!container) return [];
  const values = [];

  const params = container.querySelectorAll('.param');
  for (const param of params) {
    // Float slider - value is already 0-255
    const slider = param.querySelector('input[type="range"]');
    if (slider) {
      values.push(parseInt(slider.value));
      continue;
    }

    // Enum dropdown - direct index
    const select = param.querySelector('select');
    if (select) {
      values.push(parseInt(select.value));
      continue;
    }

    // Color ramp - convert to uint8
    if (param.classList.contains('param-color-ramp')) {
      const gp = grapickInstances[container.id];
      if (gp) {
        const handlers = gp.getHandlers();
        values.push(handlers.length);  // Stop count (uint8, max 64)
        for (const h of handlers) {
          const pos = h.getPosition() / 100;  // 0-100 -> 0-1
          const rgba = parseColor(h.getColor());
          values.push(Math.round(pos * 255));
          values.push(Math.round(rgba.r * 255));
          values.push(Math.round(rgba.g * 255));
          values.push(Math.round(rgba.b * 255));
          values.push(Math.round(rgba.a * 255));
        }
      } else {
        // Fallback: 2 stops, black-to-white
        values.push(2, 0, 0, 0, 0, 255, 255, 255, 255, 255, 255);
      }
    }
  }

  return values;
}
```

**Step 5: Update serializeStack to use Uint8Array**

Change `new Float32Array(params)` to `new Uint8Array(params)`:

```javascript
effects.push({ effectId, params: new Uint8Array(params) });
```

---

## Task 7: Update URL Serialization

**Files:**
- Modify: `script.js` (stackToUrlFormat, urlFormatToStack, serializeUrlState)

**Step 1: Simplify stackToUrlFormat**

Since params are now uint8, we can encode more efficiently:

```javascript
function stackToUrlFormat(effects) {
  // Format: [effectId, ...params] where all values are 0-255
  return effects.map(eff => {
    return [eff.effectId, ...Array.from(eff.params)];
  });
}
```

**Step 2: Update URL encoding to use raw bytes**

Change serializeUrlState from JSON+base64 to direct byte encoding:

```javascript
function serializeUrlState() {
  const params = new URLSearchParams();

  // Serialize stacks as compact byte strings
  const gradientEffects = serializeStack(gradientStack);
  const erosionEffects = serializeStack(erosionStack);

  if (gradientEffects.length > 0) {
    const bytes = encodeStackToBytes(gradientEffects);
    params.set('g', bytesToBase64(bytes));
  }

  if (erosionEffects.length > 0) {
    const bytes = encodeStackToBytes(erosionEffects);
    params.set('e', bytesToBase64(bytes));
  }
  // ... rest unchanged (d, fi, fo, demo params)
}

// Encode stack as: [count, effectId1, paramCount1, ...params1, effectId2, ...]
function encodeStackToBytes(effects) {
  const bytes = [effects.length];
  for (const eff of effects) {
    // Effect ID as 2 bytes (little endian, IDs are 100-301)
    bytes.push(eff.effectId & 0xFF);
    bytes.push((eff.effectId >> 8) & 0xFF);
    // Param count
    bytes.push(eff.params.length);
    // Params
    for (const p of eff.params) {
      bytes.push(p);
    }
  }
  return new Uint8Array(bytes);
}

function bytesToBase64(bytes) {
  return btoa(String.fromCharCode(...bytes));
}

function base64ToBytes(str) {
  return new Uint8Array(atob(str).split('').map(c => c.charCodeAt(0)));
}
```

**Step 3: Update URL decoding**

```javascript
function decodeStackFromBytes(bytes, stackCatalog) {
  const effects = [];
  let i = 0;
  const count = bytes[i++];

  for (let e = 0; e < count && i < bytes.length; e++) {
    const effectId = bytes[i] | (bytes[i + 1] << 8);
    i += 2;
    const paramCount = bytes[i++];
    const params = bytes.slice(i, i + paramCount);
    i += paramCount;

    const effectDef = findEffectDef(stackCatalog, effectId);
    if (effectDef) {
      effects.push({ effectId, params: new Uint8Array(params), def: effectDef });
    }
  }

  return effects;
}
```

---

## Task 8: Update Auto-Effect Push from C

**Files:**
- Modify: `src/effect_stack_api.c:413-422`
- Modify: `worker.js` (js_push_auto_effect handler)

**Step 1: Change js_push_auto_effect calls to use uint8 params**

In effect_stack_api.c, the auto-effect params need to be uint8:

```c
if (bits > 7.0f) {
    /* High precision - no modification needed */
} else if (bits > 5.0f) {
    /* Medium precision - use iterated blur */
    /* iterations=32 -> pack: log(32/1)/log(500/1)*255 ≈ 142 */
    /* threshold=0.01 -> pack: log(0.01/0.001)/log(1/0.001)*255 ≈ 85 */
    uint8_t blur_params[] = { 142, 85 };
    js_push_auto_effect(stack_type, 202, blur_params, 2);
} else {
    /* Low precision - use dijkstra then low pass */
    /* minkowski=1 -> pack: (1+10)/20*255 ≈ 140 */
    /* chebyshev=0 -> 0 */
    uint8_t dijkstra_params[] = { 140, 0 };
    js_push_auto_effect(stack_type, 200, dijkstra_params, 2);

    /* high_pass=0, low_pass=0.15 -> pack: 0.15*255 ≈ 38 */
    uint8_t fourier_params[] = { 0, 38 };
    js_push_auto_effect(stack_type, 201, fourier_params, 2);
}
```

**Step 2: Update js_push_auto_effect signature**

In effect_stack_api.h line 309-310, change:
```c
extern void js_push_auto_effect(int stack_type, int effect_id,
                                const float* params, int param_count);
```
to:
```c
extern void js_push_auto_effect(int stack_type, int effect_id,
                                const uint8_t* params, int param_count);
```

---

## Task 9: Build and Test

**Step 1: Rebuild WASM**

```bash
cd /home/anyuser/websites/spehleonlp.github.io/gradient-mapping/src
emcmake cmake . -G Ninja
ninja
```

**Step 2: Manual test**

1. Serve locally: `python3 -m http.server 8000`
2. Open http://localhost:8000
3. Test each effect type - verify sliders work and values display correctly
4. Test URL sharing - copy URL, paste in new tab, verify state restores
5. Verify URL length is shorter than before

---

## Summary

This refactor:
- Changes all effect params from float to uint8 (0-255)
- Adds unpack helpers to C for converting uint8 to actual ranges
- Uses log scale for large ranges (iterations, scale)
- Simplifies URL encoding (bytes instead of JSON floats)
- Maintains backward compatibility through catalog-driven UI
