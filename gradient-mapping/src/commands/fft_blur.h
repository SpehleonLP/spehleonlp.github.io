#ifndef FFT_BLUR_H
#define FFT_BLUR_H

#include <stdint.h>

typedef struct ResizingImage
{
	uint32_t width;
	uint32_t height;

	// if this is empty use original
	// (one of them should be populated)
	float  *data;

    // Original data tracking:
    // 255 = interpolated pixel (spanned multiple colors)
    // 0-254 = original uint8_t value at this location
    // if this is empty use floor(data_channel)
    uint8_t* original;
} ResizingImage;

typedef struct {
    int width;      // FFT buffer width (power of 2)
    int height;     // FFT buffer height (power of 2)

    float* real;    // Real part of complex buffer
    float* imag;    // Imaginary part of complex buffer

    // Original data tracking for this channel:
    // 255 = interpolated pixel (spanned multiple colors)
    // 0-254 = original uint8_t value at this location
    uint8_t* original;
} FFTBlurContext;

// Round up to next power of 2
int next_pow2(int n);

// Resize/upsample image to power-of-2 dimensions
// dst dimensions should be set before calling; dst->data will be allocated
// Returns 1 on success, 0 on failure
int fft_ResizeImage(ResizingImage* dst, ResizingImage* src);

// Initialize FFT context - allocates buffers only
// ctx: stack-allocated context to initialize
// width, height: dimensions (should be power of 2 for FFT)
// Returns 0 on success, -1 on failure
int fft_Initialize(FFTBlurContext* ctx, int width, int height);

// Load a channel from image into FFT buffers
// ctx: initialized context
// image: source image
// stride: number of channels (e.g., 4 for RGBA)
// channel: which channel to load (0-3)
void fft_LoadChannel(FFTBlurContext* ctx, ResizingImage* image, int stride, int channel);

// Load raw float data into FFT buffers (single channel)
// ctx: initialized context
// data: source float array (width * height)
// width, height: dimensions of source data
void fft_LoadFloatData(FFTBlurContext* ctx, const float* data, int width, int height);

// Apply low-pass filter in frequency domain
// cutoff_ratio: 0.0-1.0, fraction of frequencies to keep
void fft_LowPassFilter(FFTBlurContext* ctx, float cutoff_ratio, int flip_around);

// Apply high-pass filter in frequency domain
// cutoff_ratio: 0.0-1.0, fraction of frequencies to remove (keeps above cutoff)
void fft_HighPassFilter(FFTBlurContext* ctx, float cutoff_ratio);

// Copy results back to image channel
// image: destination image
// ctx: processed context
// stride: number of channels
// channel: which channel to write to
void fft_CopyBackToImage(ResizingImage* image, FFTBlurContext* ctx, int stride, int channel);

// Copy results to raw float array (single channel)
// data: destination float array (ctx->width * ctx->height)
void fft_CopyToFloatData(float* data, FFTBlurContext* ctx);

// Free context internal buffers (not the context itself if stack-allocated)
void fft_Free(FFTBlurContext* ctx);

// Raw 2D FFT (in-place on separate real/imag arrays)
// inverse: 0 = forward FFT, 1 = inverse FFT
// amplify: scale factor applied after transform
void fft_2d(float* real, float* imag, int width, int height, int inverse, float amplify);

#endif // FFT_BLUR_H
