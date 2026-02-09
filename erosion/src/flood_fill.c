#include "flood_fill.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
 * Priority Queue Implementation (min-heap)
 */

FFPriorityQueue* ffq_Create(uint32_t initial_capacity) {
	FFPriorityQueue* q = malloc(sizeof(FFPriorityQueue));
	if (!q) return NULL;

	q->capacity = initial_capacity > 0 ? initial_capacity : 256;
	q->items = malloc(sizeof(FFQueueItem) * q->capacity);
	if (!q->items) {
		free(q);
		return NULL;
	}
	q->count = 0;
	return q;
}

void ffq_Destroy(FFPriorityQueue* q) {
	if (q) {
		free(q->items);
		free(q);
	}
}

static void heap_swap(FFQueueItem* a, FFQueueItem* b) {
	FFQueueItem tmp = *a;
	*a = *b;
	*b = tmp;
}

static void heap_sift_up(FFPriorityQueue* q, uint32_t idx) {
	while (idx > 0) {
		uint32_t parent = (idx - 1) / 2;
		if (q->items[idx].priority < q->items[parent].priority) {
			heap_swap(&q->items[idx], &q->items[parent]);
			idx = parent;
		} else {
			break;
		}
	}
}

static void heap_sift_down(FFPriorityQueue* q, uint32_t idx) {
	while (1) {
		uint32_t smallest = idx;
		uint32_t left = 2 * idx + 1;
		uint32_t right = 2 * idx + 2;

		if (left < q->count && q->items[left].priority < q->items[smallest].priority)
			smallest = left;
		if (right < q->count && q->items[right].priority < q->items[smallest].priority)
			smallest = right;

		if (smallest != idx) {
			heap_swap(&q->items[idx], &q->items[smallest]);
			idx = smallest;
		} else {
			break;
		}
	}
}

void ffq_Push(FFPriorityQueue* q, int x, int y, float priority) {
	// Grow if needed
	if (q->count >= q->capacity) {
		uint32_t new_cap = q->capacity * 2;
		FFQueueItem* new_items = realloc(q->items, sizeof(FFQueueItem) * new_cap);
		if (!new_items) return;  // silently fail
		q->items = new_items;
		q->capacity = new_cap;
	}

	uint32_t idx = q->count++;
	q->items[idx].x = x;
	q->items[idx].y = y;
	q->items[idx].priority = priority;
	heap_sift_up(q, idx);
}

FFQueueItem ffq_Pop(FFPriorityQueue* q) {
	FFQueueItem result = q->items[0];
	q->items[0] = q->items[--q->count];
	if (q->count > 0)
		heap_sift_down(q, 0);
	return result;
}

int ffq_IsEmpty(const FFPriorityQueue* q) {
	return q->count == 0;
}

/*
 * Built-in rule functions
 */

float ff_rule_distance(const FFRuleContext* ctx) {
	float min_val = INFINITY;
	for (int i = 0; i < ctx->neighbor_count; i++) {
		float candidate = ctx->neighbors[i].value + ctx->neighbors[i].distance;
		if (candidate < min_val)
			min_val = candidate;
	}
	return min_val;
}

float ff_rule_chamfer(const FFRuleContext* ctx) {
	// Same as distance but explicit about the distance values
	return ff_rule_distance(ctx);
}

float ff_rule_weighted_avg(const FFRuleContext* ctx) {
	if (ctx->neighbor_count == 0) return INFINITY;

	float sum = 0.0f;
	float weight_sum = 0.0f;
	for (int i = 0; i < ctx->neighbor_count; i++) {
		float w = 1.0f / ctx->neighbors[i].distance;
		sum += ctx->neighbors[i].value * w;
		weight_sum += w;
	}
	return weight_sum > 0 ? sum / weight_sum : INFINITY;
}

float ff_rule_min(const FFRuleContext* ctx) {
	float min_val = INFINITY;
	for (int i = 0; i < ctx->neighbor_count; i++) {
		if (ctx->neighbors[i].value < min_val)
			min_val = ctx->neighbors[i].value;
	}
	return min_val;
}

float ff_rule_max(const FFRuleContext* ctx) {
	float max_val = -INFINITY;
	for (int i = 0; i < ctx->neighbor_count; i++) {
		if (ctx->neighbors[i].value > max_val)
			max_val = ctx->neighbors[i].value;
	}
	return max_val;
}

float ff_rule_average(const FFRuleContext* ctx) {
	if (ctx->neighbor_count == 0) return INFINITY;

	float sum = 0.0f;
	for (int i = 0; i < ctx->neighbor_count; i++) {
		sum += ctx->neighbors[i].value;
	}
	return sum / ctx->neighbor_count;
}

/*
 * Flood Fill Execution
 */

// Neighbor offsets for 8-connectivity
static const int dx8[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
static const int dy8[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

// Neighbor offsets for 4-connectivity
static const int dx4[4] = {0, -1, 1, 0};
static const int dy4[4] = {-1, 0, 0, 1};

int ff_Execute(FloodFillCmd* cmd) {
	if (!cmd || cmd->width == 0 || cmd->height == 0)
		return -1;
	if (!cmd->seeds || cmd->seed_count == 0)
		return -2;
	if (!cmd->rule)
		return -3;

	uint32_t w = cmd->width;
	uint32_t h = cmd->height;
	uint32_t N = w * h;

	float max_val = cmd->max_value > 0 ? cmd->max_value : INFINITY;

	// Allocate output if needed
	if (!cmd->output) {
		cmd->output = malloc(sizeof(float) * N);
		if (!cmd->output) return -4;
	}

	// Initialize output to infinity (unfilled)
	for (uint32_t i = 0; i < N; i++)
		cmd->output[i] = INFINITY;

	// Allocate filled tracking
	uint8_t* filled = cmd->filled;
	int owns_filled = 0;
	if (!filled) {
		filled = calloc(N, 1);
		if (!filled) return -4;
		owns_filled = 1;
	} else {
		memset(filled, 0, N);
	}

	// Create priority queue
	FFPriorityQueue* queue = ffq_Create(cmd->seed_count * 4);
	if (!queue) {
		if (owns_filled) free(filled);
		return -4;
	}

	// Add seeds
	for (uint32_t i = 0; i < cmd->seed_count; i++) {
		int x = cmd->seeds[i].x;
		int y = cmd->seeds[i].y;
		if (x >= 0 && x < (int)w && y >= 0 && y < (int)h) {
			uint32_t idx = y * w + x;
			// Check mask
			if (cmd->mask && cmd->mask[idx] == 0)
				continue;

			cmd->output[idx] = cmd->seeds[i].value;
			filled[idx] = 1;
			ffq_Push(queue, x, y, cmd->seeds[i].value);
		}
	}

	// Determine connectivity
	const int* dx = (cmd->connectivity == FF_CONNECT_4) ? dx4 : dx8;
	const int* dy = (cmd->connectivity == FF_CONNECT_4) ? dy4 : dy8;
	int num_dirs = (cmd->connectivity == FF_CONNECT_4) ? 4 : 8;

	// Scratch for neighbor info
	FFNeighbor neighbors[8];

	// Process queue
	while (!ffq_IsEmpty(queue)) {
		FFQueueItem item = ffq_Pop(queue);
		int cx = item.x;
		int cy = item.y;

		// Check each neighbor
		for (int d = 0; d < num_dirs; d++) {
			int nx = cx + dx[d];
			int ny = cy + dy[d];

			// Bounds check
			if (nx < 0 || nx >= (int)w || ny < 0 || ny >= (int)h)
				continue;

			uint32_t nidx = ny * w + nx;

			// Skip if already filled
			if (filled[nidx])
				continue;

			// Skip if masked out
			if (cmd->mask && cmd->mask[nidx] == 0)
				continue;

			// Gather filled neighbors for rule evaluation
			int neighbor_count = 0;
			for (int d2 = 0; d2 < num_dirs; d2++) {
				int nnx = nx + dx[d2];
				int nny = ny + dy[d2];
				if (nnx < 0 || nnx >= (int)w || nny < 0 || nny >= (int)h)
					continue;

				uint32_t nnidx = nny * w + nnx;
				if (filled[nnidx]) {
					neighbors[neighbor_count].value = cmd->output[nnidx];
					neighbors[neighbor_count].dx = dx[d2];
					neighbors[neighbor_count].dy = dy[d2];
					// Euclidean distance
					neighbors[neighbor_count].distance =
						sqrtf((float)(dx[d2] * dx[d2] + dy[d2] * dy[d2]));
					neighbor_count++;
				}
			}

			if (neighbor_count == 0)
				continue;

			// Evaluate rule
			FFRuleContext ctx = {
				.x = nx,
				.y = ny,
				.width = w,
				.height = h,
				.neighbors = neighbors,
				.neighbor_count = neighbor_count,
				.user_data = cmd->user_data
			};

			float new_value = cmd->rule(&ctx);

			// Skip if rule returns infinity or exceeds max
			if (!isfinite(new_value) || new_value > max_val)
				continue;

			// Fill the cell
			cmd->output[nidx] = new_value;
			filled[nidx] = 1;
			ffq_Push(queue, nx, ny, new_value);
		}
	}

	// Cleanup
	ffq_Destroy(queue);
	if (owns_filled && !cmd->filled) {
		free(filled);
	} else if (!owns_filled) {
		cmd->filled = filled;
	}

	return 0;
}

void ff_Free(FloodFillCmd* cmd) {
	if (!cmd) return;

	free(cmd->output);
	free(cmd->filled);
	cmd->output = NULL;
	cmd->filled = NULL;
}
