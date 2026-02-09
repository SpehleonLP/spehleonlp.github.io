// sources/worley_noise.c
// Worley (cellular) noise data source implementation

#include "worley_noise.h"

// Distance function based on metric
static float worley_distance(vec2 d, WorleyMetric metric) {
    switch (metric) {
    case WORLEY_METRIC_EUCLIDEAN:
        return sqrtf(d.x * d.x + d.y * d.y);
    case WORLEY_METRIC_MANHATTAN:
        return fabsf(d.x) + fabsf(d.y);
    case WORLEY_METRIC_CHEBYSHEV:
        return fmaxf(fabsf(d.x), fabsf(d.y));
    default:
        return sqrtf(d.x * d.x + d.y * d.y);
    }
}

void worley_noise(vec3* dst, const vec3* src, int W, int H,
                  WorleyNoiseParams params, uint32_t seed) {

    float inv_w = 1.0f / (float)W;
    float inv_h = 1.0f / (float)H;

    for (int py = 0; py < H; py++) {
        for (int px = 0; px < W; px++) {
            int idx = py * W + px;

            // Base texture coordinate scaled to cell space
            float tx = ((float)px + 0.5f) * inv_w * params.scale;
            float ty = ((float)py + 0.5f) * inv_h * params.scale;

            // Add source offset if provided
            if (src) {
                tx += src[idx].x * params.scale;
                ty += src[idx].y * params.scale;
            }

            // Cell coordinates
            int cx = (int)floorf(tx);
            int cy = (int)floorf(ty);

            // Position within cell [0, 1)
            float fx = tx - (float)cx;
            float fy = ty - (float)cy;

            // Find two closest distances
            float d1 = 1e10f;  // F1 - closest
            float d2 = 1e10f;  // F2 - second closest
            float nearest_dx = 0, nearest_dy = 0;

            // Check 3x3 neighborhood
            for (int oy = -1; oy <= 1; oy++) {
                for (int ox = -1; ox <= 1; ox++) {
                    int ncx = cx + ox;
                    int ncy = cy + oy;

                    // Generate random point in this cell
                    vec2 rnd = hash2d_vec2(ncx, ncy, seed);

                    // Point position with jitter
                    float point_x = (float)ox + params.jitter * (rnd.x - 0.5f) + 0.5f;
                    float point_y = (float)oy + params.jitter * (rnd.y - 0.5f) + 0.5f;

                    // Distance to this point
                    vec2 delta = { fx - point_x, fy - point_y };
                    float dist = worley_distance(delta, params.metric);

                    // Update closest distances
                    if (dist < d1) {
                        d2 = d1;
                        d1 = dist;
                        nearest_dx = delta.x;
                        nearest_dy = delta.y;
                    } else if (dist < d2) {
                        d2 = dist;
                    }
                }
            }

            // Compute output based on mode
            float value;
            switch (params.mode) {
            case WORLEY_MODE_F1:
                value = d1;
                break;
            case WORLEY_MODE_F2:
                value = d2;
                break;
            case WORLEY_MODE_F2_F1:
                value = d2 - d1;
                break;
            default:
                value = d1;
                break;
            }

            // Normalize to [0, 1] range (approximate)
            value = clampf(value, 0.0f, 1.0f);
            float len = sqrtf(nearest_dx * nearest_dx + nearest_dy * nearest_dy);
            
            if (len > 0.001f) {
                dst[idx].x = -nearest_dx / len * 0.2f;  // Point toward cell
                dst[idx].y = -nearest_dy / len * 0.2f;
            } else {
                dst[idx].x = 0;
                dst[idx].y = 0;
            }

			dst[idx].x = dst[idx].x * (1.0 - value);
			dst[idx].y = dst[idx].y * (1.0 - value);			
            dst[idx].z = value;
        }
    }
}
