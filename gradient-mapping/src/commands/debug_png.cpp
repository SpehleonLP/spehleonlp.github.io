#include "debug_png.h"
#if DEBUG_IMG_OUT == 0
typedef int nothing; // make compiler shut up about empty file;
#else
#include <stdlib.h>
#include <math.h>
#include <float.h>
#include <memory>
#include "../utility.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../image_memo/stb_image_write.h"


static inline uint8_t float_to_byte(float v) {
	return clamp_i32(v * 255 + 0.5, 0, 255);
}

static inline uint8_t float_to_ubyte(float v) 
{
	v = (v + 1.0) / 2.0;
	return clamp_i32(v * 255 + 0.5, 0, 255);
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

	auto pixels = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[N]);
	if (!pixels) return -2;

	for (uint32_t i = 0; i < N; i++) {
		float normalized = (cmd->data[i] - min_val) / range;
		if (normalized < 0.0f) normalized = 0.0f;
		if (normalized > 1.0f) normalized = 1.0f;
		pixels[i] = (uint8_t)(normalized * 255.0f);
	}

	int result = stbi_write_png(cmd->path, cmd->width, cmd->height, 1, pixels.get(), cmd->width);

	return result ? 0 : -3;
}

int png_ExportVec2(PngVec2Cmd* cmd) {
	if (!cmd || !cmd->path || !cmd->data || cmd->width == 0 || cmd->height == 0)
		return -1;

	uint32_t N = cmd->width * cmd->height;
	float scale = cmd->scale > 0 ? cmd->scale : 1.0f;
	float z_bias = cmd->z_bias > 0 ? cmd->z_bias : 0.005f;

	auto pixels = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[N * 3]);
	if (!pixels) return -2;

	for (uint32_t i = 0; i < N; i++) {
		vec3 n(cmd->data[i].x / scale * 0.5f,
		       cmd->data[i].y / scale * 0.5f,
		       z_bias);

		// Normalize
		float mag = glm::length(n);
		if (mag > 1e-6f) {
			n /= mag;
		}

		// Map [-1,1] to [0,1]
		n = n * 0.5f + 0.5f;

		pixels[i * 3 + 0] = float_to_byte(n.x);
		pixels[i * 3 + 1] = float_to_byte(n.y);
		pixels[i * 3 + 2] = float_to_byte(n.z);
	}

	int result = stbi_write_png(cmd->path, cmd->width, cmd->height, 3, pixels.get(), cmd->width * 3);

	return result ? 0 : -3;
}

int png_ExportVec3(PngVec3Cmd* cmd) {
	if (!cmd || !cmd->path || !cmd->data || cmd->width == 0 || cmd->height == 0)
		return -1;

	uint32_t N = cmd->width * cmd->height;

	auto pixels = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[N * 3]);
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

	int result = stbi_write_png(cmd->path, cmd->width, cmd->height, 3, pixels.get(), cmd->width * 3);

	return result ? 0 : -3;
}

int png_ExportInterleaved(PngInterleavedCmd* cmd) {
	if (!cmd || !cmd->path || !cmd->data || cmd->width == 0 || cmd->height == 0)
		return -1;

	uint32_t N = cmd->width * cmd->height;
	int ch = cmd->channel;
	int stride = cmd->stride > 0 ? cmd->stride : 1;

	// Extract channel
	auto extracted = std::unique_ptr<float[]>(new (std::nothrow) float[N]);
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
	PngFloatCmd float_cmd{};
	float_cmd.path = cmd->path;
	float_cmd.data = extracted.get();
	float_cmd.width = cmd->width;
	float_cmd.height = cmd->height;
	float_cmd.min_val = min_val;
	float_cmd.max_val = max_val;
	float_cmd.auto_range = 0;

	int result = png_ExportFloat(&float_cmd);

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

			float min_val, max_val;
			if (tile->scale > 0.0f) {
				// Check if data has negatives to decide signed vs unsigned range
				int has_neg = 0;
				for (uint32_t i = 0; i < w * h && !has_neg; i++)
					has_neg = data[i] < 0.0f;
				min_val = has_neg ? -tile->scale : 0.0f;
				max_val = tile->scale;
			} else {
				// Auto-range
				min_val = FLT_MAX; max_val = -FLT_MAX;
				for (uint32_t i = 0; i < w * h; i++) {
					if (data[i] < min_val) min_val = data[i];
					if (data[i] > max_val) max_val = data[i];
				}
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
			float s = tile->scale > 0.0f ? tile->scale : 1.0f;

			for (uint32_t y = 0; y < h; y++) {
				for (uint32_t x = 0; x < w; x++) {
					uint32_t i = y * w + x;
					vec3 n(data[i].x,
					       data[i].y,
					       sqrtf(max_f32(0, 1.0f - data[i].x * data[i].x - data[i].y * data[i].y)) * s);
					float len = glm::length(n);
					if (len > 1e-8f) { n /= len; }

					uint8_t* p = dest + y * dest_stride + x * 3;
					p[0] = float_to_ubyte(n.x);
					p[1] = float_to_ubyte(n.y);
					p[2] = float_to_ubyte(n.z);
				}
			}
		}
		break;

	case PNG_TILE_VEC3:
		{
			const vec3* data = (const vec3*)tile->data;
			if (!data) break;
			float s = tile->scale > 0.0f ? tile->scale : 1.0f;

			for (uint32_t y = 0; y < h; y++) {
				for (uint32_t x = 0; x < w; x++) {
					uint32_t i = y * w + x;
					vec3 n = scale_normal(data[i], s);
					
					uint8_t* p = dest + y * dest_stride + x * 3;
					p[0] = float_to_ubyte(n.x);
					p[1] = float_to_ubyte(n.y);
					p[2] = float_to_ubyte(n.z);
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

			float min_val, max_val;
			if (tile->scale > 0.0f) {
				int has_neg = 0;
				for (uint32_t i = 0; i < w * h && !has_neg; i++)
					has_neg = data[i * stride + ch] < 0.0f;
				min_val = has_neg ? -tile->scale : 0.0f;
				max_val = tile->scale;
			} else {
				min_val = FLT_MAX; max_val = -FLT_MAX;
				for (uint32_t i = 0; i < w * h; i++) {
					float v = data[i * stride + ch];
					if (v < min_val) min_val = v;
					if (v > max_val) max_val = v;
				}
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

	auto pixels = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[grid_w * grid_h * 3]());
	if (!pixels) return -2;

	for (uint32_t row = 0; row < cmd->rows; row++) {
		for (uint32_t col = 0; col < cmd->cols; col++) {
			const PngGridTile* tile = &cmd->tiles[row * cmd->cols + col];
			uint8_t* dest = pixels.get() + row * th * stride + col * tw * 3;
			render_tile(dest, stride, tile, tw, th);
		}
	}

	int result = stbi_write_png(cmd->path, grid_w, grid_h, 3, pixels.get(), stride);

	return result ? 0 : -3;
}

#endif
