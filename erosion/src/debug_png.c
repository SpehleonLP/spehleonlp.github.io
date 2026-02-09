#include "debug_png.h"
#if DEBUG_IMG_OUT == 0
typedef int nothing; // make compiler shut up about empty file;
#else
#include <stdlib.h>
#include <math.h>
#include <float.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

int clamp_i32(int, int, int);

static inline uint8_t float_to_byte(float v) {
	int i = (int)(v * 255.0f + 0.5f);
	return (uint8_t)(i < 0 ? 0 : (i > 255 ? 255 : i));
}

int png_ExportFloat(PngFloatCmd* cmd) {
	if (!cmd || !cmd->path || !cmd->data || cmd->width == 0 || cmd->height == 0)
		return -1;

	uint32_t N = cmd->width * cmd->height;
	float min_val = cmd->min_val;
	float max_val = cmd->max_val;

	if (cmd->auto_range) {
		min_val = FLT_MAX;
		max_val = -FLT_MAX;
		for (uint32_t i = 0; i < N; i++) {
			if (cmd->data[i] < min_val) min_val = cmd->data[i];
			if (cmd->data[i] > max_val) max_val = cmd->data[i];
		}
	}

	float range = max_val - min_val;
	if (range < 1e-6f) range = 1.0f;

	uint8_t* pixels = malloc(N);
	if (!pixels) return -2;

	for (uint32_t i = 0; i < N; i++) {
		float normalized = (cmd->data[i] - min_val) / range;
		if (normalized < 0.0f) normalized = 0.0f;
		if (normalized > 1.0f) normalized = 1.0f;
		pixels[i] = (uint8_t)(normalized * 255.0f);
	}

	int result = stbi_write_png(cmd->path, cmd->width, cmd->height, 1, pixels, cmd->width);
	free(pixels);

	return result ? 0 : -3;
}

int png_ExportVec2(PngVec2Cmd* cmd) {
	if (!cmd || !cmd->path || !cmd->data || cmd->width == 0 || cmd->height == 0)
		return -1;

	uint32_t N = cmd->width * cmd->height;
	float scale = cmd->scale > 0 ? cmd->scale : 1.0f;
	float z_bias = cmd->z_bias > 0 ? cmd->z_bias : 0.005f;

	uint8_t* pixels = malloc(N * 3);
	if (!pixels) return -2;

	for (uint32_t i = 0; i < N; i++) {
		float nx = cmd->data[i].x / scale * 0.5f;
		float ny = cmd->data[i].y / scale * 0.5f;
		float nz = z_bias;

		// Normalize
		float mag = sqrtf(nx * nx + ny * ny + nz * nz);
		if (mag > 1e-6f) {
			float inv_mag = 1.0f / mag;
			nx *= inv_mag;
			ny *= inv_mag;
			nz *= inv_mag;
		}

		// Map [-1,1] to [0,1]
		nx = nx * 0.5f + 0.5f;
		ny = ny * 0.5f + 0.5f;
		nz = nz * 0.5f + 0.5f;

		pixels[i * 3 + 0] = float_to_byte(nx);
		pixels[i * 3 + 1] = float_to_byte(ny);
		pixels[i * 3 + 2] = float_to_byte(nz);
	}

	int result = stbi_write_png(cmd->path, cmd->width, cmd->height, 3, pixels, cmd->width * 3);
	free(pixels);

	return result ? 0 : -3;
}

int png_ExportVec3(PngVec3Cmd* cmd) {
	if (!cmd || !cmd->path || !cmd->data || cmd->width == 0 || cmd->height == 0)
		return -1;

	uint32_t N = cmd->width * cmd->height;

	uint8_t* pixels = malloc(N * 3);
	if (!pixels) return -2;

	for (uint32_t i = 0; i < N; i++) {
		// Assume normalized in [-1,1], map to [0,1]
		float nx = cmd->data[i].x * 0.5f + 0.5f;
		float ny = cmd->data[i].y * 0.5f + 0.5f;
		float nz = cmd->data[i].z * 0.5f + 0.5f;

		pixels[i * 3 + 0] = float_to_byte(nx);
		pixels[i * 3 + 1] = float_to_byte(ny);
		pixels[i * 3 + 2] = float_to_byte(nz);
	}

	int result = stbi_write_png(cmd->path, cmd->width, cmd->height, 3, pixels, cmd->width * 3);
	free(pixels);

	return result ? 0 : -3;
}

int png_ExportInterleaved(PngInterleavedCmd* cmd) {
	if (!cmd || !cmd->path || !cmd->data || cmd->width == 0 || cmd->height == 0)
		return -1;

	uint32_t N = cmd->width * cmd->height;
	int ch = cmd->channel;
	int stride = cmd->stride > 0 ? cmd->stride : 1;

	// Extract channel
	float* extracted = malloc(N * sizeof(float));
	if (!extracted) return -2;

	float min_val = cmd->min_val;
	float max_val = cmd->max_val;

	if (cmd->auto_range) {
		min_val = FLT_MAX;
		max_val = -FLT_MAX;
	}

	for (uint32_t i = 0; i < N; i++) {
		float v = cmd->data[i * stride + ch];
		extracted[i] = v;
		if (cmd->auto_range) {
			if (v < min_val) min_val = v;
			if (v > max_val) max_val = v;
		}
	}

	// Export as grayscale
	PngFloatCmd float_cmd = {
		.path = cmd->path,
		.data = extracted,
		.width = cmd->width,
		.height = cmd->height,
		.min_val = min_val,
		.max_val = max_val,
		.auto_range = 0
	};

	int result = png_ExportFloat(&float_cmd);
	free(extracted);

	return result;
}

// Helper to render a tile into an RGB buffer
static void render_tile(
	uint8_t* dest, int dest_stride,
	const PngGridTile* tile,
	uint32_t w, uint32_t h)
{
	switch (tile->type) {
	case PNG_TILE_GRAYSCALE:
		{
			const float* data = (const float*)tile->data;
			if (!data) break;

			// Find min/max
			float min_val = FLT_MAX, max_val = -FLT_MAX;
			for (uint32_t i = 0; i < w * h; i++) {
				if (data[i] < min_val) min_val = data[i];
				if (data[i] > max_val) max_val = data[i];
			}
			float range = max_val - min_val;
			if (range < 1e-6f) range = 1.0f;

			for (uint32_t y = 0; y < h; y++) {
				for (uint32_t x = 0; x < w; x++) {
					float v = (data[y * w + x] - min_val) / range;
					if (v < 0) v = 0;
					if (v > 1) v = 1;
					uint8_t gray = (uint8_t)(v * 255.0f);
					uint8_t* p = dest + y * dest_stride + x * 3;
					p[0] = gray;
					p[1] = gray;
					p[2] = gray;
				}
			}
		}
		break;

	case PNG_TILE_VEC2:
		{
			const vec2* data = (const vec2*)tile->data;
			if (!data) break;

			for (uint32_t y = 0; y < h; y++) {
				for (uint32_t x = 0; x < w; x++) {
					uint32_t i = y * w + x;
					float nx = data[i].x * 0.5f;
					float ny = data[i].y * 0.5f;
					float nz = 0.005f;

					float mag = sqrtf(nx * nx + ny * ny + nz * nz);
					if (mag > 1e-6f) {
						float inv = 1.0f / mag;
						nx *= inv;
						ny *= inv;
						nz *= inv;
					}

					uint8_t* p = dest + y * dest_stride + x * 3;
					p[0] = float_to_byte(nx * 0.5f + 0.5f);
					p[1] = float_to_byte(ny * 0.5f + 0.5f);
					p[2] = float_to_byte(nz * 0.5f + 0.5f);
				}
			}
		}
		break;

	case PNG_TILE_VEC3:
		{
			const vec3* data = (const vec3*)tile->data;
			if (!data) break;

			for (uint32_t y = 0; y < h; y++) {
				for (uint32_t x = 0; x < w; x++) {
					uint32_t i = y * w + x;
					uint8_t* p = dest + y * dest_stride + x * 3;
					p[0] = float_to_byte(data[i].x * 0.5f + 0.5f);
					p[1] = float_to_byte(data[i].y * 0.5f + 0.5f);
					p[2] = float_to_byte(data[i].z * 0.5f + 0.5f);
				}
			}
		}
		break;

	case PNG_TILE_INTERLEAVED:
		{
			const float* data = (const float*)tile->data;
			if (!data) break;

			int ch = tile->channel;
			int stride = tile->stride > 0 ? tile->stride : 1;

			// Find min/max for this channel
			float min_val = FLT_MAX, max_val = -FLT_MAX;
			for (uint32_t i = 0; i < w * h; i++) {
				float v = data[i * stride + ch];
				if (v < min_val) min_val = v;
				if (v > max_val) max_val = v;
			}
			float range = max_val - min_val;
			if (range < 1e-6f) range = 1.0f;

			for (uint32_t y = 0; y < h; y++) {
				for (uint32_t x = 0; x < w; x++) {
					float v = data[(y * w + x) * stride + ch];
					v = (v - min_val) / range;
					if (v < 0) v = 0;
					if (v > 1) v = 1;
					uint8_t gray = (uint8_t)(v * 255.0f);
					uint8_t* p = dest + y * dest_stride + x * 3;
					p[0] = gray;
					p[1] = gray;
					p[2] = gray;
				}
			}
		}
		break;
	}
}

int png_ExportGrid(PngGridCmd* cmd) {
	if (!cmd || !cmd->path || !cmd->tiles)
		return -1;
	if (cmd->tile_width == 0 || cmd->tile_height == 0)
		return -1;
	if (cmd->cols == 0 || cmd->rows == 0)
		return -1;

	uint32_t tw = cmd->tile_width;
	uint32_t th = cmd->tile_height;
	uint32_t grid_w = tw * cmd->cols;
	uint32_t grid_h = th * cmd->rows;
	int stride = grid_w * 3;

	uint8_t* pixels = calloc(grid_w * grid_h * 3, 1);
	if (!pixels) return -2;

	for (uint32_t row = 0; row < cmd->rows; row++) {
		for (uint32_t col = 0; col < cmd->cols; col++) {
			const PngGridTile* tile = &cmd->tiles[row * cmd->cols + col];
			uint8_t* dest = pixels + row * th * stride + col * tw * 3;
			render_tile(dest, stride, tile, tw, th);
		}
	}

	int result = stbi_write_png(cmd->path, grid_w, grid_h, 3, pixels, stride);
	free(pixels);

	return result ? 0 : -3;
}

#endif
