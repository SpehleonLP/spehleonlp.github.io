#include "create_gradient.h"
#include "create_envelopes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct RGBAW
{
    float r, b, g, a;
    float quality; // lower indicates the info was more bit crushed.
    float weight;  // how valuable is the sample?
};


struct GradientBuilder
{
    ImageData erosion;
    struct EnvelopeMetadata m;
    uint32_t width, height, depth;
    uint32_t pass;
    struct RGBAW * data;
    struct RGBAW * old;
};

GradientBuilder * g_Initialize(ImageData const* erosion, ImageData const* grad, EnvelopeMetadata const * mta)
{
    GradientBuilder * g = (GradientBuilder*)malloc(sizeof(GradientBuilder));
    g->width = grad->width;
    g->height = grad->height;
    g->depth  = grad->depth;
    g->erosion = *erosion;
    g->pass = 0;
    g->m = *mta;
    g->data = 0L;
    g->old = 0;

    return g;
}

void g_Free(GradientBuilder* g)
{
    free(g->data);
    free(g->old);
    free(g);
}


int min_i32(int,int);
int max_i32(int,int);
int clamp_i32(int a, int b, int c) { return max_i32(b, min_i32(a, c));  }
float min_f32(float,float);
float max_f32(float,float);
float clamp_f32(float a, float b, float c) { return max_f32(b, min_f32(a, c));  }

void g_ReverseBlend(GradientBuilder * g, float * texCoord, float * rgba, float bit_crushed, float lerp_weight);
void g_ReverseBlendItr(GradientBuilder *g, float *coordIn, float *coordOut,
                       float *rgbaFinal, float bitCrushed, float lerpFactor);

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

	if(g->data == 0L)
   		g->data = calloc(g->width * g->height * g->depth, sizeof(struct RGBAW));

    float texCoord[3];
    float rampUV_in[3];
    float rampUV_out[3];

    const float toWidth = (g->width) / 255.0;
    const float toHeight =  (g->height) / 255.0;
    const float toDepth =  g->depth;

    const float time = frame_id / (float)g->m.total_frames;
    const float fadeInDuration = g->m.max_attack_frame / (float)g->m.total_frames ;
    const float fadeOutDuration = (g->m.max_release_frame -  g->m.min_release_frame) / (float)g->m.total_frames ;
    const float fadeOutStart = g->m.min_release_frame / (float)g->m.total_frames ;

	const float f_life_r = clamp_f32(time / fadeInDuration, 0.0, 1.0);
	const float f_life_g = clamp_f32((time - fadeOutStart) / fadeOutDuration, 0.0, 1.0);
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

            if(texEffect_r == 0.0 && texEffect_g == 0.0)
            	continue;

            float fadeInFactor = clamp_f32(f_life_r - (1.0 - texEffect_r), 0.0, 1.0);
            float fadeOutFactor = clamp_f32(texEffect_g - f_life_g, 0.0, 1.0);

            const float fadeInStart = (1.0 - texEffect_r) * fadeInDuration;
            const float fadeOutEnd  = texEffect_g * fadeOutDuration + fadeOutStart;
            const float fadeProgress = (time - fadeInStart) / (fadeOutEnd - fadeInStart);

            float weight =
            	clamp_f32(fadeInFactor * 15.0 * (1.0 - texEffect_b), 0.0, 1.0)*
            	clamp_f32(fadeOutFactor * 15.0 * (1.0 - texEffect_b), 0.0, 1.0);

            if(weight == 0)
            	continue;

			float alpha = 1.0 / weight;
            float rgba[4] = { c[0], c[1], c[2], c[3] * alpha };

// make texture cube.
			if(g->depth > 1)
			{
		        texCoord[0] = px[0] * toWidth;
		        texCoord[1] = px[1] * toHeight;
		        texCoord[2] = fadeProgress * toDepth;

          	  	g_ReverseBlend(g, texCoord, rgba, weight, 1.0);
            }
// make boom ramp
			else
			{
				rampUV_in[0] = (f_life_r * 0.5) * g->width;
				rampUV_in[1] = (1.0 - fadeInFactor) * g->height;
				rampUV_in[2] = 0;

				rampUV_out[0] = (f_life_g * 0.5 + 0.5) * g->width;
				rampUV_out[1] = (fadeOutFactor) * g->height;
				rampUV_out[2] = 0;

			  	 g_ReverseBlend(g, rampUV_in, rgba, weight, (1.f - f_life_b));
			  	 g_ReverseBlend(g, rampUV_out, rgba, weight, (f_life_b));
			}
		}
	}


	++(g->pass);
    return 0;
}

int g_BuildFinished(GradientBuilder * g)
{
	if(g->old) free(g->old);
   	g->old = g->data;
    g->data = 0L;

	return (g->pass == 2);
}

static inline uint32_t g_GetIndex(const GradientBuilder *g, int x, int y, int z)
{
    return (uint32_t)z * g->width * g->height + (uint32_t)y * g->width + (uint32_t)x;
}


int g_FillIn(GradientBuilder * env)
{
    if (!env || !env->data || !env->width || !env->height || !env->depth) return -1;

	printf("filling in...\n");

    const uint32_t size = env->width * env->height * env->depth;

    const int maxRadius = 128;

    for (int32_t z = 0; z < env->depth; ++z)
    {
        for (int32_t y = 0; y < env->height; ++y)
        {
            for (int32_t x = 0; x < env->width; ++x)
            {
                struct RGBAW * dst = &env->data[g_GetIndex(env, x, y, z)];
                if (dst->weight > 0.f) continue;

                // Search in expanding radius
                double sr = 0.f, sg = 0.f, sb = 0.f, sa = 0.f, sw = 0.f;
                for (int r = 1; r <= maxRadius; ++r)
                {
                    int d = r*2;

                    for (int dz = -r; dz <= r; ++dz)
                    {
                        int zz = z + dz;
                        int z2 = (z - zz)*(z - zz);

                        if (zz < 0 || zz >= (int)env->depth) continue;

                        int y_inc = (dz == -r || dz == r)? 1 : d;

                        for (int dy = -r; dy <= r; dy += y_inc)
                        {
                            int yy = y + dy;
                       		int yz_2 = (y - yy)*(y - yy) + z2;

                            if (yy < 0 || yy >= (int)env->height) continue;

                       		int x_inc = (y_inc == 1 || (dy == -r || dy == r))? 1 : d;

                            for (int dx = -r; dx <= r; dx += x_inc)
                            {
                                int xx = x + dx;
                                if (xx < 0 || xx >= (int)env->width) continue;

                                struct RGBAW * src =  &env->data[g_GetIndex(env, xx, yy, zz)];
                                if (src->quality > 0.f)
                                {
                                	float distance = (x - xx)*(x - xx) + yz_2;
                                	float w = (1.f / distance);

									if(w == w)
									{
		                                sr += src->r * w;  sg += src->g * w;
		                                sb += src->b * w;  sa += src->a * w;
		                                sw += w * src->weight;
                                    }
                                }
                            }
                        }
                    }

                    if(sw > 4.0)
                    {
             //       	break;
                    }
                }

                if (sw)
                {
                    dst->r = sr / sw;
                    dst->g = sg / sw;
                    dst->b = sb / sw;
                    dst->a = sa / sw;
                    dst->weight = 1.0; // prevent double divison later...
                    dst->quality = 0.f;
                }
            }
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

	//printf("here\n");
    for(int z = 0; z < g->depth; ++z)
    {
        for(int y = 0; y < g->height; ++y)
        {
            for(int x = 0; x < g->width; ++x)
            {
                int idx = (z * g->height + y) * g->width + x;

                struct RGBAW * src = &g->data[idx];
                uint8_t * dst = & image->data[idx*4];

                if(src->quality == 0)
                {
                    *(uint32_t*)dst = 0xFFFF00FF;
                }
                else
                {
                    float w = 1.0 / src->quality;
                    dst[0] = (uint8_t)clamp_i32(roundf(src->r *= w), 0, 255);
                    dst[1] = (uint8_t)clamp_i32(roundf(src->g *= w), 0, 255);
                    dst[2] = (uint8_t)clamp_i32(roundf(src->b *= w), 0, 255);
                    dst[3] = (uint8_t)clamp_i32(roundf(src->a *= w), 0, 255);

					src->quality *= 1 / src->weight;
					src->weight  *= w;
                }
            }
        }
    }


    return 0;
}



void g_ReverseBlend(GradientBuilder *g, float *texCoord, float *rgba, float bit_crushed, float lerp_weight)
{
    int x0 = (int)floorf(texCoord[0]);
    int y0 = (int)floorf(texCoord[1]);
    int z0 = (int)floorf(texCoord[2]);
    int x1 = min_i32(x0 + 1, g->width-1);
    int y1 = min_i32(y0 + 1, g->height-1);
    int z1 = min_i32(z0 + 1, g->depth-1);

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
        if ((uint32_t)xs[i] >= (uint32_t)g->width  ||
            (uint32_t)ys[i] >= (uint32_t)g->height ||
            (uint32_t)zs[i] >= (uint32_t)g->depth)
        {
            continue;
        }

        float w = weights[i] * bit_crushed * lerp_weight;
        if(w <= 0.f) continue;

        uint32_t idx = g_GetIndex(g, xs[i], ys[i], zs[i]);

		g->data[idx].r += rgba[0] * w;
		g->data[idx].g += rgba[1] * w;
		g->data[idx].b += rgba[2] * w;
		g->data[idx].a += rgba[3] * w;
		g->data[idx].weight += weights[i];
		g->data[idx].quality += w;
    }
}

static inline void getBilinearColor(GradientBuilder *g, float *coord, float outColor[4])
{
    // Quick read from old iteration's data (g->old==1). This assumes g->data holds
    // the normalized/old color. For brevity, no bounds checks or weighting logic shown:
    int x0 = (int)floorf(coord[0]);
    int y0 = (int)floorf(coord[1]);
    int z0 = (int)floorf(coord[2]);
    float fx = coord[0] - x0;
    float fy = coord[1] - y0;
    float fz = coord[2] - z0;

    // Sample corners (just nearest in this example):
    uint32_t i000 = g_GetIndex(g, x0,   y0,   z0  );
    uint32_t i100 = g_GetIndex(g, x0+1, y0,   z0  );
    uint32_t i010 = g_GetIndex(g, x0,   y0+1, z0  );
    uint32_t i001 = g_GetIndex(g, x0,   y0,   z0+1);

    // Simple bilinear blend of four corners:
    // (Expanding to all 8 corners is straightforward.)
    float c000[4] = {
        g->data[i000].r, g->data[i000].g,
        g->data[i000].b, g->data[i000].a
    };
    float c100[4] = {
        g->data[i100].r, g->data[i100].g,
        g->data[i100].b, g->data[i100].a
    };
    float c010[4] = {
        g->data[i010].r, g->data[i010].g,
        g->data[i010].b, g->data[i010].a
    };
    float c001[4] = {
        g->data[i001].r, g->data[i001].g,
        g->data[i001].b, g->data[i001].a
    };

    // Interpolate:
    for(int c = 0; c < 4; c++)
    {
        float c00 = c000[c]*(1.f - fx) + c100[c]*fx;
        float c01 = c001[c]*(1.f - fx) + c100[c]*fx;  // or corner at (x+1,y,z+1), etc.
        float c0  = c00*(1.f - fz) + c01*fz;          // z-blend
        float c1  = c010[c];                          // ignoring real y-blend for brevity
        outColor[c] = c0*(1.f - fy) + c1*fy;          // y-blend
    }
}

void g_ReverseBlendItr(GradientBuilder *g, float *coordIn, float *coordOut,
                       float *rgbaFinal, float bitCrushed, float lerpFactor)
{
    // Read old in/out colors from last iteration
    float oldIn[4], oldOut[4];
    getBilinearColor(g, coordIn,  oldIn);
    getBilinearColor(g, coordOut, oldOut);

    // Compute old final
    float oldFinal[4];
    for(int i=0; i<4; i++)
        oldFinal[i] = oldIn[i]*(1.f - lerpFactor) + oldOut[i]*lerpFactor;

    // Compare to actual final
    float diff[4];
    for(int i=0; i<4; i++)
        diff[i] = rgbaFinal[i] - oldFinal[i];

    // Push half the correction back onto the in/out coords (for example).
    // Weighted by old "quality" or “confidence” can go here if desired.
    float halfDiffIn[4], halfDiffOut[4];
    for(int i=0; i<4; i++)
    {
        halfDiffIn[i]  = diff[i]*0.5f;
        halfDiffOut[i] = diff[i]*0.5f;
    }

    // Use your existing g_ReverseBlend to accumulate these corrections:
    // (bitCrushed and weighting are up to you; tweak the distribution
    // between in/out as needed.)
    g_ReverseBlend(g, coordIn,  halfDiffIn,  bitCrushed, (1.f - lerpFactor));
    g_ReverseBlend(g, coordOut, halfDiffOut, bitCrushed, lerpFactor);
}
