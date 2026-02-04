#include "label_regions.h"
#include <stdlib.h>
#include <string.h>

/*
 * Union-Find (Disjoint Set) for connected components.
 * Uses path compression and union by rank.
 */

typedef struct {
    int32_t *parent;
    int32_t *rank;
    int32_t capacity;
    int32_t next_label;
} UnionFind;

static UnionFind *uf_create(int32_t capacity)
{
    UnionFind *uf = malloc(sizeof(UnionFind));
    if (!uf) return NULL;

    uf->parent = malloc(capacity * sizeof(int32_t));
    uf->rank = calloc(capacity, sizeof(int32_t));
    uf->capacity = capacity;
    uf->next_label = 1;  /* Labels start at 1 */

    if (!uf->parent || !uf->rank) {
        free(uf->parent);
        free(uf->rank);
        free(uf);
        return NULL;
    }

    /* Initialize: each element is its own parent */
    for (int32_t i = 0; i < capacity; i++) {
        uf->parent[i] = i;
    }

    return uf;
}

static void uf_destroy(UnionFind *uf)
{
    if (uf) {
        free(uf->parent);
        free(uf->rank);
        free(uf);
    }
}

static int32_t uf_find(UnionFind *uf, int32_t x)
{
    /* Path compression */
    if (uf->parent[x] != x) {
        uf->parent[x] = uf_find(uf, uf->parent[x]);
    }
    return uf->parent[x];
}

static void uf_union(UnionFind *uf, int32_t x, int32_t y)
{
    int32_t rx = uf_find(uf, x);
    int32_t ry = uf_find(uf, y);

    if (rx == ry) return;

    /* Union by rank */
    if (uf->rank[rx] < uf->rank[ry]) {
        uf->parent[rx] = ry;
    } else if (uf->rank[rx] > uf->rank[ry]) {
        uf->parent[ry] = rx;
    } else {
        uf->parent[ry] = rx;
        uf->rank[rx]++;
    }
}

static int32_t uf_new_label(UnionFind *uf)
{
    return uf->next_label++;
}

int label_regions(LabelRegionsCmd *cmd)
{
    if (!cmd || !cmd->src || !cmd->labels)
        return -1;
    if (cmd->W == 0 || cmd->H == 0)
        return -1;

    uint32_t W = cmd->W;
    uint32_t H = cmd->H;
    uint32_t N = W * H;
    int use_diag = (cmd->connectivity == LABEL_CONNECT_8);

    const uint8_t *src = cmd->src;
    uint32_t *labels = cmd->labels;

    /* Clear labels */
    memset(labels, 0, N * sizeof(uint32_t));

    /* Create union-find with capacity for worst case (every pixel is its own region) */
    UnionFind *uf = uf_create((int32_t)(N + 1));
    if (!uf) return -2;

    /* First pass: assign provisional labels and record equivalences */
    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            uint32_t i = y * W + x;
            uint8_t v = src[i];

            /* Collect labels from already-visited neighbors with same value */
            uint32_t neighbor_labels[4];
            int neighbor_count = 0;

            /* Left */
            if (x > 0 && src[i - 1] == v && labels[i - 1] > 0) {
                neighbor_labels[neighbor_count++] = labels[i - 1];
            }
            /* Top */
            if (y > 0 && src[i - W] == v && labels[i - W] > 0) {
                neighbor_labels[neighbor_count++] = labels[i - W];
            }
            /* Top-left (8-connected) */
            if (use_diag && x > 0 && y > 0 && src[i - W - 1] == v && labels[i - W - 1] > 0) {
                neighbor_labels[neighbor_count++] = labels[i - W - 1];
            }
            /* Top-right (8-connected) */
            if (use_diag && x < W - 1 && y > 0 && src[i - W + 1] == v && labels[i - W + 1] > 0) {
                neighbor_labels[neighbor_count++] = labels[i - W + 1];
            }

            if (neighbor_count == 0) {
                /* New region */
                labels[i] = uf_new_label(uf);
            } else {
                /* Use smallest neighbor label */
                uint32_t min_label = neighbor_labels[0];
                for (int j = 1; j < neighbor_count; j++) {
                    if (neighbor_labels[j] < min_label)
                        min_label = neighbor_labels[j];
                }
                labels[i] = min_label;

                /* Union all neighbor labels */
                for (int j = 0; j < neighbor_count; j++) {
                    uf_union(uf, (int32_t)min_label, (int32_t)neighbor_labels[j]);
                }
            }
        }
    }

    /* Second pass: resolve labels to their roots and renumber consecutively */
    uint32_t *remap = calloc(uf->next_label, sizeof(uint32_t));
    if (!remap) {
        uf_destroy(uf);
        return -3;
    }

    uint32_t final_label = 0;
    for (uint32_t i = 0; i < N; i++) {
        if (labels[i] > 0) {
            int32_t root = uf_find(uf, (int32_t)labels[i]);
            if (remap[root] == 0) {
                remap[root] = ++final_label;  /* 1-indexed internally to distinguish from unassigned */
            }
            labels[i] = remap[root] - 1;  /* Output 0-indexed labels */
        }
    }

    cmd->num_regions = final_label;

    free(remap);
    uf_destroy(uf);
    return 0;
}
