#include "erosion_dijkstra.h"
#include "interp_quantized.h"
#include <stdlib.h>
#include <string.h>

/* Access colors_used array - it's a u8vec4[256] */
typedef struct { uint8_t x, y, z, w; } u8vec4_local;

int ed_Initialize(ErosionDijkstraCmd *cmd, const uint8_t *src, uint32_t W, uint32_t H,
                  const SDFDistanceParams *params, int dbg) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->src = src;
    cmd->W = W;
    cmd->H = H;
    cmd->dbg = dbg;

    /* Set distance parameters (default to Euclidean if NULL) */
    if (params) {
        cmd->dist_params = *params;
    } else {
        cmd->dist_params.minkowski = 2.0f;  /* exp2(1) = 2 = Euclidean */
        cmd->dist_params.chebyshev = 0.0f;
    }

    /* Allocate output buffer */
    uint32_t npixels = W * H;
    cmd->output = (float *)malloc(npixels * 4 * sizeof(float));
    if (!cmd->output) {
        return -1;
    }

    return 0;
}

/*
 * Process a single channel using the interp_quantized pipeline.
 *
 * channel: 0=R, 1=G, 2=B, 3=A
 */
static int process_channel(ErosionDijkstraCmd *cmd, int channel) {
    uint32_t W = cmd->W, H = cmd->H;
    uint32_t npixels = W * H;

    /* Extract channel data as int16 (-1 for "no data" if we need it) */
    int16_t *channel_data = (int16_t *)malloc(npixels * sizeof(int16_t));
    if (!channel_data) return -1;

    for (uint32_t i = 0; i < npixels; i++) {
        channel_data[i] = (int16_t)cmd->src[i * 4 + channel];
    }

    free(channel_data);
    return 0;
}

int ed_Execute(ErosionDijkstraCmd *cmd) {
    /* Process each channel independently */
    for (int c = 0; c < 4; c++) {
        if (process_channel(cmd, c) != 0) {
            return -1;
        }
    }

    return 0;
}

void ed_Free(ErosionDijkstraCmd *cmd) {
    free(cmd->output);
    memset(cmd, 0, sizeof(*cmd));
}
