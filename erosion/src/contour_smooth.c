#include "contour_smooth.h"
#include <math.h>

/*
 * Contour smoothing for stair-step artifacts.
 *
 * For each L-corner in the contour, we examine the image to determine
 * if the edge is truly axis-aligned or diagonal, then adjust the vertex
 * position accordingly.
 */

/* Get pixel value with bounds checking */
static inline uint8_t sample(const uint8_t *src, uint32_t W, uint32_t H, int x, int y)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= (int)W) x = (int)W - 1;
    if (y >= (int)H) y = (int)H - 1;
    return src[y * W + x];
}

/*
 * Detect edge angle at a corner by examining a neighborhood of pixels.
 *
 * Samples pixels in a radius around the corner and computes a weighted
 * gradient to determine the edge direction. Larger radius = smoother result.
 *
 * Returns:
 *   0.0 = axis-aligned edge (no smoothing needed)
 *   1.0 = perfect diagonal (maximum smoothing)
 *   Values in between for partial diagonals
 *
 * Also returns the edge direction in dx, dy (normalized).
 */
static float detect_edge_angle(
    const uint8_t *src, uint32_t W, uint32_t H,
    int cx, int cy,  /* Corner position (pixel coords) */
    int radius,      /* Sampling radius */
    float *dx, float *dy)  /* Output: edge direction */
{
    if (radius < 1) radius = 1;

    /*
     * Compute gradient using Sobel-like weighted sampling.
     * Sample points in a square region around the corner,
     * weight by distance from center.
     */
    float grad_x = 0.0f;
    float grad_y = 0.0f;
    float total_weight = 0.0f;

    for (int oy = -radius; oy <= radius; oy++) {
        for (int ox = -radius; ox <= radius; ox++) {
            /* Skip the center point */
            if (ox == 0 && oy == 0) continue;

            /* Distance-based weight (closer = more weight) */
            float dist = sqrtf((float)(ox * ox + oy * oy));
            float weight = 1.0f / (dist + 0.5f);

            /* Sample this pixel and its neighbors for local gradient */
            int px = cx + ox;
            int py = cy + oy;

            float val_c = (float)sample(src, W, H, px, py);
            float val_r = (float)sample(src, W, H, px + 1, py);
            float val_d = (float)sample(src, W, H, px, py + 1);

            /* Local gradient contribution */
            float local_gx = val_r - val_c;
            float local_gy = val_d - val_c;

            grad_x += local_gx * weight;
            grad_y += local_gy * weight;
            total_weight += weight;
        }
    }

    if (total_weight > 0.0f) {
        grad_x /= total_weight;
        grad_y /= total_weight;
    }

    float grad_len = sqrtf(grad_x * grad_x + grad_y * grad_y);

    if (grad_len < 0.5f) {
        /* No clear gradient - check immediate 2x2 for axis-aligned edge */
        uint8_t tl = sample(src, W, H, cx - 1, cy - 1);
        uint8_t tr = sample(src, W, H, cx,     cy - 1);
        uint8_t bl = sample(src, W, H, cx - 1, cy);
        uint8_t br = sample(src, W, H, cx,     cy);

        int horiz_edge = (tl == tr) && (bl == br) && (tl != bl);
        int vert_edge = (tl == bl) && (tr == br) && (tl != tr);

        if (horiz_edge || vert_edge) {
            *dx = 0;
            *dy = 0;
            return 0.0f;
        }

        /* Fallback: compute gradient from 2x2 */
        grad_x = (float)(tr + br) - (float)(tl + bl);
        grad_y = (float)(bl + br) - (float)(tl + tr);
        grad_len = sqrtf(grad_x * grad_x + grad_y * grad_y);

        if (grad_len < 1.0f) {
            *dx = 0;
            *dy = 0;
            return 0.0f;
        }
    }

    /* Normalize gradient */
    grad_x /= grad_len;
    grad_y /= grad_len;

    /* Edge direction is perpendicular to gradient */
    *dx = -grad_y;
    *dy = grad_x;

    /* Diagonality: 1.0 if edge is 45 degrees, 0.0 if axis-aligned */
    /* Edge direction (dx, dy) is on unit circle */
    /* For axis-aligned: one component is ~1, other is ~0 */
    /* For 45 degrees: both components are ~0.707 */
    float diagonality = 2.0f * fabsf(*dx) * fabsf(*dy);  /* 0 for axis, 1 for 45deg */

    return diagonality;
}

/*
 * Check if a vertex is an L-corner (stair-step).
 * Returns 1 if prev->curr is different axis than curr->next.
 */
static int is_stair_corner(
    float px, float py,  /* previous vertex */
    float cx, float cy,  /* current vertex */
    float nx, float ny)  /* next vertex */
{
    float dx1 = cx - px;
    float dy1 = cy - py;
    float dx2 = nx - cx;
    float dy2 = ny - cy;

    /* Check if one segment is ~horizontal and the other ~vertical */
    int seg1_horiz = (fabsf(dx1) > fabsf(dy1));
    int seg2_horiz = (fabsf(dx2) > fabsf(dy2));

    return (seg1_horiz != seg2_horiz);
}

/*
 * Determine which direction the corner should be pulled based on
 * the stair-step geometry and edge direction.
 */
static void compute_corner_shift(
    float px, float py,  /* previous vertex */
    float cx, float cy,  /* current vertex */
    float nx, float ny,  /* next vertex */
    float diagonality,   /* How diagonal the edge is (0-1) */
    float edge_dx, float edge_dy,  /* Edge direction */
    float max_shift,     /* Maximum shift in normalized coords */
    float *shift_x, float *shift_y)  /* Output shift */
{
    /* Compute midpoint of prev and next */
    float mid_x = (px + nx) * 0.5f;
    float mid_y = (py + ny) * 0.5f;

    /* Vector from corner to midpoint */
    float to_mid_x = mid_x - cx;
    float to_mid_y = mid_y - cy;

    /* The shift is simply moving toward the midpoint, scaled by diagonality */
    /* For a 45-degree edge, we move fully to midpoint (creating a diagonal) */
    /* For an axis-aligned edge, we don't move (keep the stair-step) */
    *shift_x = to_mid_x * diagonality;
    *shift_y = to_mid_y * diagonality;

    /* Clamp to max shift */
    float len = sqrtf(*shift_x * *shift_x + *shift_y * *shift_y);
    if (len > max_shift) {
        float scale = max_shift / len;
        *shift_x *= scale;
        *shift_y *= scale;
    }
}

int contour_smooth(ContourSmoothCmd *cmd)
{
    if (!cmd || !cmd->src || !cmd->contours)
        return -1;

    int radius = cmd->radius > 0 ? cmd->radius : 3;
    float max_shift_px = cmd->max_shift > 0 ? cmd->max_shift : 1.0f;

    uint32_t W = cmd->W;
    uint32_t H = cmd->H;
    const uint8_t *src = cmd->src;

    /* Convert max_shift from pixels to normalized coords */
    float max_shift_norm = max_shift_px / (float)(W > H ? W : H);

    /* Process each contour line */
    for (int32_t i = 0; i < cmd->contours->num_lines; i++) {
        ContourLine *line = &cmd->contours->lines[i];
        if (line->num_points < 3) continue;

        int n = line->num_points;

        /* Process each vertex (skip endpoints for non-closed contours) */
        int start = line->closed ? 0 : 1;
        int end = line->closed ? n : n - 1;

        for (int j = start; j < end; j++) {
            int prev_idx = (j - 1 + n) % n;
            int next_idx = (j + 1) % n;

            float px = line->points[prev_idx].x;
            float py = line->points[prev_idx].y;
            float cx = line->points[j].x;
            float cy = line->points[j].y;
            float nx = line->points[next_idx].x;
            float ny = line->points[next_idx].y;

            /* Only process stair-step corners */
            if (!is_stair_corner(px, py, cx, cy, nx, ny))
                continue;

            /* Convert normalized coords to pixel coords for sampling */
            /* The corner is at a pixel boundary, so cx*W gives us the right pixel index */
            int pix_x = (int)(cx * W + 0.5f);
            int pix_y = (int)(cy * H + 0.5f);

            /* Detect edge angle at this corner */
            float edge_dx, edge_dy;
            float diagonality = detect_edge_angle(src, W, H, pix_x, pix_y, radius, &edge_dx, &edge_dy);

            /* Skip if edge is axis-aligned (no smoothing needed) */
            if (diagonality < 0.1f)
                continue;

            /* Compute shift */
            float shift_x, shift_y;
            compute_corner_shift(px, py, cx, cy, nx, ny,
                                 diagonality, edge_dx, edge_dy,
                                 max_shift_norm, &shift_x, &shift_y);

            /* Apply shift */
            line->points[j].x += shift_x;
            line->points[j].y += shift_y;
        }
    }

    return 0;
}
