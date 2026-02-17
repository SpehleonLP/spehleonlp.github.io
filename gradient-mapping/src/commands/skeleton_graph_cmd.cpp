#include "skeleton_graph_cmd.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <algorithm>
#include <queue>
#include <map>
#include <set>
#include <unordered_map>
#include <memory>

/* =========================================================================
 * Multi-source Dijkstra for edge connectivity
 *
 * Seeds all vertices at cost 0 and expands through the walkable mask.
 * Cost per step = euclidean_dist / (|divergence| + epsilon), so strong
 * divergence is cheap to traverse and weak divergence is expensive.
 * When two wavefronts from different sources meet, an edge is recorded.
 * ========================================================================= */

struct DijkEntry {
	float cost;
	uint32_t pixel;
	bool operator>(const DijkEntry& o) const { return cost > o.cost; }
};

struct EdgeCandidate {
	float cost;
	uint32_t meet_a;  /* pixel on source A side */
	uint32_t meet_b;  /* pixel on source B side */
};

/* 8-connected neighbor offsets */
static const int dx8[] = {-1, 0, 1, -1, 1, -1, 0, 1};
static const int dy8[] = {-1, -1, -1, 0, 0, 1, 1, 1};
static const float dist8[] = {1.414f, 1.0f, 1.414f, 1.0f, 1.0f, 1.414f, 1.0f, 1.414f};

int skeleton_graph_Execute(SkeletonGraphCmd* cmd)
{
	if (!cmd || !cmd->divergence || !cmd->walkable || !cmd->vertices) {
		fprintf(stderr, "[skeleton_graph] Error: NULL input\n");
		return -1;
	}

	uint32_t W = cmd->W;
	uint32_t H = cmd->H;
	uint32_t N = W * H;
	const std::vector<DCELVertex>& verts = *cmd->vertices;

	cmd->edges.clear();

	auto source = std::unique_ptr<int32_t[]>(new int32_t[N]);
	auto cost   = std::unique_ptr<float[]>(new float[N]);
	auto pred   = std::unique_ptr<int32_t[]>(new int32_t[N]);

	for (uint32_t i = 0; i < N; i++) {
		source[i] = -1;
		cost[i] = INFINITY;
		pred[i] = -1;
	}

	std::priority_queue<DijkEntry, std::vector<DijkEntry>, std::greater<DijkEntry>> pq;

	/* Seed all vertices */
	for (int v = 0; v < (int)verts.size(); v++) {
		uint32_t idx = (uint32_t)verts[v].y * W + (uint32_t)verts[v].x;
		source[idx] = v;
		cost[idx] = 0.0f;
		pq.push({0.0f, idx});
	}

	/* Edge candidates keyed by (min_v, max_v) → cheapest meeting point */
	std::map<std::pair<int,int>, EdgeCandidate> edge_map;

	while (!pq.empty()) {
		DijkEntry cur = pq.top();
		pq.pop();

		if (cur.cost > cost[cur.pixel]) continue; /* stale */

		int src = source[cur.pixel];
		uint32_t px = cur.pixel % W;
		uint32_t py = cur.pixel / W;

		for (int d = 0; d < 8; d++) {
			int nx = (int)px + dx8[d];
			int ny = (int)py + dy8[d];
			if (nx < 0 || nx >= (int)W || ny < 0 || ny >= (int)H) continue;

			uint32_t nidx = (uint32_t)ny * W + (uint32_t)nx;

			/* Allow expansion into walkable pixels or vertex pixels */
			if (!cmd->walkable[nidx] && source[nidx] < 0) continue;

			float step = dist8[d] / (fabsf(cmd->divergence[nidx]) + 0.001f);
			float new_cost = cur.cost + step;

			if (source[nidx] >= 0 && source[nidx] != src) {
				/* Wavefront collision: record edge between two sources */
				float total = cur.cost + cost[nidx] + step;
				auto key = std::make_pair(std::min(src, source[nidx]),
				                          std::max(src, source[nidx]));
				auto it = edge_map.find(key);
				if (it == edge_map.end() || total < it->second.cost) {
					edge_map[key] = {total, cur.pixel, nidx};
				}
				continue; /* don't overwrite other source's territory */
			}

			if (new_cost < cost[nidx]) {
				cost[nidx] = new_cost;
				source[nidx] = src;
				pred[nidx] = (int32_t)cur.pixel;
				pq.push({new_cost, nidx});
			}
		}
	}

	printf("[skeleton_graph] Dijkstra found %zu edge candidates\n", edge_map.size());

	/* Reconstruct pixel paths for each edge candidate */
	struct InternalEdge {
		DCELEdgeType type;
		std::vector<uint32_t> pixels;
	};
	std::vector<InternalEdge> internal_edges;

	for (auto& [key, ec] : edge_map) {
		/* Backtrack from meet_a to v0 */
		std::vector<uint32_t> path_a;
		for (int32_t p = (int32_t)ec.meet_a; p >= 0; p = pred[p])
			path_a.push_back((uint32_t)p);
		std::reverse(path_a.begin(), path_a.end());

		/* Backtrack from meet_b to v1 */
		for (int32_t p = (int32_t)ec.meet_b; p >= 0; p = pred[p])
			path_a.push_back((uint32_t)p);

		/* Classify by majority divergence sign */
		int ridge_count = 0, valley_count = 0;
		for (auto px : path_a) {
			if (cmd->divergence[px] < 0) ridge_count++;
			else if (cmd->divergence[px] > 0) valley_count++;
		}

		InternalEdge ie;
		ie.type = (ridge_count >= valley_count) ? DCEL_EDGE_RIDGE : DCEL_EDGE_VALLEY;
		ie.pixels = std::move(path_a);
		internal_edges.push_back(std::move(ie));
	}

	/* =================================================================
	 * Expand pixel paths into mesh vertices + pixel-step edges
	 *
	 * Each pixel along a path becomes a vertex. Consecutive pixels
	 * become edges. Shared pixels are deduplicated (natural junctions).
	 * ================================================================= */

	/* Start with a copy of the original skeleton vertices */
	cmd->expanded_vertices = *cmd->vertices;

	/* Map pixel index → vertex index */
	std::unordered_map<uint32_t, int32_t> px_to_vert;
	for (int v = 0; v < (int)cmd->expanded_vertices.size(); v++) {
		uint32_t idx = (uint32_t)cmd->expanded_vertices[v].y * W + (uint32_t)cmd->expanded_vertices[v].x;
		px_to_vert[idx] = v;
	}

	/* Edge dedup: (min_v, max_v) */
	std::set<std::pair<int32_t, int32_t>> edge_set;
	cmd->edges.clear();

	auto find_or_create_vertex = [&](uint32_t px) -> int32_t {
		auto it = px_to_vert.find(px);
		if (it != px_to_vert.end()) return it->second;

		DCELVertex v{};
		v.x = (float)(px % W);
		v.y = (float)(px / W);
		v.height = cmd->heightmap[px];
		v.divergence = cmd->divergence[px];
		v.type = DCEL_VERTEX_PATH;
		v.edge = -1;
		int32_t idx = (int32_t)cmd->expanded_vertices.size();
		cmd->expanded_vertices.push_back(v);
		px_to_vert[px] = idx;
		return idx;
	};

	for (const auto& ie : internal_edges) {
		for (size_t i = 0; i + 1 < ie.pixels.size(); i++) {
			int32_t va = find_or_create_vertex(ie.pixels[i]);
			int32_t vb = find_or_create_vertex(ie.pixels[i + 1]);
			if (va == vb) continue;

			auto ekey = std::make_pair(std::min(va, vb), std::max(va, vb));
			if (edge_set.count(ekey)) continue;
			edge_set.insert(ekey);

			UndirectedEdge e;
			e.v0 = ekey.first;
			e.v1 = ekey.second;
			e.type = ie.type;
			cmd->edges.push_back(std::move(e));
		}
	}

	printf("[skeleton_graph] Expanded: %zu vertices (%zu original + %zu path), %zu edges\n",
	       cmd->expanded_vertices.size(), cmd->vertices->size(),
	       cmd->expanded_vertices.size() - cmd->vertices->size(),
	       cmd->edges.size());

	return 0;
}
