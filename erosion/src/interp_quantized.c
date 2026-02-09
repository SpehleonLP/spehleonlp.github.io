#include "interp_quantized.h"
#include "sdf_layered.h"
#include "label_regions.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ========== Debug PNG Export Helpers ========== */
#if DEBUG_IMG_OUT
#include <stdio.h>

static void iq_debug_export_distances(InterpolateQuantizedCmd *cmd) {
    uint32_t W = cmd->W, H = cmd->H;
    uint32_t npixels = W * H;

    float *dist_lower = malloc(npixels * sizeof(float));
    float *dist_higher = malloc(npixels * sizeof(float));
    float *src_data = malloc(npixels * sizeof(float));
    float *output_data = malloc(npixels * sizeof(float));

    if (!dist_lower || !dist_higher || !src_data || !output_data) {
        free(dist_lower);
        free(dist_higher);
        free(src_data);
        free(output_data);
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
    PngFloatCmd cmd_lower = {
        .path = "/iq_dist_lower.png",
        .data = dist_lower,
        .width = W, .height = H,
        .min_val = 0, .max_val = max_dist > 0 ? max_dist : 1,
        .auto_range = 0
    };
    png_ExportFloat(&cmd_lower);

    PngFloatCmd cmd_higher = {
        .path = "/iq_dist_higher.png",
        .data = dist_higher,
        .width = W, .height = H,
        .min_val = 0, .max_val = max_dist > 0 ? max_dist : 1,
        .auto_range = 0
    };
    png_ExportFloat(&cmd_higher);

    PngFloatCmd cmd_output = {
        .path = "/iq_output.png",
        .data = output_data,
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
    float *coverage = malloc(npixels * sizeof(float));
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

        PngFloatCmd cmd_coverage = {
            .path = "/iq_coverage.png",
            .data = coverage,
            .width = W, .height = H,
            .min_val = 0, .max_val = 1,
            .auto_range = 0
        };
        png_ExportFloat(&cmd_coverage);
        free(coverage);
    }

    /* Export a 2x2 grid comparison */
    PngGridTile tiles[4] = {
        { .type = PNG_TILE_GRAYSCALE, .data = src_data },       // top-left: source
        { .type = PNG_TILE_GRAYSCALE, .data = output_data },    // top-right: output t
        { .type = PNG_TILE_GRAYSCALE, .data = dist_lower },     // bottom-left: dist to V-1
        { .type = PNG_TILE_GRAYSCALE, .data = dist_higher }     // bottom-right: dist to V+1
    };

    PngGridCmd grid_cmd = {
        .path = "/iq_grid.png",
        .tile_width = W,
        .tile_height = H,
        .cols = 2,
        .rows = 2,
        .tiles = tiles
    };
    png_ExportGrid(&grid_cmd);

    free(dist_lower);
    free(dist_higher);
    free(src_data);
    free(output_data);
}

#endif /* DEBUG_IMG_OUT */

int iq_Initialize(InterpolateQuantizedCmd *cmd, const int16_t *src, uint32_t W, uint32_t H, int dbg) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->src = src;
    cmd->W = W;
    cmd->H = H;
    cmd->dbg = dbg;

    uint32_t npixels = W * H;

    /* Allocate labels */
    cmd->labels = malloc(npixels * sizeof(int32_t));
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

    /* Allocate per-pixel data */
    cmd->pixels = malloc(npixels * sizeof(InterpPixel));
    if (!cmd->pixels) goto fail;
    for (uint32_t i = 0; i < npixels; i++) {
        cmd->pixels[i].dist_lower = -1.0f;
        cmd->pixels[i].dist_higher = -1.0f;
    }

    /* Allocate per-region data */
    cmd->regions = malloc(cmd->num_regions * sizeof(InterpRegion));
    if (!cmd->regions) goto fail;
    for (uint32_t i = 0; i < cmd->num_regions; i++) {
        cmd->regions[i].max_dist_lower = -1.0f;
        cmd->regions[i].max_dist_higher = -1.0f;
    }

    /* Allocate output */
    cmd->output = malloc(npixels * sizeof(float));
    if (!cmd->output) goto fail;

    return 0;

fail:
    iq_Free(cmd);
    return -1;
}

/* After each SDF iteration, extract V-1 and V+1 distances */
static void extract_neighbor_distances(InterpolateQuantizedCmd *cmd, SDFContext *sdf) {
    uint32_t W = cmd->W, H = cmd->H;

    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            uint32_t idx = y * W + x;
            SDFCell *cell = &sdf->cells[idx];

            /* Skip invalid cells (source_value=256 means no result) */
            if (cell->source_value == 256) continue;

            uint8_t V = cmd->src[idx];
            int16_t found_val = cell->source_value;
            float dist = sqrtf((float)(cell->dx * cell->dx + cell->dy * cell->dy));

            /* Check if this is V-1 or V+1 */
            if (found_val == (int16_t)V - 1 && cmd->pixels[idx].dist_lower < 0) {
                cmd->pixels[idx].dist_lower = dist;
            } else if (found_val == (int16_t)V + 1 && cmd->pixels[idx].dist_higher < 0) {
                cmd->pixels[idx].dist_higher = dist;
            }
        }
    }
}

/* Compute max distances per region */
static void compute_region_max_distances(InterpolateQuantizedCmd *cmd) {
    uint32_t W = cmd->W, H = cmd->H;

    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            uint32_t idx = y * W + x;
            int32_t rid = cmd->labels[idx];
            if (rid < 0 || (uint32_t)rid >= cmd->num_regions) continue;

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
            int32_t rid = cmd->labels[idx];

            if(cmd->src[idx] < 0)  /* -1 = transparent */
            {
				cmd->output[idx] = 0;
				continue;
			}

            float V = (float)cmd->src[idx];
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
            cmd->output[idx] = fmaxf(0, (V-1.f) + t);
        }
    }
}

int iq_Execute(InterpolateQuantizedCmd *cmd) {
    /* Initialize and run SDF */
    SDFContext sdf;
    if (sdf_Initialize(&sdf, (int16_t *)cmd->src, cmd->W, cmd->H, cmd->dbg) != 0) {
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
        extract_neighbor_distances(cmd, &sdf);

        iteration++;
        if (iteration > 255) break;  /* Safety limit */
    } while (result == 1);

    sdf_Free(&sdf);

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
