#ifndef EROSION_PIPELINE_H
#define EROSION_PIPELINE_H
#include "effect_stack_api.h"


uint8_t* process_erosion_stack(Effect const* effects, int effect_count, int* out_w, int* out_h);

#endif // EROSION_PIPELINE_H
