#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten.h>

// Define the ImageData structure
typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t* data; // GL_RGBA_8888
} ImageData;


// Global variables to store images
static ImageData* images[2] = { NULL, NULL };
static int total_frames = 0;
static int current_frame = 0;
static int frame_width = 0;
static int frame_height = 0;

ImageData * MakeImage(int width, int height)
{
    ImageData * r = (ImageData*)malloc(sizeof(ImageData));

    r->width=width;
    r->height=height;
    r->data = (uint8_t*)calloc(width * height, 4);

	return r;
}

// Initialize the processor
EMSCRIPTEN_KEEPALIVE
void initialize(int total_frames_input, int width, int height) {
    total_frames = total_frames_input;
    frame_width = width;
    frame_height = height;
    current_frame = 0;

	images[0] = MakeImage(width, height);
	images[1] = MakeImage(128, 128);

// basic:
	for(int i = 0; i < 128; ++i)
	{
		for(int j = 0; j < 128; ++j)
		{
			images[1]->data[j*4+0] = i*2;
			images[1]->data[j*4+1] = j*2;
			images[1]->data[j*4+3] = 255;
		}
	}

}

// Push a frame into the processor
EMSCRIPTEN_KEEPALIVE
void push_frame(uint8_t* data) {
    if (current_frame >= total_frames) {
        // Exceeded the expected number of frames
        return;
    }




    current_frame++;
}

// Finish processing
EMSCRIPTEN_KEEPALIVE
void finish_processing() {
    // Finalize processing if needed
    // For example, average the accumulated pixel values
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < frame_width * frame_height * 4; ++j) {
            images[i]->data[j] /= total_frames;
        }
    }
}

// Get the processed image
EMSCRIPTEN_KEEPALIVE
ImageData* get_image(int id) {
    if ((uint32_t)id > 1) return NULL;
    return images[id];
}

// Shutdown and release resources
EMSCRIPTEN_KEEPALIVE
void shutdownAndRelease() {
    for (int i = 0; i < 2; ++i) {
        if (images[i]) {
            if (images[i]->data) {
                free(images[i]->data);
                images[i]->data = NULL;
            }
            free(images[i]);
            images[i] = NULL;
        }
    }
}
