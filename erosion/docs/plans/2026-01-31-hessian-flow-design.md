# Hessian-Based Flow Field Generation

**Date:** 2026-01-31
**Status:** Design approved, ready for implementation
**Location:** `prototype/src/commands/hessian_flow.{h,c}`

## Overview

Generate flow fields from heightmaps using Hessian eigendecomposition. The goal is to create "flowy" effects by amplifying features along the direction of minimum curvature (the minor eigenvector).

## Motivation

Previous attempts at flow field generation (e.g., `ContourFlowCmd`) didn't produce usable results. The Hessian approach provides a principled way to find flow directions:

- **Major eigenvector**: points across ridges/valleys (maximum curvature)
- **Minor eigenvector**: points along ridges/valleys (minimum curvature)

Amplifying the minor eigenvector should enhance flow along natural features.

## Architecture

Three composable commands following the project's command pattern:

1. **HessianCmd**: Heightmap → Hessian matrix field
2. **EigenDecomposeCmd**: Hessian field → Eigenvalue/vector pairs
3. **HessianFlowDebugCmd**: Full pipeline with comprehensive debug visualization

## Data Structures

```c
/* Hessian matrix (2x2 symmetric) at a single pixel */
typedef struct {
    float xx;  // ∂²f/∂x²
    float xy;  // ∂²f/∂x∂y
    float yy;  // ∂²f/∂y²
} Hessian2D;

/* Eigenvalue + eigenvector pair */
typedef struct {
    vec2 vector;  // normalized eigenvector direction
    float value;  // eigenvalue magnitude
} EigenVec2;
```

**Design notes:**
- `Hessian2D` is semantically distinct from `vec3` despite same memory layout
- `EigenVec2` bundles eigenvalue with direction for clarity
- Both are plain structs compatible with existing command infrastructure

## Command 1: Hessian Computation

```c
typedef struct {
    /* Input */
    const float* heightmap;
    uint32_t W, H;
    int kernel_size;     // 3 or 5 (for 3x3 or 5x5 stencil)

    /* Output (allocated by caller or by hessian_Execute) */
    Hessian2D* hessian;  // W*H array of Hessian matrices
} HessianCmd;

int hessian_Execute(HessianCmd* cmd);
void hessian_Free(HessianCmd* cmd);
```

**Implementation approach:**
- Central finite differences for interior pixels
- Forward/backward differences at boundaries
- 3x3 kernel: standard 3-point stencils
- 5x5 kernel: 5-point stencils for smoother second derivatives

**Finite difference formulas (assuming dx=dy=1):**

3x3 kernel:
```
∂²f/∂x² ≈ f[x-1,y] - 2*f[x,y] + f[x+1,y]
∂²f/∂y² ≈ f[x,y-1] - 2*f[x,y] + f[x,y+1]
∂²f/∂x∂y ≈ (f[x+1,y+1] - f[x-1,y+1] - f[x+1,y-1] + f[x-1,y-1]) / 4
```

5x5 kernel:
```
∂²f/∂x² ≈ (-f[x-2] + 16*f[x-1] - 30*f[x] + 16*f[x+1] - f[x+2]) / 12
∂²f/∂y² ≈ (-f[y-2] + 16*f[y-1] - 30*f[y] + 16*f[y+1] - f[y+2]) / 12
∂²f/∂x∂y ≈ similar 5x5 mixed stencil
```

## Command 2: Eigendecomposition

```c
typedef struct {
    /* Input */
    const Hessian2D* hessian;
    uint32_t W, H;

    /* Output (allocated by caller or by eigen_Execute) */
    EigenVec2* major;  // W*H array, larger eigenvalue + vector
    EigenVec2* minor;  // W*H array, smaller eigenvalue + vector
} EigenDecomposeCmd;

int eigen_Execute(EigenDecomposeCmd* cmd);
void eigen_Free(EigenDecomposeCmd* cmd);
```

**Implementation approach:**

Closed-form solution for 2×2 symmetric matrices (no iterative solver needed).

Given Hessian `H = [xx xy; xy yy]`:

**Eigenvalues:**
```
trace = xx + yy
det = xx*yy - xy*xy
discriminant = sqrt(trace² - 4*det)
λ₁ = (trace + discriminant) / 2
λ₂ = (trace - discriminant) / 2
```

**Eigenvectors:**
```
If xy ≠ 0:
  v₁ = normalize([λ₁ - yy, xy])
  v₂ = normalize([λ₂ - yy, xy])
Else if xx > yy:
  v₁ = [1, 0], v₂ = [0, 1]
Else:
  v₁ = [0, 1], v₂ = [1, 0]
```

**Sorting:**
- Assign to major/minor based on `|λ₁| >= |λ₂|`
- Guarantees major always has larger magnitude

## Command 3: Debug Visualization

```c
typedef struct {
    /* Input */
    const float* heightmap;
    uint32_t W, H;
    int kernel_size;      // 3 or 5 for Hessian computation
    const char* output_path;  // where to save the debug PNG grid

    /* Intermediate results (optional - kept for inspection) */
    Hessian2D* hessian;   // allocated internally if NULL
    EigenVec2* major;     // allocated internally if NULL
    EigenVec2* minor;     // allocated internally if NULL
    float* coherence;     // allocated internally if NULL (minor/major ratio)
} HessianFlowDebugCmd;

int hessian_flow_debug_Execute(HessianFlowDebugCmd* cmd);
void hessian_flow_debug_Free(HessianFlowDebugCmd* cmd);
```

**Grid layout (3×3):**

```
Row 1: [heightmap]    [hessian_xx]   [hessian_xy]
Row 2: [hessian_yy]   [major_val]    [minor_val]
Row 3: [major_vec]    [minor_vec]    [coherence]
```

**Tile rendering:**
- Grayscale tiles (heightmap, Hessian components, eigenvalues, coherence): auto-ranged via `PngFloatCmd`
- Vec2 tiles (eigenvectors): rendered as flow fields via `PngVec2Cmd`
- Uses existing `PngGridCmd` infrastructure

**Coherence metric:**
```
coherence[i] = |minor.value| / max(|major.value|, epsilon)
```
where epsilon = 1e-6 prevents divide-by-zero.

**Interpretation:**
- coherence → 1: isotropic curvature (blob-like, no preferred direction)
- coherence → 0: anisotropic curvature (ridge/valley with strong directional flow)

The minor eigenvector field (tile 8) is the "money shot" - it shows the direction along which to amplify for flowy effects.

## Pipeline Execution

```c
// Internal execution flow of hessian_flow_debug_Execute():

1. Allocate intermediate buffers if needed
2. Run HessianCmd:
   - Input: heightmap
   - Output: hessian field
3. Run EigenDecomposeCmd:
   - Input: hessian field
   - Output: major/minor eigenvalue+vector arrays
4. Compute coherence:
   - For each pixel: coherence[i] = |minor[i].value| / max(|major[i].value|, 1e-6)
5. Build PngGridCmd with 9 tiles:
   - Extract components from Hessian2D/EigenVec2 structs into separate float arrays
   - Tile 1: heightmap (PNG_TILE_GRAYSCALE)
   - Tiles 2-4: hessian.xx, .xy, .yy (PNG_TILE_GRAYSCALE)
   - Tiles 5-6: major.value, minor.value (PNG_TILE_GRAYSCALE)
   - Tiles 7-8: major.vector, minor.vector (PNG_TILE_VEC2)
   - Tile 9: coherence (PNG_TILE_GRAYSCALE)
6. Execute png_ExportGrid()
7. Return 0 on success
```

## Next Steps

1. Implement `hessian_flow.c` with the three commands
2. Add to `prototype/src/commands/` alongside existing commands
3. Test with erosion timing maps to validate flow field quality
4. If successful, create follow-up effect to actually apply the flow (warp/advect the image)

## Open Questions

- **Amplification method**: Once we have the minor eigenvector field, how do we apply it?
  - Option A: Advect pixels along the flow (particle tracing)
  - Option B: Use as displacement/warp field
  - Option C: Anisotropic diffusion weighted by eigenvectors
- **Eigenvalue sign**: Should we preserve sign or use absolute value for sorting?
  - Current design uses absolute value for magnitude comparison
  - Sign indicates convex (+) vs concave (-) curvature
