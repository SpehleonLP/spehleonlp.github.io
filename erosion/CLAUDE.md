# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a WebGL-based alpha erosion video effect application that creates animated dissolve effects. The project uses WebAssembly (compiled from C) to process video frames and generate specialized textures (envelopes and gradients), which are then used by WebGL shaders to create erosion effects in real-time.

## Build Commands

### WebAssembly Compilation

Build the WASM module from C sources:
```bash
cd src
make
```

This compiles `video_processor.c`, `create_envelopes.c`, and `create_gradient.c` into:
- `../scripts/video_processor.js`
- `../scripts/video_processor.wasm`

Clean build artifacts:
```bash
cd src
make clean
```

## Architecture

### Two-Phase Processing Pipeline

The application uses a sophisticated two-phase pipeline to analyze video frames and generate textures:

1. **Envelope Detection Phase** (C, in `create_envelopes.c`):
   - Processes each frame's alpha channel pixel-by-pixel
   - Tracks per-pixel "envelopes" with attack/sustain/release states (similar to audio ADSR)
   - Identifies when each pixel fades in (attack), holds (sustain), and fades out (release)
   - Builds a 2D "erosion texture" where:
     - R channel: inverted normalized attack timing (when pixel appears)
     - G channel: normalized release timing (when pixel disappears)
     - B channel: edge softness (based on fade speed)
   - This texture drives the temporal behavior of the erosion effect

2. **Gradient Reconstruction Phase** (C, in `create_gradient.c`):
   - Uses the envelope data to reverse-engineer source colors
   - Creates either a 2D "boom ramp" texture or 3D gradient cube
   - Uses trilinear interpolation with weighted accumulation (`g_ReverseBlend`)
   - The gradient stores the original pixel colors mapped to erosion timing
   - Shader samples this gradient using erosion texture coordinates to recreate original appearance

### WebGL Rendering (JavaScript + GLSL)

The fragment shader (`shaders/erosion.frag`) combines the envelope and gradient textures:
- Uses the erosion texture to determine per-pixel fade timing
- Computes `fadeInFactor` and `fadeOutFactor` based on current time
- Samples the gradient texture (2D or 3D) to get the color at that timing
- Applies edge softness modulation for smooth transitions
- Output alpha is the product of fade factors, creating the dissolve effect

### Video Export Pipeline

When saving videos (`scripts/create_video.js`):
- Creates an offscreen framebuffer matching source video dimensions
- Renders frames at high FPS (120) by stepping through time values
- Captures each frame as ImageData from the framebuffer
- Writes frames as PNG files to FFmpeg.js virtual filesystem
- Uses FFmpeg Web Worker to encode frames into VP9 WebM video

### Key WebAssembly Exports

The C code exports these functions (see `video_processor.c`):
- `initialize(width, height)` - Sets up processing for given dimensions
- `push_frame(data, byteLength)` - Adds a video frame for analysis
- `finishPushingFrames()` - Triggers envelope building
- `computeGradient()` - Generates gradient texture from frames
- `get_image(id)` - Returns processed texture (0=erosion, 1=gradient)
- `GetMetadata()` - Returns computed fade durations
- `shutdownAndRelease()` - Cleanup

## File Organization

- `src/` - C source files for WASM video processing
  - `video_processor.c` - Main WASM interface and coordination
  - `create_envelopes.c` - Per-pixel envelope detection algorithm
  - `create_gradient.c` - Gradient texture generation with reverse blending
  - `smart_blur.c` - Constraint-based iterative diffusion for sub-frame timing precision
  - `Makefile` - Emscripten build configuration
- `shaders/` - GLSL shader files
  - `erosion.frag` - Fragment shader implementing erosion effect
  - `erosion.vert` - Vertex shader for fullscreen quad
- `scripts/` - JavaScript modules
  - `document_gl.js` - WebGL setup, texture management, main render loop
  - `create_video.js` - Video capture and FFmpeg encoding
  - `create_textures.js` - Video loading and WASM texture generation
  - `video_processor.js/wasm` - Generated WASM module
  - `worker.js` - Web Worker for WASM processing
  - `ffmpeg-worker.js` - Web Worker for video encoding
- `index.html` - Main application UI with drag-drop texture areas and sliders

## Coding Style

### Command Pattern for Data Transforms

Prefer a **functional command pattern** for data processing operations:

1. **Command structs**: Define a struct containing all parameters for an operation
2. **Pure transforms**: Functions take a command struct, perform a data transform, and return with no side effects
3. **Chainable design**: Results from one operation can feed into the next (C-style fluid interface)

**Example** (from `debug_png.h`):
```c
typedef struct PngFloatCmd {
    const char* path;
    const float* data;
    uint32_t width, height;
    float min_val, max_val;
    int auto_range;
} PngFloatCmd;

int png_ExportFloat(PngFloatCmd* cmd);
```

**Benefits**:
- Self-documenting: struct fields name all parameters
- Easy to extend: add fields without changing function signatures
- Composable: chain transforms by passing output of one as input to next
- Testable: pure functions with explicit inputs/outputs

**Apply this pattern** when adding new image processing algorithms, data transforms, or multi-step pipelines.

## Working with the Codebase

### Modifying the Envelope Algorithm

The envelope detection logic in `create_envelopes.c` uses a state machine (NOT_IN_ENVELOPE → IN_ATTACK → IN_SUSTAIN → IN_RELEASE). When modifying:
- State transitions are in `e_ProcessFrame()`
- The "best envelope" selection uses area comparison (`compare_env`)
- Thresholds: `ALPHA_THRESHOLD=16`, `NOISE_FRAMES=4`, `NOISE_ALPHA=32`
- To debug a specific pixel, set `LOOP` to 0 and adjust `TEST_X`/`TEST_Y`

### Smart Blur for Low Frame Rates

The erosion texture building in `e_Build()` uses a constraint-based smart blur (`smart_blur.c`) to achieve sub-frame precision from low frame rate sources (e.g., 15fps key animations).

**Problem**: With discrete frame numbers, low frame rate sources create visible "steps" in the normalized R/G timing channels. The quantization error per frame is `error_width = 256.0 / total_frames`. For example, with 15 frames, each frame index has ±8.5 value uncertainty in [0, 255] output space, causing severe banding.

**Understanding Error Bars**: The envelope detection gives us frame ranges, not single frames:
- `[attack_start, attack_end]` - the range of frames where the key press happened
- `[release_start, release_end]` - the range of frames where the key release happened
- These ranges ARE the error bars on the true timing

**Solution**: Conditional smart blur based on error_width:
1. **Skip blur** if `error_width < 2.0` (high FPS, low quantization error)
   - Use midpoint of envelope ranges directly
2. **Apply smart blur** if `error_width >= 2.0` (low FPS, high quantization error)
   - Normalize envelope ranges to [0, 255] space: `[attack_start_norm, attack_end_norm]` and `[release_start_norm, release_end_norm]`
   - Initialize values to midpoint of normalized ranges
   - Iteratively blur (3x3 box filter) while clamping each pixel to its valid range
   - Converge to stable solution (typically 100-200 iterations at threshold 0.01)

The constraints prevent decay to uniform grey - values can only take on what's valid based on the envelope data. Where neighboring pixels have different envelope frames, the constraints naturally create smooth gradients that respect the known transition points.

**Future Enhancement**: For very low frame rates (error_width >> 10), simple blur may not be sufficient and smarter interpolation may be needed. See [fluid_interpolation_concept.md](reference/fluid_interpolation_concept.md) for experimental approach using Navier-Stokes fluid dynamics to create organic, swirly patterns in severely undersampled envelope data (e.g., 4 keyframes → error_bars = 64).

**Implementation**: See `smart_blur.c` for the diffusion algorithm. The context stores `values`, `min_values` (error bar lower bounds), `max_values` (error bar upper bounds), and `temp_values` for double-buffering.

**Banding Detection**: See `e_MeasureBanding()` in `create_envelopes.c` for algorithm that detects severe banding by measuring band widths and minimal risers. Used to decide between smart blur, fluid solver, or raw values.

### Chromakey Behavior

The `GetAlpha()` function in `create_envelopes.c` has two modes:

**Greyscale Mode** (auto-detected when key color has R≈G≈B):
- Uses luminance-based alpha extraction
- If key is white: white→transparent, black→opaque, grey→proportional
- If key is black: black→transparent, white→opaque, grey→proportional
- Useful for GIFs/videos that encode alpha as greyscale values

**Color Chromakey Mode** (when key is a distinct color):
- Uses Euclidean distance in RGB space (not angular similarity)
- Applies power function (γ=0.6) for steeper falloff and cleaner edges
- 32-value threshold eliminates near-key colors
- Better for greenscreen/bluescreen footage with less color bleed

The key color is sampled from the top-left pixel (0,0) of the first frame. Only activated if key alpha > 32.

### Modifying Shader Behavior

The erosion effect calculations are in `GetColorOriginal()` in `erosion.frag`:
- `f_life.r` is normalized fade-in time, `f_life.g` is fade-out time
- Edge softness from erosion texture B channel modulates fade factors
- When `u_has3DGradient` is set, uses 3D texture lookup instead of 2D ramp blend

### Texture Coordinate Systems

- **Erosion Texture**: RG channels encode per-pixel timing (R=attack, G=release)
- **2D Gradient ("Boom Ramp")**: X axis is time (0-0.5 fade in, 0.5-1.0 fade out), Y is fade factor
- **3D Gradient Cube**: XY from erosion RG, Z is overall progress through lifetime

### Memory Management

The C code uses manual memory allocation:
- `video->frames[]` stores pointers to all pushed frames (grows via realloc)
- `builder->pixels` array holds per-pixel envelope state (width × height)
- `g->data` holds the gradient accumulation buffer (width × height × depth)
- Always call `shutdownAndRelease()` to free all allocated memory

## Cross-Origin Isolation

The application requires cross-origin isolation for SharedArrayBuffer (needed by FFmpeg.js). This is handled by `scripts/coi-serviceworker.js` which sets appropriate headers via service worker.

## GIF Support

The application can load animated GIFs as input:
- GIF decoding is done entirely in C/WASM using `stb_image.h` (`gif_decoder.c`)
- Raw GIF bytes are passed from JS to the `push_gif()` function
- Frame disposal methods and compositing are handled by stb_image internally
- Frame delays are summed to calculate total duration
- Both videos and GIFs go through the same WASM processing pipeline

## Testing the Effect

1. Open `index.html` in a browser with WebGL2 support
2. Load a video or GIF using the "Load Video/GIF" button - this triggers WASM processing
3. Drag/drop video or GIF files directly onto the canvas
4. Drag/drop images onto "Erosion Texture" and "Gradient Texture" areas to use custom textures
5. Adjust sliders (Fade In/Out Duration, Lifetime, Rate) to modify effect timing
6. Use time slider and Play/Pause to preview animation
7. Click "Save Video" to export as WebM (disabled on iOS)

## Current Work: Quantized Image Interpolation

### Problem Statement

We have heavily quantized images (3-bit color = 8 levels) that need to be converted to 8-bit (256 levels) by smoothly interpolating within the bands to remove banding artifacts.

### Previous Approach (Pixel-Based)

Used `measure_grad_distance.c` / `VoroniInput`:
1. For each pixel, find closest pixel with different value (first boundary)
2. Find closest pixel that's neither current value nor first boundary value (second boundary)
3. Lerp by distance between these two boundaries
4. For pixels with no second boundary, find the "middle" of the region (ridge/valley) and lerp toward it

**Result**: Worked reasonably well but not well enough. The pixel-grid representation caused stair-step artifacts on diagonal edges.

### Current Approach (Contour-Based)

Converted to mesh representation for more accurate boundaries:

1. **ContourExtractCmd**: Extract boundaries between different pixel values as polylines
   - Two-pass algorithm: horizontal scan for vertical edges, vertical scan for horizontal edges
   - Each contour stores `val_low` and `val_high` (the values on either side)
   - Vertices can appear in multiple contours (at triple junctions where 3+ regions meet)

2. **ContourSmoothCmd**: Smooth stair-step corners
   - Detects L-corners (where horizontal segment meets vertical segment)
   - Samples image neighborhood to detect if edge is actually diagonal
   - Moves corners toward midpoint of neighbors, scaled by "diagonality"

### Proposed Interpolation (Option B - Inverse Distance Weighting)

For each pixel with quantized value V:

1. Find all contours where `V ∈ {val_low, val_high}` (boundaries of this region)
2. For each such contour, compute distance to nearest line segment
3. The "neighbor value" is the other value in the pair (not V)
4. Inverse-distance weighted blend:
   ```
   new_value = Σ(neighbor_value / distance) / Σ(1 / distance)
   ```

**Advantages**:
- Handles regions surrounded by 4+ different values naturally
- Sub-pixel accurate boundaries from smoothed contours
- Distance to line segments is continuous (no stair-stepping)

**Key insight**: A pixel only "sees" contours that bound its region. If pixel has value 2, it ignores the 0-1 boundary because 2 is not in that pair.

### Still Needs Implementation

Create `contour_lerp.c` (or similar) with:

```c
typedef struct ContourLerpCmd {
    const uint8_t *src;      // Original quantized image
    uint32_t W, H;
    const ContourSet *contours;  // Smoothed contours
    float *output;           // Interpolated float output [0, 255] or [0, 1]
} ContourLerpCmd;

int contour_lerp(ContourLerpCmd *cmd);
```

Algorithm:
1. For each pixel (x, y):
   - Get value V = src[y * W + x]
   - Initialize weighted_sum = 0, weight_total = 0
   - For each contour where V == val_low or V == val_high:
     - Find minimum distance from (x, y) to any segment in that contour
     - neighbor_val = (V == val_low) ? val_high : val_low
     - weight = 1.0 / max(distance, epsilon)
     - weighted_sum += neighbor_val * weight
     - weight_total += weight
   - If weight_total > 0: output = weighted_sum / weight_total
   - Else: output = V (no nearby boundaries, keep original)

### Open Questions

- Do we even need contours? Could use `FloodFillCmd` with `ff_rule_distance` seeded from boundary pixels instead. Contours give sub-pixel accuracy for diagonals, but adds complexity.
- For medial axis / ridge finding: would need to stitch contours that share a region value into complete region borders, then find skeleton.

## Command Pattern Reference

All command structs follow the pattern: define inputs/options, call `*_Execute()` or similar, use outputs, call `*_Free()` to cleanup.

### Image Analysis & Transforms

#### ChamferCmd (`chamfer.h`)
Simple chamfer distance transform - finds nearest pixel with different value.
```c
ChamferCmd cmd = {
    .src = image,           // uint8_t* input image
    .W = width, .H = height,
    .nearest = out_points,  // ChamferPoint* output (caller allocates)
    .distance = out_dist,   // float* optional distance output
};
chamfer_compute(&cmd);
```

#### LabelRegionsCmd (`label_regions.h`)
Connected components labeling - assigns unique IDs to contiguous regions.
```c
LabelRegionsCmd cmd = {
    .src = image,
    .W = width, .H = height,
    .connectivity = LABEL_CONNECT_4,  // or LABEL_CONNECT_8
    .labels = out_labels,   // int32_t* output (caller allocates)
};
label_regions(&cmd);
// cmd.num_regions contains count
```

#### VoroniInput (`measure_grad_distance.h`)
Interpolates heavily quantized images using distance to band edges.
```c
VoroniInput cmd = {
    .src = quantized_image,  // uint8_t* input
    .dst = float_output,     // float* output
    .sources = NULL,         // QuadSources* (allocated internally if NULL)
    .W = width, .H = height,
};
measure_distances_to_edges(&cmd);  // populates sources
voroni_crackle(&cmd);              // interpolates to dst
```

#### SmartBlurContext (`smart_blur.h`)
Constraint-based diffusion - blurs while respecting per-pixel min/max bounds.
```c
SmartBlurContext* ctx = sb_Initialize(width, height);
sb_SetValue(ctx, x, y, value);  // for each pixel
sb_Setup(ctx);
sb_RunUntilConverged(ctx, 0.01f, 500);  // threshold, max_iters
float result = sb_GetValue(ctx, x, y);
sb_Free(ctx);
```

### Contour Operations

#### ContourExtractCmd (`contour_extract.h`)
Extracts boundaries between different pixel values as polylines.
```c
ContourExtractCmd cmd = {
    .src = image,
    .W = width, .H = height,
    .extract_all_levels = 1,  // extract all boundaries
};
contour_extract(&cmd);
// cmd.result is ContourSet* with lines, each having val_low/val_high
contour_free(cmd.result);
```

#### ContourSmoothCmd (`contour_smooth.h`)
Smooths stair-step corners on contours by detecting diagonal edges.
```c
ContourSmoothCmd cmd = {
    .src = image,             // original image for gradient sampling
    .W = width, .H = height,
    .contours = contour_set,  // modified in place
    .radius = 5,              // sampling radius for edge detection
    .max_shift = 1.5f,        // max vertex shift in pixels
};
contour_smooth(&cmd);
```

### Vector Field Operations

#### GradientCmd (`normal_map.h`)
Computes 2D gradient (velocity field) from height data.
```c
GradientCmd cmd = {
    .width = w, .height = h,
    .height_data = heights,   // float* input
    .channel = 0,             // for interlaced data
    .stride = 1,              // 1 = non-interlaced, 4 = RGBA-style
    .zero_value = 0.0f,       // treated as "no data"
};
grad_Execute(&cmd);
// cmd.gradient is vec2* output
grad_Free(&cmd);
```

#### NormalMapCmd (`normal_map.h`)
Computes 3D surface normals from height field.
```c
NormalMapCmd cmd = {
    .width = w, .height = h,
    .height_data = heights,
    .channel = 0, .stride = 1,
    .scale = 1.0f,            // height scale (affects steepness)
};
nm_Execute(&cmd);
// cmd.normals is vec3* output
nm_Free(&cmd);
```

#### HeightFromNormalsCmd (`normal_map.h`)
Reconstructs height map from normal map via Poisson solve.
```c
HeightFromNormalsCmd cmd = {
    .width = w, .height = h,
    .normals = normal_map,    // vec3* input
    .iterations = 100,
    .scale = 1.0f,
};
height_from_normals_Execute(&cmd);
// cmd.heightmap is float* output
height_from_normals_Free(&cmd);
```

#### HelmholtzCmd (`helmholtz.h`)
Helmholtz-Hodge decomposition - splits velocity into divergence-free + curl-free.
```c
HelmholtzCmd cmd = {
    .width = w, .height = h,
    .velocity = vel_field,    // vec2* input
    .iterations = 40,
};
helmholtz_Execute(&cmd);
// cmd.incompressible = divergence-free component (vec2*)
// cmd.gradient = curl-free component (vec2*)
helmholtz_Free(&cmd);
```

#### SwirlCmd (`swirl.h`)
Generates divergence-driven rotational swirl fields.
```c
SwirlCmd cmd = {
    .width = w, .height = h,
    .velocity = vel_field,
    .divergence = NULL,       // computed internally if NULL
    .strength = 1.0f,
};
swirl_Execute(&cmd);
// cmd.swirl is vec2* output
swirl_Free(&cmd);
```

#### ContourFlowCmd (`contour_flow.h`)
Computes flow along contour lines (tangent to gradient).
```c
ContourFlowCmd cmd = {
    .width = w, .height = h,
    .heightmap = heights,
    .ridge_mode = CF_RIDGE_BOTH,
    .ridge_threshold = 0.1f,
    .min_gradient = 0.01f,
};
cf_Execute(&cmd);
// cmd.flow = tangent flow field (vec2*)
// cmd.direction = chosen direction at each pixel (int8_t*)
cf_Free(&cmd);
```

#### FluidSolver (`fluid_solver.h`)
High-level coordinator for fluid field analysis pipeline.
```c
FluidSolver fs = {
    .width = w, .height = h,
    .height_interlaced0to4 = height_data,  // 4-channel interlaced
};
fs_Setup(&fs);
// fs.vel0, fs.vel3 = velocity fields
// fs.incomp0, fs.incomp3 = divergence-free
// fs.grad0, fs.grad3 = curl-free
// fs.swirl0, fs.swirl3 = rotational
fs_Free(&fs);
```

### Flood Fill

#### FloodFillCmd (`flood_fill.h`)
Priority-queue flood fill with customizable automata rules.
```c
FFSeed seeds[] = {{x, y, 0.0f}};
FloodFillCmd cmd = {
    .width = w, .height = h,
    .seeds = seeds,
    .seed_count = 1,
    .rule = ff_rule_distance,  // or ff_rule_chamfer, ff_rule_weighted_avg, etc.
    .connectivity = FF_CONNECT_8,
};
ff_Execute(&cmd);
// cmd.output is float* distance field
ff_Free(&cmd);
```

Built-in rules: `ff_rule_distance`, `ff_rule_chamfer`, `ff_rule_weighted_avg`, `ff_rule_min`, `ff_rule_max`, `ff_rule_average`.

### FFT / Frequency Domain

#### FFTBlurContext (`fft_blur.h`)
FFT-based low/high pass filtering.
```c
FFTBlurContext ctx;
fft_Initialize(&ctx, next_pow2(w), next_pow2(h));
fft_LoadChannel(&ctx, &image, stride, channel);
fft_LowPassFilter(&ctx, 0.5f, 0);  // keep 50% of frequencies
fft_CopyBackToImage(&image, &ctx, stride, channel);
fft_Free(&ctx);
```

#### ResizingImage (`fft_blur.h`)
Helper for resizing images to power-of-2 dimensions.
```c
ResizingImage src = {.width = w, .height = h, .data = float_data};
ResizingImage dst = {.width = next_pow2(w), .height = next_pow2(h)};
fft_ResizeImage(&dst, &src);
// dst.data is allocated, dst.original tracks which pixels were interpolated
```

### Debug PNG Export

All in `debug_png.h`, only compiled when `DEBUG_IMG_OUT=1`.

#### PngFloatCmd
```c
PngFloatCmd cmd = {
    .path = "/debug.png",
    .data = float_array,
    .width = w, .height = h,
    .auto_range = 1,  // or set min_val/max_val manually
};
png_ExportFloat(&cmd);
```

#### PngVec2Cmd
```c
PngVec2Cmd cmd = {
    .path = "/vectors.png",
    .data = vec2_array,
    .width = w, .height = h,
    .scale = 1.0f,
};
png_ExportVec2(&cmd);
```

#### PngVec3Cmd
```c
PngVec3Cmd cmd = {
    .path = "/normals.png",
    .data = vec3_array,
    .width = w, .height = h,
};
png_ExportVec3(&cmd);
```

#### PngInterleavedCmd
```c
PngInterleavedCmd cmd = {
    .path = "/channel.png",
    .data = interlaced_floats,
    .width = w, .height = h,
    .channel = 0, .stride = 4,
    .auto_range = 1,
};
png_ExportInterleaved(&cmd);
```

#### PngGridCmd
```c
PngGridTile tiles[] = {
    {PNG_TILE_GRAYSCALE, data1},
    {PNG_TILE_VEC2, data2},
};
PngGridCmd cmd = {
    .path = "/grid.png",
    .tile_width = w, .tile_height = h,
    .cols = 2, .rows = 1,
    .tiles = tiles,
};
png_ExportGrid(&cmd);
```

### Contour Debug Export

```c
// Render contours to image and export
debug_export_contours("/contours.png", contour_set,
                      out_w, out_h,           // output size
                      src_gray, src_w, src_h); // optional background
```

### Ridge MST Debug Export (`measure_grad_distance.h`)

```c
RidgeMST mst;
extract_ridge_mst(&voroni_input, 1.0f, &mst);
debug_export_ridge_mst("/ridges.png", &mst, src_gray);
free_ridge_mst(&mst);
```
