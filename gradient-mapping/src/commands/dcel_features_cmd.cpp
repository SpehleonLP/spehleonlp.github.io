#include "dcel_features_cmd.h"
#include <math.h>
#include <stdio.h>
#include <float.h>
#include <algorithm>

int dcel_features_Execute(DCELFeaturesCmd* cmd)
{
	if (!cmd || !cmd->mesh) {
		fprintf(stderr, "[dcel_features] Error: NULL input\n");
		return -1;
	}

	DCELMesh* mesh = cmd->mesh;
	int32_t num_he = (int32_t)mesh->half_edges.size();

	/* Initialize all faces to -1 (unvisited) */
	for (auto& he : mesh->half_edges) {
		he.face = -1;
	}
	mesh->features.clear();

	size_t closed_count = 0;
	size_t open_count = 0;

	/* Track which face has the largest negative signed area (infinite face) */
	int32_t infinite_face = -1;
	float most_negative_area = 0.0f;

	for (int32_t i = 0; i < num_he; i++) {
		if (mesh->half_edges[i].face != -1) continue;

		/* Start a new face cycle from half-edge i */
		int32_t first = i;
		int32_t cur = first;
		int32_t edge_count = 0;
		bool is_open = false;

		/* First pass: determine if cycle is closed or open, count edges */
		std::vector<int32_t> cycle;
		cycle.reserve(16);

		do {
			cycle.push_back(cur);
			edge_count++;

			/* Guard against infinite loops */
			if (edge_count > num_he) {
				fprintf(stderr, "[dcel_features] Warning: infinite loop detected at half-edge %d\n", i);
				is_open = true;
				break;
			}

			int32_t nxt = mesh->half_edges[cur].next;
			if (nxt < 0 || nxt >= num_he) {
				/* Open chain: next pointer is invalid */
				is_open = true;
				break;
			}
			cur = nxt;
		} while (cur != first);

		int32_t face_idx = (int32_t)mesh->features.size();

		/* Assign face index to all half-edges in the cycle */
		for (int32_t he_idx : cycle) {
			mesh->half_edges[he_idx].face = face_idx;
		}

		/* Compute AABB and signed area */
		AABB bbox;
		bbox.min_x = FLT_MAX;
		bbox.min_y = FLT_MAX;
		bbox.max_x = -FLT_MAX;
		bbox.max_y = -FLT_MAX;

		float area_signed = 0.0f;

		for (int32_t he_idx : cycle) {
			const DCELHalfEdge& he = mesh->half_edges[he_idx];
			float x0 = (float)mesh->vertices[he.origin].x;
			float y0 = (float)mesh->vertices[he.origin].y;

			/* Update AABB with origin vertex */
			bbox.min_x = std::min(bbox.min_x, x0);
			bbox.min_y = std::min(bbox.min_y, y0);
			bbox.max_x = std::max(bbox.max_x, x0);
			bbox.max_y = std::max(bbox.max_y, y0);

			/* Shoelace formula: use twin's origin as destination */
			int32_t dest = dcel_dest(*mesh, he_idx);
			float x1 = (float)mesh->vertices[dest].x;
			float y1 = (float)mesh->vertices[dest].y;
			area_signed += (x0 * y1 - x1 * y0);
		}
		area_signed *= 0.5f;

		DCELFeature feature;
		feature.type = is_open ? DCEL_FEATURE_OPEN : DCEL_FEATURE_CLOSED;
		feature.first_edge = cycle[0];
		feature.edge_count = (int32_t)cycle.size();
		feature.parent = -1;
		feature.bbox = bbox;
		feature.area_signed = area_signed;

		mesh->features.push_back(feature);

		if (feature.type == DCEL_FEATURE_CLOSED) {
			closed_count++;
		} else {
			open_count++;
		}

		/* Track largest negative area for infinite face detection */
		if (feature.type == DCEL_FEATURE_CLOSED && area_signed < most_negative_area) {
			most_negative_area = area_signed;
			infinite_face = face_idx;
		}
	}

	/* Mark the infinite face: set all its half-edges to face = -2 */
	if (infinite_face >= 0) {
		DCELFeature& inf = mesh->features[infinite_face];
		int32_t cur = inf.first_edge;
		for (int32_t j = 0; j < inf.edge_count; j++) {
			mesh->half_edges[cur].face = -2;
			cur = mesh->half_edges[cur].next;
		}

		/* Remove from features by swapping with last and popping */
		if (infinite_face < (int32_t)mesh->features.size() - 1) {
			/* Update face indices for the swapped feature's half-edges */
			DCELFeature& last = mesh->features.back();
			int32_t last_first = last.first_edge;
			int32_t hc = last_first;
			for (int32_t j = 0; j < last.edge_count; j++) {
				mesh->half_edges[hc].face = infinite_face;
				hc = mesh->half_edges[hc].next;
			}
			mesh->features[infinite_face] = mesh->features.back();
		}
		mesh->features.pop_back();
		closed_count--;

		printf("[dcel_features] Infinite face: area=%.1f (removed from features)\n",
		       most_negative_area);
	}

	/* Compute parent: for each feature, find the smallest adjacent closed
	 * feature that encloses it.  Walk twin faces of each feature's boundary;
	 * among those that are closed and have area > ours, pick the smallest
	 * whose AABB contains ours.  For open features where all twins reference
	 * the same face, that face is the parent (the feature is embedded in it). */
	for (size_t fi = 0; fi < mesh->features.size(); fi++) {
		DCELFeature& f = mesh->features[fi];
		float f_area = fabsf(f.area_signed);
		float best_area = INFINITY;

		int32_t cur = f.first_edge;
		for (int32_t j = 0; j < f.edge_count; j++) {
			int32_t twin_face = mesh->half_edges[mesh->half_edges[cur].twin].face;

			if (twin_face >= 0 && twin_face != (int32_t)fi) {
				const DCELFeature& c = mesh->features[twin_face];
				if (c.type == DCEL_FEATURE_CLOSED) {
					float c_area = fabsf(c.area_signed);
					if (c_area > f_area && c_area < best_area) {
						const AABB& cb = c.bbox;
						if (cb.min_x <= f.bbox.min_x && cb.max_x >= f.bbox.max_x &&
						    cb.min_y <= f.bbox.min_y && cb.max_y >= f.bbox.max_y) {
							best_area = c_area;
							f.parent = twin_face;
						}
					}
				}
			}

			cur = mesh->half_edges[cur].next;
		}
	}

	/* Size distribution */
	int bucket_3 = 0, bucket_4_8 = 0, bucket_9_20 = 0, bucket_21_50 = 0, bucket_big = 0;
	float area_3 = 0, area_4_8 = 0, area_9_20 = 0, area_21_50 = 0, area_big = 0;
	for (const auto& f : mesh->features) {
		float a = fabsf(f.area_signed);
		if (f.edge_count <= 3)       { bucket_3++;    area_3 += a; }
		else if (f.edge_count <= 8)  { bucket_4_8++;  area_4_8 += a; }
		else if (f.edge_count <= 20) { bucket_9_20++; area_9_20 += a; }
		else if (f.edge_count <= 50) { bucket_21_50++;area_21_50 += a; }
		else                         { bucket_big++;  area_big += a; }
	}
	printf("[dcel_features] Size distribution:\n");
	printf("  edges<=3:   %3d faces, total area=%.1f\n", bucket_3, area_3);
	printf("  edges 4-8:  %3d faces, total area=%.1f\n", bucket_4_8, area_4_8);
	printf("  edges 9-20: %3d faces, total area=%.1f\n", bucket_9_20, area_9_20);
	printf("  edges 21-50:%3d faces, total area=%.1f\n", bucket_21_50, area_21_50);
	printf("  edges >50:  %3d faces, total area=%.1f\n", bucket_big, area_big);

	printf("[dcel_features] %zu features (%zu closed, %zu open)\n",
	       mesh->features.size(), closed_count, open_count);

	return 0;
}
