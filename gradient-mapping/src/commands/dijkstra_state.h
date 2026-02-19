#ifndef DIJKSTRA_STATE_H
#define DIJKSTRA_STATE_H

#include <stdint.h>
#include <stdlib.h>

struct DijkstraChannelState {
    uint32_t* labels;       /* [N] region labels */
    uint32_t  num_regions;
    int16_t*  dx_lower;     /* [N] displacement x to V-1 boundary */
    int16_t*  dy_lower;     /* [N] displacement y to V-1 boundary */
    int16_t*  dx_higher;    /* [N] displacement x to V+1 boundary */
    int16_t*  dy_higher;    /* [N] displacement y to V+1 boundary */
    uint8_t*  has_lower;    /* [N] 1 if dist_lower was valid (>= 0) */
    uint8_t*  has_higher;   /* [N] 1 if dist_higher was valid (>= 0) */
};

struct DijkstraState {
    DijkstraChannelState channels[3];
    uint32_t W, H;
};

/* Ensures *state_io points to a valid DijkstraState with populated dx/dy caches.
 * If *state_io is NULL or dimensions mismatch, runs the full SDF (cold path)
 * and stores the new state. Otherwise, no-op.
 * Defined in erosion_pipeline.cpp (depends on ErosionImageMemo). */
void dijkstra_state_ensure(DijkstraState** state_io, uint32_t W, uint32_t H);

static inline void dijkstra_state_free(void* ptr) {
    if (!ptr) return;
    DijkstraState* state = (DijkstraState*)ptr;
    for (int c = 0; c < 3; c++) {
        free(state->channels[c].labels);
        free(state->channels[c].dx_lower);
        free(state->channels[c].dy_lower);
        free(state->channels[c].dx_higher);
        free(state->channels[c].dy_higher);
        free(state->channels[c].has_lower);
        free(state->channels[c].has_higher);
    }
    free(state);
}

#endif /* DIJKSTRA_STATE_H */
