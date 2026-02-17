
#include "ridge_mesh_cmd.h"
#include "divergence_field_cmd.h"
#include "skeleton_cmd.h"
#include "skeleton_graph_cmd.h"
#include "dcel_build_cmd.h"
#include "mesh_simplify_cmd.h"
#include "mesh_decimate_cmd.h"
#include "dcel_features_cmd.h"
#include "ridge_energy_cmd.h"
#include "edge_dijkstra_cmd.h"
#include "debug_png.h"
#include "../debug_output.h"
#include <algorithm>
#include <vector>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

/* stb_image_write - declaration only (implementation in debug_png.cpp) */
extern "C" int stbi_write_png(const char* filename, int w, int h, int comp,
                               const void* data, int stride_in_bytes);

/* =========================================================================
 * Catmull-Rom interpolation along DCEL edge chains
 * ========================================================================= */

/* Find the best chain continuation from a vertex.
 * At the origin vertex of `from_he`, find another outgoing edge whose
 * direction best aligns with (ref_tx, ref_ty).
 * Returns the destination vertex index, or -1 if no other edges exist. */
static int32_t find_continuation(
	const DCELMesh& mesh, int32_t from_he, float ref_tx, float ref_ty)
{
	int32_t best_dest = -1;
	float best_align = -2.0f;

	int32_t cur = dcel_next_around_vertex(mesh, from_he);
	while (cur != from_he) {
		int32_t d = dcel_dest(mesh, cur);
		const DCELVertex& vo = mesh.vertices[mesh.half_edges[cur].origin];
		const DCELVertex& vd = mesh.vertices[d];
		float dx = vd.x - vo.x, dy = vd.y - vo.y;
		float m = sqrtf(dx * dx + dy * dy);
		if (m > 1e-6f) {
			float align = (dx * ref_tx + dy * ref_ty) / m;
			if (align > best_align) {
				best_align = align;
				best_dest = d;
			}
		}
		cur = dcel_next_around_vertex(mesh, cur);
	}

	return best_dest;
}

/* Compute Catmull-Rom spline tangent at a pixel's projection onto an edge.
 * Walks the DCEL chain to get 4 control points (P0, P1, P2, P3),
 * projects (px,py) onto P1-P2, evaluates the spline derivative.
 * Returns a unit tangent, or the raw edge tangent as fallback. */
static vec2 catmull_rom_edge_tangent(
	const DCELMesh& mesh, int32_t he_idx, float px, float py)
{
	if (he_idx < 0) return vec2(0.0f);

	const DCELHalfEdge& he = mesh.half_edges[he_idx];
	int32_t dest_idx = dcel_dest(mesh, he_idx);
	const DCELVertex& v1 = mesh.vertices[he.origin];
	const DCELVertex& v2 = mesh.vertices[dest_idx];

	vec2 P1(v1.x, v1.y);
	vec2 P2(v2.x, v2.y);
	vec2 edge_dir = P2 - P1;

	/* P0: chain continuation backwards from origin.
	 * At a terminal, reflect (q'(0) = edge dir = contour direction). */
	int32_t p0_vi = find_continuation(mesh, he_idx, -he.tangent_x, -he.tangent_y);
	vec2 P0 = (p0_vi >= 0)
		? vec2(mesh.vertices[p0_vi].x, mesh.vertices[p0_vi].y)
		: 2.0f * P1 - P2;

	/* P3: chain continuation forwards from dest */
	int32_t p3_vi = find_continuation(mesh, he.twin, he.tangent_x, he.tangent_y);
	vec2 P3 = (p3_vi >= 0)
		? vec2(mesh.vertices[p3_vi].x, mesh.vertices[p3_vi].y)
		: 2.0f * P2 - P1;

	/* Project pixel onto P1-P2 → parameter t ∈ [0,1] */
	float edge_len_sq = edge_dir.x * edge_dir.x + edge_dir.y * edge_dir.y;
	float t;
	if (edge_len_sq < 1e-6f) {
		t = 0.5f;
	} else {
		vec2 to_px = vec2(px, py) - P1;
		t = (to_px.x * edge_dir.x + to_px.y * edge_dir.y) / edge_len_sq;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
	}

	/* Catmull-Rom derivative at t:
	 * q(t)  = 0.5*[(-t³+2t²-t)P0 + (3t³-5t²+2)P1 + (-3t³+4t²+t)P2 + (t³-t²)P3]
	 * q'(t) = 0.5*[(-3t²+4t-1)P0 + (9t²-10t)P1 + (-9t²+8t+1)P2 + (3t²-2t)P3] */
	float t2 = t * t;
	float b0 = -3*t2 + 4*t - 1;
	float b1 =  9*t2 - 10*t;
	float b2 = -9*t2 + 8*t + 1;
	float b3 =  3*t2 - 2*t;
	vec2 tangent = 0.5f * (b0 * P0 + b1 * P1 + b2 * P2 + b3 * P3);

	float m = sqrtf(tangent.x * tangent.x + tangent.y * tangent.y);
	if (m > 1e-6f) return tangent / m;
	return vec2(he.tangent_x, he.tangent_y);  /* fallback */
}

/* =========================================================================
 * Orchestrator: chains Phase 1→7
 * ========================================================================= */

int ridge_mesh_Execute(RidgeMeshCmd* cmd)
{
	if (!cmd || !cmd->heightmap) {
		fprintf(stderr, "[ridge_mesh] Error: NULL input\n");
		return -1;
	}

	uint32_t W = cmd->W;
	uint32_t H = cmd->H;

	float lo = cmd->low_threshold > 0.0f ? cmd->low_threshold : 0.10f;

	printf("[ridge_mesh] %ux%u, F=%.4f, high=%.4f, low=%.4f\n",
	       W, H,
	       cmd->normal_scale > 0.0f ? cmd->normal_scale : 1.0f,
	       cmd->high_threshold > 0.0f ? cmd->high_threshold : 0.25f,
	       lo);

	/* Phase 1: Divergence field */
	DivergenceFieldCmd div_cmd{};
	div_cmd.heightmap = cmd->heightmap;
	div_cmd.W = W;
	div_cmd.H = H;
	div_cmd.normal_scale = cmd->normal_scale;
	if (divergence_field_Execute(&div_cmd) != 0) return -1;

	/* Phase 2: Skeleton extraction */
	SkeletonCmd skel_cmd{};
	skel_cmd.heightmap = cmd->heightmap;
	skel_cmd.divergence = div_cmd.divergence.get();
	skel_cmd.W = W;
	skel_cmd.H = H;
	skel_cmd.high_threshold = cmd->high_threshold;
	skel_cmd.min_branch_length = 5;
	if (skeleton_Execute(&skel_cmd) != 0) return -1;

	/* Phase 3: Walkable mask + Dijkstra edge connectivity */
	uint32_t N = W * H;
	auto walkable = std::unique_ptr<uint8_t[]>(new uint8_t[N]);
	int walk_count = 0;
	for (uint32_t i = 0; i < N; i++) {
		walkable[i] = (fabsf(div_cmd.divergence[i]) > lo) ? 1 : 0;
		if (walkable[i]) walk_count++;
	}
	printf("[ridge_mesh] Walkable mask (|div| > %.4f): %d pixels\n", lo, walk_count);

	SkeletonGraphCmd graph_cmd{};
	graph_cmd.heightmap = cmd->heightmap;
	graph_cmd.divergence = div_cmd.divergence.get();
	graph_cmd.walkable = walkable.get();
	graph_cmd.vertices = &skel_cmd.vertices;
	graph_cmd.W = W;
	graph_cmd.H = H;
	if (skeleton_graph_Execute(&graph_cmd) != 0) return -1;

	/* Phase 4: DCEL build (uses expanded vertices from Dijkstra paths) */
	DCELBuildCmd build_cmd{};
	build_cmd.vertices = std::move(graph_cmd.expanded_vertices);
	build_cmd.edges = &graph_cmd.edges;
	if (dcel_build_Execute(&build_cmd) != 0) return -1;

	/* Phase 4b: Simplify mesh (collapse tiny faces) */
	MeshSimplifyCmd simplify_cmd{};
	simplify_cmd.mesh = &build_cmd.mesh;
	simplify_cmd.min_area = 4.0f;
	if (mesh_simplify_Execute(&simplify_cmd) != 0) return -1;

	/* Phase 4c: RDP chain simplification */
	MeshDecimateCmd decimate_cmd{};
	decimate_cmd.mesh = &build_cmd.mesh;
	decimate_cmd.epsilon = 1.0f;
	if (mesh_decimate_Execute(&decimate_cmd) != 0) return -1;

	/* Phase 5: Feature discovery */
	DCELFeaturesCmd feat_cmd{};
	feat_cmd.mesh = &build_cmd.mesh;
	if (dcel_features_Execute(&feat_cmd) != 0) return -1;

	/* Phase 6: Ridge/valley energy */
	RidgeEnergyCmd energy_cmd{};
	energy_cmd.mesh = &build_cmd.mesh;
	energy_cmd.heightmap = cmd->heightmap;
	energy_cmd.W = W;
	energy_cmd.H = H;
	if (ridge_energy_Execute(&energy_cmd) != 0) return -1;

	/* Phase 7: Edge Dijkstra — height-guided propagation from edges */
	EdgeDijkstraCmd dijk_cmd{};
	dijk_cmd.mesh = &build_cmd.mesh;
	dijk_cmd.heightmap = cmd->heightmap;
	dijk_cmd.W = W;
	dijk_cmd.H = H;
	dijk_cmd.height_bias = 50.0f;
	if (edge_dijkstra_Execute(&dijk_cmd) != 0) return -1;

	/* Move results to output */
	cmd->mesh = std::move(build_cmd.mesh);
	cmd->divergence = std::move(div_cmd.divergence);
	printf("[ridge_mesh] Done: %zu vertices, %zu half-edges, %zu features\n",
	       cmd->mesh.vertices.size(),
	       cmd->mesh.half_edges.size(),
	       cmd->mesh.features.size());

	/* Debug: dump edges near a specific point */
	{
		float qx = 4.0f, qy = 88.0f, radius = 3.0f;
		float r2 = radius * radius;
		printf("[debug] Edges near (%.0f, %.0f) radius=%.0f:\n", qx, qy, radius);
		int32_t num_he = (int32_t)cmd->mesh.half_edges.size();
		for (int32_t i = 0; i < num_he; i++) {
			const DCELHalfEdge& he = cmd->mesh.half_edges[i];
			if (he.twin < i) continue;  /* each undirected edge once */
			const DCELVertex& v0 = cmd->mesh.vertices[he.origin];
			int32_t dest = dcel_dest(cmd->mesh, i);
			const DCELVertex& v1 = cmd->mesh.vertices[dest];
			float mx = (v0.x + v1.x) * 0.5f, my = (v0.y + v1.y) * 0.5f;
			float dx0 = v0.x - qx, dy0 = v0.y - qy;
			float dx1 = v1.x - qx, dy1 = v1.y - qy;
			float dxm = mx - qx, dym = my - qy;
			if (dx0*dx0+dy0*dy0 > r2 && dx1*dx1+dy1*dy1 > r2 && dxm*dxm+dym*dym > r2)
				continue;
			const DCELHalfEdge& tw = cmd->mesh.half_edges[he.twin];
			printf("  he[%d] (%5.1f,%5.1f)->(%5.1f,%5.1f) %s len=%.1f "
			       "tang=(%.3f,%.3f) e=%.1f | twin[%d] e=%.1f "
			       "face=%d next=%d prev=%d "
			       "v0:%s v1:%s h=(%.3f,%.3f)\n",
			       i, v0.x, v0.y, v1.x, v1.y,
			       he.type == DCEL_EDGE_RIDGE ? "RIDGE " : "VALLEY",
			       he.length,
			       he.tangent_x, he.tangent_y,
			       he.energy, he.twin, tw.energy,
			       he.face, he.next, he.prev,
			       v0.type == DCEL_VERTEX_ENDPOINT ? "END" :
			       v0.type == DCEL_VERTEX_JUNCTION ? "JCT" :
			       v0.type == DCEL_VERTEX_MAXIMUM  ? "MAX" :
			       v0.type == DCEL_VERTEX_MINIMUM  ? "MIN" : "PTH",
			       v1.type == DCEL_VERTEX_ENDPOINT ? "END" :
			       v1.type == DCEL_VERTEX_JUNCTION ? "JCT" :
			       v1.type == DCEL_VERTEX_MAXIMUM  ? "MAX" :
			       v1.type == DCEL_VERTEX_MINIMUM  ? "MIN" : "PTH",
			       v0.height, v1.height);
		}
	}

#if DEBUG_IMG_OUT
	/* =================================================================
	 * Ground truth comparison: bitangent from heightmap gradient vs
	 * Dijkstra-propagated direction from mesh edges.
	 *
	 * Ridges/valleys follow contours, bitangent follows contours,
	 * so |dot(ground_truth, dijkstra)| should be close to 1.
	 * ================================================================= */
	{
		uint32_t N = W * H;

		/* Ground truth bitangent: 90° CCW rotation of heightmap gradient */
		auto gt = std::unique_ptr<vec2[]>(new vec2[N]);

		for (uint32_t y = 1; y < H - 1; y++) {
			for (uint32_t x = 1; x < W - 1; x++) {
				float dx = (cmd->heightmap[y * W + (x + 1)]
				          - cmd->heightmap[y * W + (x - 1)]) * 0.5f;
				float dy = (cmd->heightmap[(y + 1) * W + x]
				          - cmd->heightmap[(y - 1) * W + x]) * 0.5f;
				/* Bitangent = perpendicular to gradient = (-dy, dx) */
				float bx = -dy;
				float by = dx;
				float mag = sqrtf(bx * bx + by * by);
				if (mag > 1e-6f) {
					gt[y * W + x] = vec2(bx / mag, by / mag);
				} else {
					gt[y * W + x] = vec2(0.0f, 0.0f);
				}
			}
		}
		/* Border: copy from adjacent interior row/col */
		for (uint32_t x = 0; x < W; x++) {
			gt[x] = gt[W + x];
			gt[(H - 1) * W + x] = gt[(H - 2) * W + x];
		}
		for (uint32_t y = 0; y < H; y++) {
			gt[y * W] = gt[y * W + 1];
			gt[y * W + W - 1] = gt[y * W + W - 2];
		}

		/* Output: ground truth bitangent as normal map */
		char buf[512];
		PngVec2Cmd gt_png{};
		gt_png.path = debug_path("ground_truth_bt.png", buf, sizeof(buf));
		gt_png.data = gt.get();
		gt_png.width = W;
		gt_png.height = H;
		gt_png.scale = 1.0f;
		gt_png.z_bias = 0.5f;
		png_ExportVec2(&gt_png);
		printf("[ridge_mesh] Wrote: %s\n", buf);

		/* ---- Edge Dijkstra comparison ---- */
		{
			auto dijk_combined = std::unique_ptr<vec2[]>(new vec2[N]);
			auto dijk_dot = std::unique_ptr<float[]>(new float[N]());

			/* Pass 1: compute dijk_combined for all pixels */
			for (uint32_t i = 0; i < N; i++)
			{
			// special case: no data
				if(cmd->heightmap[i] == 0)
				{
					dijk_combined[i] = glm::vec2{0};
					dijk_dot[i] = 1;
					continue;
				}
				
				float px = (float)(i % W);
				float py = (float)(i / W);
				int32_t u_he = dijk_cmd.uphill_edge_id[i];
				int32_t d_he = dijk_cmd.downhill_edge_id[i];
				float uc = dijk_cmd.uphill_cost[i];
				float dc = dijk_cmd.downhill_cost[i];

				/* Catmull-Rom interpolated tangent from each edge chain */
				vec2 u_tang = catmull_rom_edge_tangent(cmd->mesh, u_he, px, py);
				vec2 d_tang = catmull_rom_edge_tangent(cmd->mesh, d_he, px, py);

				/* Sign from Dijkstra-propagated tangent: the Dijkstra stores
				 * the canonical (energy-bearing) half-edge's tangent, which has
				 * consistent sign along each chain. Align each Catmull-Rom
				 * tangent to match its Dijkstra-propagated sign. */
				float u_dx = dijk_cmd.uphill_dir[i*2+0];
				float u_dy = dijk_cmd.uphill_dir[i*2+1];
				float d_dx = dijk_cmd.downhill_dir[i*2+0];
				float d_dy = dijk_cmd.downhill_dir[i*2+1];

				if (u_tang.x * u_dx + u_tang.y * u_dy < 0.0f)
					u_tang = -u_tang;
				if (d_tang.x * d_dx + d_tang.y * d_dy < 0.0f)
					d_tang = -d_tang;

				/* Weighted signed blend */
				float u_w = (uc < INFINITY) ? 1.0f / (uc + 1.0f) : 0.0f;
				float d_w = (dc < INFINITY) ? 1.0f / (dc + 1.0f) : 0.0f;

				float u_m = sqrtf(u_tang.x*u_tang.x + u_tang.y*u_tang.y);
				float d_m = sqrtf(d_tang.x*d_tang.x + d_tang.y*d_tang.y);
				vec2 u_dir = (u_m > 1e-6f) ? u_tang / u_m : vec2(0);
				vec2 d_dir = (d_m > 1e-6f) ? d_tang / d_m : vec2(0);

				vec2 combined = u_w * u_dir + d_w * d_dir;
				float cm = sqrtf(combined.x*combined.x + combined.y*combined.y);
				if (cm > 1e-8f) {
					dijk_combined[i] = combined / cm;
				} else {
					dijk_combined[i] = vec2(0.0f, 0.0f);
				}

				}

			/* ---- Sign discontinuity on RAW output (before GT alignment) ---- */
			{
				int sign_flips = 0, sign_total = 0;
				int gt_flips = 0, gt_total = 0;
				for (uint32_t y = 0; y < H; y++) {
					for (uint32_t x = 0; x < W; x++) {
						uint32_t i = y * W + x;
						float cx = dijk_combined[i].x, cy = dijk_combined[i].y;
						float cm = cx*cx + cy*cy;
						/* Raw output sign flips */
						if (cm > 1e-6f) {
							if (x + 1 < W) {
								uint32_t j = i + 1;
								float nm = dijk_combined[j].x*dijk_combined[j].x + dijk_combined[j].y*dijk_combined[j].y;
								if (nm > 1e-6f) {
									sign_total++;
									if (cx*dijk_combined[j].x + cy*dijk_combined[j].y < 0.0f) sign_flips++;
								}
							}
							if (y + 1 < H) {
								uint32_t j = i + W;
								float nm = dijk_combined[j].x*dijk_combined[j].x + dijk_combined[j].y*dijk_combined[j].y;
								if (nm > 1e-6f) {
									sign_total++;
									if (cx*dijk_combined[j].x + cy*dijk_combined[j].y < 0.0f) sign_flips++;
								}
							}
						}
						/* GT sign flips (for comparison) */
						float gx = gt[i].x, gy = gt[i].y;
						float gm = gx*gx + gy*gy;
						if (gm > 1e-6f) {
							if (x + 1 < W) {
								uint32_t j = i + 1;
								float gnm = gt[j].x*gt[j].x + gt[j].y*gt[j].y;
								if (gnm > 1e-6f) {
									gt_total++;
									if (gx*gt[j].x + gy*gt[j].y < 0.0f) gt_flips++;
								}
							}
							if (y + 1 < H) {
								uint32_t j = i + W;
								float gnm = gt[j].x*gt[j].x + gt[j].y*gt[j].y;
								if (gnm > 1e-6f) {
									gt_total++;
									if (gx*gt[j].x + gy*gt[j].y < 0.0f) gt_flips++;
								}
							}
						}
					}
				}
				printf("[ridge_mesh]   Sign flips (raw output): %d / %d pairs (%.2f%%)\n",
				       sign_flips, sign_total,
				       sign_total > 0 ? 100.0f * sign_flips / sign_total : 0.0f);
				printf("[ridge_mesh]   Sign flips (ground truth): %d / %d pairs (%.2f%%)\n",
				       gt_flips, gt_total,
				       gt_total > 0 ? 100.0f * gt_flips / gt_total : 0.0f);
			}

			/* Output raw signed vector field before GT alignment */
			{
				char buf[512];
				PngVec2Cmd raw_png{};
				raw_png.path = debug_path("dijkstra_raw_signed.png", buf, sizeof(buf));
				raw_png.data = dijk_combined.get();
				raw_png.width = W;
				raw_png.height = H;
				raw_png.scale = 1.0f;
				raw_png.z_bias = 0.5f;
				png_ExportVec2(&raw_png);
				printf("[ridge_mesh] Wrote: %s\n", buf);
			}

			/* Pass 2: Compare with ground truth; flip sign to match for visualization */
			float dijk_sum = 0.0f;
			int dijk_count = 0;
			int dijk_bucket[5] = {};

			for (uint32_t i = 0; i < N; i++) {
				float gt_mag = sqrtf(gt[i].x * gt[i].x + gt[i].y * gt[i].y);

				if (std::isnormal(gt_mag) == false || cmd->heightmap[i] == 0)
				{
					dijk_combined[i] = glm::vec2{0};
					dijk_dot[i] = 1;
				}
				else
				{
					float d = gt[i].x * dijk_combined[i].x
					        + gt[i].y * dijk_combined[i].y;
					if (d < 0.0f) {
						dijk_combined[i] = -dijk_combined[i];
						d = -d;
					}
					dijk_dot[i] = d;
					dijk_sum += d;
					dijk_count++;
					int b = (int)(d * 5.0f);
					if (b > 4) b = 4;
					dijk_bucket[b]++;
				}
			}

			float dijk_mean = dijk_count > 0 ? dijk_sum / dijk_count : 0.0f;
			printf("[ridge_mesh] Dijkstra |dot|: mean=%.4f (%d pixels)\n",
			       dijk_mean, dijk_count);

			/* 16-bucket normalized histogram */
			int histo[16] = {};
			for (uint32_t i = 0; i < N; i++) {
				if (dijk_dot[i] > 0.0f) {
					int b = (int)(dijk_dot[i] * 16.0f);
					if (b > 15) b = 15;
					histo[b]++;
				}
			}
			int histo_max = 0;
			for (int b = 0; b < 16; b++)
				if (histo[b] > histo_max) histo_max = histo[b];

			printf("[ridge_mesh]   |dot| histogram (%d pixels):\n", dijk_count);
			for (int b = 0; b < 16; b++) {
				int bar = histo_max > 0 ? (histo[b] * 40 + histo_max / 2) / histo_max : 0;
				printf("    %4.2f-%4.2f |", b / 16.0f, (b + 1) / 16.0f);
				for (int j = 0; j < bar; j++) putchar('#');
				printf(" %d\n", histo[b]);
			}

			/* ---- Breakdown: |dot| vs distance to nearest edge ---- */
			{
				/* Find max min-cost for binning */
				float max_min_cost = 0;
				for (uint32_t i = 0; i < N; i++) {
					float mc = std::min(dijk_cmd.uphill_cost[i], dijk_cmd.downhill_cost[i]);
					if (mc < INFINITY && mc > max_min_cost) max_min_cost = mc;
				}

				const int NBINS = 12;
				float bin_w = max_min_cost / NBINS;
				float dist_sum[NBINS] = {};
				int dist_cnt[NBINS] = {};

				for (uint32_t i = 0; i < N; i++) {
					if (dijk_dot[i] <= 0.0f) continue;
					float mc = std::min(dijk_cmd.uphill_cost[i], dijk_cmd.downhill_cost[i]);
					if (mc >= INFINITY) continue;
					int b = (int)(mc / bin_w);
					if (b >= NBINS) b = NBINS - 1;
					dist_sum[b] += dijk_dot[i];
					dist_cnt[b]++;
				}

				printf("[ridge_mesh]   |dot| vs edge distance (cost bins of %.1f):\n", bin_w);
				for (int b = 0; b < NBINS; b++) {
					float mean = dist_cnt[b] > 0 ? dist_sum[b] / dist_cnt[b] : 0;
					int bar = (int)(mean * 40 + 0.5f);
					printf("    %5.1f-%5.1f |", b * bin_w, (b + 1) * bin_w);
					for (int j = 0; j < bar; j++) putchar('#');
					printf(" %.3f (%d)\n", mean, dist_cnt[b]);
				}
			}

			/* ---- Breakdown: |dot| vs |divergence| ---- */
			{
				float max_div = 0;
				for (uint32_t i = 0; i < N; i++) {
					float ad = fabsf(cmd->divergence[i]);
					if (ad > max_div) max_div = ad;
				}

				const int NBINS = 12;
				float bin_w = max_div / NBINS;
				float div_sum[NBINS] = {};
				int div_cnt[NBINS] = {};

				for (uint32_t i = 0; i < N; i++) {
					if (dijk_dot[i] <= 0.0f) continue;
					float ad = fabsf(cmd->divergence[i]);
					int b = (int)(ad / bin_w);
					if (b >= NBINS) b = NBINS - 1;
					div_sum[b] += dijk_dot[i];
					div_cnt[b]++;
				}

				printf("[ridge_mesh]   |dot| vs |divergence| (bins of %.4f):\n", bin_w);
				for (int b = 0; b < NBINS; b++) {
					float mean = div_cnt[b] > 0 ? div_sum[b] / div_cnt[b] : 0;
					int bar = (int)(mean * 40 + 0.5f);
					printf("    %5.3f-%5.3f |", b * bin_w, (b + 1) * bin_w);
					for (int j = 0; j < bar; j++) putchar('#');
					printf(" %.3f (%d)\n", mean, div_cnt[b]);
				}
			}

			/* ---- Breakdown: |dot| vs edge-to-pixel alignment ---- */
			/* dot(normalize(pixel - seed), edge_tangent) tells us if the
			 * pixel is "along" the edge (|dot|≈1) or perpendicular (|dot|≈0) */
			{
				const int NBINS = 12;
				float align_sum[NBINS] = {};
				int align_cnt[NBINS] = {};

				for (uint32_t i = 0; i < N; i++) {
					if (dijk_dot[i] <= 0.0f) continue;

					/* Use whichever pass had lower cost */
					float uc = dijk_cmd.uphill_cost[i];
					float dc = dijk_cmd.downhill_cost[i];
					float sx, sy, tx, ty;
					if (uc <= dc) {
						sx = dijk_cmd.uphill_seed_xy[i*2+0];
						sy = dijk_cmd.uphill_seed_xy[i*2+1];
						tx = dijk_cmd.uphill_dir[i*2+0];
						ty = dijk_cmd.uphill_dir[i*2+1];
					} else {
						sx = dijk_cmd.downhill_seed_xy[i*2+0];
						sy = dijk_cmd.downhill_seed_xy[i*2+1];
						tx = dijk_cmd.downhill_dir[i*2+0];
						ty = dijk_cmd.downhill_dir[i*2+1];
					}

					float vx = (float)(i % W) - sx;
					float vy = (float)(i / W) - sy;
					float vmag = sqrtf(vx * vx + vy * vy);
					float tmag = sqrtf(tx * tx + ty * ty);
					if (vmag < 1.0f || tmag < 1e-6f) continue;

					float align = fabsf((vx * tx + vy * ty) / (vmag * tmag));
					int b = (int)(align * NBINS);
					if (b >= NBINS) b = NBINS - 1;
					align_sum[b] += dijk_dot[i];
					align_cnt[b]++;
				}

				printf("[ridge_mesh]   |dot| vs edge-to-pixel alignment:\n");
				for (int b = 0; b < NBINS; b++) {
					float mean = align_cnt[b] > 0 ? align_sum[b] / align_cnt[b] : 0;
					int bar = (int)(mean * 40 + 0.5f);
					printf("    %4.2f-%4.2f |", b / (float)NBINS, (b + 1) / (float)NBINS);
					for (int j = 0; j < bar; j++) putchar('#');
					printf(" %.3f (%d)\n", mean, align_cnt[b]);
				}
			}

			/* ---- Breakdown: terminal vs non-terminal edges ---- */
			{
				float term_sum = 0, nonterm_sum = 0;
				int term_cnt = 0, nonterm_cnt = 0;
				int term_bucket[5] = {}, nonterm_bucket[5] = {};

				for (uint32_t i = 0; i < N; i++) {
					if (dijk_dot[i] <= 0.0f) continue;

					/* Check if the closer seed was terminal */
					float uc = dijk_cmd.uphill_cost[i];
					float dc = dijk_cmd.downhill_cost[i];
					uint8_t term = (uc <= dc) ? dijk_cmd.uphill_terminal[i]
					                          : dijk_cmd.downhill_terminal[i];

					int b = (int)(dijk_dot[i] * 5.0f);
					if (b > 4) b = 4;

					if (term) {
						term_sum += dijk_dot[i];
						term_cnt++;
						term_bucket[b]++;
					} else {
						nonterm_sum += dijk_dot[i];
						nonterm_cnt++;
						nonterm_bucket[b]++;
					}
				}

				float term_mean = term_cnt > 0 ? term_sum / term_cnt : 0;
				float nonterm_mean = nonterm_cnt > 0 ? nonterm_sum / nonterm_cnt : 0;
				printf("[ridge_mesh]   Terminal edges:     mean=%.4f (%d px)  "
				       "0-0.2:%d  0.2-0.4:%d  0.4-0.6:%d  0.6-0.8:%d  0.8-1.0:%d\n",
				       term_mean, term_cnt,
				       term_bucket[0], term_bucket[1], term_bucket[2],
				       term_bucket[3], term_bucket[4]);
				printf("[ridge_mesh]   Non-terminal edges: mean=%.4f (%d px)  "
				       "0-0.2:%d  0.2-0.4:%d  0.4-0.6:%d  0.6-0.8:%d  0.8-1.0:%d\n",
				       nonterm_mean, nonterm_cnt,
				       nonterm_bucket[0], nonterm_bucket[1], nonterm_bucket[2],
				       nonterm_bucket[3], nonterm_bucket[4]);
			}

			/* Output combined Dijkstra direction as normal map */
			PngVec2Cmd dijk_png{};
			dijk_png.path = debug_path("dijkstra_combined.png", buf, sizeof(buf));
			dijk_png.data = dijk_combined.get();
			dijk_png.width = W;
			dijk_png.height = H;
			dijk_png.scale = 1.0f;
			dijk_png.z_bias = 0.5f;
			png_ExportVec2(&dijk_png);
			printf("[ridge_mesh] Wrote: %s\n", buf);

			/* Output: |dot| map (white = 1.0 = perfect match) */
			PngFloatCmd dot_png{};
			dot_png.path = debug_path("dijkstra_dot.png", buf, sizeof(buf));
			dot_png.data = dijk_dot.get();
			dot_png.width = W;
			dot_png.height = H;
			dot_png.min_val = 0.0f;
			dot_png.max_val = 1.0f;
			dot_png.auto_range = 0;
			png_ExportFloat(&dot_png);
			printf("[ridge_mesh] Wrote: %s\n", buf);
		}
	}
#endif

	return 0;
}

/* =========================================================================
 * Debug render: draws DCEL mesh to PNG
 *
 * Color scheme:
 *   Background: height as dark grayscale
 *   Ridge half-edges: dark red (low energy) → bright yellow (high energy)
 *   Valley half-edges: dim gray-blue (unassigned) or dark blue → cyan (assigned)
 *   Vertices: green=maximum, yellow=minimum, white=junction, gray=endpoint
 * ========================================================================= */

int ridge_mesh_DebugRender(const RidgeMeshCmd* cmd)
{
	if (!cmd || !cmd->heightmap || !cmd->divergence) {
		fprintf(stderr, "[ridge_mesh] DebugRender: NULL input\n");
		return -1;
	}

	uint32_t W = cmd->W;
	uint32_t H = cmd->H;
	uint32_t N = W * H;

	auto rgb = std::unique_ptr<uint8_t[]>(new uint8_t[N * 3]);

	/* Background: height as gray, walkable regions tinted, non-walkable dimmed */
	float lo_thresh = cmd->low_threshold > 0.0f ? cmd->low_threshold : 0.10f;
	for (uint32_t i = 0; i < N; i++) {
		float d = cmd->divergence[i];
		if (fabsf(d) > lo_thresh) {
			/* Walkable: brighter base, warm tint for ridges, cool for valleys */
			uint8_t gray = (uint8_t)(cmd->heightmap[i] * 140.0f);
			if (d < 0.0f) {
				rgb[i*3+0] = (uint8_t)std::min(255, (int)gray + 50);
				rgb[i*3+1] = gray;
				rgb[i*3+2] = gray;
			} else {
				rgb[i*3+0] = gray;
				rgb[i*3+1] = gray;
				rgb[i*3+2] = (uint8_t)std::min(255, (int)gray + 50);
			}
		} else {
			/* Non-walkable: dimmed */
			uint8_t gray = (uint8_t)(cmd->heightmap[i] * 80.0f);
			rgb[i*3+0] = gray;
			rgb[i*3+1] = gray;
			rgb[i*3+2] = gray;
		}
	}

	/* Find max energy for normalization */
	float max_ridge_e = 1.0f, max_valley_e = 1.0f;
	for (const auto& he : cmd->mesh.half_edges) {
		if (he.type == DCEL_EDGE_RIDGE && he.energy > max_ridge_e)
			max_ridge_e = he.energy;
		if (he.type == DCEL_EDGE_VALLEY && he.energy > max_valley_e)
			max_valley_e = he.energy;
	}

	/* Draw edges as lines between vertices */
	int32_t num_he = (int32_t)cmd->mesh.half_edges.size();
	for (int32_t i = 0; i < num_he; i++) {
		const DCELHalfEdge& he = cmd->mesh.half_edges[i];
		/* Only draw each undirected edge once (fwd half-edge) */
		if (he.twin < i) continue;

		/* Use whichever half-edge has more energy for coloring */
		float energy = he.energy;
		const DCELHalfEdge& twin_he = cmd->mesh.half_edges[he.twin];
		if (twin_he.energy > energy) energy = twin_he.energy;

		const DCELVertex& v0 = cmd->mesh.vertices[he.origin];
		const DCELVertex& v1 = cmd->mesh.vertices[twin_he.origin];

		/* Bresenham line between v0 and v1 */
		int x0 = (int)v0.x, y0 = (int)v0.y;
		int x1 = (int)v1.x, y1 = (int)v1.y;

		int adx = abs(x1 - x0);
		int ady = abs(y1 - y0);
		int sx = x0 < x1 ? 1 : -1;
		int sy = y0 < y1 ? 1 : -1;
		int err = adx - ady;

		uint8_t r, g, b;
		if (he.type == DCEL_EDGE_RIDGE) {
			float t = energy / max_ridge_e;
			r = (uint8_t)(80 + 175 * t);
			g = (uint8_t)(30 + 200 * t);
			b = 30;
		} else {
			if (energy > 0.0f) {
				float t = energy / max_valley_e;
				r = 30;
				g = (uint8_t)(60 + 195 * t);
				b = (uint8_t)(120 + 135 * t);
			} else {
				r = 60; g = 60; b = 100;
			}
		}

		for (;;) {
			if ((uint32_t)x0 < W && (uint32_t)y0 < H) {
				uint32_t pi = (uint32_t)y0 * W + (uint32_t)x0;
				rgb[pi*3+0] = r;
				rgb[pi*3+1] = g;
				rgb[pi*3+2] = b;
			}
			if (x0 == x1 && y0 == y1) break;
			int e2 = 2 * err;
			if (e2 > -ady) { err -= ady; x0 += sx; }
			if (e2 < adx)  { err += adx; y0 += sy; }
		}
	}

	/* Draw vertices on top (skip PATH vertices — already covered by edges) */
	for (const auto& v : cmd->mesh.vertices) {
		if (v.type == DCEL_VERTEX_PATH) continue;

		uint8_t r, g, b;
		int radius;
		switch (v.type) {
		case DCEL_VERTEX_MAXIMUM:  r = 0;   g = 255; b = 0;   radius = 1; break;
		case DCEL_VERTEX_MINIMUM:  r = 255; g = 255; b = 0;   radius = 1; break;
		case DCEL_VERTEX_JUNCTION: r = 255; g = 255; b = 255; radius = 0; break;
		case DCEL_VERTEX_ENDPOINT: r = 180; g = 180; b = 180; radius = 0; break;
		case DCEL_VERTEX_PATH: continue;
		}

		for (int dy = -radius; dy <= radius; dy++) {
			for (int dx = -radius; dx <= radius; dx++) {
				int px = (int)roundf(v.x) + dx;
				int py = (int)roundf(v.y) + dy;
				if (px >= 0 && px < (int)W && py >= 0 && py < (int)H) {
					uint32_t pi = (uint32_t)py * W + (uint32_t)px;
					rgb[pi*3+0] = r;
					rgb[pi*3+1] = g;
					rgb[pi*3+2] = b;
				}
			}
		}
	}

	char buf[512];
	const char* path = debug_path("ridge_mesh.png", buf, sizeof(buf));
	stbi_write_png(path, W, H, 3, rgb.get(), W * 3);
	printf("[ridge_mesh] Wrote debug image: %s\n", path);

	/* Also write divergence field */
	PngFloatCmd div_png{};
	div_png.path = debug_path("divergence.png", buf, sizeof(buf));
	div_png.data = cmd->divergence.get();
	div_png.width = W;
	div_png.height = H;
	div_png.auto_range = 1;
	png_ExportFloat(&div_png);
	printf("[ridge_mesh] Wrote divergence: %s\n", div_png.path);

	return 0;
}
