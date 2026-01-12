#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten.h>
#include <math.h>
#include <stdio.h>
#include "create_envelopes.h"
#include "create_gradient.h"

struct Video
{
	uint32_t width;
	uint32_t height;
	uint32_t no_frames;
	uint32_t allocated;
	uint8_t * frames[];
};

struct Metadata
{
	float fadeInDuration;
	float fadeOutDuration;
};

// Global variables to store images
static ImageData* images[2] = { NULL, NULL };
static EnvelopeBuilder * envelopeBuilder = 0;
static GradientBuilder * gradientBuilder = 0;
static struct Video * video = 0;
static struct Metadata metadata;


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

EMSCRIPTEN_KEEPALIVE
struct Metadata * GetMetadata()
{
	return &metadata;
}

// Initialize the processor
EMSCRIPTEN_KEEPALIVE
void initialize(int width, int height) {
	if(envelopeBuilder != 0L)
		e_Free(envelopeBuilder);

	envelopeBuilder = e_Initialize(width, height);
	video = calloc(sizeof(struct Video) + sizeof(uint8_t*)*8, 1);
	video->width = width;
	video->height = height;
	video->allocated = 8;

	printf("initialized with width of %d and height of %d\n", width, height);

	images[0] = MakeImage(width, height, 1);
	images[1] = MakeImage(128, 128, 1);

	initUVCube(images[0]);
	initUVCube(images[1]);
}

EMSCRIPTEN_KEEPALIVE
int finishPushingFrames()
{
	fprintf(stdout, "done pushing frames\n");

    if(envelopeBuilder)
    {
        EnvelopeMetadata mta;

		fprintf(stdout, "building envelope\n");
    	e_Build(envelopeBuilder, images[0], &mta, video->no_frames);
    	e_Free(envelopeBuilder);
    	envelopeBuilder = 0;

		// Calculate durations from MinMax ranges
		int attack_duration = mta.end_attack_frame.max - mta.start_attack_frame.min;
		int release_duration = mta.end_release_frame.max - mta.start_release_frame.min;

		metadata.fadeInDuration = attack_duration / (float)mta.total_frames;
		metadata.fadeOutDuration = release_duration / (float)mta.total_frames;

	    gradientBuilder = g_Initialize(images[0], images[1], &mta);

    	return 1;
    }

	return -1;
}

// Push a frame into the processor
EMSCRIPTEN_KEEPALIVE
void push_frame(uint8_t* data, uint32_t byteLength) {
	ImageData img;
	img.width = video->width;
	img.height = video->height;
    img.depth = 1;
	img.data = data;


	uint32_t expectedLength = img.width*img.height*img.depth*4;
	if(byteLength != expectedLength)
	{
		fprintf(stderr, "got buffer of size %d expected %d\n", byteLength, expectedLength);
	}

	if(video->no_frames == video->allocated)
	{
		fprintf(stdout, "realloced\n");

		uint32_t size = video->allocated * 2;
		video = realloc(video, sizeof(struct Video) + sizeof(uint8_t*)*size);
		video->allocated = size;
	}

	video->frames[video->no_frames++] = data;

    if(envelopeBuilder)
    {
      	e_ProcessFrame(envelopeBuilder, &img, video->no_frames);
    }
}

EMSCRIPTEN_KEEPALIVE
void computeGradient()
{
    if(!gradientBuilder)
    	return;

	ImageData img;
	img.width = video->width;
	img.height = video->height;
	img.depth = 1;

	if(g_BuildFinished(gradientBuilder) == 0)
	{
		fprintf(stdout, "computing gradient\n");

		for(uint32_t i = 0u; i < video->no_frames; ++i)
		{
			img.data = video->frames[i];
			g_ProcessFrame(gradientBuilder, &img, i);
		}
	}
	else
	{
	//	g_FillIn(gradientBuilder);
	}

	g_Build(gradientBuilder, images[1], video->no_frames);
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

	if(video)
	{
		for(int i = 0; i < video->no_frames; ++i)
		{
			free(video->frames[i]);
		}

		free(video);
		video = 0L;
	}

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
