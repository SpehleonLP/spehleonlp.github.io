#include "energy_flood_cmd.h"
#include "debug_png.h"
#include "../debug_output.h"
#include <math.h>
#include <stdio.h>
#include <vector>

/* =========================================================================
 * Bitangent-guided interpolation from mesh edges to pixels
 *
 * The bitangent DIRECTION is known from the heightmap gradient (perpendicular
 * to steepest descent).  Only its SIGN is ambiguous.  We use this known
 * direction to select which edges are relevant to each pixel:
 *
 *   weight = |dot(edge_tangent, pixel_bitangent)| / (dist² + 1)
 *
 * Edges aligned with the local contour get high weight; perpendicular edges
 * get zero.  The dot product sign resolves the bitangent sign ambiguity.
 *
 * Spatial filtering: edges are grouped by DCEL face; only faces whose AABB
 * contains the pixel (with margin) are checked.
 * ========================================================================= */

struct EdgeInfo {
	float ax, ay;       /* segment start */
	float dx, dy;       /* segment direction (B - A) */
	float len_sq;       /* |B - A|^2 */
	float tx, ty;       /* unit tangent direction */
	DCELEdgeType type;
};

/* Squared distance from point (px, py) to line segment */
static inline float point_seg_dist_sq(float px, float py, const EdgeInfo& e)
{
	float apx = px - e.ax;
	float apy = py - e.ay;

	if (e.len_sq < 1e-8f) {
		return apx * apx + apy * apy;
	}

	float t = (apx * e.dx + apy * e.dy) / e.len_sq;
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;

	float cx = e.ax + t * e.dx - px;
	float cy = e.ay + t * e.dy - py;
	return cx * cx + cy * cy;
}

int energy_flood_Execute(EnergyFloodCmd* cmd)
{
	if (!cmd || !cmd->mesh || !cmd->heightmap) {
		fprintf(stderr, "[energy_flood] Error: NULL input\n");
		return -1;
	}

	const DCELMesh& mesh = *cmd->mesh;
	uint32_t W = cmd->W, H = cmd->H;
	uint32_t N = W * H;
	int32_t num_he = (int32_t)mesh.half_edges.size();
	int32_t num_feat = (int32_t)mesh.features.size();

	/* ---- Build per-feature edge lists with AABB ---- */

	/* Collect one EdgeInfo per undirected edge (deduplicate twins) */
	std::vector<EdgeInfo> all_edges;
	/* For each undirected edge, which faces it borders */
	std::vector<std::pair<int32_t, int32_t>> edge_faces;

	for (int32_t hi = 0; hi < num_he; hi++) {
		const DCELHalfEdge& he = mesh.half_edges[hi];
		if (he.twin < hi) continue;

		int32_t dest = mesh.half_edges[he.twin].origin;

		EdgeInfo ei;
		ei.ax = mesh.vertices[he.origin].x;
		ei.ay = mesh.vertices[he.origin].y;
		float bx = mesh.vertices[dest].x;
		float by = mesh.vertices[dest].y;
		ei.dx = bx - ei.ax;
		ei.dy = by - ei.ay;
		ei.len_sq = ei.dx * ei.dx + ei.dy * ei.dy;
		ei.tx = he.tangent_x;
		ei.ty = he.tangent_y;
		ei.type = he.type;

		int32_t f0 = he.face;
		int32_t f1 = mesh.half_edges[he.twin].face;
		edge_faces.push_back({f0, f1});
		all_edges.push_back(ei);
	}

	/* Index edges by feature */
	std::vector<std::vector<int32_t>> feat_edges(num_feat);
	for (int32_t ei = 0; ei < (int32_t)all_edges.size(); ei++) {
		auto [f0, f1] = edge_faces[ei];
		if (f0 >= 0 && f0 < num_feat)
			feat_edges[f0].push_back(ei);
		if (f1 >= 0 && f1 < num_feat && f1 != f0)
			feat_edges[f1].push_back(ei);
	}

	/* Expand AABBs by a margin so nearby pixels still see feature edges */
	static const float AABB_MARGIN = 8.0f;

	printf("[energy_flood] %zu edges, %d features, bitangent-guided\n",
	       all_edges.size(), num_feat);

	/* ---- Pre-compute per-pixel bitangent from heightmap gradient ---- */

	auto bt = std::unique_ptr<float[]>(new float[N * 2]);

	for (uint32_t y = 0; y < H; y++) {
		for (uint32_t x = 0; x < W; x++) {
			uint32_t i = y * W + x;

			/* Central differences (clamped at borders) */
			uint32_t xm = x > 0 ? x - 1 : 0;
			uint32_t xp = x < W - 1 ? x + 1 : W - 1;
			uint32_t ym = y > 0 ? y - 1 : 0;
			uint32_t yp = y < H - 1 ? y + 1 : H - 1;

			float gx = (cmd->heightmap[y * W + xp]
			          - cmd->heightmap[y * W + xm])
			         * (xp > xm ? 0.5f : 1.0f);
			float gy = (cmd->heightmap[yp * W + x]
			          - cmd->heightmap[ym * W + x])
			         * (yp > ym ? 0.5f : 1.0f);

			/* Bitangent = 90° CCW rotation of gradient = (-gy, gx) */
			float bx = -gy;
			float by = gx;
			float mag = sqrtf(bx * bx + by * by);
			if (mag > 1e-6f) {
				bt[i*2+0] = bx / mag;
				bt[i*2+1] = by / mag;
			} else {
				bt[i*2+0] = 0.0f;
				bt[i*2+1] = 0.0f;
			}
		}
	}

	/* ---- Allocate outputs ---- */
	cmd->ridge_energy.reset(new float[N * 2]);
	cmd->valley_energy.reset(new float[N * 2]);
	cmd->ridge_dist.reset(new float[N]);
	cmd->valley_dist.reset(new float[N]);

	/* ---- For each pixel, interpolate using bitangent-guided weighting ---- */

	/* Temp: track which edges to check per pixel (from AABB containment) */
	std::vector<uint8_t> edge_active(all_edges.size());

	for (uint32_t y = 0; y < H; y++) {
		float py = (float)y;

		/* Mark edges reachable via features whose AABB contains this row */
		for (auto& a : edge_active) a = 0;
		for (int32_t fi = 0; fi < num_feat; fi++) {
			const AABB& bb = mesh.features[fi].bbox;
			if (py < bb.min_y - AABB_MARGIN || py > bb.max_y + AABB_MARGIN)
				continue;
			for (int32_t ei : feat_edges[fi])
				edge_active[ei] = true;
		}

		for (uint32_t x = 0; x < W; x++) {
			uint32_t i = y * W + x;
			float px = (float)x;

			float btx = bt[i*2+0];
			float bty = bt[i*2+1];
			float bt_mag = sqrtf(btx * btx + bty * bty);

			float r_sx = 0, r_sy = 0;
			float v_sx = 0, v_sy = 0;
			float r_min_d = INFINITY;
			float v_min_d = INFINITY;

			for (int32_t ei = 0; ei < (int32_t)all_edges.size(); ei++) {
				if (!edge_active[ei]) continue;

				const EdgeInfo& e = all_edges[ei];
				float d2 = point_seg_dist_sq(px, py, e);
				float d = sqrtf(d2);
				float dist_w = 1.0f / (d2 + 1.0f);

				float contrib_x, contrib_y, w;

				if (bt_mag > 0.1f) {
					/* Bitangent-guided: alignment selects + resolves sign */
					float alignment = e.tx * btx + e.ty * bty;
					float abs_align = fabsf(alignment);
					w = abs_align * dist_w;
					/* Flip tangent if it opposes the bitangent */
					float sign = (alignment >= 0.0f) ? 1.0f : -1.0f;
					contrib_x = sign * e.tx;
					contrib_y = sign * e.ty;
				} else {
					/* No clear bitangent — use doubled-angle fallback */
					float c2 = e.tx * e.tx - e.ty * e.ty;
					float s2 = 2.0f * e.tx * e.ty;
					/* Store c2/s2 in contrib, handle recovery later */
					contrib_x = c2;
					contrib_y = s2;
					w = dist_w;
				}

				if (e.type == DCEL_EDGE_RIDGE) {
					r_sx += w * contrib_x;
					r_sy += w * contrib_y;
					if (d < r_min_d) r_min_d = d;
				} else {
					v_sx += w * contrib_x;
					v_sy += w * contrib_y;
					if (d < v_min_d) v_min_d = d;
				}
			}

			if (bt_mag > 0.1f) {
				/* Direct sum — already sign-resolved */
				cmd->ridge_energy[i*2+0] = r_sx;
				cmd->ridge_energy[i*2+1] = r_sy;
				cmd->valley_energy[i*2+0] = v_sx;
				cmd->valley_energy[i*2+1] = v_sy;
			} else {
				/* Recover from doubled-angle accumulators */
				float r_mag = sqrtf(r_sx * r_sx + r_sy * r_sy);
				if (r_mag > 1e-8f) {
					float theta = atan2f(r_sy, r_sx) * 0.5f;
					cmd->ridge_energy[i*2+0] = r_mag * cosf(theta);
					cmd->ridge_energy[i*2+1] = r_mag * sinf(theta);
				} else {
					cmd->ridge_energy[i*2+0] = 0.0f;
					cmd->ridge_energy[i*2+1] = 0.0f;
				}
				float v_mag = sqrtf(v_sx * v_sx + v_sy * v_sy);
				if (v_mag > 1e-8f) {
					float theta = atan2f(v_sy, v_sx) * 0.5f;
					cmd->valley_energy[i*2+0] = v_mag * cosf(theta);
					cmd->valley_energy[i*2+1] = v_mag * sinf(theta);
				} else {
					cmd->valley_energy[i*2+0] = 0.0f;
					cmd->valley_energy[i*2+1] = 0.0f;
				}
			}

			cmd->ridge_dist[i] = r_min_d;
			cmd->valley_dist[i] = v_min_d;
		}
	}

	/* Stats */
	float max_rd = 0, max_vd = 0;
	float max_rmag = 0, max_vmag = 0;
	for (uint32_t i = 0; i < N; i++) {
		if (cmd->ridge_dist[i] < INFINITY && cmd->ridge_dist[i] > max_rd)
			max_rd = cmd->ridge_dist[i];
		if (cmd->valley_dist[i] < INFINITY && cmd->valley_dist[i] > max_vd)
			max_vd = cmd->valley_dist[i];
		float rm = cmd->ridge_energy[i*2]*cmd->ridge_energy[i*2]
		         + cmd->ridge_energy[i*2+1]*cmd->ridge_energy[i*2+1];
		float vm = cmd->valley_energy[i*2]*cmd->valley_energy[i*2]
		         + cmd->valley_energy[i*2+1]*cmd->valley_energy[i*2+1];
		if (rm > max_rmag) max_rmag = rm;
		if (vm > max_vmag) max_vmag = vm;
	}

	printf("[energy_flood] Ridge: max_dist=%.0f, max_mag=%.1f\n",
	       max_rd, sqrtf(max_rmag));
	printf("[energy_flood] Valley: max_dist=%.0f, max_mag=%.1f\n",
	       max_vd, sqrtf(max_vmag));

#if DEBUG_IMG_OUT
	{
		auto combined = std::unique_ptr<vec2[]>(new vec2[N]);

		for (uint32_t i = 0; i < N; i++) {
			float rx = cmd->ridge_energy[i*2+0];
			float ry = cmd->ridge_energy[i*2+1];
			float vx = cmd->valley_energy[i*2+0];
			float vy = cmd->valley_energy[i*2+1];

			/* Sum ridge + valley — signs are already resolved */
			float sx = rx + vx;
			float sy = ry + vy;
			float mag = sqrtf(sx * sx + sy * sy);
			if (mag > 1e-6f) {
				combined[i] = vec2(sx / mag, sy / mag);
			} else {
				combined[i] = vec2(0.0f, 0.0f);
			}
		}

		char buf[512];
		PngVec2Cmd png_cmd{};
		png_cmd.path = debug_path("energy_normal.png", buf, sizeof(buf));
		png_cmd.data = combined.get();
		png_cmd.width = W;
		png_cmd.height = H;
		png_cmd.scale = 1.0f;
		png_cmd.z_bias = 0.5f;
		png_ExportVec2(&png_cmd);
		printf("[energy_flood] Wrote: %s\n", buf);
	}
#endif

	return 0;
}
