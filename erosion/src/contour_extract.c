#include "contour_extract.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
 * Simple boundary extraction.
 *
 * Instead of marching squares, we directly find pixel boundary edges:
 * - Horizontal pass: where pixel[x,y] != pixel[x+1,y], add vertical segment
 * - Vertical pass: where pixel[x,y] != pixel[x,y+1], add horizontal segment
 *
 * Produces axis-aligned stair-step contours at pixel boundaries.
 * Each edge is found exactly once - no duplicates, no ambiguity.
 */

void contour_line_append(ContourLine *line, float x, float y)
{
    if (line->num_points >= line->capacity) {
        int32_t new_cap = line->capacity ? line->capacity * 2 : 16;
        ContourPoint *new_pts = realloc(line->points, new_cap * sizeof(ContourPoint));
        if (!new_pts) return;
        line->points = new_pts;
        line->capacity = new_cap;
    }
    line->points[line->num_points].x = x;
    line->points[line->num_points].y = y;
    line->num_points++;
}

ContourLine *contour_set_new_line(ContourSet *set, uint8_t val_low, uint8_t val_high)
{
    if (set->num_lines >= set->capacity) {
        int32_t new_cap = set->capacity ? set->capacity * 2 : 16;
        ContourLine *new_lines = realloc(set->lines, new_cap * sizeof(ContourLine));
        if (!new_lines) return NULL;
        set->lines = new_lines;
        set->capacity = new_cap;
    }
    ContourLine *line = &set->lines[set->num_lines++];
    memset(line, 0, sizeof(ContourLine));
    line->value_low = val_low;
    line->value_high = val_high;
    return line;
}

void contour_free(ContourSet *set)
{
    if (!set) return;
    for (int32_t i = 0; i < set->num_lines; i++) {
        free(set->lines[i].points);
    }
    free(set->lines);
    free(set);
}


/*
 * Graph-based segment chaining with quantized coordinates.
 * Uses uint32 packed coordinates as keys to automatically merge duplicate endpoints.
 */

/* Pack normalized [0,1] coords into uint32 key (16 bits each) */
static uint32_t pack_coord(float x, float y)
{
    uint16_t ix = (uint16_t)(x * 65535.0f + 0.5f);
    uint16_t iy = (uint16_t)(y * 65535.0f + 0.5f);
    return ((uint32_t)ix << 16) | (uint32_t)iy;
}

static void unpack_coord(uint32_t key, float *x, float *y)
{
    uint16_t ix = (uint16_t)(key >> 16);
    uint16_t iy = (uint16_t)(key & 0xFFFF);
    *x = (float)ix / 65535.0f;
    *y = (float)iy / 65535.0f;
}

/* Edge in the graph: connects two quantized endpoints */
typedef struct {
    uint32_t p0, p1;  /* Packed endpoint coordinates */
    uint8_t val_low;  /* Lower pixel value on one side of boundary */
    uint8_t val_high; /* Higher pixel value on other side of boundary */
    int used;
} GraphEdge;

typedef struct {
    GraphEdge *edges;
    int32_t count;
    int32_t capacity;
} EdgeList;

static void edge_add(EdgeList *list, uint32_t p0, uint32_t p1, uint8_t v0, uint8_t v1)
{
    /* Skip degenerate edges */
    if (p0 == p1) return;

    /* Normalize so val_low <= val_high for consistent matching */
    uint8_t val_low = v0 < v1 ? v0 : v1;
    uint8_t val_high = v0 < v1 ? v1 : v0;

    /* Check for duplicate edges (same endpoints and same boundary values) */
    for (int32_t i = 0; i < list->count; i++) {
        GraphEdge *e = &list->edges[i];
        if (e->val_low != val_low || e->val_high != val_high)
            continue;  /* Different boundary, not a duplicate */
        if ((e->p0 == p0 && e->p1 == p1) || (e->p0 == p1 && e->p1 == p0)) {
            return;  /* Duplicate, skip */
        }
    }

    if (list->count >= list->capacity) {
        int32_t new_cap = list->capacity ? list->capacity * 2 : 256;
        GraphEdge *new_edges = realloc(list->edges, new_cap * sizeof(GraphEdge));
        if (!new_edges) return;
        list->edges = new_edges;
        list->capacity = new_cap;
    }
    GraphEdge *e = &list->edges[list->count++];
    e->p0 = p0;
    e->p1 = p1;
    e->val_low = val_low;
    e->val_high = val_high;
    e->used = 0;
}

/* Find an unused edge connected to the given endpoint with matching boundary values */
static int find_edge_from(EdgeList *list, uint32_t endpoint,
                          uint8_t val_low, uint8_t val_high, uint32_t *other_end)
{
    for (int32_t i = 0; i < list->count; i++) {
        GraphEdge *e = &list->edges[i];
        if (e->used) continue;

        /* Must match boundary values (the "handshake") */
        if (e->val_low != val_low || e->val_high != val_high)
            continue;

        if (e->p0 == endpoint) {
            e->used = 1;
            *other_end = e->p1;
            return 1;
        } else if (e->p1 == endpoint) {
            e->used = 1;
            *other_end = e->p0;
            return 1;
        }
    }
    return 0;
}

/* Chain edges into polylines, only connecting edges with matching boundary values */
static void chain_edges(EdgeList *edges, ContourSet *set)
{
    for (int32_t i = 0; i < edges->count; i++) {
        if (edges->edges[i].used) continue;

        GraphEdge *e = &edges->edges[i];
        e->used = 1;

        /* Get boundary values for this contour line */
        uint8_t val_low = e->val_low;
        uint8_t val_high = e->val_high;

        ContourLine *line = contour_set_new_line(set, val_low, val_high);
        if (!line) break;

        /* Start with this edge's two endpoints */
        uint32_t head = e->p0;
        uint32_t tail = e->p1;

        float x, y;
        unpack_coord(head, &x, &y);
        contour_line_append(line, x, y);
        unpack_coord(tail, &x, &y);
        contour_line_append(line, x, y);

        /* Extend from both ends, only connecting edges with same boundary values */
        int found;
        do {
            found = 0;

            /* Extend tail */
            uint32_t next;
            if (find_edge_from(edges, tail, val_low, val_high, &next)) {
                unpack_coord(next, &x, &y);
                contour_line_append(line, x, y);
                tail = next;
                found = 1;
            }

            /* Extend head (prepend) */
            if (find_edge_from(edges, head, val_low, val_high, &next)) {
                unpack_coord(next, &x, &y);
                /* Insert at front */
                if (line->num_points >= line->capacity) {
                    int32_t new_cap = line->capacity ? line->capacity * 2 : 16;
                    ContourPoint *new_pts = realloc(line->points, new_cap * sizeof(ContourPoint));
                    if (new_pts) {
                        line->points = new_pts;
                        line->capacity = new_cap;
                    }
                }
                if (line->num_points < line->capacity) {
                    memmove(&line->points[1], &line->points[0], line->num_points * sizeof(ContourPoint));
                    line->points[0].x = x;
                    line->points[0].y = y;
                    line->num_points++;
                    head = next;
                    found = 1;
                }
            }
        } while (found);

        /* Check if closed (head meets tail) */
        if (head == tail) {
            line->closed = 1;
        }
    }
}

/*
 * Extract boundary edges between regions of different values.
 * Two-pass approach: horizontal scan for vertical edges, vertical scan for horizontal edges.
 * Each edge found exactly once - no duplicates possible.
 */
static int extract_boundaries(const uint8_t *src, uint32_t W, uint32_t H, ContourSet *set)
{
    EdgeList edges = {0};

    /* Normalization: pixel boundary at x goes to x/W in [0,1] space */
    /* Using W and H (not W-1, H-1) so boundaries at edges map to 0 and 1 */

    /* Pass 1: Horizontal scan - find vertical boundary edges */
    /* Where pixel[x,y] != pixel[x+1,y], add vertical segment at x+1 */
    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W - 1; x++) {
            uint8_t v0 = src[y * W + x];
            uint8_t v1 = src[y * W + x + 1];
            if (v0 != v1) {
                /* Vertical segment at x+1, from y to y+1 */
                /* Normalized: ((x+1)/W, y/H) to ((x+1)/W, (y+1)/H) */
                uint32_t px = ((x + 1) * 65535) / W;
                uint32_t py0 = (y * 65535) / H;
                uint32_t py1 = ((y + 1) * 65535) / H;
                uint32_t p0 = (px << 16) | py0;
                uint32_t p1 = (px << 16) | py1;
                edge_add(&edges, p0, p1, v0, v1);
            }
        }
    }

    /* Pass 2: Vertical scan - find horizontal boundary edges */
    /* Where pixel[x,y] != pixel[x,y+1], add horizontal segment at y+1 */
    for (uint32_t y = 0; y < H - 1; y++) {
        for (uint32_t x = 0; x < W; x++) {
            uint8_t v0 = src[y * W + x];
            uint8_t v1 = src[(y + 1) * W + x];
            if (v0 != v1) {
                /* Horizontal segment at y+1, from x to x+1 */
                /* Normalized: (x/W, (y+1)/H) to ((x+1)/W, (y+1)/H) */
                uint32_t py = ((y + 1) * 65535) / H;
                uint32_t px0 = (x * 65535) / W;
                uint32_t px1 = ((x + 1) * 65535) / W;
                uint32_t p0 = (px0 << 16) | py;
                uint32_t p1 = (px1 << 16) | py;
                edge_add(&edges, p0, p1, v0, v1);
            }
        }
    }

    /* Chain edges into polylines, matching by boundary values */
    chain_edges(&edges, set);

    free(edges.edges);
    return 0;
}

int contour_extract(ContourExtractCmd *cmd)
{
    if (!cmd || !cmd->src)
        return -1;
    if (cmd->W < 2 || cmd->H < 2)
        return -1;

    ContourSet *set = calloc(1, sizeof(ContourSet));
    if (!set) return -2;

    /* Simple boundary extraction - finds all edges between different pixel values */
    extract_boundaries(cmd->src, cmd->W, cmd->H, set);

    cmd->result = set;
    return 0;
}

#if DEBUG_IMG_OUT
#include "stb_image_write.h"

/* Draw a line using Bresenham's algorithm */
static void draw_line(uint8_t *img, uint32_t W, uint32_t H,
                      float x0, float y0, float x1, float y1,
                      uint8_t r, uint8_t g, uint8_t b)
{
    int ix0 = (int)(x0 + 0.5f);
    int iy0 = (int)(y0 + 0.5f);
    int ix1 = (int)(x1 + 0.5f);
    int iy1 = (int)(y1 + 0.5f);

    int dx = abs(ix1 - ix0);
    int dy = abs(iy1 - iy0);
    int sx = ix0 < ix1 ? 1 : -1;
    int sy = iy0 < iy1 ? 1 : -1;
    int err = dx - dy;

    while (1) {
        if (ix0 >= 0 && ix0 < (int)W && iy0 >= 0 && iy0 < (int)H) {
            uint8_t *p = img + (iy0 * W + ix0) * 3;
            p[0] = r; p[1] = g; p[2] = b;
        }

        if (ix0 == ix1 && iy0 == iy1) break;

        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; ix0 += sx; }
        if (e2 < dx)  { err += dx; iy0 += sy; }
    }
}

/* Generate a color from value (simple hue rotation) */
static void value_to_color(uint8_t val, uint8_t *r, uint8_t *g, uint8_t *b)
{
    /* Map value to hue (0-255 -> 0-360 degrees, as RGB) */
    float h = (float)val / 255.0f * 6.0f;
    float x = 1.0f - fabsf(fmodf(h, 2.0f) - 1.0f);

    float rf, gf, bf;
    if (h < 1)      { rf = 1; gf = x; bf = 0; }
    else if (h < 2) { rf = x; gf = 1; bf = 0; }
    else if (h < 3) { rf = 0; gf = 1; bf = x; }
    else if (h < 4) { rf = 0; gf = x; bf = 1; }
    else if (h < 5) { rf = x; gf = 0; bf = 1; }
    else            { rf = 1; gf = 0; bf = x; }

    *r = (uint8_t)(rf * 255);
    *g = (uint8_t)(gf * 255);
    *b = (uint8_t)(bf * 255);
}

uint8_t *render_contours(const ContourSet *set, uint32_t W, uint32_t H,
                         const uint8_t *src_gray, uint32_t src_W, uint32_t src_H)
{
    uint8_t *img = malloc(W * H * 3);
    if (!img) return NULL;

    /* Fill background */
    if (src_gray && src_W > 0 && src_H > 0) {
        /* Scale background to output size using nearest-neighbor */
        for (uint32_t y = 0; y < H; y++) {
            for (uint32_t x = 0; x < W; x++) {
                uint32_t sx = (x * src_W) / W;
                uint32_t sy = (y * src_H) / H;
                if (sx >= src_W) sx = src_W - 1;
                if (sy >= src_H) sy = src_H - 1;
                uint8_t v = src_gray[sy * src_W + sx] / 2;  /* Dim the background */
                uint8_t *p = img + (y * W + x) * 3;
                p[0] = v; p[1] = v; p[2] = v;
            }
        }
    } else {
        memset(img, 32, W * H * 3);  /* Dark gray background */
    }

    /* Scale factors: normalized [0,1] -> pixel coords */
    float scale_x = (float)W;
    float scale_y = (float)H;

    /* Draw each contour line */
    for (int32_t i = 0; i < set->num_lines; i++) {
        const ContourLine *line = &set->lines[i];
        if (line->num_points < 2) continue;

        /* Color based on the value boundary */
        uint8_t r, g, b;
        value_to_color((line->value_low + line->value_high) / 2, &r, &g, &b);

        /* Draw line segments (scale normalized coords to output size) */
        for (int32_t j = 0; j < line->num_points - 1; j++) {
            draw_line(img, W, H,
                      line->points[j].x * scale_x, line->points[j].y * scale_y,
                      line->points[j + 1].x * scale_x, line->points[j + 1].y * scale_y,
                      r, g, b);
        }

        /* Close the loop if needed */
        if (line->closed && line->num_points > 2) {
            draw_line(img, W, H,
                      line->points[line->num_points - 1].x * scale_x,
                      line->points[line->num_points - 1].y * scale_y,
                      line->points[0].x * scale_x, line->points[0].y * scale_y,
                      r, g, b);
        }
    }

    return img;
}

int debug_export_contours(const char *path, const ContourSet *set,
                          uint32_t W, uint32_t H,
                          const uint8_t *src_gray, uint32_t src_W, uint32_t src_H)
{
    uint8_t *img = render_contours(set, W, H, src_gray, src_W, src_H);
    if (!img) return -1;

    int result = stbi_write_png(path, W, H, 3, img, W * 3);
    free(img);

    return result ? 0 : -2;
}
#endif
