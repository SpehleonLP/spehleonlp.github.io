#include "mesh_decimate_cmd.h"
#include "skeleton_graph_cmd.h"
#include "dcel_build_cmd.h"
#include <math.h>
#include <stdio.h>
#include <algorithm>
#include <vector>
#include <set>

/* =========================================================================
 * Ramer-Douglas-Peucker on degree-2 chains
 *
 * 1. Build adjacency, compute vertex degree
 * 2. Walk chains of degree-2 vertices between junctions/extrema
 * 3. Apply RDP to each chain polyline
 * 4. Build edges between consecutive surviving vertices per chain
 * 5. Rebuild DCEL
 * ========================================================================= */

static float point_to_line_dist(float px, float py,
                                 float ax, float ay, float bx, float by)
{
	float dx = bx - ax, dy = by - ay;
	float len_sq = dx * dx + dy * dy;
	if (len_sq < 1e-12f)
		return sqrtf((px - ax) * (px - ax) + (py - ay) * (py - ay));
	return fabsf(dy * px - dx * py + bx * ay - by * ax) / sqrtf(len_sq);
}

static void rdp_mark(const std::vector<int32_t>& chain,
                      const std::vector<DCELVertex>& verts,
                      int start, int end, float epsilon,
                      std::vector<bool>& keep)
{
	if (end - start < 2) return;

	float ax = verts[chain[start]].x, ay = verts[chain[start]].y;
	float bx = verts[chain[end]].x,   by = verts[chain[end]].y;

	float dmax = 0.0f;
	int idx = start;

	for (int i = start + 1; i < end; i++) {
		float d = point_to_line_dist(verts[chain[i]].x, verts[chain[i]].y,
		                              ax, ay, bx, by);
		if (d > dmax) {
			dmax = d;
			idx = i;
		}
	}

	if (dmax > epsilon) {
		keep[idx] = true;
		rdp_mark(chain, verts, start, idx, epsilon, keep);
		rdp_mark(chain, verts, idx, end, epsilon, keep);
	}
}

int mesh_decimate_Execute(MeshDecimateCmd* cmd)
{
	if (!cmd || !cmd->mesh) {
		fprintf(stderr, "[mesh_decimate] Error: NULL input\n");
		return -1;
	}

	DCELMesh& mesh = *cmd->mesh;
	float epsilon = cmd->epsilon > 0.0f ? cmd->epsilon : 1.0f;
	int32_t num_verts = (int32_t)mesh.vertices.size();
	int32_t num_he = (int32_t)mesh.half_edges.size();

	/* ---- Build adjacency ---- */
	struct Neighbor {
		int32_t vertex;
		int32_t he_idx;
	};
	std::vector<std::vector<Neighbor>> adj(num_verts);

	for (int32_t i = 0; i < num_he; i++) {
		int32_t orig = mesh.half_edges[i].origin;
		int32_t dest = mesh.half_edges[mesh.half_edges[i].twin].origin;
		adj[orig].push_back({dest, i});
	}

	/* Compute degree */
	std::vector<int> degree(num_verts, 0);
	for (int32_t v = 0; v < num_verts; v++) {
		std::set<int32_t> unique_neighbors;
		for (const auto& n : adj[v])
			unique_neighbors.insert(n.vertex);
		degree[v] = (int)unique_neighbors.size();
	}

	/* ---- Walk chains, apply RDP, build edges ---- */
	std::vector<bool> vertex_survives(num_verts, false);
	std::vector<bool> visited(num_verts, false);

	/* All non-degree-2 vertices always survive */
	for (int32_t v = 0; v < num_verts; v++) {
		if (degree[v] != 2)
			vertex_survives[v] = true;
	}

	/* Collect edges as we process chains */
	std::set<std::pair<int32_t, int32_t>> edge_set;

	struct PendingEdge {
		int32_t v0, v1;
		DCELEdgeType type;
	};
	std::vector<PendingEdge> pending_edges;

	/* Helper: add edges between consecutive survivors in a chain,
	 * using the edge type from the first half-edge of that chain segment */
	auto add_chain_edges = [&](const std::vector<int32_t>& chain,
	                           const std::vector<bool>& keep,
	                           DCELEdgeType chain_type) {
		int32_t last_kept = -1;
		for (size_t i = 0; i < chain.size(); i++) {
			if (!keep[i]) continue;
			if (last_kept >= 0) {
				int32_t va = chain[last_kept];
				int32_t vb = chain[i];
				if (va != vb) {
					auto key = std::make_pair(std::min(va, vb), std::max(va, vb));
					if (!edge_set.count(key)) {
						edge_set.insert(key);
						pending_edges.push_back({va, vb, chain_type});
					}
				}
			}
			last_kept = (int32_t)i;
		}
	};

	int chains_found = 0;

	/* Process chains starting from non-degree-2 vertices */
	for (int32_t start = 0; start < num_verts; start++) {
		if (degree[start] == 2) continue;

		for (const auto& n : adj[start]) {
			if (degree[n.vertex] != 2) continue;
			if (visited[n.vertex]) continue;

			/* Walk the chain */
			std::vector<int32_t> chain;
			chain.push_back(start);

			DCELEdgeType chain_type = mesh.half_edges[n.he_idx].type;
			int32_t cur = n.vertex;
			int32_t prev = start;

			while (degree[cur] == 2 && !visited[cur]) {
				visited[cur] = true;
				chain.push_back(cur);

				int32_t next = -1;
				for (const auto& nb : adj[cur]) {
					if (nb.vertex != prev) {
						next = nb.vertex;
						break;
					}
				}
				if (next < 0) break;
				prev = cur;
				cur = next;
			}

			chain.push_back(cur);

			if (chain.size() < 3) {
				/* Too short for RDP — keep both endpoints, add edge */
				vertex_survives[chain[0]] = true;
				vertex_survives[chain.back()] = true;
				std::vector<bool> keep(chain.size(), false);
				keep[0] = true;
				keep[chain.size() - 1] = true;
				add_chain_edges(chain, keep, chain_type);
				continue;
			}

			/* Apply RDP */
			std::vector<bool> keep(chain.size(), false);
			keep[0] = true;
			keep[chain.size() - 1] = true;
			rdp_mark(chain, mesh.vertices, 0, (int)chain.size() - 1, epsilon, keep);

			for (size_t i = 0; i < chain.size(); i++) {
				if (keep[i])
					vertex_survives[chain[i]] = true;
			}

			add_chain_edges(chain, keep, chain_type);
			chains_found++;
		}
	}

	/* Handle degree-2 cycles */
	for (int32_t v = 0; v < num_verts; v++) {
		if (degree[v] != 2 || visited[v]) continue;

		std::vector<int32_t> chain;
		int32_t cur = v;
		int32_t prev = -1;
		DCELEdgeType chain_type = DCEL_EDGE_RIDGE;

		do {
			visited[cur] = true;
			chain.push_back(cur);

			int32_t next = -1;
			for (const auto& nb : adj[cur]) {
				if (nb.vertex != prev) {
					next = nb.vertex;
					chain_type = mesh.half_edges[nb.he_idx].type;
					break;
				}
			}
			prev = cur;
			cur = next;
		} while (cur != v && cur >= 0);

		if (chain.size() < 4) {
			for (int32_t vi : chain)
				vertex_survives[vi] = true;
			/* Keep all edges in the cycle */
			chain.push_back(chain[0]);
			std::vector<bool> keep(chain.size(), true);
			add_chain_edges(chain, keep, chain_type);
			continue;
		}

		/* Break cycle: duplicate first vertex */
		chain.push_back(chain[0]);

		std::vector<bool> keep(chain.size(), false);
		keep[0] = true;
		keep[chain.size() - 1] = true;
		rdp_mark(chain, mesh.vertices, 0, (int)chain.size() - 1, epsilon, keep);

		for (size_t i = 0; i < chain.size() - 1; i++) {
			if (keep[i])
				vertex_survives[chain[i]] = true;
		}

		add_chain_edges(chain, keep, chain_type);
		chains_found++;
	}

	/* Also add direct edges between non-degree-2 vertices (not part of chains) */
	for (int32_t i = 0; i < num_he; i++) {
		int32_t va = mesh.half_edges[i].origin;
		int32_t vb = mesh.half_edges[mesh.half_edges[i].twin].origin;

		if (degree[va] != 2 && degree[vb] != 2) {
			auto key = std::make_pair(std::min(va, vb), std::max(va, vb));
			if (!edge_set.count(key)) {
				edge_set.insert(key);
				pending_edges.push_back({va, vb, mesh.half_edges[i].type});
			}
		}
	}

	/* ---- Build new vertex array ---- */
	std::vector<int32_t> v_remap(num_verts, -1);
	std::vector<DCELVertex> new_verts;

	for (int32_t v = 0; v < num_verts; v++) {
		if (vertex_survives[v]) {
			v_remap[v] = (int32_t)new_verts.size();
			new_verts.push_back(mesh.vertices[v]);
		}
	}

	/* Remap edges */
	std::vector<UndirectedEdge> new_edges;
	for (const auto& pe : pending_edges) {
		int32_t na = v_remap[pe.v0];
		int32_t nb = v_remap[pe.v1];
		if (na < 0 || nb < 0 || na == nb) continue;

		UndirectedEdge e;
		e.v0 = std::min(na, nb);
		e.v1 = std::max(na, nb);
		e.type = pe.type;
		new_edges.push_back(std::move(e));
	}

	int verts_removed = num_verts - (int)new_verts.size();

	printf("[mesh_decimate] RDP epsilon=%.1f: %d chains, %d vertices removed\n",
	       epsilon, chains_found, verts_removed);

	/* Rebuild DCEL */
	DCELBuildCmd rebuild{};
	rebuild.vertices = std::move(new_verts);
	rebuild.edges = &new_edges;
	if (dcel_build_Execute(&rebuild) != 0) return -1;

	*cmd->mesh = std::move(rebuild.mesh);

	printf("[mesh_decimate] Result: %zu vertices, %zu half-edges\n",
	       cmd->mesh->vertices.size(), cmd->mesh->half_edges.size());

	return 0;
}
