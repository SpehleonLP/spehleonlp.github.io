#include "edge_dijkstra_cmd.h"
#include "debug_png.h"
#include "../debug_output.h"
#include <math.h>
#include <stdio.h>
#include <queue>
#include <vector>

/* =========================================================================
 * Edge Dijkstra: propagate signed direction from mesh edges to all pixels
 *
 * 1. Rasterize mesh edges (Bresenham) onto pixel grid as seeds
 * 2. Uphill pass:  Dijkstra from seeds, cost favors going downhill
 *                  → each pixel learns "which edge I'd reach going uphill"
 * 3. Downhill pass: Dijkstra from seeds, cost favors going uphill
 *                  → each pixel learns "which edge I'd reach going downhill"
 *
 * In flat regions, both passes degrade to nearest-edge-by-distance.
 * Height bias controls how strongly the gradient guides the expansion.
 * ========================================================================= */

static const float SQRT2 = 1.41421356f;

/* 8-connected neighbor offsets: dx, dy, base_cost */
static const int DX[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
static const int DY[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
static const float DCOST[8] = {
	SQRT2, 1.0f, SQRT2,
	1.0f,        1.0f,
	SQRT2, 1.0f, SQRT2
};

struct SeedPixel {
	float tx, ty;       /* signed tangent direction */
	int32_t half_edge;  /* half-edge index into mesh (or -1) */
	DCELEdgeType type;
	uint8_t terminal;   /* 1 if edge has an endpoint vertex */
};

/* Bresenham rasterization of one edge segment */
static void rasterize_edge(
	int x0, int y0, int x1, int y1,
	float tx, float ty, int32_t half_edge,
	DCELEdgeType type, uint8_t terminal,
	SeedPixel* seeds, uint8_t* is_seed,
	uint32_t W, uint32_t H)
{
	int adx = abs(x1 - x0), ady = abs(y1 - y0);
	int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
	int err = adx - ady;

	for (;;) {
		if ((uint32_t)x0 < W && (uint32_t)y0 < H) {
			uint32_t pi = (uint32_t)y0 * W + (uint32_t)x0;
			if (!is_seed[pi]) {
				seeds[pi].tx = tx;
				seeds[pi].ty = ty;
				seeds[pi].half_edge = half_edge;
				seeds[pi].type = type;
				seeds[pi].terminal = terminal;
				is_seed[pi] = 1;
			}
		}
		if (x0 == x1 && y0 == y1) break;
		int e2 = 2 * err;
		if (e2 > -ady) { err -= ady; x0 += sx; }
		if (e2 < adx)  { err += adx; y0 += sy; }
	}
}

struct PQEntry {
	float cost;
	uint32_t idx;
	bool operator>(const PQEntry& o) const { return cost > o.cost; }
};

/* Pre-compute per-pixel gradient direction (unit vector, or zero if flat) */
static void compute_gradients(
	const float* heightmap, uint32_t W, uint32_t H,
	float* grad)  /* W*H*2 floats */
{
	for (uint32_t y = 0; y < H; y++) {
		for (uint32_t x = 0; x < W; x++) {
			uint32_t i = y * W + x;
			uint32_t xm = x > 0 ? x - 1 : 0;
			uint32_t xp = x < W - 1 ? x + 1 : W - 1;
			uint32_t ym = y > 0 ? y - 1 : 0;
			uint32_t yp = y < H - 1 ? y + 1 : H - 1;

			float gx = (heightmap[y * W + xp] - heightmap[y * W + xm])
			         * (xp > xm ? 0.5f : 1.0f);
			float gy = (heightmap[yp * W + x] - heightmap[ym * W + x])
			         * (yp > ym ? 0.5f : 1.0f);

			float mag = sqrtf(gx * gx + gy * gy);
			if (mag > 1e-6f) {
				grad[i*2+0] = gx / mag;
				grad[i*2+1] = gy / mag;
			} else {
				grad[i*2+0] = 0.0f;
				grad[i*2+1] = 0.0f;
			}
		}
	}
}

/* Run one Dijkstra pass from seed pixels.
 * uphill=true:  edges flood downhill (pixel learns "edge I'd reach going uphill")
 * uphill=false: edges flood uphill   (pixel learns "edge I'd reach going downhill")
 *
 * Cost = base_dist * (1 + height_bias*height_penalty + dir_bias*(1 - gradient_alignment))
 * This forces paths to follow gradient flow lines, so pixels reach edges
 * whose tangents are naturally aligned with the pixel's contour direction. */
static void dijkstra_pass(
	const float* heightmap, const float* grad,
	const SeedPixel* seeds, const uint8_t* is_seed,
	uint32_t W, uint32_t H,
	float height_bias, float dir_bias, float tang_bias,
	float terminal_cost, bool uphill,
	float* out_dir,      /* W*H*2 floats */
	float* out_cost,     /* W*H floats */
	float* out_seed_xy,  /* W*H*2 floats */
	int32_t* out_edge_id, /* W*H half-edge indices */
	uint8_t* out_term)   /* W*H */
{
	uint32_t N = W * H;

	for (uint32_t i = 0; i < N; i++) {
		out_cost[i] = INFINITY;
		out_dir[i*2+0] = 0.0f;
		out_dir[i*2+1] = 0.0f;
		out_seed_xy[i*2+0] = 0.0f;
		out_seed_xy[i*2+1] = 0.0f;
		out_edge_id[i] = -1;
		out_term[i] = 0;
	}

	std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<PQEntry>> pq;

	for (uint32_t i = 0; i < N; i++) {
		if (is_seed[i]) {
			float init_cost = seeds[i].terminal ? terminal_cost : 0.0f;
			out_cost[i] = init_cost;
			out_dir[i*2+0] = seeds[i].tx;
			out_dir[i*2+1] = seeds[i].ty;
			out_seed_xy[i*2+0] = (float)(i % W);
			out_seed_xy[i*2+1] = (float)(i / W);
			out_edge_id[i] = seeds[i].half_edge;
			out_term[i] = seeds[i].terminal;
			pq.push({init_cost, i});
		}
	}

	/* Step direction unit vectors (pre-normalized) */
	static const float SDX[8] = {-SQRT2/2, 0, SQRT2/2, -1, 1, -SQRT2/2, 0, SQRT2/2};
	static const float SDY[8] = {-SQRT2/2, -1, -SQRT2/2, 0, 0, SQRT2/2, 1, SQRT2/2};

	while (!pq.empty()) {
		auto [cost, ci] = pq.top();
		pq.pop();

		if (cost > out_cost[ci]) continue;

		int cx = (int)(ci % W);
		int cy = (int)(ci / W);
		float h_cur = heightmap[ci];

		/* Gradient at current pixel (for direction penalty) */
		float gx = grad[ci*2+0];
		float gy = grad[ci*2+1];
		float g_mag = sqrtf(gx*gx + gy*gy);

		for (int d = 0; d < 8; d++) {
			int nx = cx + DX[d];
			int ny = cy + DY[d];
			if (nx < 0 || nx >= (int)W || ny < 0 || ny >= (int)H)
				continue;

			uint32_t ni = (uint32_t)ny * W + (uint32_t)nx;
			float h_nb = heightmap[ni];
			float dh = h_nb - h_cur;

			/* Height penalty */
			float h_penalty = uphill ? (dh > 0 ? dh : 0.0f)
			                         : (dh < 0 ? -dh : 0.0f);

			/* Direction penalty: how much this step deviates from gradient.
			 * We want to follow the gradient (uphill) or anti-gradient (downhill).
			 * alignment = |dot(step, ±gradient)|; penalty = 1 - alignment */
			float d_penalty = 0.0f;
			if (g_mag > 0.1f) {
				float dot = SDX[d] * gx + SDY[d] * gy;
				/* uphill pass floods downhill: want steps AGAINST gradient */
				/* downhill pass floods uphill: want steps ALONG gradient */
				float alignment = uphill ? -dot : dot;
				/* Clamp to [0,1]: alignment<0 means going the wrong way */
				if (alignment < 0.0f) alignment = 0.0f;
				d_penalty = 1.0f - alignment;
			}

			/* Tangent alignment penalty: discourage spreading along the edge */
			float t_penalty = 0.0f;
			float tx = out_dir[ci*2+0], ty = out_dir[ci*2+1];
			float t_mag = sqrtf(tx*tx + ty*ty);
			if (t_mag > 1e-6f) {
				t_penalty = fabsf(SDX[d] * tx + SDY[d] * ty) / t_mag;
			}

			float new_cost = cost + DCOST[d]
				* (1.0f + height_bias * h_penalty + dir_bias * d_penalty + tang_bias * t_penalty);

			if (new_cost < out_cost[ni]) {
				out_cost[ni] = new_cost;
				out_dir[ni*2+0] = out_dir[ci*2+0];
				out_dir[ni*2+1] = out_dir[ci*2+1];
				out_seed_xy[ni*2+0] = out_seed_xy[ci*2+0];
				out_seed_xy[ni*2+1] = out_seed_xy[ci*2+1];
				out_edge_id[ni] = out_edge_id[ci];
				out_term[ni] = out_term[ci];
				pq.push({new_cost, ni});
			}
		}
	}
}

int edge_dijkstra_Execute(EdgeDijkstraCmd* cmd)
{
	if (!cmd || !cmd->mesh || !cmd->heightmap) {
		fprintf(stderr, "[edge_dijkstra] Error: NULL input\n");
		return -1;
	}

	const DCELMesh& mesh = *cmd->mesh;
	uint32_t W = cmd->W, H = cmd->H;
	uint32_t N = W * H;
	float bias = cmd->height_bias > 0.0f ? cmd->height_bias : 50.0f;

	/* ---- Rasterize mesh edges as seed pixels ---- */

	auto seeds = std::unique_ptr<SeedPixel[]>(new SeedPixel[N]);
	auto is_seed = std::unique_ptr<uint8_t[]>(new uint8_t[N]());

	int32_t num_he = (int32_t)mesh.half_edges.size();
	int seed_count = 0;

	for (int32_t hi = 0; hi < num_he; hi++) {
		const DCELHalfEdge& he = mesh.half_edges[hi];
		if (he.twin < hi) continue;  /* each undirected edge once */
		const DCELHalfEdge& tw = mesh.half_edges[he.twin];

		int32_t dest = tw.origin;
		int x0 = (int)roundf(mesh.vertices[he.origin].x);
		int y0 = (int)roundf(mesh.vertices[he.origin].y);
		int x1 = (int)roundf(mesh.vertices[dest].x);
		int y1 = (int)roundf(mesh.vertices[dest].y);

		uint8_t term = (mesh.vertices[he.origin].type == DCEL_VERTEX_ENDPOINT
		             || mesh.vertices[dest].type == DCEL_VERTEX_ENDPOINT) ? 1 : 0;

		/* Use the half-edge with higher energy as the canonical direction.
		 * Energy propagates uphill along chains, so the energy-bearing half-edge
		 * has consistent sign along the chain. Storing its index means the
		 * Catmull-Rom interpolation later will also evaluate in the canonical
		 * direction (origin→dest of the energy-bearing half-edge). */
		int32_t canon_hi = (he.energy >= tw.energy) ? hi : he.twin;
		const DCELHalfEdge& canon = mesh.half_edges[canon_hi];

		rasterize_edge(x0, y0, x1, y1,
		               canon.tangent_x, canon.tangent_y, canon_hi,
		               he.type, term,
		               seeds.get(), is_seed.get(), W, H);
	}

	for (uint32_t i = 0; i < N; i++)
		if (is_seed[i]) seed_count++;

	float dir_bias = 20.0f;       /* gradient alignment penalty */
	float tang_bias = 10.0f;      /* tangent alignment penalty */
	float terminal_cost = 3.0f;   /* initial cost for terminal seeds */

	printf("[edge_dijkstra] %d seed pixels from %d edges, h_bias=%.0f, d_bias=%.1f, t_bias=%.1f, term_cost=%.1f\n",
	       seed_count, num_he / 2, bias, dir_bias, tang_bias, terminal_cost);

	/* ---- Pre-compute gradient field ---- */
	auto grad = std::unique_ptr<float[]>(new float[N * 2]);
	compute_gradients(cmd->heightmap, W, H, grad.get());

	/* ---- Allocate outputs ---- */
	cmd->uphill_dir.reset(new float[N * 2]);
	cmd->uphill_cost.reset(new float[N]);
	cmd->uphill_seed_xy.reset(new float[N * 2]);
	cmd->uphill_edge_id.reset(new int32_t[N]);
	cmd->uphill_terminal.reset(new uint8_t[N]);
	cmd->downhill_dir.reset(new float[N * 2]);
	cmd->downhill_cost.reset(new float[N]);
	cmd->downhill_seed_xy.reset(new float[N * 2]);
	cmd->downhill_edge_id.reset(new int32_t[N]);
	cmd->downhill_terminal.reset(new uint8_t[N]);

	/* ---- Uphill pass: edges flood downhill ---- */
	dijkstra_pass(cmd->heightmap, grad.get(), seeds.get(), is_seed.get(),
	              W, H, bias, dir_bias, tang_bias, terminal_cost, true,
	              cmd->uphill_dir.get(), cmd->uphill_cost.get(),
	              cmd->uphill_seed_xy.get(), cmd->uphill_edge_id.get(),
	              cmd->uphill_terminal.get());

	/* ---- Downhill pass: edges flood uphill ---- */
	dijkstra_pass(cmd->heightmap, grad.get(), seeds.get(), is_seed.get(),
	              W, H, bias, dir_bias, tang_bias, terminal_cost, false,
	              cmd->downhill_dir.get(), cmd->downhill_cost.get(),
	              cmd->downhill_seed_xy.get(), cmd->downhill_edge_id.get(),
	              cmd->downhill_terminal.get());

	/* Stats */
	float max_up = 0, max_dn = 0;
	int up_reached = 0, dn_reached = 0;
	for (uint32_t i = 0; i < N; i++) {
		if (cmd->uphill_cost[i] < INFINITY) {
			up_reached++;
			if (cmd->uphill_cost[i] > max_up) max_up = cmd->uphill_cost[i];
		}
		if (cmd->downhill_cost[i] < INFINITY) {
			dn_reached++;
			if (cmd->downhill_cost[i] > max_dn) max_dn = cmd->downhill_cost[i];
		}
	}

	printf("[edge_dijkstra] Uphill:   %d/%d reached, max_cost=%.1f\n",
	       up_reached, N, max_up);
	printf("[edge_dijkstra] Downhill: %d/%d reached, max_cost=%.1f\n",
	       dn_reached, N, max_dn);

#if DEBUG_IMG_OUT
	{
		auto up_viz = std::unique_ptr<vec2[]>(new vec2[N]);
		auto dn_viz = std::unique_ptr<vec2[]>(new vec2[N]);

		for (uint32_t i = 0; i < N; i++) {
			up_viz[i] = vec2(cmd->uphill_dir[i*2+0], cmd->uphill_dir[i*2+1]);
			dn_viz[i] = vec2(cmd->downhill_dir[i*2+0], cmd->downhill_dir[i*2+1]);
		}

		char buf[512];
		PngVec2Cmd png{};
		png.width = W;
		png.height = H;
		png.scale = 1.0f;
		png.z_bias = 0.5f;

		png.path = debug_path("dijkstra_uphill.png", buf, sizeof(buf));
		png.data = up_viz.get();
		png_ExportVec2(&png);
		printf("[edge_dijkstra] Wrote: %s\n", buf);

		png.path = debug_path("dijkstra_downhill.png", buf, sizeof(buf));
		png.data = dn_viz.get();
		png_ExportVec2(&png);
		printf("[edge_dijkstra] Wrote: %s\n", buf);
	}
#endif

	return 0;
}
