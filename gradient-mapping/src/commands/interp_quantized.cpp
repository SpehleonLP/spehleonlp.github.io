#include "interp_quantized.h"
#include "sdf_layered.h"
#include "label_regions.h"
//#include "debug_png.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <memory>

/* ========== Debug PNG Export Helpers ========== */
#if DEBUG_IMG_OUT
#include <stdio.h>
#include "../debug_output.h"

static void iq_debug_export_distances(InterpolateQuantizedCmd *cmd) {
    uint32_t W = cmd->W, H = cmd->H;
    uint32_t npixels = W * H;

    auto dist_lower = std::unique_ptr<float[]>(new (std::nothrow) float[npixels]);
    auto dist_higher = std::unique_ptr<float[]>(new (std::nothrow) float[npixels]);
    auto src_data = std::unique_ptr<float[]>(new (std::nothrow) float[npixels]);
    auto output_data = std::unique_ptr<float[]>(new (std::nothrow) float[npixels]);

    if (!dist_lower || !dist_higher || !src_data || !output_data) {
        return;
    }

    float max_dist = 0;
    for (uint32_t i = 0; i < npixels; i++) {
        if (cmd->pixels[i].dist_lower > max_dist) max_dist = cmd->pixels[i].dist_lower;
        if (cmd->pixels[i].dist_higher > max_dist) max_dist = cmd->pixels[i].dist_higher;
    }

    for (uint32_t i = 0; i < npixels; i++) {
        src_data[i] = (float)cmd->src[i];
        output_data[i] = cmd->output[i];

        /* Use -1 as "no data" indicator, shown as black */
        dist_lower[i] = cmd->pixels[i].dist_lower >= 0 ? cmd->pixels[i].dist_lower : 0;
        dist_higher[i] = cmd->pixels[i].dist_higher >= 0 ? cmd->pixels[i].dist_higher : 0;
    }

    /* Export individual images */
    char pathbuf[512];

    PngFloatCmd cmd_lower = {
        .path = debug_path("iq_dist_lower.png", pathbuf, sizeof(pathbuf)),
        .data = dist_lower.get(),
        .width = W, .height = H,
        .min_val = 0, .max_val = max_dist > 0 ? max_dist : 1,
        .auto_range = 0
    };
    png_ExportFloat(&cmd_lower);

    char pathbuf2[512];
    PngFloatCmd cmd_higher = {
        .path = debug_path("iq_dist_higher.png", pathbuf2, sizeof(pathbuf2)),
        .data = dist_higher.get(),
        .width = W, .height = H,
        .min_val = 0, .max_val = max_dist > 0 ? max_dist : 1,
        .auto_range = 0
    };
    png_ExportFloat(&cmd_higher);

    char pathbuf3[512];
    PngFloatCmd cmd_output = {
        .path = debug_path("iq_output.png", pathbuf3, sizeof(pathbuf3)),
        .data = output_data.get(),
        .width = W, .height = H,
        .min_val = 0, .max_val = 1,
        .auto_range = 0
    };
    png_ExportFloat(&cmd_output);

    /* Create coverage visualization:
     * 0.00 = no data (neither boundary found) - black
     * 0.33 = only lower boundary found - dark grey
     * 0.66 = only higher boundary found - light grey
     * 1.00 = both boundaries found - white
     */
    auto coverage = std::unique_ptr<float[]>(new (std::nothrow) float[npixels]);
    if (coverage) {
        for (uint32_t i = 0; i < npixels; i++) {
            int has_lower = cmd->pixels[i].dist_lower >= 0;
            int has_higher = cmd->pixels[i].dist_higher >= 0;
            if (has_lower && has_higher) {
                coverage[i] = 1.0f;
            } else if (has_lower) {
                coverage[i] = 0.33f;
            } else if (has_higher) {
                coverage[i] = 0.66f;
            } else {
                coverage[i] = 0.0f;
            }
        }

        char pathbuf4[512];
        PngFloatCmd cmd_coverage = {
            .path = debug_path("iq_coverage.png", pathbuf4, sizeof(pathbuf4)),
            .data = coverage.get(),
            .width = W, .height = H,
            .min_val = 0, .max_val = 1,
            .auto_range = 0
        };
        png_ExportFloat(&cmd_coverage);
    }

    /* Export a 2x2 grid comparison */
    PngGridTile tiles[4] = {
        { .type = PNG_TILE_GRAYSCALE, .data = src_data.get() },       // top-left: source
        { .type = PNG_TILE_GRAYSCALE, .data = output_data.get() },    // top-right: output t
        { .type = PNG_TILE_GRAYSCALE, .data = dist_lower.get() },     // bottom-left: dist to V-1
        { .type = PNG_TILE_GRAYSCALE, .data = dist_higher.get() }     // bottom-right: dist to V+1
    };

    char pathbuf5[512];
    PngGridCmd grid_cmd = {
        .path = debug_path("iq_grid.png", pathbuf5, sizeof(pathbuf5)),
        .tile_width = W,
        .tile_height = H,
        .cols = 2,
        .rows = 2,
        .tiles = tiles
    };
    png_ExportGrid(&grid_cmd);
}

#endif /* DEBUG_IMG_OUT */

int iq_Initialize(InterpolateQuantizedCmd *cmd, const uint8_t *src, const uint8_t * prev_color, uint32_t W, uint32_t H,
                  const SDFDistanceParams *params, int dbg) {
    cmd->src = src;
    cmd->prev_color = prev_color;
    cmd->W = W;
    cmd->H = H;
    cmd->dbg = dbg;

    /* Set distance parameters (default to Euclidean if NULL) */
    if (params) {
        cmd->dist_params = *params;
    } else {
        cmd->dist_params.minkowski = 1.0f;  /* exp2(1) = 2 = Euclidean */
        cmd->dist_params.chebyshev = 0.0f;
    }

    uint32_t npixels = W * H;

	if(cmd->labels == 0L)
	{
		/* Allocate labels */
		cmd->labels = (uint32_t *)malloc(npixels * sizeof(uint32_t));
		if (!cmd->labels) goto fail;

		/* Run label_regions */
		LabelRegionsCmd label_cmd = {
			.src = src,
			.W = W,
			.H = H,
			.connectivity = LABEL_CONNECT_8,
			.labels = cmd->labels,
		};
		if (label_regions(&label_cmd) != 0) goto fail;
		cmd->num_regions = label_cmd.num_regions;
    }

    /* Allocate per-pixel data */
    if (!cmd->pixels) cmd->pixels = (InterpPixel *)malloc(npixels * sizeof(InterpPixel));
    if (!cmd->pixels) goto fail;
    for (uint32_t i = 0; i < npixels; i++) {
        cmd->pixels[i].dist_lower = -1.0f;
        cmd->pixels[i].dist_higher = -1.0f;
    }

    /* Allocate per-region data */
    if (!cmd->regions) cmd->regions = (InterpRegion *)malloc(cmd->num_regions * sizeof(InterpRegion));
    if (!cmd->regions) goto fail;
    for (uint32_t i = 0; i < cmd->num_regions; i++) {
        cmd->regions[i].max_dist_lower = -1.0f;
        cmd->regions[i].max_dist_higher = -1.0f;
    }

    /* Allocate output */
    if (!cmd->output) cmd->output = (float *)malloc(npixels * sizeof(float));
    if (!cmd->output) goto fail;

	memset(cmd->next_color, 0, sizeof(cmd->next_color));

	for(int i = 0; i < 256; ++i)
	{
		if(prev_color[i] != 255)
			cmd->next_color[prev_color[i]] = i;
	}

#if DEBUG_IMG_OUT
    if (dbg) {
        /* Debug: show color adjacency mapping */
        printf("IQ: prev_color/next_color mapping:\n");
        int mapped_count = 0;
        for (int i = 0; i < 256; ++i) {
            if (prev_color[i] != 255 || cmd->next_color[i] != 0) {
                if (mapped_count < 20) {  /* limit output */
                    printf("  V=%d: prev=%d, next=%d\n", i, prev_color[i], cmd->next_color[i]);
                }
                mapped_count++;
            }
        }
        if (mapped_count > 20) printf("  ... and %d more\n", mapped_count - 20);

        /* Debug: show unique values in source */
        int value_counts[256] = {0};
        for (uint32_t i = 0; i < npixels; i++) {
            value_counts[src[i]]++;
        }
        printf("IQ: unique source values: ");
        for (int i = 0; i < 256; i++) {
            if (value_counts[i] > 0) printf("%d(%d) ", i, value_counts[i]);
        }
        printf("\n");
    }
#endif

    return 0;

fail:
    iq_Free(cmd);
    return -1;
}

/* After each SDF iteration, extract V-1 and V+1 distances */
static void extract_neighbor_distances(InterpolateQuantizedCmd *cmd, SDFContext *sdf, int iteration) {
    uint32_t W = cmd->W, H = cmd->H;

#if DEBUG_IMG_OUT
    int invalid_count = 0;
    int same_val_count = 0;
    int match_lower_count = 0;
    int match_higher_count = 0;
    int no_match_count = 0;
    int already_set_count = 0;
    int no_match_examples = 0;  /* how many no-match examples we've printed */
#endif

    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            uint32_t idx = y * W + x;
            SDFCell *cell = &sdf->cells[idx];

            /* Skip invalid cells (source_value=256 means no result) */
            if (cell->source_value == 256) {
#if DEBUG_IMG_OUT
                invalid_count++;
#endif
                continue;
            }

            uint8_t V = cmd->src[idx];
            uint8_t lowa = cmd->prev_color[V];
            uint8_t uppa = cmd->next_color[V];

            uint8_t found_val = cell->source_value;
            float dist = cell->distance;  /* use pre-computed metric distance */

            /* Check if this is V-1 or V+1 */
            if(found_val != V)
            {
				if (lowa < V && found_val == lowa) {
				    if (cmd->pixels[idx].dist_lower < 0) {
					    cmd->pixels[idx].dist_lower = dist;
#if DEBUG_IMG_OUT
                        match_lower_count++;
#endif
				    } else {
#if DEBUG_IMG_OUT
				        already_set_count++;
#endif
				    }
				} else if (uppa > V && found_val == uppa) {
				    if (cmd->pixels[idx].dist_higher < 0) {
					    cmd->pixels[idx].dist_higher = dist;
#if DEBUG_IMG_OUT
                        match_higher_count++;
#endif
				    } else {
#if DEBUG_IMG_OUT
				        already_set_count++;
#endif
				    }
				} else {
#if DEBUG_IMG_OUT
				    no_match_count++;
				    if (cmd->dbg && no_match_examples < 5) {
				        printf("  no_match: V=%d lowa=%d uppa=%d found_val=%d dist=%.2f\n",
				               V, lowa, uppa, found_val, dist);
				        no_match_examples++;
				    }
#endif
				}
            } else {
#if DEBUG_IMG_OUT
                same_val_count++;
#endif
            }
        }
    }

#if DEBUG_IMG_OUT
    if (cmd->dbg && iteration < 5) {  /* Only print first 5 iterations */
        printf("IQ iter %d: invalid=%d same_val=%d match_lo=%d match_hi=%d no_match=%d already=%d\n",
               iteration, invalid_count, same_val_count, match_lower_count, match_higher_count,
               no_match_count, already_set_count);
    }
    (void)no_match_examples;  /* suppress unused warning when dbg=0 */
#endif
}

/* Compute max distances per region */
static void compute_region_max_distances(InterpolateQuantizedCmd *cmd) {
    uint32_t W = cmd->W, H = cmd->H;

    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            uint32_t idx = y * W + x;
            uint32_t rid = cmd->labels[idx];
            if ((uint32_t)rid >= cmd->num_regions) continue;

            InterpPixel *pix = &cmd->pixels[idx];
            InterpRegion *reg = &cmd->regions[rid];

            if (pix->dist_lower > reg->max_dist_lower) {
                reg->max_dist_lower = pix->dist_lower;
            }
            if (pix->dist_higher > reg->max_dist_higher) {
                reg->max_dist_higher = pix->dist_higher;
            }
        }
    }
    
    for(uint32_t rid = 0; rid < cmd->num_regions; ++rid)
    {
		InterpRegion *reg = &cmd->regions[rid];
		reg->max_dist_lower += 1;
		reg->max_dist_higher += 1;
    }
}

/* Perform the interpolation */
static void interpolate(InterpolateQuantizedCmd *cmd) {
    uint32_t W = cmd->W, H = cmd->H;

    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            uint32_t idx = y * W + x;
            uint32_t rid = cmd->labels[idx];

            if(cmd->src[idx] == 0)  /* -1 = transparent */
            {
				cmd->output[idx] = 0;
				continue;
			}

            float V = (float)cmd->src[idx];
            float lowa = (float)cmd->prev_color[cmd->src[idx]];
            float range = V - lowa;
            
            InterpPixel *pix = &cmd->pixels[idx];

            float t = 0;

            if((pix->dist_lower >= 0 && pix->dist_higher >  0)
            || (pix->dist_lower >  0 && pix->dist_higher >= 0))
			{
				t = pix->dist_lower / (pix->dist_lower + pix->dist_higher);
			}
            else
            {
				InterpRegion *reg = &cmd->regions[rid];

				if(reg->max_dist_lower > 0)
                    t = pix->dist_lower / reg->max_dist_lower;

				if(reg->max_dist_higher > 0)
					t = 1.0 - pix->dist_higher / reg->max_dist_higher;
            }


            /* t=0 at V-1 boundary, t=1 at V+1 boundary */
            /* Clamp to >= 0 for edge case when V=0 (no V-1 exists) */
            cmd->output[idx] = fmaxf(0, lowa + t*range);
        }
    }
}

int iq_Execute(InterpolateQuantizedCmd *cmd) {
    /* Initialize and run SDF */
    SDFContext sdf = {0};
    sdf.labels = cmd->labels;
    sdf.num_regions = cmd->num_regions;
    if (sdf_Initialize(&sdf, cmd->src, cmd->W, cmd->H,
                       &cmd->dist_params, cmd->dbg) != 0) {
        return -1;
    }

    /* Run SDF iterations, extracting V-1 and V+1 distances after each */
    int iteration = 0;
    int result;
    do {
        result = sdf_Iterate(&sdf);
        if (result < 0) {
            sdf_Free(&sdf);
            return -1;
        }

        /* Extract distances to V-1 and V+1 from this iteration's results */
        extract_neighbor_distances(cmd, &sdf, iteration);

        iteration++;
        if (iteration > 255) break;  /* Safety limit */
    } while (result == 1);

    sdf_Free(&sdf);

#if DEBUG_IMG_OUT
    if (cmd->dbg) {
        printf("IQ: completed %d SDF iterations\n", iteration);
        /* Count coverage */
        uint32_t npix = cmd->W * cmd->H;
        int has_lower = 0, has_higher = 0, has_both = 0, has_none = 0;
        for (uint32_t i = 0; i < npix; i++) {
            int lo = cmd->pixels[i].dist_lower >= 0;
            int hi = cmd->pixels[i].dist_higher >= 0;
            if (lo && hi) has_both++;
            else if (lo) has_lower++;
            else if (hi) has_higher++;
            else has_none++;
        }
        printf("IQ: coverage: both=%d lower_only=%d higher_only=%d none=%d (total=%u)\n",
               has_both, has_lower, has_higher, has_none, npix);
    }
#endif

    /* Compute max distances per region */
    compute_region_max_distances(cmd);

    /* Perform interpolation */
    interpolate(cmd);

#if DEBUG_IMG_OUT
    if (cmd->dbg) {
        iq_debug_export_distances(cmd);
    }
#endif

    return 0;
}

void iq_Free(InterpolateQuantizedCmd *cmd) {
    free(cmd->labels);
    free(cmd->pixels);
    free(cmd->regions);
    free(cmd->output);
    memset(cmd, 0, sizeof(*cmd));
}
