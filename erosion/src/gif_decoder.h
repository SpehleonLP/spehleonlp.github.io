#ifndef GIF_DECODER_H
#define GIF_DECODER_H

#include <stdint.h>

typedef struct GifDecoder GifDecoder;

typedef struct GifFrameInfo {
    int width;
    int height;
    int delay;  // in centiseconds (1/100th of a second)
} GifFrameInfo;

// Initialize a GIF decoder from memory buffer
// Returns NULL on failure
GifDecoder* gif_init(const uint8_t* data, int length);

// Get GIF dimensions
int gif_get_width(GifDecoder* decoder);
int gif_get_height(GifDecoder* decoder);

// Get total number of frames
int gif_get_frame_count(GifDecoder* decoder);

// Get total duration in centiseconds
int gif_get_total_duration(GifDecoder* decoder);

// Decode next frame and composite it onto the output buffer
// Returns 1 on success, 0 if no more frames
// output must be width*height*4 bytes (RGBA)
// delay_out receives frame delay in centiseconds
int gif_decode_next_frame(GifDecoder* decoder, uint8_t* output, int* delay_out);

// Reset decoder to first frame
void gif_reset(GifDecoder* decoder);

// Free decoder resources
void gif_free(GifDecoder* decoder);

#endif // GIF_DECODER_H
