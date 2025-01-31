#include <stdint.h>

// Define the ImageData structure
typedef struct ImageData {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    // ensure pointer has 8 byte alignment.
	uint32_t pad00;
    uint8_t* data; // GL_RGBA_8888
} ImageData;

union Color
{
	struct
	{
		uint8_t r;
		uint8_t g;
		uint8_t b;
		uint8_t a;
	};

	uint32_t c;
};

ImageData * MakeImage(int width, int height, int depth);

typedef struct EnvelopeMetadata
{
	int total_frames;
	int min_attack_frame;
	int max_attack_frame;
	int min_release_frame;
	int max_release_frame;
	union Color key;
} EnvelopeMetadata;

typedef struct EnvelopeBuilder EnvelopeBuilder;

EnvelopeBuilder * e_Initialize(int width, int height);
int e_ProcessFrame(EnvelopeBuilder * env, ImageData const* src, int frame_id);
int e_Build(EnvelopeBuilder * env, ImageData * dst, EnvelopeMetadata * out, int total_frames);
void e_Free(EnvelopeBuilder*);
