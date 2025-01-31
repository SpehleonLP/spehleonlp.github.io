#include <stdint.h>

typedef struct ImageData ImageData;
typedef struct GradientBuilder GradientBuilder;
typedef struct EnvelopeMetadata EnvelopeMetadata;

GradientBuilder * g_Initialize(ImageData const* erosion, ImageData const* grad, EnvelopeMetadata const * mta);
int g_ProcessFrame(GradientBuilder * env,  ImageData const* src, int frame_id);
int g_Build(GradientBuilder * env, ImageData * dst, int total_frames);
int g_BuildFinished(GradientBuilder * env);
int g_FillIn(GradientBuilder * env);

void g_Free(GradientBuilder*);
