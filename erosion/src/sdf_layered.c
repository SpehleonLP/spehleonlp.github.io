#include "sdf_layered.h"
//#include "debug_png.h"
#include "label_regions.h"
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// external functions
int min_i32(int, int);

/* ========== Debug PNG Export Helpers ========== */
#if DEBUG_IMG_OUT

static void sdf_debug_export_source(SDFContext *ctx, const char *path) {
	uint32_t W = ctx->W, H = ctx->H;
	float *data = malloc(W * H * sizeof(float));
	if (!data)
		return;
	
	for (uint32_t i = 0; i < W * H; i++) {
		data[i] = (float)ctx->src[i];
	}
	
	PngFloatCmd cmd = {.path = path,
					   .data = data,
					   .width = W,
					   .height = H,
					   .min_val = 0,
					   .max_val = 255,
					   .auto_range = 0};
	png_ExportFloat(&cmd);
	free(data);
}

static void sdf_debug_export_labels(SDFContext *ctx, const char *path) {
	uint32_t W = ctx->W, H = ctx->H;
	float *data = malloc(W * H * sizeof(float));
	if (!data)
		return;
	
	for (uint32_t i = 0; i < W * H; i++) {
		data[i] = (float)ctx->labels[i];
	}
	
	PngFloatCmd cmd = {
		.path = path, .data = data, .width = W, .height = H, .auto_range = 1};
	png_ExportFloat(&cmd);
	free(data);
}

/* Export a grid showing all key data for one iteration */
static void sdf_debug_export_iteration_grid(SDFContext *ctx, int iteration) {
	uint32_t W = ctx->W, H = ctx->H;
	uint32_t npixels = W * H;
	
	/* Allocate all data arrays */
	float *src_data = malloc(npixels * sizeof(float));
	float *dist_data = malloc(npixels * sizeof(float));
	float *srcval_data = malloc(npixels * sizeof(float));
	vec2 *disp_data = malloc(npixels * sizeof(vec2));
	
	if (!src_data || !dist_data || !srcval_data || !disp_data) {
		free(src_data);
		free(dist_data);
		free(srcval_data);
		free(disp_data);
		return;
	}
	
	for (uint32_t i = 0; i < npixels; i++) {
		src_data[i] = (float)ctx->src[i];
		
		SDFCell *c = &ctx->cells[i];
		if (c->source_value == 256) {
			dist_data[i] = 0.0f;
			srcval_data[i] = 0.0f;
			disp_data[i].x = 0;
			disp_data[i].y = 0;
		} else {
			dist_data[i] = sqrtf((float)(c->dx * c->dx + c->dy * c->dy));
			srcval_data[i] = (float)c->source_value;
			disp_data[i].x = (float)c->dx;
			disp_data[i].y = (float)c->dy;
		}
	}
	
	PngGridTile tiles[4] = {
		{.type = PNG_TILE_GRAYSCALE, .data = src_data},  // top-left: source
		{.type = PNG_TILE_GRAYSCALE, .data = dist_data}, // top-right: distance
		{.type = PNG_TILE_GRAYSCALE,
		 .data = srcval_data},                     // bottom-left: found values
		{.type = PNG_TILE_VEC2, .data = disp_data} // bottom-right: displacement
	};
	
	char path[64];
	snprintf(path, sizeof(path), "/sdf_iter_%02d.png", iteration);
	
	PngGridCmd cmd = {.path = path,
					  .tile_width = W,
					  .tile_height = H,
					  .cols = 2,
					  .rows = 2,
					  .tiles = tiles};
	png_ExportGrid(&cmd);
	
	free(src_data);
	free(dist_data);
	free(srcval_data);
	free(disp_data);
}

#endif /* DEBUG_IMG_OUT */

/* ========== SDF Implementation ========== */

#define CELL_INVALID(c) ((c).source_value == 256)
#define CELL_DIST_SQ(c) ((int32_t)(c).dx * (c).dx + (int32_t)(c).dy * (c).dy)

int sdf_Initialize(SDFContext *ctx, int16_t *src, uint32_t W, uint32_t H,
				   int dbg) {
	memset(ctx, 0, sizeof(*ctx));
	ctx->src = src;
	ctx->W = W;
	ctx->H = H;
	ctx->dbg = dbg;
	
	uint32_t npixels = W * H;
	
	/* Allocate labels array */
	ctx->labels = malloc(npixels * sizeof(int32_t));
	if (!ctx->labels)
		goto fail;
	
	/* Compute connected components */
	LabelRegionsCmd label_cmd = {
		.src = src,
		.W = W,
		.H = H,
		.connectivity = LABEL_CONNECT_4,
		.labels = ctx->labels,
	};
	if (label_regions(&label_cmd) != 0)
		goto fail;
	ctx->num_regions = label_cmd.num_regions;
	
	/* Allocate per-region state */
	ctx->regions = calloc(ctx->num_regions, sizeof(SDFRegion));
	if (!ctx->regions)
		goto fail;
	
	/* Initialize regions - find value for each region */
	/* First pass: mark all as uninitialized */
	for (uint32_t i = 0; i < ctx->num_regions; i++) {
		ctx->regions[i].region_value = 0;
		ctx->regions[i].target_floor = 0;
	}
	
	/* Second pass: set region_value from first pixel of each region */
	for (uint32_t i = 0; i < npixels; i++) {
		int32_t rid = ctx->labels[i];
		ctx->regions[rid].region_value = src[i];
	}
	
	/* Allocate cells array */
	ctx->cells = malloc(npixels * sizeof(SDFCell));
	if (!ctx->cells)
		goto fail;
	
#if DEBUG_IMG_OUT
	if (ctx->dbg) {
		sdf_debug_export_source(ctx, "/sdf_00_source.png");
		sdf_debug_export_labels(ctx, "/sdf_00_labels.png");
	}
#endif
	
	return 0;

fail:
	sdf_Free(ctx);
	return -1;
}

static void reset_cells(SDFContext *ctx) {
	
	for (uint32_t i = 0; i < ctx->num_regions; i++) {
		ctx->regions[i].target_floor = ctx->regions[i].next_floor;
		ctx->regions[i].next_floor = 255;
	}
	
	uint32_t npixels = ctx->W * ctx->H;
	for (uint32_t i = 0; i < npixels; i++) {
		ctx->cells[i].dx = 0;
		ctx->cells[i].dy = 0;
		ctx->cells[i].source_value = 256;
	}
}

typedef enum {
	u_NONE,
	u_MORE_WORK,
	u_REPLACE

} ShouldUpdateFlags;

/* Try to update a cell, returns 1 if updated, also checks for more_work */
static int should_update_cell(SDFContext const *ctx, uint32_t x, uint32_t y,
							  int16_t new_dx, int16_t new_dy,
							  int16_t source_value) {
	assert(source_value != 256);
	
	uint32_t idx = y * ctx->W + x;
	SDFCell const *cell = &ctx->cells[idx];
	
	// never take a cell with the same value as us
	if (ctx->src[idx] == source_value) {
		return 0;
	}
	
	// never take a cell with a bigger value than us
	// (if marked invalid then source value will be 256)
	if (source_value > cell->source_value) {
		// if we didn't take it then there is an SDF to be had where we did.
		return u_MORE_WORK;
	}
	
	// most likely, short circuit here to prevent memory indexes
	if (source_value == cell->source_value) {
		int32_t new_dist_sq = (int32_t)new_dx * new_dx + (int32_t)new_dy * new_dy;
		int32_t old_dist_sq = CELL_DIST_SQ(*cell);
		
		if (new_dist_sq < old_dist_sq) {
			return u_REPLACE;
		}
		
		return 0;
	}
	
	uint32_t region = ctx->labels[idx];
	assert(region < ctx->num_regions);
	
	// only take a cell with a value bigger than the target floor
	if (source_value <= ctx->regions[region].target_floor) {
		return 0;
	}
	
	return u_REPLACE | ((cell->source_value != 256) ? u_MORE_WORK : u_NONE);
}

/* Try to update a cell, returns 1 if updated, also checks for more_work */
static int try_update_cell(SDFContext *ctx, uint32_t x, uint32_t y,
                           int16_t new_dx, int16_t new_dy,
						   int16_t source_value) {
	
	int code = should_update_cell(ctx, x, y, new_dx, new_dy, source_value);
	
	if (code & u_MORE_WORK)
		ctx->more_work = true;
	
	if (code & u_REPLACE) {
		uint32_t idx = y * ctx->W + x;
		SDFCell *cell = &ctx->cells[idx];
		
		cell->dx = new_dx;
		cell->dy = new_dy;
		
		if (cell->source_value != source_value) {
			cell->source_value = source_value;
			uint32_t region = ctx->labels[idx];
			
			ctx->regions[region].next_floor =
					min_i32(ctx->regions[region].next_floor, source_value);
		}
		
		return 1;
	}
	
	return 0;
}

/* ========== Priority Queue for Dijkstra ========== */

typedef struct {
	int32_t x, y;
	int16_t dx, dy;
	int16_t source_value;
} DijkstraEntry;

typedef struct {
	DijkstraEntry *data;
	int32_t size;
	int32_t capacity;
} DijkstraQueue;

static int32_t dq_dist_sq(const DijkstraEntry *e) {
	return (int32_t)e->dx * e->dx + (int32_t)e->dy * e->dy;
}

static void dq_init(DijkstraQueue *q) {
	q->data = NULL;
	q->size = 0;
	q->capacity = 0;
}

static void dq_free(DijkstraQueue *q) {
	free(q->data);
	q->data = NULL;
	q->size = 0;
	q->capacity = 0;
}

static int dq_push(DijkstraQueue *q, DijkstraEntry entry) {
	if (q->size >= q->capacity) {
		int32_t new_cap = q->capacity ? q->capacity * 2 : 256;
		DijkstraEntry *new_data = realloc(q->data, new_cap * sizeof(DijkstraEntry));
		if (!new_data) return -1;
		q->data = new_data;
		q->capacity = new_cap;
	}

	/* Insert at end and bubble up (min-heap by distance) */
	int32_t i = q->size++;
	int32_t d = dq_dist_sq(&entry);
	while (i > 0) {
		int32_t parent = (i - 1) / 2;
		if (dq_dist_sq(&q->data[parent]) <= d) break;
		q->data[i] = q->data[parent];
		i = parent;
	}
	q->data[i] = entry;
	return 0;
}

static int dq_pop(DijkstraQueue *q, DijkstraEntry *out) {
	if (q->size == 0) return -1;

	*out = q->data[0];
	q->size--;
	if (q->size == 0) return 0;

	/* Move last to root and bubble down */
	DijkstraEntry last = q->data[q->size];
	int32_t d = dq_dist_sq(&last);
	int32_t i = 0;
	while (1) {
		int32_t left = 2 * i + 1;
		int32_t right = 2 * i + 2;
		int32_t smallest = i;
		int32_t smallest_d = d;

		if (left < q->size) {
			int32_t ld = dq_dist_sq(&q->data[left]);
			if (ld < smallest_d) {
				smallest = left;
				smallest_d = ld;
			}
		}
		if (right < q->size) {
			int32_t rd = dq_dist_sq(&q->data[right]);
			if (rd < smallest_d) {
				smallest = right;
			}
		}
		if (smallest == i) break;
		q->data[i] = q->data[smallest];
		i = smallest;
	}
	q->data[i] = last;
	return 0;
}

/* ========== Dijkstra Flood Fill ========== */

static void flood_corrections(SDFContext *ctx) {
	uint32_t W = ctx->W, H = ctx->H;

	/* 8-neighbor offsets */
	const int8_t dx8[] = {1, -1, 0, 0, 1, -1, 1, -1};
	const int8_t dy8[] = {0, 0, 1, -1, 1, -1, -1, 1};

	DijkstraQueue queue;
	dq_init(&queue);

	/* Seed: add all boundary pixels (those adjacent to different value) */
	for (uint32_t y = 0; y < H; y++) {
		for (uint32_t x = 0; x < W; x++) {
			uint32_t idx = y * W + x;
			int16_t val = ctx->src[idx];

			if(val < 0) continue;

			/* Check 4-neighbors for different values */
			for (int d = 0; d < 4; d++) {
				int nx = (int)x + dx8[d];
				int ny = (int)y + dy8[d];
				if (nx < 0 || nx >= (int)W || ny < 0 || ny >= (int)H)
					continue;

				int16_t neighbor_val = ctx->src[ny * W + nx];
			
				if (neighbor_val != val) {
					/* Seed with distance 1 - MUST push each one, not just last */
					DijkstraEntry entry = {
						.x = (int32_t)x,
						.y = (int32_t)y,
						.dx = (int16_t)(0),
						.dy = (int16_t)(0),
						.source_value = neighbor_val
					};
					dq_push(&queue, entry);
				}
			}
		}
	}

	/* Process queue in distance order */
	DijkstraEntry entry;
	while (dq_pop(&queue, &entry) == 0) {
		uint32_t x = (uint32_t)entry.x;
		uint32_t y = (uint32_t)entry.y;
		uint32_t idx = y * W + x;
		int16_t src_val = ctx->src[idx];

		/* Try to update this cell */
		if (!try_update_cell(ctx, x, y, entry.dx, entry.dy, entry.source_value)) {
			continue; /* Not an improvement, skip propagation */
		}

		SDFCell *cell = &ctx->cells[idx];

		/* Propagate to 8-neighbors on same plane */
		for (int d = 0; d < 8; d++) {
			int nx = (int)x + dx8[d];
			int ny = (int)y + dy8[d];
			if (nx < 0 || nx >= (int)W || ny < 0 || ny >= (int)H)
				continue;

			/* Same-plane constraint */
			if (ctx->src[ny * W + nx] != src_val)
				continue;

			/* Compute candidate displacement for neighbor */
			int16_t cand_dx = cell->dx + (int16_t)abs(dx8[d]);
			int16_t cand_dy = cell->dy + (int16_t)abs(dy8[d]);

			/* Check if this would be an improvement before queuing */
			if (should_update_cell(ctx, (uint32_t)nx, (uint32_t)ny,
								   cand_dx, cand_dy, cell->source_value) & u_REPLACE) {
				/* MUST push each valid neighbor - they all need processing */
				DijkstraEntry neighbor_entry = {
					.x = nx,
					.y = ny,
					.dx = cand_dx,
					.dy = cand_dy,
					.source_value = cell->source_value
				};
				dq_push(&queue, neighbor_entry);
			}
		}
	}

	dq_free(&queue);
}


#if DEBUG_IMG_OUT
static int g_sdf_iteration = 0;
#endif

int sdf_Iterate(SDFContext *ctx) {

	reset_cells(ctx);
	ctx->more_work = 0;

	/* Dijkstra flood fill from boundaries */
	flood_corrections(ctx);

#if DEBUG_IMG_OUT
	g_sdf_iteration++;
	
	if (ctx->dbg) {
		sdf_debug_export_iteration_grid(ctx, g_sdf_iteration);
	}
#endif

	return ctx->more_work ? 1 : 0;
}

int sdf_Run(SDFContext *ctx) {
	int iterations = 0;
	int result;
	
	do {
		result = sdf_Iterate(ctx);
		if (result < 0)
			return -1;
		iterations++;
		
		/* Safety limit */
		if (iterations > 255)
			break;
	} while (result == 1);
	
	return iterations;
}

int32_t sdf_GetDistanceSq(SDFContext *ctx, uint32_t x, uint32_t y) {
	if (x >= ctx->W || y >= ctx->H)
		return -1;
	SDFCell *cell = &ctx->cells[y * ctx->W + x];
	if (CELL_INVALID(*cell))
		return -1;
	return CELL_DIST_SQ(*cell);
}

void sdf_Free(SDFContext *ctx) {
	free(ctx->labels);
	free(ctx->regions);
	free(ctx->cells);
	memset(ctx, 0, sizeof(*ctx));
}
