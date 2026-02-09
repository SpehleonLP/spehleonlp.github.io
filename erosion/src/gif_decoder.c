#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR

#include "stb_image.h"
#include "gif_decoder.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct GifDecoder {
    stbi__context context;
    stbi__gif gif;
    const uint8_t* data;
    int data_length;
    int width;
    int height;
    int frame_count;
    int current_frame;
    int total_duration;
    uint8_t* composite;  // Composited frame buffer
    int initialized;
};

// Forward declarations for stb_image internal functions we need
static int gif_count_frames_and_duration(const uint8_t* data, int length, int* duration_out);

GifDecoder* gif_init(const uint8_t* data, int length) {
    if (!data || length <= 0) {
        return NULL;
    }

    GifDecoder* decoder = (GifDecoder*)calloc(1, sizeof(GifDecoder));
    if (!decoder) {
        return NULL;
    }

    decoder->data = data;
    decoder->data_length = length;

    // Initialize stb context for memory reading
    stbi__start_mem(&decoder->context, data, length);

    // Check if it's actually a GIF
    if (!stbi__gif_test(&decoder->context)) {
        fprintf(stderr, "gif_init: not a valid GIF\n");
        free(decoder);
        return NULL;
    }

    // Reset context after test
    stbi__start_mem(&decoder->context, data, length);

    // Initialize gif structure
    memset(&decoder->gif, 0, sizeof(decoder->gif));

    // Do a first pass to count frames and get dimensions
    int duration = 0;
    decoder->frame_count = gif_count_frames_and_duration(data, length, &duration);
    decoder->total_duration = duration;

    if (decoder->frame_count <= 0) {
        fprintf(stderr, "gif_init: no frames found\n");
        free(decoder);
        return NULL;
    }

    // Get dimensions by loading first frame
    stbi__start_mem(&decoder->context, data, length);
    memset(&decoder->gif, 0, sizeof(decoder->gif));

    int comp;
    uint8_t* first_frame = stbi__gif_load_next(&decoder->context, &decoder->gif, &comp, 4, NULL);
    if (!first_frame || first_frame == (uint8_t*)&decoder->context) {
        fprintf(stderr, "gif_init: failed to load first frame\n");
        free(decoder);
        return NULL;
    }

    decoder->width = decoder->gif.w;
    decoder->height = decoder->gif.h;

    // Allocate composite buffer
    decoder->composite = (uint8_t*)malloc(decoder->width * decoder->height * 4);
    if (!decoder->composite) {
        if (decoder->gif.out) STBI_FREE(decoder->gif.out);
        if (decoder->gif.history) STBI_FREE(decoder->gif.history);
        if (decoder->gif.background) STBI_FREE(decoder->gif.background);
        free(decoder);
        return NULL;
    }

    // Copy first frame to composite
    memcpy(decoder->composite, first_frame, decoder->width * decoder->height * 4);

    decoder->current_frame = 1;
    decoder->initialized = 1;

    printf("gif_init: %dx%d, %d frames, %d centiseconds total\n",
           decoder->width, decoder->height, decoder->frame_count, decoder->total_duration);

    return decoder;
}

static int gif_count_frames_and_duration(const uint8_t* data, int length, int* duration_out) {
    stbi__context ctx;
    stbi__gif gif;
    int count = 0;
    int total_delay = 0;

    stbi__start_mem(&ctx, data, length);
    memset(&gif, 0, sizeof(gif));

    int comp;
    uint8_t* frame;

    while ((frame = stbi__gif_load_next(&ctx, &gif, &comp, 4, NULL)) != NULL) {
        if (frame == (uint8_t*)&ctx) {
            // End marker
            break;
        }
        count++;
        total_delay += gif.delay ? gif.delay : 10;  // Default 10 centiseconds
    }

    // Clean up
    if (gif.out) STBI_FREE(gif.out);
    if (gif.history) STBI_FREE(gif.history);
    if (gif.background) STBI_FREE(gif.background);

    *duration_out = total_delay;
    return count;
}

int gif_get_width(GifDecoder* decoder) {
    return decoder ? decoder->width : 0;
}

int gif_get_height(GifDecoder* decoder) {
    return decoder ? decoder->height : 0;
}

int gif_get_frame_count(GifDecoder* decoder) {
    return decoder ? decoder->frame_count : 0;
}

int gif_get_total_duration(GifDecoder* decoder) {
    return decoder ? decoder->total_duration : 0;
}

int gif_decode_next_frame(GifDecoder* decoder, uint8_t* output, int* delay_out) {
    if (!decoder || !output) {
        return 0;
    }

    // First frame was already decoded in init
    if (decoder->current_frame == 1) {
        memcpy(output, decoder->composite, decoder->width * decoder->height * 4);
        if (delay_out) {
            *delay_out = decoder->gif.delay ? decoder->gif.delay : 10;
        }
        decoder->current_frame++;
        return 1;
    }

    if (decoder->current_frame > decoder->frame_count) {
        return 0;
    }

    int comp;
    uint8_t* frame = stbi__gif_load_next(&decoder->context, &decoder->gif, &comp, 4, NULL);

    if (!frame || frame == (uint8_t*)&decoder->context) {
        return 0;
    }

    // stbi__gif_load_next handles disposal and compositing internally,
    // the frame buffer is the final composited result
    memcpy(decoder->composite, frame, decoder->width * decoder->height * 4);
    memcpy(output, decoder->composite, decoder->width * decoder->height * 4);

    if (delay_out) {
        *delay_out = decoder->gif.delay ? decoder->gif.delay : 10;
    }

    decoder->current_frame++;
    return 1;
}

void gif_reset(GifDecoder* decoder) {
    if (!decoder) return;

    // Clean up existing gif state
    if (decoder->gif.out) STBI_FREE(decoder->gif.out);
    if (decoder->gif.history) STBI_FREE(decoder->gif.history);
    if (decoder->gif.background) STBI_FREE(decoder->gif.background);

    // Re-initialize
    stbi__start_mem(&decoder->context, decoder->data, decoder->data_length);
    memset(&decoder->gif, 0, sizeof(decoder->gif));

    // Load first frame
    int comp;
    uint8_t* first_frame = stbi__gif_load_next(&decoder->context, &decoder->gif, &comp, 4, NULL);
    if (first_frame && first_frame != (uint8_t*)&decoder->context) {
        memcpy(decoder->composite, first_frame, decoder->width * decoder->height * 4);
    }

    decoder->current_frame = 1;
}

void gif_free(GifDecoder* decoder) {
    if (!decoder) return;

    if (decoder->gif.out) STBI_FREE(decoder->gif.out);
    if (decoder->gif.history) STBI_FREE(decoder->gif.history);
    if (decoder->gif.background) STBI_FREE(decoder->gif.background);
    if (decoder->composite) free(decoder->composite);

    free(decoder);
}
