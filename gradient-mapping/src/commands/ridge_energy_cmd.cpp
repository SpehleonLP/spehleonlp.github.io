#include "ridge_energy_cmd.h"
#include "debug_png.h"
#include "../debug_output.h"
#include <math.h>
#include <stdio.h>
#include <algorithm>
#include <memory>
#include <vector>

/* stb_image_write declaration (implementation in debug_png.cpp) */
extern "C" int stbi_write_png(const char* filename, int w, int h, int comp,
                               const void* data, int stride_in_bytes);

/* =========================================================================
 * Confidence-based energy propagation
 *
 * Phase A: Seed + propagate along ridge chains (DAG, ascending height)
 *          confidence = accumulated_length * Π(dot_products)
 * Phase B: Seed + propagate along valley chains (same, lower factor)
 * Phase C: Ridge→Valley transfer via face adjacency
 * Phase D: Re-propagate valley chains with transferred energy
 * ========================================================================= */

static const float VALLEY_FACTOR = 0.3f;
static const float ALIGN_THRESH  = 0.3f;

/* -------------------------------------------------------------------------
 * Debug: render mesh edges colored by energy onto heightmap background
 * Ridge: dark red → bright yellow
 * Valley: dark blue → cyan
 * Zero-energy edges: dim gray
 * ------------------------------------------------------------------------- */
#if DEBUG_IMG_OUT
static void render_energy_debug(
	const char* path,
	const DCELMesh& mesh,
	const float* heightmap,
	uint32_t W, uint32_t H)
{
	uint32_t N = W * H;
	auto rgb = std::unique_ptr<uint8_t[]>(new uint8_t[N * 3]);

	/* Background: heightmap as dark gray */
	for (uint32_t i = 0; i < N; i++) {
		uint8_t gray = (uint8_t)(heightmap[i] * 100.0f);
		rgb[i*3+0] = gray;
		rgb[i*3+1] = gray;
		rgb[i*3+2] = gray;
	}

	/* Find max energy per type for normalization */
	float max_ridge_e = 1.0f, max_valley_e = 1.0f;
	for (const auto& he : mesh.half_edges) {
		if (he.type == DCEL_EDGE_RIDGE && he.energy > max_ridge_e)
			max_ridge_e = he.energy;
		if (he.type == DCEL_EDGE_VALLEY && he.energy > max_valley_e)
			max_valley_e = he.energy;
	}

	/* Draw each half-edge individually.
	 * Half-edges belonging to a closed feature (face >= 0) are offset
	 * 1 px inward (left-hand normal of edge direction → face interior).
	 * Half-edges on the infinite face (face < 0) draw on the edge itself. */
	int32_t num_he = (int32_t)mesh.half_edges.size();
	for (int32_t i = 0; i < num_he; i++) {
		const DCELHalfEdge& he = mesh.half_edges[i];
		float energy = he.energy;

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

		const DCELVertex& v0 = mesh.vertices[he.origin];
		int32_t dest = dcel_dest(mesh, i);
		const DCELVertex& v1 = mesh.vertices[dest];

		/* Compute 1px inward offset for closed features */
		float ox = 0.0f, oy = 0.0f;
		if (he.face >= 0 && he.face < (int32_t)mesh.features.size()
		    && mesh.features[he.face].type == DCEL_FEATURE_CLOSED) {
			float ex = v1.x - v0.x, ey = v1.y - v0.y;
			float elen = sqrtf(ex * ex + ey * ey);
			if (elen > 1e-6f) {
				/* Left-hand normal points into the face (CCW winding) */
				ox = -ey / elen;
				oy =  ex / elen;
			}
		}

		/* Bresenham line with offset */
		int x0 = (int)(v0.x + ox), y0 = (int)(v0.y + oy);
		int x1 = (int)(v1.x + ox), y1 = (int)(v1.y + oy);
		int adx = abs(x1 - x0), ady = abs(y1 - y0);
		int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
		int err = adx - ady;

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

	stbi_write_png(path, W, H, 3, rgb.get(), W * 3);
}
#endif

/* -------------------------------------------------------------------------
 * Find continuation of a same-type chain at a vertex.
 *
 * At the dest vertex of `arriving_he`, find another outgoing half-edge of
 * the same type (not the twin of arriving_he). Returns -1 if none found
 * (vertex is a chain endpoint: ENDPOINT, JUNCTION, MAX, MIN, or dead end).
 * ------------------------------------------------------------------------- */
static int32_t chain_continuation(const DCELMesh& mesh, int32_t arriving_he)
{
	const DCELHalfEdge& arr = mesh.half_edges[arriving_he];
	int32_t twin_arr = arr.twin;
	/* twin_arr is outgoing from dest vertex — rotate from there */
	int32_t dest = mesh.half_edges[twin_arr].origin;
	if (mesh.vertices[dest].type != DCEL_VERTEX_PATH) return -1;

	int32_t iter = dcel_next_around_vertex(mesh, twin_arr);
	while (iter != twin_arr) {
		if (mesh.half_edges[iter].type == arr.type)
			return iter;
		iter = dcel_next_around_vertex(mesh, iter);
	}
	return -1;
}

/* -------------------------------------------------------------------------
 * Trace a chain forward from a starting half-edge through PATH vertices.
 * Returns ordered list of half-edge indices in the chain direction.
 * ------------------------------------------------------------------------- */
static std::vector<int32_t> trace_chain(const DCELMesh& mesh, int32_t start)
{
	std::vector<int32_t> chain;
	chain.push_back(start);
	int32_t cur = start;
	for (;;) {
		int32_t next = chain_continuation(mesh, cur);
		if (next < 0) break;
		if (next == start) break;  /* closed loop */
		chain.push_back(next);
		cur = next;
	}
	return chain;
}

/* -------------------------------------------------------------------------
 * Chain-topological propagation along edges of a given type.
 *
 * Instead of orienting edges uphill (which breaks direction continuity at
 * peaks), traces chains through PATH vertices using DCEL adjacency.
 * The chain-direction half-edge gets energy; its twin gets zero.
 *
 * For each chain:
 *   Forward pass:  energy accumulates from chain start → end
 *   Backward pass: energy accumulates from chain end → start (on twins)
 *   At each edge, the direction with more energy wins — its half-edge
 *   keeps the energy, the other is zeroed. This gives a consistent
 *   canonical direction along the chain.
 * ------------------------------------------------------------------------- */
static void propagate_chains(DCELMesh* mesh, DCELEdgeType type, float seed_factor)
{
	int32_t num_he = (int32_t)mesh->half_edges.size();

	/* Track which edge pairs have been processed */
	std::vector<uint8_t> visited(num_he, 0);

	int chain_count = 0;

	for (int32_t hi = 0; hi < num_he; hi++) {
		if (mesh->half_edges[hi].type != type) continue;
		if (visited[hi]) continue;

		/* Walk backward from hi to find one end of the chain */
		int32_t chain_start = hi;
		for (;;) {
			int32_t origin = mesh->half_edges[chain_start].origin;
			if (mesh->vertices[origin].type != DCEL_VERTEX_PATH) break;

			/* At origin, find the other same-type outgoing edge */
			int32_t other = -1;
			int32_t iter = dcel_next_around_vertex(*mesh, chain_start);
			while (iter != chain_start) {
				if (mesh->half_edges[iter].type == type) {
					other = iter;
					break;
				}
				iter = dcel_next_around_vertex(*mesh, iter);
			}
			if (other < 0) break;

			/* Step backward: twin of `other` goes from prev vertex to origin */
			int32_t prev = mesh->half_edges[other].twin;
			if (prev == hi || visited[prev]) break;  /* loop or already done */
			chain_start = prev;
		}

		/* Trace the full chain forward from chain_start */
		auto chain = trace_chain(*mesh, chain_start);
		int n = (int)chain.size();

		/* Mark all chain edges as visited (both half-edges) */
		for (int32_t idx : chain) {
			visited[idx] = 1;
			visited[mesh->half_edges[idx].twin] = 1;
		}

		/* Propagate energy along the chain (bidirectional, take max).
		 * Energy = total chain length visible from this edge with
		 * dot-product decay. Both directions contribute to confidence,
		 * but the chain-direction half-edge always gets the energy
		 * (ensuring consistent tangent sign along the chain). */

		/* Forward pass */
		std::vector<float> fwd(n);
		fwd[0] = mesh->half_edges[chain[0]].length * seed_factor;
		for (int i = 1; i < n; i++) {
			const DCELHalfEdge& prev_he = mesh->half_edges[chain[i-1]];
			const DCELHalfEdge& cur_he = mesh->half_edges[chain[i]];
			float dot = prev_he.tangent_x * cur_he.tangent_x
			          + prev_he.tangent_y * cur_he.tangent_y;
			float incoming = (dot > 0.0f) ? fwd[i-1] * dot : 0.0f;
			fwd[i] = incoming + cur_he.length * seed_factor;
		}

		/* Backward pass */
		std::vector<float> bwd(n);
		bwd[n-1] = mesh->half_edges[chain[n-1]].length * seed_factor;
		for (int i = n - 2; i >= 0; i--) {
			const DCELHalfEdge& next_he = mesh->half_edges[chain[i+1]];
			const DCELHalfEdge& cur_he = mesh->half_edges[chain[i]];
			float dot = next_he.tangent_x * cur_he.tangent_x
			          + next_he.tangent_y * cur_he.tangent_y;
			float incoming = (dot > 0.0f) ? bwd[i+1] * dot : 0.0f;
			bwd[i] = incoming + cur_he.length * seed_factor;
		}

		/* Assign max(fwd, bwd) to the chain-direction half-edge.
		 * Twin always gets zero — this makes the chain-direction
		 * half-edge the canonical one for Dijkstra seeding. */
		for (int i = 0; i < n; i++) {
			int32_t he_idx = chain[i];
			int32_t tw_idx = mesh->half_edges[he_idx].twin;
			mesh->half_edges[he_idx].energy = std::max(fwd[i], bwd[i]);
			mesh->half_edges[tw_idx].energy = 0.0f;
		}

		chain_count++;
	}

	printf("[ridge_energy] propagate_chains(%s): %d chains\n",
	       type == DCEL_EDGE_RIDGE ? "ridge" : "valley", chain_count);
}

int ridge_energy_Execute(RidgeEnergyCmd* cmd)
{
	if (!cmd || !cmd->mesh) {
		fprintf(stderr, "[ridge_energy] Error: NULL input\n");
		return -1;
	}

	DCELMesh* mesh = cmd->mesh;
	int32_t num_he = (int32_t)mesh->half_edges.size();

	/* =================================================================
	 * Phase A+B: Seed + propagate along ridge and valley chains
	 * ================================================================= */

	/* Zero all energies */
	for (auto& he : mesh->half_edges) {
		he.energy = 0.0f;
	}

	/* Propagate ridge chains (high confidence) */
	propagate_chains(mesh, DCEL_EDGE_RIDGE, 1.0f);

	/* Propagate valley chains (lower confidence) */
	propagate_chains(mesh, DCEL_EDGE_VALLEY, VALLEY_FACTOR);

	/* Stats after chain propagation */
	float max_ridge_e = 0, max_valley_e = 0;
	int ridge_count = 0, valley_count = 0;
	for (const auto& he : mesh->half_edges) {
		if (he.energy > 0.0f) {
			if (he.type == DCEL_EDGE_RIDGE) {
				ridge_count++;
				if (he.energy > max_ridge_e) max_ridge_e = he.energy;
			} else {
				valley_count++;
				if (he.energy > max_valley_e) max_valley_e = he.energy;
			}
		}
	}
	printf("[ridge_energy] Chain propagation: ridge=%d (max=%.1f), valley=%d (max=%.1f)\n",
	       ridge_count, max_ridge_e, valley_count, max_valley_e);

#if DEBUG_IMG_OUT
	if (cmd->heightmap && cmd->W > 0 && cmd->H > 0) {
		char buf[512];
		render_energy_debug(
			debug_path("initial_energy.png", buf, sizeof(buf)),
			*mesh, cmd->heightmap, cmd->W, cmd->H);
		printf("[ridge_energy] Wrote: %s\n", buf);
	}
#endif

	/* =================================================================
	 * Phase C: Ridge → Valley transfer via face adjacency
	 *
	 * For each valley edge, find the nearest ridge edge in the same
	 * face(s). Transfer energy based on alignment and distance.
	 * ================================================================= */

	int32_t num_features = (int32_t)mesh->features.size();

	struct RidgeMid {
		float mx, my;
		int32_t he_idx;
	};

	/* Index ridge midpoints by adjacent face */
	std::vector<std::vector<RidgeMid>> face_ridges(num_features);

	for (int32_t i = 0; i < num_he; i++) {
		const DCELHalfEdge& he = mesh->half_edges[i];
		if (he.type != DCEL_EDGE_RIDGE || he.energy <= 0.0f) continue;

		int32_t dest = dcel_dest(*mesh, i);
		float mx = ((float)mesh->vertices[he.origin].x +
		            (float)mesh->vertices[dest].x) * 0.5f;
		float my = ((float)mesh->vertices[he.origin].y +
		            (float)mesh->vertices[dest].y) * 0.5f;
		RidgeMid rm = {mx, my, i};

		int32_t f0 = he.face;
		int32_t f1 = mesh->half_edges[he.twin].face;

		if (f0 >= 0 && f0 < num_features)
			face_ridges[f0].push_back(rm);
		if (f1 >= 0 && f1 < num_features && f1 != f0)
			face_ridges[f1].push_back(rm);
	}

	/* For each valley edge, find nearest ridge in adjacent faces */
	int assigned = 0, unassigned = 0;

	for (int32_t i = 0; i < num_he; i++) {
		DCELHalfEdge& he = mesh->half_edges[i];
		if (he.type != DCEL_EDGE_VALLEY) continue;
		int32_t twin = he.twin;
		if (twin < 0 || twin < i) continue;

		int32_t dest = dcel_dest(*mesh, i);
		float vx = ((float)mesh->vertices[he.origin].x +
		            (float)mesh->vertices[dest].x) * 0.5f;
		float vy = ((float)mesh->vertices[he.origin].y +
		            (float)mesh->vertices[dest].y) * 0.5f;

		int32_t f0 = he.face;
		int32_t f1 = mesh->half_edges[twin].face;

		float best_d2 = INFINITY;
		int32_t best_ri = -1;
		const std::vector<RidgeMid>* best_list = nullptr;

		auto scan = [&](int32_t face) {
			if (face < 0 || face >= num_features) return;
			const auto& list = face_ridges[face];
			for (size_t r = 0; r < list.size(); r++) {
				float dx = vx - list[r].mx;
				float dy = vy - list[r].my;
				float d2 = dx * dx + dy * dy;
				if (d2 < best_d2) {
					best_d2 = d2;
					best_ri = (int32_t)r;
					best_list = &list;
				}
			}
		};

		scan(f0);
		if (f1 != f0) scan(f1);

		if (best_ri < 0) {
			unassigned++;
			continue;
		}

		const DCELHalfEdge& ridge_he =
			mesh->half_edges[(*best_list)[best_ri].he_idx];

		float dot = he.tangent_x * ridge_he.tangent_x
		          + he.tangent_y * ridge_he.tangent_y;

		if (fabsf(dot) < ALIGN_THRESH) {
			unassigned++;
			continue;
		}

		float dist = sqrtf(best_d2) + 1.0f;
		float transfer = ridge_he.energy * fabsf(dot) / dist;

		/* Transfer to the aligned valley half-edge (preserving direction) */
		int32_t target = (dot > 0.0f) ? i : twin;
		DCELHalfEdge& target_he = mesh->half_edges[target];
		if (transfer > target_he.energy)
			target_he.energy = transfer;

		assigned++;
	}

	printf("[ridge_energy] Transfer: %d assigned, %d unassigned (%d features)\n",
	       assigned, unassigned, num_features);

	/* =================================================================
	 * Phase D: Re-propagate valley chains with transferred energy
	 * ================================================================= */
	propagate_chains(mesh, DCEL_EDGE_VALLEY, VALLEY_FACTOR);

	/* Final stats */
	max_ridge_e = 0; max_valley_e = 0;
	ridge_count = 0; valley_count = 0;
	for (const auto& he : mesh->half_edges) {
		if (he.energy > 0.0f) {
			if (he.type == DCEL_EDGE_RIDGE) {
				ridge_count++;
				if (he.energy > max_ridge_e) max_ridge_e = he.energy;
			} else {
				valley_count++;
				if (he.energy > max_valley_e) max_valley_e = he.energy;
			}
		}
	}
	printf("[ridge_energy] Final: ridge=%d (max=%.1f), valley=%d (max=%.1f)\n",
	       ridge_count, max_ridge_e, valley_count, max_valley_e);

#if DEBUG_IMG_OUT
	if (cmd->heightmap && cmd->W > 0 && cmd->H > 0) {
		char buf[512];
		render_energy_debug(
			debug_path("propagated_energy.png", buf, sizeof(buf)),
			*mesh, cmd->heightmap, cmd->W, cmd->H);
		printf("[ridge_energy] Wrote: %s\n", buf);
	}
#endif

	return 0;
}
