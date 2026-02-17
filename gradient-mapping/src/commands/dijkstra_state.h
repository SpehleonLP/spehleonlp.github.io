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
    /* Sentinel: dx=0,dy=0 with no valid boundary is distinguishable
     * because dist_lower/dist_higher < 0 means "no data" in InterpPixel.
     * We use dx=0,dy=0 as the sentinel here (distance would be 0). */
};

struct DijkstraState {
    DijkstraChannelState channels[3];
    uint32_t W, H;
};

static inline void dijkstra_state_free(void* ptr) {
    if (!ptr) return;
    DijkstraState* state = (DijkstraState*)ptr;
    for (int c = 0; c < 3; c++) {
        free(state->channels[c].labels);
        free(state->channels[c].dx_lower);
        free(state->channels[c].dy_lower);
        free(state->channels[c].dx_higher);
        free(state->channels[c].dy_higher);
    }
    free(state);
}

#endif /* DIJKSTRA_STATE_H */
