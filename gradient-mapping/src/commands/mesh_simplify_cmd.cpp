#include "mesh_simplify_cmd.h"
#include "skeleton_graph_cmd.h"
#include "dcel_build_cmd.h"
#include <math.h>
#include <stdio.h>
#include <algorithm>
#include <vector>
#include <set>

/* =========================================================================
 * Union-Find with path compression and union by rank
 * ========================================================================= */

struct UnionFind {
	std::vector<int32_t> parent, rnk;

	UnionFind(int n) : parent(n), rnk(n, 0) {
		for (int i = 0; i < n; i++) parent[i] = i;
	}

	int32_t find(int32_t x) {
		while (parent[x] != x) {
			parent[x] = parent[parent[x]];
			x = parent[x];
		}
		return x;
	}

	void unite(int32_t a, int32_t b) {
		a = find(a); b = find(b);
		if (a == b) return;
		if (rnk[a] < rnk[b]) std::swap(a, b);
		parent[b] = a;
		if (rnk[a] == rnk[b]) rnk[a]++;
	}
};

/* =========================================================================
 * mesh_simplify_Execute
 *
 * 1. Discover face cycles, find tiny ones (|area| < min_area)
 * 2. Union-find: merge all vertices of each tiny face to one point
 * 3. Compute centroid position for each merged group
 * 4. Rebuild edge list: remap vertices, drop self-loops, dedup parallels
 * 5. Rebuild DCEL via dcel_build_Execute
 * ========================================================================= */

int mesh_simplify_Execute(MeshSimplifyCmd* cmd)
{
	if (!cmd || !cmd->mesh) {
		fprintf(stderr, "[mesh_simplify] Error: NULL input\n");
		return -1;
	}

	DCELMesh& mesh = *cmd->mesh;
	float min_area = cmd->min_area > 0.0f ? cmd->min_area : 4.0f;
	int32_t num_verts = (int32_t)mesh.vertices.size();
	int32_t num_he = (int32_t)mesh.half_edges.size();

	/* ---- Step 1: Discover face cycles ---- */
	for (auto& he : mesh.half_edges)
		he.face = -1;

	struct FaceInfo {
		std::vector<int32_t> verts;   /* vertex indices in this face */
		float area_signed;
	};
	std::vector<FaceInfo> faces;
	float most_negative_area = 0.0f;
	int32_t infinite_face_idx = -1;

	for (int32_t i = 0; i < num_he; i++) {
		if (mesh.half_edges[i].face != -1) continue;

		int32_t face_idx = (int32_t)faces.size();
		FaceInfo fi;
		fi.area_signed = 0.0f;

		int32_t first = i;
		int32_t cur = first;
		int count = 0;
		bool valid = true;

		do {
			mesh.half_edges[cur].face = face_idx;
			count++;
			if (count > num_he) { valid = false; break; }

			int32_t orig = mesh.half_edges[cur].origin;
			fi.verts.push_back(orig);

			float x0 = mesh.vertices[orig].x;
			float y0 = mesh.vertices[orig].y;
			int32_t dest = mesh.half_edges[mesh.half_edges[cur].twin].origin;
			float x1 = mesh.vertices[dest].x;
			float y1 = mesh.vertices[dest].y;
			fi.area_signed += (x0 * y1 - x1 * y0);

			cur = mesh.half_edges[cur].next;
		} while (cur != first && cur >= 0);

		if (!valid || cur != first) {
			faces.push_back(std::move(fi));
			continue;
		}
		fi.area_signed *= 0.5f;

		if (fi.area_signed < most_negative_area) {
			most_negative_area = fi.area_signed;
			infinite_face_idx = face_idx;
		}

		faces.push_back(std::move(fi));
	}

	/* ---- Step 2: Union-find merge vertices of tiny faces ---- */
	UnionFind uf(num_verts);
	int faces_collapsed = 0;

	for (int32_t fi = 0; fi < (int32_t)faces.size(); fi++) {
		if (fi == infinite_face_idx) continue;

		const FaceInfo& f = faces[fi];
		if (f.verts.size() < 3) continue;
		if (fabsf(f.area_signed) >= min_area) continue;

		/* Unite all vertices of this face */
		for (size_t j = 1; j < f.verts.size(); j++) {
			uf.unite(f.verts[0], f.verts[j]);
		}
		faces_collapsed++;
	}

	if (faces_collapsed == 0) {
		printf("[mesh_simplify] No tiny faces (area < %.1f)\n", min_area);
		return 0;
	}

	/* ---- Step 3: Compute centroid for each merged group ---- */
	/* Accumulate positions per group representative */
	std::vector<float> sum_x(num_verts, 0), sum_y(num_verts, 0);
	std::vector<float> sum_h(num_verts, 0), sum_d(num_verts, 0);
	std::vector<int> group_count(num_verts, 0);
	/* Track the "best" vertex type for each group (prefer non-PATH) */
	std::vector<DCELVertexType> group_type(num_verts, DCEL_VERTEX_PATH);

	for (int32_t v = 0; v < num_verts; v++) {
		int32_t rep = uf.find(v);
		sum_x[rep] += mesh.vertices[v].x;
		sum_y[rep] += mesh.vertices[v].y;
		sum_h[rep] += mesh.vertices[v].height;
		sum_d[rep] += mesh.vertices[v].divergence;
		group_count[rep]++;

		/* Keep the most "important" vertex type in the group */
		DCELVertexType vt = mesh.vertices[v].type;
		if (vt < group_type[rep])  /* lower enum value = more important */
			group_type[rep] = vt;
	}

	/* Build new vertex array: one vertex per unique group representative */
	std::vector<int32_t> v_remap(num_verts, -1);
	std::vector<DCELVertex> new_verts;

	for (int32_t v = 0; v < num_verts; v++) {
		int32_t rep = uf.find(v);
		if (v_remap[rep] >= 0) {
			/* Already created — just map this vertex to same new index */
			v_remap[v] = v_remap[rep];
			continue;
		}

		int n = group_count[rep];
		DCELVertex nv{};
		nv.x = sum_x[rep] / (float)n;
		nv.y = sum_y[rep] / (float)n;
		nv.height = sum_h[rep] / (float)n;
		nv.divergence = sum_d[rep] / (float)n;
		nv.type = group_type[rep];
		nv.edge = -1;

		v_remap[rep] = (int32_t)new_verts.size();
		v_remap[v] = v_remap[rep];
		new_verts.push_back(nv);
	}

	/* Fill in remaining remap entries */
	for (int32_t v = 0; v < num_verts; v++) {
		if (v_remap[v] < 0) {
			v_remap[v] = v_remap[uf.find(v)];
		}
	}

	/* ---- Step 4: Rebuild edge list (skip self-loops, dedup parallels) ---- */
	std::set<std::pair<int32_t, int32_t>> edge_set;
	std::vector<UndirectedEdge> new_edges;

	for (int32_t i = 0; i < num_he; i += 2) {
		/* Process each edge pair once (half-edges created in fwd/twin pairs) */
		if (i + 1 >= num_he) break;

		int32_t orig_a = mesh.half_edges[i].origin;
		int32_t orig_b = mesh.half_edges[i + 1].origin;

		int32_t new_a = v_remap[orig_a];
		int32_t new_b = v_remap[orig_b];

		/* Skip self-loops */
		if (new_a == new_b) continue;

		auto key = std::make_pair(std::min(new_a, new_b), std::max(new_a, new_b));
		if (edge_set.count(key)) continue;
		edge_set.insert(key);

		UndirectedEdge e;
		e.v0 = key.first;
		e.v1 = key.second;
		e.type = mesh.half_edges[i].type;
		new_edges.push_back(std::move(e));
	}

	int merged_verts = num_verts - (int)new_verts.size();
	int removed_edges = (num_he / 2) - (int)new_edges.size();

	printf("[mesh_simplify] Collapsed %d tiny faces (area < %.1f)\n",
	       faces_collapsed, min_area);
	printf("[mesh_simplify] Merged %d vertices, removed %d edges\n",
	       merged_verts, removed_edges);

	/* ---- Step 5: Rebuild DCEL ---- */
	DCELBuildCmd rebuild{};
	rebuild.vertices = std::move(new_verts);
	rebuild.edges = &new_edges;
	if (dcel_build_Execute(&rebuild) != 0) return -1;

	*cmd->mesh = std::move(rebuild.mesh);

	printf("[mesh_simplify] Result: %zu vertices, %zu half-edges\n",
	       cmd->mesh->vertices.size(), cmd->mesh->half_edges.size());

	return 0;
}
