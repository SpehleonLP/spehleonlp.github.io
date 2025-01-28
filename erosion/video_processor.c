#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten.h>
#include <math.h>
#include <stdio.h>
#include "create_envelopes.h"
#include "create_gradient.h"



// Global variables to store images
static ImageData* images[2] = { NULL, NULL };
static int total_frames = 0;
static int current_frame = 0;
static int frame_width = 0;
static int frame_height = 0;
static int pass = 0;
static EnvelopeBuilder * envelopeBuilder = 0;
static GradientBuilder * gradientBuilder = 0;

int max_i32(int, int);

ImageData * MakeImage(int width, int height, int depth)
{
    if (width <= 0 || height <= 0 || depth <= 0) {
        // Handle invalid dimensions
        return NULL;
    }

    ImageData * r = (ImageData*)malloc(sizeof(ImageData));
    if (!r) {
        // Handle malloc failure
        return NULL;
    }

    r->width = max_i32(1, width);
    r->height = max_i32(1, height);
    r->depth = max_i32(1, depth);

    r->data = (uint8_t*)calloc(r->width * r->height * r->depth, 4);
    if (!r->data) {
        // Handle calloc failure
        free(r);
        return NULL;
    }


    return r;
}

void initUVCube(ImageData* image) {
    if (!image || !image->data) return;

    // Ensure we're working with RGBA format (4 bytes per pixel)
    const uint32_t bytesPerPixel = 4;
    const uint32_t stride = image->width * bytesPerPixel;
    const uint32_t area = image->height * stride;

    for (uint32_t z = 0; z < image->depth; z++)
    {
		float w = (float)z / (image->depth);
		uint8_t b = (uint8_t)roundf(w * 255.0f);

		for (uint32_t y = 0; y < image->height; y++)
		{
		   float v = (float)y / (image->height);
		   uint8_t g = (uint8_t)roundf(v * 255.0f);

		   for (uint32_t x = 0; x < image->width; x++)
		   {
		        // Calculate normalized UV coordinates (0.0 to 1.0)
		        float u = (float)x / (image->width);
		        uint8_t r = (uint8_t)roundf(u * 255.0f);

		        // Calculate pixel offset in the data array
		        uint32_t offset = z * area + y * stride + x * bytesPerPixel;

		        // Store UV values in red and green channels (scaled to 0-255)
		        image->data[offset + 0] = r;
		        image->data[offset + 1] = g;
		        image->data[offset + 2] = b;
		        image->data[offset + 3] = 255;
		    }
		}
    }
}


// Initialize the processor
EMSCRIPTEN_KEEPALIVE
void initialize(int width, int height) {
	if(envelopeBuilder != 0L)
		e_Free(envelopeBuilder);

	envelopeBuilder = e_Initialize(width, height);

    frame_width = width;
    frame_height = height;
    current_frame = 0;
    pass = 0;

	images[0] = MakeImage(width, height, 1);
	images[1] = MakeImage(64, 32, 32);

	initUVCube(images[0]);
	initUVCube(images[1]);
}

EMSCRIPTEN_KEEPALIVE
int finish_pass()
{
	++pass;
    if(pass == 1)
    {
        total_frames = current_frame;

        EnvelopeMetadata mta;
    	e_Build(envelopeBuilder, images[0], &mta, current_frame);
    	e_Free(envelopeBuilder);
    	envelopeBuilder = 0;
		current_frame = 0;

	    gradientBuilder = g_Initialize(images[0], images[1], &mta);

    	return 1;
    }

    if(pass == 2)
    {
    	g_Build(gradientBuilder, images[1], current_frame);
    	g_Free(gradientBuilder);

    	gradientBuilder = 0;
		current_frame = 0;
    }

	return 0;

}

// Push a frame into the processor
EMSCRIPTEN_KEEPALIVE
void push_frame(uint8_t* data) {
	ImageData img;
	img.width = frame_width;
	img.height = frame_height;
    img.depth = 1;
	img.data = data;

    if(pass == 0)
    {
      	e_ProcessFrame(envelopeBuilder, &img, current_frame);
	}
    if(pass == 1)
    {
      	g_ProcessFrame(gradientBuilder, &img, current_frame);
	}

    current_frame++;
}

// Finish processing
EMSCRIPTEN_KEEPALIVE
void finish_processing() {
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
    if(gradientBuilder)
        g_Free(gradientBuilder);
    if(envelopeBuilder)
        e_Free(envelopeBuilder);

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
