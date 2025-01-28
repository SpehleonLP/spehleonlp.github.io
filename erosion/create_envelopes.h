#include <stdint.h>

// Define the ImageData structure
typedef struct ImageData {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
	uint32_t pad00;
    uint8_t* data; // GL_RGBA_8888
} ImageData;

ImageData * MakeImage(int width, int height, int depth);

typedef struct EnvelopeMetadata
{
	int total_frames;
	int min_attack_frame;
	int max_attack_frame;
	int min_release_frame;
	int max_release_frame;
} EnvelopeMetadata;

typedef struct EnvelopeBuilder EnvelopeBuilder;

EnvelopeBuilder * e_Initialize(int width, int height);
int e_ProcessFrame(EnvelopeBuilder * env, ImageData const* src, int frame_id);
int e_Build(EnvelopeBuilder * env, ImageData * dst, EnvelopeMetadata * out, int total_frames);
void e_Free(EnvelopeBuilder*);
