#include "create_gradient.h"
#include "create_envelopes.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

struct RGBAW
{
    float r, b, g, a, w;
};

struct GradientBuilder
{
    ImageData erosion;
    struct EnvelopeMetadata m;
    uint32_t width, height, depth;
    struct RGBAW * data;
};

GradientBuilder * g_Initialize(ImageData const* erosion, ImageData const* grad, EnvelopeMetadata const * mta)
{
    GradientBuilder * g = (GradientBuilder*)malloc(sizeof(GradientBuilder));
    g->width = grad->width;
    g->height = grad->height;
    g->depth  = grad->depth;
    g->erosion = *erosion;
    g->m = *mta;
    g->data = calloc(grad->width * grad->height * grad->depth, sizeof(struct RGBAW));
    return g;
}

void g_Free(GradientBuilder* g)
{
    free(g->data);
    free(g);
}


int min_i32(int,int);
int max_i32(int,int);
int clamp_i32(int a, int b, int c) { return max_i32(b, min_i32(a, c));  }
float min_f32(float,float);
float max_f32(float,float);
float clamp_f32(float a, float b, float c) { return max_f32(b, min_f32(a, c));  }

void g_ReverseBlend(GradientBuilder * g, float * texCoord, float * rgba);

int g_ProcessFrame(GradientBuilder * g,  ImageData const* src, int frame_id)
{
    if(!g || !src)
    {
        fprintf(stderr, "null pointer access");
        return -1;
    }

    if(src->width != g->erosion.width || src->height != g->erosion.height)
    {
        fprintf(stderr, "invalid argument");
        return -1;
    }

    float texCoord[3];
    const float toWidth = g->width / 255.0;
    const float toHeight =  g->height / 255.0;
    const float toDepth =  g->depth;

    const float time = frame_id / (float)g->m.total_frames;
    const float fadeInDuration = g->m.max_attack_frame / (float)g->m.total_frames ;
    const float fadeOutDuration = (g->m.max_release_frame -  g->m.min_release_frame) / (float)g->m.total_frames ;
    const float fadeOutStart = g->m.min_release_frame / (float)g->m.total_frames ;

	const float f_life_r  = clamp_f32(time / fadeInDuration, 0.0, 1.0);
	const float f_life_g = (time - fadeOutStart) / fadeOutDuration;
	const float f_life_b = time;

    for(int y = 0; y < g->erosion.height; ++y)
    {
        for(int x = 0; x < g->erosion.width; ++x)
        {
            int idx = (y*g->erosion.width + x) * 4;
            uint8_t const* px = &g->erosion.data[idx];
            uint8_t const* c = &src->data[idx];

            const float texEffect_r = (px[0] / 255.0);
            const float texEffect_g = (px[1] / 255.0);
            const float texEffect_b = (px[2] / 255.0);

            float fadeInFactor = clamp_f32(f_life_r - (1.0 - texEffect_r), 0.0, 1.0);
            float fadeOutFactor = clamp_f32(texEffect_g - f_life_g, 0.0, 1.0);

            const float fadeInStart = texEffect_r * fadeInDuration;
            const float fadeOutEnd  = texEffect_g * fadeOutDuration + fadeOutStart;
            const float fadeProgress = (time - fadeInStart) / (fadeOutEnd - fadeInStart);

	        fadeInFactor = clamp_f32(fadeInFactor * 15.0 * (1.0 - texEffect_b), 0.0, 1.0);
	        fadeOutFactor = clamp_f32(fadeOutFactor * 15.0 * (1.0 - texEffect_b), 0.0, 1.0);

            float alpha = 1.0; //fadeInFactor*fadeOutFactor;

            texCoord[0] = px[0] * toWidth;
            texCoord[1] = px[1] * toHeight;
            texCoord[2] = fadeProgress * toDepth;

            if(alpha == 0 || fadeProgress < 0 || fadeProgress > 1)
                continue;

            alpha = 1.0 / alpha;
            float rgba[4] = { c[0] * alpha, c[1] * alpha, c[2] * alpha, c[3] * alpha };

// idea here is a reverse bilinear blend.
            g_ReverseBlend(g, texCoord, rgba);
        }
    }

    return 0;
}


int g_Build(GradientBuilder * g, ImageData * image, int total_frames)
{
    if(!g || !image)
    {
        fprintf(stderr, "g_Build: invalid argument");
        return -1;
    }

    for(int z = 0; z < g->depth; ++z)
    {
        for(int y = 0; y < g->height; ++y)
        {
            for(int x = 0; x < g->width; ++x)
            {
                int idx = (z * g->height + y) * g->width + x;

                struct RGBAW * src = &g->data[idx];
                uint8_t * dst = & image->data[idx*4];

                if(src->w == 0)
                {
                    *(uint32_t*)dst = 0xFFFF00FF;
                }
                else
                {
                    float w = 1.0 / src->w;
                    dst[0] = (uint8_t)clamp_i32(roundf(src->r * w), 0, 255);
                    dst[1] = (uint8_t)clamp_i32(roundf(src->g * w), 0, 255);
                    dst[2] = (uint8_t)clamp_i32(roundf(src->b * w), 0, 255);
                    dst[3] = (uint8_t)clamp_i32(roundf(src->a * w), 0, 255);
                }
            }
        }
    }


    return 0;
}


static inline uint32_t g_GetIndex(const GradientBuilder *g, int x, int y, int z)
{
    return (uint32_t)z * g->width * g->height + (uint32_t)y * g->width + (uint32_t)x;
}

void g_ReverseBlend(GradientBuilder *g, float *texCoord, float *rgba)
{
    int x0 = (int)floorf(texCoord[0]);
    int y0 = (int)floorf(texCoord[1]);
    int z0 = (int)floorf(texCoord[2]);
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    int z1 = z0 + 1;

    float fx = texCoord[0] - x0;
    float fy = texCoord[1] - y0;
    float fz = texCoord[2] - z0;

    float w000 = (1.f - fx)*(1.f - fy)*(1.f - fz);
    float w001 = (1.f - fx)*(1.f - fy)*fz;
    float w010 = (1.f - fx)*fy*(1.f - fz);
    float w011 = (1.f - fx)*fy*fz;
    float w100 = fx*(1.f - fy)*(1.f - fz);
    float w101 = fx*(1.f - fy)*fz;
    float w110 = fx*fy*(1.f - fz);
    float w111 = fx*fy*fz;

    float weights[8] = { w000, w001, w010, w011, w100, w101, w110, w111 };
    int xs[8] = { x0, x0, x0, x0, x1, x1, x1, x1 };
    int ys[8] = { y0, y0, y1, y1, y0, y0, y1, y1 };
    int zs[8] = { z0, z1, z0, z1, z0, z1, z0, z1 };

    for(int i = 0; i < 8; i++)
    {
        //  skip if out of bounds
        if ((uint32_t)xs[i] >= (int)g->width  ||
            (uint32_t)ys[i] >= (int)g->height ||
            (uint32_t)zs[i] >= (int)g->depth)
        {
            continue;
        }

        float w = weights[i];
        if(w <= 0.f) continue;

        uint32_t idx = g_GetIndex(g, xs[i], ys[i], zs[i]);
        g->data[idx].r += rgba[0] * w;
        g->data[idx].g += rgba[1] * w;
        g->data[idx].b += rgba[2] * w;
        g->data[idx].a += rgba[3] * w;
        g->data[idx].w += w;
    }
}
