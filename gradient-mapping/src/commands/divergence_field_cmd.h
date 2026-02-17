#ifndef DIVERGENCE_FIELD_CMD_H
#define DIVERGENCE_FIELD_CMD_H

#include <stdint.h>
#include <memory>

struct DivergenceFieldCmd {
    /* Input (borrowed) */
    const float* heightmap;
    uint32_t W, H;
    float normal_scale;     // F factor: scales normal.z

    /* Output (owned) */
    std::unique_ptr<float[]> divergence;  // normalized to [-1, +1]
};

int divergence_field_Execute(DivergenceFieldCmd* cmd);

#endif
