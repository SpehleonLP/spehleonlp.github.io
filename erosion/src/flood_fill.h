#ifndef FLOOD_FILL_H
#define FLOOD_FILL_H

#include <stdint.h>

/*
 * FloodFillCmd - Priority queue flood fill with automata rules
 *
 * Performs flood fill propagation using a priority queue, where the
 * fill value at each cell is determined by automata-style rules based
 * on already-filled neighbor cells.
 *
 * Algorithm:
 *   1. Seed cells are added to priority queue with initial values
 *   2. Pop lowest-priority cell from queue
 *   3. For each unfilled neighbor:
 *      a. Compute candidate value using rule function based on filled neighbors
 *      b. Add to queue with priority = candidate value
 *   4. Repeat until queue empty
 *
 * The rule function receives neighbor values and returns the new cell value.
 * This enables distance transforms, watershed, Dijkstra-like propagation, etc.
 */

// Neighbor info passed to rule function
typedef struct {
	float value;       // neighbor's filled value
	int dx, dy;        // relative position (-1, 0, or 1)
	float distance;    // Euclidean distance (1.0 or ~1.414 for diagonal)
} FFNeighbor;

// Context passed to rule function
typedef struct {
	int x, y;                    // current cell position
	uint32_t width, height;      // grid dimensions
	const FFNeighbor* neighbors; // array of filled neighbors
	int neighbor_count;          // number of filled neighbors (1-8)
	const void* user_data;       // optional user data
} FFRuleContext;

// Rule function: computes fill value from filled neighbors
// Return value becomes the cell's value and its queue priority
// Return INFINITY to skip filling this cell
typedef float (*FFRuleFunc)(const FFRuleContext* ctx);

// Seed point for starting the fill
typedef struct {
	int x, y;
	float value;       // initial value (also priority)
} FFSeed;

// Connectivity options
typedef enum {
	FF_CONNECT_4 = 4,  // cardinal directions only
	FF_CONNECT_8 = 8   // include diagonals
} FFConnectivity;

typedef struct FloodFillCmd {
	// Grid parameters
	uint32_t width;
	uint32_t height;

	// Seeds
	const FFSeed* seeds;
	uint32_t seed_count;

	// Rule function
	FFRuleFunc rule;
	const void* user_data;     // passed to rule function

	// Options
	FFConnectivity connectivity;
	float max_value;           // stop propagation above this value (default INFINITY)

	// Optional mask: if non-NULL, only fill where mask[y*width+x] != 0
	const uint8_t* mask;

	// Output (allocated by Execute if NULL)
	float* output;             // filled values, size = width * height
	uint8_t* filled;           // 1 if cell was filled, 0 otherwise (optional)

} FloodFillCmd;

// Execute the flood fill
// Returns 0 on success, negative on error
int ff_Execute(FloodFillCmd* cmd);

// Free allocated outputs
void ff_Free(FloodFillCmd* cmd);

/*
 * Built-in rule functions
 */

// Distance transform: value = min(neighbor.value + neighbor.distance)
// Produces Euclidean distance from seeds
float ff_rule_distance(const FFRuleContext* ctx);

// Chamfer distance: like distance but uses 3/4 approximation for diagonals
// value = min(neighbor.value + (diagonal ? 1.414 : 1.0))
float ff_rule_chamfer(const FFRuleContext* ctx);

// Weighted average: value = weighted avg of neighbors by inverse distance
float ff_rule_weighted_avg(const FFRuleContext* ctx);

// Min propagation: value = min(neighbor values)
float ff_rule_min(const FFRuleContext* ctx);

// Max propagation: value = max(neighbor values)
float ff_rule_max(const FFRuleContext* ctx);

// Average of all filled neighbors
float ff_rule_average(const FFRuleContext* ctx);

/*
 * Priority Queue (exposed for custom use)
 */

typedef struct {
	int x, y;
	float priority;
} FFQueueItem;

typedef struct FFPriorityQueue {
	FFQueueItem* items;
	uint32_t count;
	uint32_t capacity;
} FFPriorityQueue;

FFPriorityQueue* ffq_Create(uint32_t initial_capacity);
void ffq_Destroy(FFPriorityQueue* q);
void ffq_Push(FFPriorityQueue* q, int x, int y, float priority);
FFQueueItem ffq_Pop(FFPriorityQueue* q);
int ffq_IsEmpty(const FFPriorityQueue* q);

#endif
