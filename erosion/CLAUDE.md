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

**Future Enhancement**: For very low frame rates (error_width >> 10), simple blur may not be sufficient and smarter interpolation may be needed.

**Implementation**: See `smart_blur.c` for the diffusion algorithm. The context stores `values`, `min_values` (error bar lower bounds), `max_values` (error bar upper bounds), and `temp_values` for double-buffering.

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
- GIF frames are parsed using the `omggif` library (loaded from CDN)
- Frame disposal methods are handled correctly (restore to background, restore to previous, overlay)
- Frame delays are used to calculate total duration
- Partial frames are composited onto previous frames
- Both videos and GIFs go through the same WASM processing pipeline

## Testing the Effect

1. Open `index.html` in a browser with WebGL2 support
2. Load a video or GIF using the "Load Video/GIF" button - this triggers WASM processing
3. Drag/drop video or GIF files directly onto the canvas
4. Drag/drop images onto "Erosion Texture" and "Gradient Texture" areas to use custom textures
5. Adjust sliders (Fade In/Out Duration, Lifetime, Rate) to modify effect timing
6. Use time slider and Play/Pause to preview animation
7. Click "Save Video" to export as WebM (disabled on iOS)
