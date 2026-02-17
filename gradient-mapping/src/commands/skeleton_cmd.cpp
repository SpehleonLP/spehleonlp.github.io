#include "skeleton_cmd.h"
#include <string.h>
#include <stdio.h>
#include <memory>

/* =========================================================================
 * Find local maxima and minima
 * ========================================================================= */

static void find_extrema(const float* height, uint32_t W, uint32_t H,
                          uint8_t* is_max, uint8_t* is_min)
{
	memset(is_max, 0, W * H);
	memset(is_min, 0, W * H);

	for (uint32_t y = 1; y < H - 1; y++) {
		for (uint32_t x = 1; x < W - 1; x++) {
			uint32_t idx = y * W + x;
			float h = height[idx];

			int all_lower = 1;
			int all_higher = 1;

			for (int dy = -1; dy <= 1; dy++) {
				for (int dx = -1; dx <= 1; dx++) {
					if (dx == 0 && dy == 0) continue;
					float hn = height[(y + dy) * W + (x + dx)];
					if (hn >= h) all_lower = 0;
					if (hn <= h) all_higher = 0;
				}
			}

			if (all_lower) is_max[idx] = 1;
			if (all_higher) is_min[idx] = 1;
		}
	}
}

/* =========================================================================
 * Zhang-Suen morphological thinning
 *
 * Thins a binary mask to a 1-pixel-wide skeleton while preserving topology.
 * Neighborhood:
 *   P9 P2 P3
 *   P8 P1 P4
 *   P7 P6 P5
 * ========================================================================= */

static void zhang_suen_thin(uint8_t* mask, uint32_t W, uint32_t H)
{
	auto tmp = std::unique_ptr<uint8_t[]>(new uint8_t[W * H]);
	int changed = 1;

	while (changed) {
		changed = 0;

		/* Sub-iteration 1 */
		memcpy(tmp.get(), mask, W * H);
		for (uint32_t y = 1; y < H - 1; y++) {
			for (uint32_t x = 1; x < W - 1; x++) {
				uint32_t idx = y * W + x;
				if (!mask[idx]) continue;

				uint8_t p2 = mask[(y-1)*W + x];
				uint8_t p3 = mask[(y-1)*W + x+1];
				uint8_t p4 = mask[y*W + x+1];
				uint8_t p5 = mask[(y+1)*W + x+1];
				uint8_t p6 = mask[(y+1)*W + x];
				uint8_t p7 = mask[(y+1)*W + x-1];
				uint8_t p8 = mask[y*W + x-1];
				uint8_t p9 = mask[(y-1)*W + x-1];

				int B = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9;
				if (B < 2 || B > 6) continue;

				int A = 0;
				A += (!p2 && p3);
				A += (!p3 && p4);
				A += (!p4 && p5);
				A += (!p5 && p6);
				A += (!p6 && p7);
				A += (!p7 && p8);
				A += (!p8 && p9);
				A += (!p9 && p2);
				if (A != 1) continue;

				if (p2 * p4 * p6 != 0) continue;
				if (p4 * p6 * p8 != 0) continue;

				tmp[idx] = 0;
				changed = 1;
			}
		}
		memcpy(mask, tmp.get(), W * H);

		/* Sub-iteration 2 */
		memcpy(tmp.get(), mask, W * H);
		for (uint32_t y = 1; y < H - 1; y++) {
			for (uint32_t x = 1; x < W - 1; x++) {
				uint32_t idx = y * W + x;
				if (!mask[idx]) continue;

				uint8_t p2 = mask[(y-1)*W + x];
				uint8_t p3 = mask[(y-1)*W + x+1];
				uint8_t p4 = mask[y*W + x+1];
				uint8_t p5 = mask[(y+1)*W + x+1];
				uint8_t p6 = mask[(y+1)*W + x];
				uint8_t p7 = mask[(y+1)*W + x-1];
				uint8_t p8 = mask[y*W + x-1];
				uint8_t p9 = mask[(y-1)*W + x-1];

				int B = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9;
				if (B < 2 || B > 6) continue;

				int A = 0;
				A += (!p2 && p3);
				A += (!p3 && p4);
				A += (!p4 && p5);
				A += (!p5 && p6);
				A += (!p6 && p7);
				A += (!p7 && p8);
				A += (!p8 && p9);
				A += (!p9 && p2);
				if (A != 1) continue;

				if (p2 * p4 * p8 != 0) continue;
				if (p2 * p6 * p8 != 0) continue;

				tmp[idx] = 0;
				changed = 1;
			}
		}
		memcpy(mask, tmp.get(), W * H);
	}
}

/* Count 8-connected skeleton neighbors of a pixel */
int skeleton_count_neighbors(const uint8_t* skel, uint32_t W, uint32_t H,
                              uint32_t x, uint32_t y)
{
	int count = 0;
	for (int dy = -1; dy <= 1; dy++) {
		for (int dx = -1; dx <= 1; dx++) {
			if (dx == 0 && dy == 0) continue;
			uint32_t nx = x + dx;
			uint32_t ny = y + dy;
			if (nx < W && ny < H && skel[ny * W + nx])
				count++;
		}
	}
	return count;
}

/* =========================================================================
 * Prune short skeleton branches
 *
 * Iteratively remove endpoint pixels whose branch is shorter than min_length.
 * An endpoint is a skeleton pixel with exactly 1 neighbor.
 * ========================================================================= */

static void prune_skeleton(uint8_t* mask, uint32_t W, uint32_t H, int min_length)
{
	int changed = 1;
	while (changed) {
		changed = 0;
		for (uint32_t y = 1; y < H - 1; y++) {
			for (uint32_t x = 1; x < W - 1; x++) {
				uint32_t idx = y * W + x;
				if (!mask[idx]) continue;

				int nn = skeleton_count_neighbors(mask, W, H, x, y);
				if (nn != 1) continue;

				uint32_t cx = x, cy = y;
				uint32_t px = W, py = H;
				int length = 0;
				uint32_t trace[256];

				while (length < min_length) {
					uint32_t cidx = cy * W + cx;
					trace[length++] = cidx;

					uint32_t nx = W, ny = H;
					for (int dy = -1; dy <= 1; dy++) {
						for (int dx = -1; dx <= 1; dx++) {
							if (dx == 0 && dy == 0) continue;
							uint32_t nnx = cx + dx, nny = cy + dy;
							if (nnx >= W || nny >= H) continue;
							if (nnx == px && nny == py) continue;
							if (mask[nny * W + nnx]) {
								nx = nnx; ny = nny;
							}
						}
					}

					if (nx >= W) break;

					int next_nn = skeleton_count_neighbors(mask, W, H, nx, ny);
					if (next_nn >= 3) break;

					px = cx; py = cy;
					cx = nx; cy = ny;
				}

				if (length < min_length) {
					for (int i = 0; i < length; i++) {
						mask[trace[i]] = 0;
					}
					changed = 1;
				}
			}
		}
	}
}

/* =========================================================================
 * skeleton_Execute - Extract skeleton and vertices from divergence field
 * ========================================================================= */

int skeleton_Execute(SkeletonCmd* cmd)
{
	if (!cmd || !cmd->heightmap || !cmd->divergence) {
		fprintf(stderr, "[skeleton] Error: NULL input\n");
		return -1;
	}

	uint32_t W = cmd->W;
	uint32_t H = cmd->H;
	uint32_t N = W * H;

	float hi = cmd->high_threshold > 0.0f ? cmd->high_threshold : 0.25f;
	int min_branch = cmd->min_branch_length > 0 ? cmd->min_branch_length : 5;

	/* Find height extrema */
	auto is_max = std::unique_ptr<uint8_t[]>(new uint8_t[N]);
	auto is_min = std::unique_ptr<uint8_t[]>(new uint8_t[N]);
	find_extrema(cmd->heightmap, W, H, is_max.get(), is_min.get());

	int max_count = 0, min_count = 0;
	for (uint32_t i = 0; i < N; i++) {
		if (is_max[i]) max_count++;
		if (is_min[i]) min_count++;
	}
	printf("[skeleton] Found %d maxima, %d minima\n", max_count, min_count);

	/* Threshold divergence to get ridge/valley masks */
	auto ridge_hi = std::unique_ptr<uint8_t[]>(new uint8_t[N]);
	auto valley_hi = std::unique_ptr<uint8_t[]>(new uint8_t[N]);
	for (uint32_t i = 0; i < N; i++) {
		ridge_hi[i]  = (cmd->divergence[i] < -hi) ? 1 : 0;
		valley_hi[i] = (cmd->divergence[i] >  hi) ? 1 : 0;
	}

	/* Include extrema in masks (maxima -> ridge, minima -> valley) */
	for (uint32_t i = 0; i < N; i++) {
		if (is_max[i]) ridge_hi[i] = 1;
		if (is_min[i]) valley_hi[i] = 1;
	}

	int hi_ridge_px = 0, hi_valley_px = 0;
	for (uint32_t i = 0; i < N; i++) {
		if (ridge_hi[i]) hi_ridge_px++;
		if (valley_hi[i]) hi_valley_px++;
	}
	printf("[skeleton] High threshold: %d ridge px, %d valley px\n", hi_ridge_px, hi_valley_px);

	/* Thin to 1px skeleton */
	zhang_suen_thin(ridge_hi.get(), W, H);
	zhang_suen_thin(valley_hi.get(), W, H);
	prune_skeleton(ridge_hi.get(), W, H, min_branch);
	prune_skeleton(valley_hi.get(), W, H, min_branch);

	int skel_ridge = 0, skel_valley = 0;
	for (uint32_t i = 0; i < N; i++) {
		if (ridge_hi[i]) skel_ridge++;
		if (valley_hi[i]) skel_valley++;
	}
	printf("[skeleton] Skeleton: %d ridge px, %d valley px\n", skel_ridge, skel_valley);

	/* Combine into one skeleton mask */
	auto skel = std::unique_ptr<uint8_t[]>(new uint8_t[N]);
	for (uint32_t i = 0; i < N; i++)
		skel[i] = (ridge_hi[i] || valley_hi[i]) ? 1 : 0;

	/* Build vertex list: extrema first */
	for (uint32_t y = 0; y < H; y++) {
		for (uint32_t x = 0; x < W; x++) {
			uint32_t idx = y * W + x;
			if (is_max[idx]) {
				DCELVertex v{};
				v.x = (float)x;
				v.y = (float)y;
				v.height = cmd->heightmap[idx];
				v.divergence = cmd->divergence[idx];
				v.type = DCEL_VERTEX_MAXIMUM;
				v.edge = -1;
				cmd->vertices.push_back(v);
			} else if (is_min[idx]) {
				DCELVertex v{};
				v.x = (float)x;
				v.y = (float)y;
				v.height = cmd->heightmap[idx];
				v.divergence = cmd->divergence[idx];
				v.type = DCEL_VERTEX_MINIMUM;
				v.edge = -1;
				cmd->vertices.push_back(v);
			}
		}
	}

	/* Then junctions and endpoints from skeleton */
	size_t extrema_count = cmd->vertices.size();
	for (uint32_t y = 1; y < H - 1; y++) {
		for (uint32_t x = 1; x < W - 1; x++) {
			uint32_t idx = y * W + x;
			if (!skel[idx]) continue;
			/* Skip if already an extremum vertex */
			if (is_max[idx] || is_min[idx]) continue;

			int nn = skeleton_count_neighbors(skel.get(), W, H, x, y);
			if (nn != 2) {
				DCELVertexType type = (nn >= 3) ? DCEL_VERTEX_JUNCTION : DCEL_VERTEX_ENDPOINT;
				DCELVertex v{};
				v.x = (float)x;
				v.y = (float)y;
				v.height = cmd->heightmap[idx];
				v.divergence = cmd->divergence[idx];
				v.type = type;
				v.edge = -1;
				cmd->vertices.push_back(v);
			}
		}
	}

	printf("[skeleton] %zu extrema + %zu skeleton vertices = %zu total\n",
	       extrema_count, cmd->vertices.size() - extrema_count,
	       cmd->vertices.size());

	/* Build vertex_map: pixel -> vertex index, or -1 */
	cmd->vertex_map = std::unique_ptr<int32_t[]>(new int32_t[N]);
	for (uint32_t i = 0; i < N; i++)
		cmd->vertex_map[i] = -1;
	for (size_t vi = 0; vi < cmd->vertices.size(); vi++) {
		uint32_t idx = (uint32_t)cmd->vertices[vi].y * W + (uint32_t)cmd->vertices[vi].x;
		cmd->vertex_map[idx] = (int32_t)vi;
	}

	/* Move skeleton masks to output */
	cmd->skeleton = std::move(skel);
	cmd->ridge_skel = std::move(ridge_hi);
	cmd->valley_skel = std::move(valley_hi);

	return 0;
}
