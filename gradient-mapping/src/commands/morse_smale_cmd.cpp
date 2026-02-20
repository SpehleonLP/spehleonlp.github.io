#include "morse_smale_cmd.h"
#include "divergence_field_cmd.h"
#include "heightmap_ops.h"
#include "debug_png.h"
#include "../debug_output.h"

/* diamorse library headers */
#include "CubicalComplex.hpp"
#include "MorseVectorField.hpp"
#include "PackedMap.hpp"
#include "VertexMap.hpp"
#include "vectorFieldExtraction.hpp"
#include "traversals.hpp"

#include <stdio.h>
#include <math.h>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <algorithm>

using namespace anu_am::diamorse;

typedef CubicalComplex::cell_id_type Cell;
typedef MorseVectorField<PackedMap> Field;

/*
 * Build the discrete Morse gradient on a 2D heightmap and extract
 * critical points + ridge/valley skeleton masks.
 *
 * The cubical complex treats the WxH image as a 3D grid with zdim=1.
 * Cell dimensions in 2D: 0=vertex (pixel), 1=edge (saddle), 2=face.
 *
 * Ridge lines = descending separatrices from dim-1 saddles toward dim-0 minima.
 * Valley lines = ascending separatrices from dim-1 saddles toward dim-0 maxima.
 *
 * (Naming note: in terrain analysis, "ridges" connect saddles to maxima and
 * "valleys" connect saddles to minima. The diamorse V field flows downhill,
 * so V-traversal from a saddle traces valley lines, and coV traces ridges.)
 */
int morse_smale_Execute(MorseSmaleCmd* cmd)
{
	if (!cmd || !cmd->heightmap || cmd->W < 2 || cmd->H < 2)
		return -1;

	uint32_t W = cmd->W, H = cmd->H;
	uint32_t N = W * H;

	/* --- 1. Set up cubical complex (zdim=1 for 2D) --- */
	CubicalComplex complex(W, H, 1);
	Facets I(W, H, 1, false);  /* boundary operator */
	Facets coI(W, H, 1, true); /* coboundary operator */
	Vertices vertices(W, H, 1);

	/* Build no-data mask: heightmap==0 means transparent / no data */
	std::vector<bool> nodata(N, false);
	int nodata_count = 0;
	for (uint32_t i = 0; i < N; i++) {
		if (cmd->heightmap[i] == 0.f) {
			nodata[i] = true;
			nodata_count++;
		}
	}

	/* Wrap heightmap as VertexMap.  diamorse wants a shared_ptr<vector>.
	 * No-data pixels get a large negative value so the Morse gradient
	 * treats them as deep sinks — ridges/valleys won't cross into them. */
	auto scalarData = std::make_shared<std::vector<float>>(N);
	for (uint32_t i = 0; i < N; i++)
		(*scalarData)[i] = nodata[i] ? -1e6f : cmd->heightmap[i];

	VertexMap<CubicalComplex, float> scalars(complex, scalarData);

	fprintf(stderr, "[morse_smale] %ux%u: %d no-data pixels (%.1f%%)\n",
			W, H, nodata_count, 100.f * nodata_count / N);

	/* --- 2. Compute discrete Morse gradient --- */
	Field field(complex);
	fillMorseVectorField(complex, scalars, field);

	/* Helper: check if a cell touches any no-data pixel */
	auto cell_touches_nodata = [&](Cell c) -> bool {
		int nv = vertices.count(c);
		for (int i = 0; i < nv; i++) {
			Cell v = vertices(c, i);
			uint32_t x = complex.cellX(v);
			uint32_t y = complex.cellY(v);
			if (x < W && y < H && nodata[y * W + x])
				return true;
		}
		return false;
	};

	/* --- 3. Collect critical points --- */
	cmd->critical_points.clear();

	std::vector<Cell> saddles;
	int n_max = 0, n_min = 0, n_saddle = 0, n_skip = 0;

	for (Cell c = 0; c < complex.cellIdLimit(); ++c) {
		if (!complex.isCell(c) || !field.isCritical(c))
			continue;

		/* Skip critical points fully inside no-data (all vertices are no-data).
		 * Keep boundary saddles — they represent the terrain edge. */
		if (cell_touches_nodata(c)) {
			/* Check if ALL vertices are no-data (fully inside) */
			bool all_nodata = true;
			int nv = vertices.count(c);
			for (int vi = 0; vi < nv; vi++) {
				Cell v = vertices(c, vi);
				uint32_t x = complex.cellX(v);
				uint32_t y = complex.cellY(v);
				if (x < W && y < H && !nodata[y * W + x])
					all_nodata = false;
			}
			if (all_nodata) {
				n_skip++;
				continue;
			}
		}

		int dim = complex.cellDimension(c);
		/* In 2D (zdim=1), only dim 0, 1, 2 are meaningful.
		 * dim 0 = vertex = extremum (max or min)
		 * dim 1 = edge = saddle
		 * dim 2 = face = extremum (the "other" kind) */

		auto pos = complex.cellPosition(c);
		float val = 0;
		int nv = vertices.count(c);
		for (int i = 0; i < nv; i++)
			val = fmaxf(val, scalars.get(vertices(c, i)));

		MorseSmaleCriticalPoint cp;
		cp.x = pos[0];
		cp.y = pos[1];
		cp.dimension = dim;
		cp.value = val;
		cmd->critical_points.push_back(cp);

		if (dim == 1)
			saddles.push_back(c);

		if (dim == 0) n_max++;
		else if (dim == 1) n_saddle++;
		else if (dim == 2) n_min++;
	}

	fprintf(stderr, "[morse_smale] %ux%u: %d maxima (dim-0), %d saddles (dim-1), %d minima (dim-2), %d skipped (no-data)\n",
			W, H, n_max, n_saddle, n_min, n_skip);

	/* --- 4. Compute divergence field --- */
	float* div_field = nullptr;
	DivergenceFieldCmd div_cmd{};
	if (cmd->normal_scale > 0.f) {
		div_cmd.heightmap = cmd->heightmap;
		div_cmd.W = W;
		div_cmd.H = H;
		div_cmd.normal_scale = cmd->normal_scale;
		divergence_field_Execute(&div_cmd);
		div_field = div_cmd.divergence.get(); /* normalized to [-1, +1] */
	}

	/* --- 5. Trace separatrices to build skeleton masks --- */
	cmd->ridge_mask.assign(N, 0);
	cmd->valley_mask.assign(N, 0);
	cmd->div_mask.assign(N, 0);

	Field::Vectors V = field.V();    /* descending (toward minima) */
	Field::Vectors coV = field.coV(); /* ascending (toward maxima) */

	/* Cell centroid as sub-pixel coordinate (center of pixel = +0.5) */
	auto cell_centroid = [&](Cell c, float& cx, float& cy) {
		int nv = vertices.count(c);
		if (nv == 0) { cx = cy = 0; return; }
		cx = cy = 0;
		for (int i = 0; i < nv; i++) {
			Cell v = vertices(c, i);
			cx += (float)complex.cellX(v);
			cy += (float)complex.cellY(v);
		}
		cx = cx / nv + 0.5f;
		cy = cy / nv + 0.5f;
	};

	/* Cell centroid rounded to pixel index (for mask painting) */
	auto cell_pixel = [&](Cell c) -> int32_t {
		int nv = vertices.count(c);
		if (nv == 0) return -1;
		float cx = 0, cy = 0;
		for (int i = 0; i < nv; i++) {
			Cell v = vertices(c, i);
			cx += (float)complex.cellX(v);
			cy += (float)complex.cellY(v);
		}
		uint32_t x = (uint32_t)(cx / nv + 0.5f);
		uint32_t y = (uint32_t)(cy / nv + 0.5f);
		if (x >= W || y >= H || nodata[y * W + x]) return -1;
		return (int32_t)(y * W + x);
	};

	/* Add cell to a separatrix. Returns false if cell is in no-data. */
	auto sep_add_cell = [&](MorseSmaleCmd::Separatrix& sep, Cell c) -> bool {
		int32_t px = cell_pixel(c);
		if (px < 0) return false; /* no-data or out of bounds */
		sep.pixels.push_back((uint32_t)px);
		float cx, cy;
		cell_centroid(c, cx, cy);
		sep.path_x.push_back(cx);
		sep.path_y.push_back(cy);
		return true;
	};

	/*
	 * Trace a single gradient path from a starting cell adjacent to a saddle.
	 *
	 * For descending (VF=V, IF=I): start_cell is a dim-0 vertex facet of
	 * the saddle. Follow V(vertex)=edge, then take the other facet of that
	 * edge, repeat until critical.
	 *
	 * For ascending (VF=coV, IF=coI): start_cell is a dim-2 face cofacet
	 * of the saddle. Follow coV(face)=edge, then take the other cofacet of
	 * that edge, repeat until critical.
	 */
	auto trace_single_path = [&](
		Cell saddle, Cell start_cell,
		const auto& VF,
		const auto& IF,
		bool ascending
	) -> MorseSmaleCmd::Separatrix {
		MorseSmaleCmd::Separatrix sep;
		sep.ascending = ascending;

		sep_add_cell(sep, saddle);
		if (!sep_add_cell(sep, start_cell))
			return sep; /* start cell is in no-data */

		Cell current = start_cell;
		int max_steps = (int)(W * H); /* safety limit */

		while (--max_steps > 0) {
			if (!VF.defined(current) || field.isCritical(current))
				break; /* reached a critical cell (extremum) */

			Cell partner = VF(current); /* paired cell (one dim away) */
			if (partner == current) break;

			if (!sep_add_cell(sep, partner))
				break; /* entered no-data */

			/* Find the other facet/cofacet of partner that isn't current */
			Cell next = current; /* sentinel */
			int nf = IF.count(partner);
			for (int i = 0; i < nf; i++) {
				Cell candidate = IF(partner, i);
				if (candidate != current) {
					next = candidate;
					break;
				}
			}
			if (next == current) break; /* dead end */

			if (!sep_add_cell(sep, next))
				break; /* entered no-data */
			current = next;
		}

		return sep;
	};

	cmd->separatrices.clear();

	int n_boundary_skip = 0;
	for (Cell saddle : saddles) {
		/* Skip saddles on the data/no-data boundary — they produce
		 * artificial separatrices along the cliff edge.  The explicit
		 * boundary pass (step 6) handles boundary marking. */
		if (cell_touches_nodata(saddle)) {
			n_boundary_skip++;
			continue;
		}

		/* Descending: saddle (dim-1 edge) → facets (dim-0 vertices) → minima.
		 * Each facet vertex starts one branch. */
		int nf = I.count(saddle);
		for (int i = 0; i < nf; i++) {
			Cell start = I(saddle, i);
			auto sep = trace_single_path(saddle, start, V, I, false);
			for (uint32_t idx : sep.pixels)
				cmd->valley_mask[idx] = 1;
			cmd->separatrices.push_back(std::move(sep));
		}

		/* Ascending: saddle (dim-1 edge) → cofacets (dim-2 faces) → maxima.
		 * Each cofacet face starts one branch. */
		int ncf = coI.count(saddle);
		for (int i = 0; i < ncf; i++) {
			Cell start = coI(saddle, i);
			auto sep = trace_single_path(saddle, start, coV, coI, true);
			for (uint32_t idx : sep.pixels)
				cmd->ridge_mask[idx] = 1;
			cmd->separatrices.push_back(std::move(sep));
		}
	}
	if (n_boundary_skip > 0)
		fprintf(stderr, "[morse_smale] Skipped %d boundary saddles\n", n_boundary_skip);

	fprintf(stderr, "[morse_smale] Traced %zu raw separatrices\n",
	        cmd->separatrices.size());

	/* ===================================================================
	 * MESH POST-PROCESSING: filter, boundary contour, merge/split, RDP
	 * =================================================================== */

	/* --- Step A: Filter boundary/degenerate separatrices ---
	 * Remove any separatrix whose BOTH endpoints are on the image border
	 * (within 1px of edge), plus any with < 3 pixels. */
	{
		auto on_border = [&](float x, float y) -> bool {
			return x <= 1.0f || x >= (float)(W - 1) - 0.5f
			    || y <= 1.0f || y >= (float)(H - 1) - 0.5f;
		};

		size_t before = cmd->separatrices.size();
		auto& seps = cmd->separatrices;
		seps.erase(std::remove_if(seps.begin(), seps.end(),
			[&](const MorseSmaleCmd::Separatrix& s) {
				if (s.pixels.size() < 3) return true;
				bool start_border = on_border(s.path_x.front(), s.path_y.front());
				bool end_border = on_border(s.path_x.back(), s.path_y.back());
				return start_border && end_border;
			}), seps.end());
		fprintf(stderr, "[morse_smale] Filtered: %zu → %zu separatrices\n",
		        before, seps.size());
	}

	/* --- Step B: Trace data/no-data boundary contour as valley polylines ---
	 * Chain boundary pixels (data adjacent to no-data, 4-connected) into
	 * ordered contours and add as MSEdge with ascending=false. */
	struct BoundaryContour {
		std::vector<uint32_t> pixels;
		std::vector<float> path_x, path_y;
	};
	std::vector<BoundaryContour> boundary_contours;

	{
		/* Mark boundary pixels: data pixel adjacent to no-data (4-connected)
		 * or on image edge */
		std::vector<bool> is_boundary(N, false);
		for (uint32_t y = 0; y < H; y++) {
			for (uint32_t x = 0; x < W; x++) {
				uint32_t idx = y * W + x;
				if (nodata[idx]) continue;
				bool adj = false;
				if (x == 0 || x == W-1 || y == 0 || y == H-1)
					adj = true;
				else {
					adj = nodata[idx-1] || nodata[idx+1]
					   || nodata[idx-W] || nodata[idx+W];
				}
				if (adj) is_boundary[idx] = true;
			}
		}

		/* Chain boundary pixels into ordered contours via 8-connected walk */
		std::vector<bool> visited(N, false);
		/* Neighbor offsets for 8-connected walk */
		const int dx8[8] = {1, 1, 0, -1, -1, -1, 0, 1};
		const int dy8[8] = {0, 1, 1, 1, 0, -1, -1, -1};

		for (uint32_t start = 0; start < N; start++) {
			if (!is_boundary[start] || visited[start]) continue;

			BoundaryContour contour;
			uint32_t cur = start;
			while (true) {
				visited[cur] = true;
				contour.pixels.push_back(cur);
				contour.path_x.push_back((float)(cur % W) + 0.5f);
				contour.path_y.push_back((float)(cur / W) + 0.5f);

				/* Find next unvisited boundary neighbor (8-connected) */
				uint32_t next = UINT32_MAX;
				int cx = (int)(cur % W), cy = (int)(cur / W);
				for (int d = 0; d < 8; d++) {
					int nx = cx + dx8[d], ny = cy + dy8[d];
					if (nx < 0 || nx >= (int)W || ny < 0 || ny >= (int)H)
						continue;
					uint32_t ni = (uint32_t)ny * W + (uint32_t)nx;
					if (is_boundary[ni] && !visited[ni]) {
						next = ni;
						break;
					}
				}
				if (next == UINT32_MAX) break;
				cur = next;
			}

			if (contour.pixels.size() >= 3)
				boundary_contours.push_back(std::move(contour));
		}
		fprintf(stderr, "[morse_smale] Traced %zu boundary contours\n",
		        boundary_contours.size());
	}

	/* --- Step C: Build simplified mesh (merge shared segments, split at
	 *             branch points, deduplicate) ---
	 *
	 * Convert raw separatrices + boundary contours into a junction graph:
	 * MSVertex at saddles, extrema, branch points; MSEdge between them. */
	{
		cmd->mesh_vertices.clear();
		cmd->mesh_edges.clear();

		/* Collect all polylines (separatrices + boundary contours) into a
		 * uniform representation for processing. */
		struct RawPoly {
			std::vector<uint32_t> pixels;
			std::vector<float> px, py;
			bool ascending;
		};
		std::vector<RawPoly> all_polys;
		all_polys.reserve(cmd->separatrices.size() + boundary_contours.size());

		for (auto& sep : cmd->separatrices) {
			if (sep.pixels.size() < 2) continue;
			RawPoly rp;
			rp.pixels = sep.pixels;
			rp.px = sep.path_x;
			rp.py = sep.path_y;
			rp.ascending = sep.ascending;
			all_polys.push_back(std::move(rp));
		}
		for (auto& bc : boundary_contours) {
			RawPoly rp;
			rp.pixels = bc.pixels;
			rp.px = bc.path_x;
			rp.py = bc.path_y;
			rp.ascending = false; /* boundary = valley */
			all_polys.push_back(std::move(rp));
		}

		/* Build pixel → poly-set map: for each pixel, which poly indices use it */
		std::unordered_map<uint32_t, std::vector<int>> pixel_to_polys;
		for (int pi = 0; pi < (int)all_polys.size(); pi++) {
			for (uint32_t px : all_polys[pi].pixels)
				pixel_to_polys[px].push_back(pi);
		}

		/* Identify junction pixels.  A junction is where the topology of
		 * overlapping polylines changes — i.e., where the set of polys
		 * passing through a pixel differs from the set at the adjacent
		 * pixel along any polyline.  This correctly handles 3+ overlapping
		 * polys: the shared interior is NOT marked, only the transition
		 * points where a poly joins or leaves the group. */
		std::unordered_set<uint32_t> junction_pixels;

		/* All endpoints are junctions (saddles, extrema, boundary ends) */
		for (auto& rp : all_polys) {
			if (rp.pixels.empty()) continue;
			junction_pixels.insert(rp.pixels.front());
			junction_pixels.insert(rp.pixels.back());
		}

		/* Deduplicate the pixel_to_polys entries and sort them so we can
		 * compare sets by equality */
		for (auto& [px, polys] : pixel_to_polys) {
			std::sort(polys.begin(), polys.end());
			polys.erase(std::unique(polys.begin(), polys.end()), polys.end());
		}

		/* Walk each polyline and mark junctions where the overlap set changes */
		{
			auto get_poly_set = [&](uint32_t px) -> const std::vector<int>& {
				static const std::vector<int> empty;
				auto it = pixel_to_polys.find(px);
				return (it != pixel_to_polys.end()) ? it->second : empty;
			};

			for (auto& rp : all_polys) {
				for (size_t k = 1; k < rp.pixels.size(); k++) {
					auto& prev_set = get_poly_set(rp.pixels[k-1]);
					auto& cur_set  = get_poly_set(rp.pixels[k]);
					if (prev_set != cur_set) {
						/* The overlap changed — mark the last pixel of the
						 * old set (the pixel that's still shared) */
						junction_pixels.insert(rp.pixels[k-1]);
					}
				}
			}
		}

		/* Cluster adjacent junction pixels (8-connected) into single vertices.
		 * Each cluster becomes one MSVertex at the centroid.  This collapses
		 * the 2-3px transition zone around a real branch point into one vertex
		 * instead of 4+. */
		std::unordered_map<uint32_t, int> pixel_to_vertex; /* pixel → vertex index */

		/* Build a lookup of critical points by pixel */
		std::unordered_map<uint32_t, int> crit_at_pixel; /* pixel → dimension */
		for (auto& cp : cmd->critical_points) {
			uint32_t px_x = (uint32_t)(cp.x + 0.5f);
			uint32_t px_y = (uint32_t)(cp.y + 0.5f);
			if (px_x < W && px_y < H)
				crit_at_pixel[px_y * W + px_x] = cp.dimension;
		}

		{
			std::unordered_set<uint32_t> remaining(junction_pixels);
			const int dx8[8] = {1, 1, 0, -1, -1, -1, 0, 1};
			const int dy8[8] = {0, 1, 1, 1, 0, -1, -1, -1};

			while (!remaining.empty()) {
				/* BFS to find connected component */
				uint32_t seed = *remaining.begin();
				std::vector<uint32_t> cluster;
				std::vector<uint32_t> queue = {seed};
				remaining.erase(seed);

				while (!queue.empty()) {
					uint32_t cur = queue.back();
					queue.pop_back();
					cluster.push_back(cur);

					int cx = (int)(cur % W), cy = (int)(cur / W);
					for (int d = 0; d < 8; d++) {
						int nx = cx + dx8[d], ny = cy + dy8[d];
						if (nx < 0 || nx >= (int)W || ny < 0 || ny >= (int)H)
							continue;
						uint32_t ni = (uint32_t)ny * W + (uint32_t)nx;
						if (remaining.count(ni)) {
							remaining.erase(ni);
							queue.push_back(ni);
						}
					}
				}

				/* Create one vertex for this cluster at centroid */
				float cx = 0, cy = 0;
				for (uint32_t px : cluster) {
					cx += (float)(px % W) + 0.5f;
					cy += (float)(px / W) + 0.5f;
				}
				cx /= (float)cluster.size();
				cy /= (float)cluster.size();

				int vi = (int)cmd->mesh_vertices.size();
				MSVertex v;
				v.x = cx;
				v.y = cy;
				v.divergence = 0.f;

				/* Classify: best type from any pixel in the cluster.
				 * Priority: critical point > boundary > branch */
				v.type = 3; /* default: branch */
				for (uint32_t px : cluster) {
					auto it = crit_at_pixel.find(px);
					if (it != crit_at_pixel.end()) {
						if (it->second == 0) v.type = 1;      /* maximum */
						else if (it->second == 2) v.type = 2;  /* minimum */
						else v.type = 0;                        /* saddle */
						break; /* critical point wins */
					}
					uint32_t bx = px % W, by = px / W;
					bool is_img_border = (bx == 0 || bx == W-1 || by == 0 || by == H-1);
					bool adj_nd = false;
					if (!is_img_border && px >= W && px + W < N) {
						adj_nd = nodata[px-1] || nodata[px+1]
						      || nodata[px-W] || nodata[px+W];
					}
					if (is_img_border || adj_nd)
						v.type = 4; /* boundary */
				}

				cmd->mesh_vertices.push_back(v);

				/* Map all cluster pixels to this single vertex */
				for (uint32_t px : cluster)
					pixel_to_vertex[px] = vi;
			}
		}

		/* Split each polyline at junction pixels to create MSEdges */
		/* Use a set to deduplicate edges by their pixel sequences */
		std::set<std::pair<uint32_t, uint32_t>> seen_edges; /* (min_start_px, min_end_px) */

		for (auto& rp : all_polys) {
			if (rp.pixels.size() < 2) continue;

			/* Walk the polyline, splitting at every junction pixel */
			int seg_start = 0;
			for (int k = 0; k < (int)rp.pixels.size(); k++) {
				bool is_junc = junction_pixels.count(rp.pixels[k]) > 0;
				bool is_last = (k == (int)rp.pixels.size() - 1);

				if ((is_junc && k > seg_start) || is_last) {
					int seg_end = k;
					if (is_last && !is_junc) seg_end = k; /* include last */

					/* Create an edge from seg_start to seg_end */
					if (seg_end > seg_start) {
						uint32_t sp = rp.pixels[seg_start];
						uint32_t ep = rp.pixels[seg_end];

						/* Deduplicate: canonical key is ordered pair */
						auto key = std::make_pair(std::min(sp, ep), std::max(sp, ep));
						if (seen_edges.insert(key).second) {
							MSEdge edge;
							edge.ascending = rp.ascending;

							/* Ensure start vertex exists */
							if (pixel_to_vertex.find(sp) == pixel_to_vertex.end()) {
								int vi = (int)cmd->mesh_vertices.size();
								pixel_to_vertex[sp] = vi;
								MSVertex v;
								v.x = rp.px[seg_start];
								v.y = rp.py[seg_start];
								v.divergence = 0.f;
								v.type = 3;
								cmd->mesh_vertices.push_back(v);
							}
							/* Ensure end vertex exists */
							if (pixel_to_vertex.find(ep) == pixel_to_vertex.end()) {
								int vi = (int)cmd->mesh_vertices.size();
								pixel_to_vertex[ep] = vi;
								MSVertex v;
								v.x = rp.px[seg_end];
								v.y = rp.py[seg_end];
								v.divergence = 0.f;
								v.type = 3;
								cmd->mesh_vertices.push_back(v);
							}

							/* Add all interior points as vertices too */
							edge.vertex_indices.push_back(pixel_to_vertex[sp]);
							for (int m = seg_start + 1; m < seg_end; m++) {
								int vi = (int)cmd->mesh_vertices.size();
								MSVertex v;
								v.x = rp.px[m];
								v.y = rp.py[m];
								v.divergence = 0.f;
								v.type = -1; /* interior, will be simplified away */
								cmd->mesh_vertices.push_back(v);
								edge.vertex_indices.push_back(vi);
							}
							edge.vertex_indices.push_back(pixel_to_vertex[ep]);

							cmd->mesh_edges.push_back(std::move(edge));
						}
					}

					seg_start = k;
				}
			}
		}

		fprintf(stderr, "[morse_smale] Mesh before RDP: %zu vertices, %zu edges\n",
		        cmd->mesh_vertices.size(), cmd->mesh_edges.size());

		/* --- Step D: RDP simplification of edge interiors ---
		 * Keep first/last vertex (junctions), simplify interior. */
		{
			/* point_to_line_dist — copied from mesh_decimate_cmd.cpp */
			auto pt_line_dist = [](float px, float py,
			                       float ax, float ay, float bx, float by) -> float {
				float ddx = bx - ax, ddy = by - ay;
				float len_sq = ddx * ddx + ddy * ddy;
				if (len_sq < 1e-12f)
					return sqrtf((px - ax) * (px - ax) + (py - ay) * (py - ay));
				return fabsf(ddy * px - ddx * py + bx * ay - by * ax) / sqrtf(len_sq);
			};

			/* Recursive RDP mark on a vertex index array */
			std::function<void(const std::vector<int>&, int, int, float,
			                   std::vector<bool>&)> rdp_mark;
			rdp_mark = [&](const std::vector<int>& vi, int start, int end,
			               float epsilon, std::vector<bool>& keep) {
				if (end - start < 2) return;
				auto& verts = cmd->mesh_vertices;
				float ax = verts[vi[start]].x, ay = verts[vi[start]].y;
				float bx = verts[vi[end]].x,   by = verts[vi[end]].y;
				float dmax = 0.f;
				int idx = start;
				for (int i = start + 1; i < end; i++) {
					float d = pt_line_dist(verts[vi[i]].x, verts[vi[i]].y,
					                       ax, ay, bx, by);
					if (d > dmax) { dmax = d; idx = i; }
				}
				if (dmax > epsilon) {
					keep[idx] = true;
					rdp_mark(vi, start, idx, epsilon, keep);
					rdp_mark(vi, idx, end, epsilon, keep);
				}
			};

			float epsilon = 1.0f;
			size_t total_before = 0, total_after = 0;

			for (auto& edge : cmd->mesh_edges) {
				int n = (int)edge.vertex_indices.size();
				total_before += n;
				if (n <= 2) { total_after += n; continue; }

				std::vector<bool> keep(n, false);
				keep[0] = true;
				keep[n - 1] = true;
				/* Also keep any junction/classified vertices */
				for (int i = 0; i < n; i++) {
					if (cmd->mesh_vertices[edge.vertex_indices[i]].type >= 0)
						keep[i] = true;
				}

				rdp_mark(edge.vertex_indices, 0, n - 1, epsilon, keep);

				std::vector<int> simplified;
				for (int i = 0; i < n; i++) {
					if (keep[i])
						simplified.push_back(edge.vertex_indices[i]);
				}
				edge.vertex_indices = std::move(simplified);
				total_after += edge.vertex_indices.size();
			}

			fprintf(stderr, "[morse_smale] RDP: %zu → %zu edge vertices (eps=%.1f)\n",
			        total_before, total_after, epsilon);
		}

		/* Remove degenerate edges (0-length or single-vertex) */
		{
			size_t before = cmd->mesh_edges.size();
			auto& edges = cmd->mesh_edges;
			edges.erase(std::remove_if(edges.begin(), edges.end(),
				[&](const MSEdge& e) {
					if (e.vertex_indices.size() < 2) return true;
					/* Check if all vertices are at the same position */
					auto& v0 = cmd->mesh_vertices[e.vertex_indices.front()];
					auto& v1 = cmd->mesh_vertices[e.vertex_indices.back()];
					float dx = v1.x - v0.x, dy = v1.y - v0.y;
					return (dx * dx + dy * dy) < 0.01f;
				}), edges.end());
			if (before != edges.size())
				fprintf(stderr, "[morse_smale] Removed %zu degenerate edges\n",
				        before - edges.size());
		}

		/* Compute per-vertex divergence before merging so we can use it
		 * as a merge constraint (vertices where divergence changes sign
		 * mark ridge/valley transitions and should not be absorbed). */
		if (div_field) {
			for (auto& v : cmd->mesh_vertices) {
				uint32_t px = (uint32_t)(v.x);
				uint32_t py = (uint32_t)(v.y);
				if (px < W && py < H)
					v.divergence = div_field[py * W + px];
			}
		}

		/* Merge chains of degree-2 vertices into single edges.
		 * A vertex with exactly 2 incident edges (and type==branch or interior)
		 * can be absorbed into a longer polyline edge, unless it sits at a
		 * divergence sign change (ridge↔valley transition). */
		{
			/* Build vertex → incident edge list */
			int nv = (int)cmd->mesh_vertices.size();
			int ne = (int)cmd->mesh_edges.size();
			std::vector<std::vector<int>> v_edges(nv);
			for (int ei = 0; ei < ne; ei++) {
				auto& e = cmd->mesh_edges[ei];
				if (e.vertex_indices.size() < 2) continue;
				v_edges[e.vertex_indices.front()].push_back(ei);
				v_edges[e.vertex_indices.back()].push_back(ei);
			}

			/* Find degree-2 vertices that are branch/interior (mergeable) */
			std::vector<bool> mergeable(nv, false);
			int div_blocked = 0;
			for (int vi = 0; vi < nv; vi++) {
				if (v_edges[vi].size() != 2) continue;
				int t = cmd->mesh_vertices[vi].type;
				/* Only merge branch (3) or interior (-1) vertices.
				 * Keep saddle(0), max(1), min(2), boundary(4) as-is. */
				if (t == 3 || t < 0) {
					/* Also require both edges have same ascending flag */
					auto& e0 = cmd->mesh_edges[v_edges[vi][0]];
					auto& e1 = cmd->mesh_edges[v_edges[vi][1]];
					if (e0.ascending != e1.ascending)
						continue;

					/* Block merge at divergence sign changes: find the
					 * neighbor endpoints on each side and check signs. */
					if (div_field) {
						float my_div = cmd->mesh_vertices[vi].divergence;
						/* Get the "other end" vertex of each incident edge */
						auto other_end = [&](int ei, int vi) -> int {
							auto& e = cmd->mesh_edges[ei];
							return (e.vertex_indices.front() == vi)
								? e.vertex_indices.back()
								: e.vertex_indices.front();
						};
						int n0 = other_end(v_edges[vi][0], vi);
						int n1 = other_end(v_edges[vi][1], vi);
						float d0 = cmd->mesh_vertices[n0].divergence;
						float d1 = cmd->mesh_vertices[n1].divergence;

						/* Don't merge if this vertex is a sign-change point:
						 * either the vertex itself differs in sign from both
						 * neighbors, or it has high |div| relative to neighbors
						 * (local extremum of divergence along the path). */
						bool sign_change =
							(my_div > 0.01f && d0 < -0.01f && d1 < -0.01f) ||
							(my_div < -0.01f && d0 > 0.01f && d1 > 0.01f) ||
							(d0 > 0.01f && d1 < -0.01f) ||
							(d0 < -0.01f && d1 > 0.01f);
						if (sign_change) {
							div_blocked++;
							continue;
						}
					}

					mergeable[vi] = true;
				}
			}
			if (div_blocked > 0)
				fprintf(stderr, "[morse_smale] Divergence blocked %d merges\n",
				        div_blocked);

			/* Walk chains and merge */
			std::vector<bool> edge_dead(ne, false);
			int merged_count = 0;

			for (int ei = 0; ei < ne; ei++) {
				if (edge_dead[ei]) continue;
				auto& e = cmd->mesh_edges[ei];
				if (e.vertex_indices.size() < 2) continue;

				/* Try extending forward: if the last vertex is mergeable,
				 * find the other edge and append its vertices */
				bool did_merge = true;
				while (did_merge) {
					did_merge = false;
					int tail = e.vertex_indices.back();
					if (!mergeable[tail]) continue;

					for (int other_ei : v_edges[tail]) {
						if (other_ei == ei || edge_dead[other_ei]) continue;
						auto& other = cmd->mesh_edges[other_ei];
						if (other.vertex_indices.size() < 2) continue;

						/* Determine orientation: does 'other' start or end at tail? */
						if (other.vertex_indices.front() == tail) {
							/* Append other's vertices (skip first, it's the shared vertex) */
							for (size_t k = 1; k < other.vertex_indices.size(); k++)
								e.vertex_indices.push_back(other.vertex_indices[k]);
						} else if (other.vertex_indices.back() == tail) {
							/* Append reversed (skip last, it's the shared vertex) */
							for (int k = (int)other.vertex_indices.size() - 2; k >= 0; k--)
								e.vertex_indices.push_back(other.vertex_indices[k]);
						} else {
							continue;
						}
						edge_dead[other_ei] = true;
						merged_count++;
						did_merge = true;
						break;
					}
				}

				/* Try extending backward similarly */
				did_merge = true;
				while (did_merge) {
					did_merge = false;
					int head = e.vertex_indices.front();
					if (!mergeable[head]) continue;

					for (int other_ei : v_edges[head]) {
						if (other_ei == ei || edge_dead[other_ei]) continue;
						auto& other = cmd->mesh_edges[other_ei];
						if (other.vertex_indices.size() < 2) continue;

						std::vector<int> prefix;
						if (other.vertex_indices.back() == head) {
							/* Prepend other's vertices (skip last) */
							for (size_t k = 0; k + 1 < other.vertex_indices.size(); k++)
								prefix.push_back(other.vertex_indices[k]);
						} else if (other.vertex_indices.front() == head) {
							/* Prepend reversed (skip first) */
							for (int k = (int)other.vertex_indices.size() - 1; k > 0; k--)
								prefix.push_back(other.vertex_indices[k]);
						} else {
							continue;
						}
						prefix.insert(prefix.end(), e.vertex_indices.begin(), e.vertex_indices.end());
						e.vertex_indices = std::move(prefix);
						edge_dead[other_ei] = true;
						merged_count++;
						did_merge = true;
						break;
					}
				}
			}

			/* Remove dead edges */
			if (merged_count > 0) {
				auto& edges = cmd->mesh_edges;
				std::vector<MSEdge> surviving;
				for (int ei = 0; ei < ne; ei++) {
					if (!edge_dead[ei])
						surviving.push_back(std::move(edges[ei]));
				}
				edges = std::move(surviving);
				fprintf(stderr, "[morse_smale] Merged %d degree-2 chains → %zu edges\n",
				        merged_count, cmd->mesh_edges.size());
			}
		}

		/* (Step E divergence already computed before merge step above) */

		/* Remove interior-only vertices (type == -1) that survived RDP
		 * by compacting the vertex array and remapping edge indices */
		{
			std::vector<int> remap(cmd->mesh_vertices.size(), -1);
			std::vector<MSVertex> compacted;
			for (int i = 0; i < (int)cmd->mesh_vertices.size(); i++) {
				/* Keep if it's referenced by any edge */
				remap[i] = -1; /* will set below if referenced */
			}
			/* Mark all referenced vertices */
			for (auto& edge : cmd->mesh_edges) {
				for (int vi : edge.vertex_indices)
					remap[vi] = 0; /* mark as referenced */
			}
			/* Compact */
			int next_id = 0;
			for (int i = 0; i < (int)cmd->mesh_vertices.size(); i++) {
				if (remap[i] == 0) {
					remap[i] = next_id++;
					compacted.push_back(cmd->mesh_vertices[i]);
				}
			}
			/* Remap edge indices */
			for (auto& edge : cmd->mesh_edges) {
				for (int& vi : edge.vertex_indices)
					vi = remap[vi];
			}
			cmd->mesh_vertices = std::move(compacted);
		}

		fprintf(stderr, "[morse_smale] Final mesh: %zu vertices, %zu edges\n",
		        cmd->mesh_vertices.size(), cmd->mesh_edges.size());
	}

	/* Per-pixel divergence → div_mask (abs 0-255) + div_signed (raw [-1,+1]) */
	cmd->div_signed.assign(N, 0.f);
	if (div_field) {
		for (uint32_t i = 0; i < N; i++) {
			cmd->div_signed[i] = div_field[i];
			if (cmd->ridge_mask[i] || cmd->valley_mask[i]) {
				float d = fabsf(div_field[i]);
				uint8_t v = (uint8_t)(d * 255.f + 0.5f);
				cmd->div_mask[i] = v;
			}
		}
	}

	/* --- 6. Mark data/no-data boundary as ridge pixels --- */
	int boundary_px = 0;
	for (uint32_t y = 0; y < H; y++) {
		for (uint32_t x = 0; x < W; x++) {
			uint32_t idx = y * W + x;
			if (nodata[idx]) continue;
			bool on_boundary = false;
			if (x == 0 || x == W-1 || y == 0 || y == H-1)
				on_boundary = true;
			else {
				on_boundary = nodata[idx-1] || nodata[idx+1]
				           || nodata[idx-W] || nodata[idx+W];
			}
			if (on_boundary && !cmd->ridge_mask[idx]) {
				cmd->ridge_mask[idx] = 1;
				boundary_px++;
			}
		}
	}

	/* Log */
	int ridge_px = 0, valley_px = 0;
	int n_low = 0, n_mid = 0, n_high = 0;
	for (uint32_t i = 0; i < N; i++) {
		ridge_px += cmd->ridge_mask[i];
		valley_px += cmd->valley_mask[i];
		if (cmd->ridge_mask[i] || cmd->valley_mask[i]) {
			if (cmd->div_mask[i] < 85) n_low++;
			else if (cmd->div_mask[i] < 170) n_mid++;
			else n_high++;
		}
	}
	fprintf(stderr, "[morse_smale] Skeleton: %d ridge (%d boundary), %d valley\n",
			ridge_px, boundary_px, valley_px);
	if (div_field)
		fprintf(stderr, "[morse_smale] |Divergence|: %d low, %d mid, %d high\n",
				n_low, n_mid, n_high);

	return 0;
}

#if DEBUG_IMG_OUT

extern "C" int stbi_write_png(const char* filename, int w, int h, int comp,
                               const void* data, int stride_in_bytes);

int morse_smale_DebugRender(const MorseSmaleCmd* cmd)
{
	if (!cmd || cmd->ridge_mask.empty())
		return -1;

	uint32_t W = cmd->W, H = cmd->H;
	uint32_t N = W * H;

	auto rgb = std::unique_ptr<uint8_t[]>(new uint8_t[N * 3]);

	/* Background: heightmap as dark gray */
	for (uint32_t i = 0; i < N; i++) {
		uint8_t gray = (uint8_t)(cmd->heightmap[i] * 60.0f);
		rgb[i*3+0] = gray;
		rgb[i*3+1] = gray;
		rgb[i*3+2] = gray;
	}

	/* Classify each separatrix: pure (all same sign) vs crossing (sign changes).
	 * Mark pixels with 255 if pure, 0 if crossing. */
	std::vector<uint8_t> pure_mask(N, 0);
	for (auto& sep : cmd->separatrices) {
		if (sep.pixels.empty()) continue;

		/* Determine start and end sign from signed divergence */
		int start_sign = 0, end_sign = 0;
		for (size_t pi = 0; pi < sep.pixels.size() && start_sign == 0; pi++) {
			float sv = cmd->div_signed[sep.pixels[pi]];
			if (sv > 0.02f) start_sign = 1;
			else if (sv < -0.02f) start_sign = -1;
		}
		for (size_t pi = sep.pixels.size(); pi > 0 && end_sign == 0; pi--) {
			float sv = cmd->div_signed[sep.pixels[pi - 1]];
			if (sv > 0.02f) end_sign = 1;
			else if (sv < -0.02f) end_sign = -1;
		}

		bool pure = (start_sign == end_sign) && (start_sign != 0);
		uint8_t val = pure ? 255 : 0;
		for (uint32_t idx : sep.pixels)
			pure_mask[idx] = val;
	}

	/* Additive: R = pure(255)/crossing(0), G = ridge, B = valley */
	auto add_clamp = [](uint8_t base, uint8_t add) -> uint8_t {
		int v = (int)base + (int)add;
		return v > 255 ? 255 : (uint8_t)v;
	};

	for (uint32_t i = 0; i < N; i++) {
		uint8_t r = cmd->ridge_mask[i] ? 200 : 0;
		uint8_t v = cmd->valley_mask[i] ? 200 : 0;
		if (r || v) {
			rgb[i*3+0] = add_clamp(rgb[i*3+0], pure_mask[i]); /* R = pure */
			rgb[i*3+1] = add_clamp(rgb[i*3+1], r);            /* G = ridge */
			rgb[i*3+2] = add_clamp(rgb[i*3+2], v);            /* B = valley */
		}
	}

	/* Draw critical points as small markers */
	for (auto& cp : cmd->critical_points) {
		int cx = (int)(cp.x + 0.5f);
		int cy = (int)(cp.y + 0.5f);

		uint8_t r, g, b;
		int radius;
		if (cp.dimension == 0) {
			/* dim-0 = maximum: bright red dot */
			r = 255; g = 0; b = 0;
			radius = 2;
		} else if (cp.dimension == 2) {
			/* dim-2 = minimum: bright green dot */
			r = 0; g = 255; b = 0;
			radius = 2;
		} else {
			/* dim-1 = saddle: white cross */
			r = 255; g = 255; b = 255;
			radius = 1;
		}

		for (int dy = -radius; dy <= radius; dy++) {
			for (int dx = -radius; dx <= radius; dx++) {
				/* For saddles draw a cross, for extrema a filled circle */
				if (cp.dimension == 1 && dx != 0 && dy != 0)
					continue;
				int px = cx + dx, py = cy + dy;
				if ((uint32_t)px < W && (uint32_t)py < H) {
					uint32_t pi = (uint32_t)py * W + (uint32_t)px;
					rgb[pi*3+0] = r;
					rgb[pi*3+1] = g;
					rgb[pi*3+2] = b;
				}
			}
		}
	}

	char buf[512];
	stbi_write_png(debug_path("morse_smale.png", buf, sizeof(buf)),
	               W, H, 3, rgb.get(), W * 3);
	fprintf(stderr, "[morse_smale] Wrote: %s\n", buf);

	/* --- HTML interactive viewer --- */
	{
		char html_buf[512];
		debug_path("morse_smale.html", html_buf, sizeof(html_buf));
		FILE* f = fopen(html_buf, "w");
		if (f) {
			/* Emit separatrix data as JSON */
			fprintf(f, "<!DOCTYPE html><html><head><meta charset='utf-8'>\n"
			  "<title>Morse-Smale Separatrices</title>\n"
			  "<style>\n"
			  "* { margin:0; padding:0; box-sizing:border-box; }\n"
			  "body { background:#1a1a1a; color:#ccc; font:13px/1.4 monospace;\n"
			  "  display:flex; height:100vh; overflow:hidden; }\n"
			  "#panel { width:280px; display:flex; flex-direction:column;\n"
			  "  border-right:1px solid #333; }\n"
			  "#toolbar { padding:6px; border-bottom:1px solid #333;\n"
			  "  display:flex; flex-wrap:wrap; gap:4px; }\n"
			  "#toolbar button { background:#333; color:#ccc; border:1px solid #555;\n"
			  "  padding:3px 8px; cursor:pointer; font:inherit; }\n"
			  "#toolbar button:hover { background:#444; }\n"
			  "#list { flex:1; overflow-y:auto; }\n"
			  ".sep { padding:3px 8px; cursor:pointer; display:flex;\n"
			  "  align-items:center; gap:6px; border-bottom:1px solid #222; }\n"
			  ".sep:hover { background:#2a2a2a; }\n"
			  ".sep.hidden { opacity:0.35; }\n"
			  ".sep.selected { background:#333; outline:1px solid #888; }\n"
			  ".swatch { width:10px; height:10px; border-radius:2px; flex-shrink:0; }\n"
			  ".tag { font-size:11px; color:#888; margin-left:auto; }\n"
			  "#viewport { flex:1; position:relative; overflow:hidden; cursor:crosshair; }\n"
			  "canvas { position:absolute; top:0; left:0; image-rendering:pixelated; }\n"
			  "#info { position:absolute; bottom:8px; left:8px; background:rgba(0,0,0,0.7);\n"
			  "  padding:4px 8px; pointer-events:none; }\n"
			  "</style></head><body>\n"
			  "<div id='panel'>\n"
			  "<div id='toolbar'>\n"
			  "  <button onclick='showAll()'>Show All</button>\n"
			  "  <button onclick='hideAll()'>Hide All</button>\n"
			  "  <button onclick='invertAll()'>Invert</button>\n"
			  "  <button onclick='copyKeep()'>Copy Keep</button>\n"
			  "  <button onclick='copyRemove()'>Copy Remove</button>\n"
			  "</div>\n"
			  "<div id='list'></div>\n"
			  "</div>\n"
			  "<div id='viewport'><canvas id='c'></canvas>\n"
			  "<div id='info'></div></div>\n"
			  "<script>\n");

			/* Heightmap as base64 grayscale PNG would be large; emit as array */
			fprintf(f, "const W=%u, H=%u;\n", W, H);

			/* Heightmap values (quantized to 0-255) */
			fprintf(f, "const hmap=[");
			for (uint32_t i = 0; i < N; i++) {
				if (i > 0) fprintf(f, ",");
				fprintf(f, "%d", (int)(cmd->heightmap[i] * 255.f + 0.5f));
			}
			fprintf(f, "];\n");

			/* Divergence mask (absolute) */
			fprintf(f, "const divmask=[");
			for (uint32_t i = 0; i < N; i++) {
				if (i > 0) fprintf(f, ",");
				fprintf(f, "%d", cmd->div_mask.empty() ? 0 : cmd->div_mask[i]);
			}
			fprintf(f, "];\n");

			/* Signed divergence (quantized to int16 range for compactness) */
			fprintf(f, "const divsign=[");
			for (uint32_t i = 0; i < N; i++) {
				if (i > 0) fprintf(f, ",");
				fprintf(f, "%d", (int)(cmd->div_signed[i] * 1000.f));
			}
			fprintf(f, "];\n");

			/* Separatrices as array of {asc, avgDiv, path:[[x,y],...]} */
			fprintf(f, "const seps=[");
			for (size_t si = 0; si < cmd->separatrices.size(); si++) {
				auto& sep = cmd->separatrices[si];
				if (sep.path_x.size() < 2) continue;

				float avg_div = 0;
				int n_pos = 0, n_neg = 0, n_flips = 0;
				if (!cmd->div_mask.empty()) {
					int count = 0;
					int prev_sign = 0;
					for (uint32_t idx : sep.pixels) {
						avg_div += cmd->div_mask[idx];
						count++;
						float sv = cmd->div_signed[idx];
						int sign = (sv > 0.01f) ? 1 : (sv < -0.01f) ? -1 : 0;
						if (sign > 0) n_pos++;
						else if (sign < 0) n_neg++;
						if (sign != 0 && prev_sign != 0 && sign != prev_sign)
							n_flips++;
						if (sign != 0) prev_sign = sign;
					}
					if (count > 0) avg_div /= count;
				}

				/* sf=sign flips, sp=positive fraction, sn=negative fraction */
				int total_signed = n_pos + n_neg;
				float pos_frac = total_signed > 0 ? (float)n_pos / total_signed : 0;
				float neg_frac = total_signed > 0 ? (float)n_neg / total_signed : 0;

				fprintf(f, "%s{i:%zu,a:%d,d:%.1f,sf:%d,sp:%.2f,sn:%.2f,p:[",
				        si > 0 ? "," : "",
				        si, sep.ascending ? 1 : 0, avg_div,
				        n_flips, pos_frac, neg_frac);
				for (size_t pi = 0; pi < sep.path_x.size(); pi++) {
					if (pi > 0) fprintf(f, ",");
					fprintf(f, "[%.1f,%.1f]", sep.path_x[pi], sep.path_y[pi]);
				}
				fprintf(f, "]}");
			}
			fprintf(f, "];\n");

			/* Critical points */
			fprintf(f, "const crits=[");
			for (size_t ci = 0; ci < cmd->critical_points.size(); ci++) {
				auto& cp = cmd->critical_points[ci];
				fprintf(f, "%s{x:%.1f,y:%.1f,dim:%d}",
				        ci > 0 ? "," : "", cp.x, cp.y, cp.dimension);
			}
			fprintf(f, "];\n");

			/* Interactive viewer JS */
			fprintf(f, "%s",
			  "const cv=document.getElementById('c'),ctx=cv.getContext('2d');\n"
			  "const vp=document.getElementById('viewport');\n"
			  "const info=document.getElementById('info');\n"
			  "let scale=4, ox=0, oy=0, selected=-1;\n"
			  "const vis=seps.map(()=>true);\n"
			  "\n"
			  "function resize(){\n"
			  "  cv.width=vp.clientWidth; cv.height=vp.clientHeight; draw();\n"
			  "}\n"
			  "window.addEventListener('resize',resize);\n"
			  "\n"
			  "function draw(){\n"
			  "  const cw=cv.width, ch=cv.height;\n"
			  "  ctx.fillStyle='#111'; ctx.fillRect(0,0,cw,ch);\n"
			  "  // build heightmap image once\n"
			  "  if(!window._hmapCv){\n"
			  "    const hc=document.createElement('canvas');\n"
			  "    hc.width=W; hc.height=H;\n"
			  "    const hx=hc.getContext('2d');\n"
			  "    const id=hx.createImageData(W,H);\n"
			  "    for(let i=0;i<W*H;i++){\n"
			  "      let g=hmap[i];\n"
			  "      id.data[i*4]=g; id.data[i*4+1]=g; id.data[i*4+2]=g; id.data[i*4+3]=255;\n"
			  "    }\n"
			  "    hx.putImageData(id,0,0);\n"
			  "    window._hmapCv=hc;\n"
			  "  }\n"
			  "  ctx.save(); ctx.translate(ox,oy); ctx.scale(scale,scale);\n"
			  "  ctx.imageSmoothingEnabled=false;\n"
			  "  ctx.drawImage(window._hmapCv,0,0);\n"
			  "  // draw separatrices\n"
			  "  ctx.lineCap='round'; ctx.lineJoin='round';\n"
			  "  for(let si=0;si<seps.length;si++){\n"
			  "    if(!vis[si]) continue;\n"
			  "    const s=seps[si], sel=(si===selected);\n"
			  "    ctx.lineWidth=sel?2/scale:1/scale;\n"
			  "    if(sel) ctx.strokeStyle='#fff';\n"
			  "    else ctx.strokeStyle=s.a?'rgba(0,200,0,0.8)':'rgba(68,68,255,0.8)';\n"
			  "    ctx.beginPath();\n"
			  "    ctx.moveTo(s.p[0][0],s.p[0][1]);\n"
			  "    for(let j=1;j<s.p.length;j++) ctx.lineTo(s.p[j][0],s.p[j][1]);\n"
			  "    ctx.stroke();\n"
			  "    // label\n"
			  "    if(scale>=3){\n"
			  "      const mid=s.p[Math.floor(s.p.length/2)];\n"
			  "      ctx.fillStyle=sel?'#ff0':'#aaa';\n"
			  "      ctx.font=(10/scale)+'px monospace';\n"
			  "      ctx.fillText(s.i,mid[0]+1/scale,mid[1]-1/scale);\n"
			  "    }\n"
			  "  }\n"
			  "  // critical points\n"
			  "  for(const c of crits){\n"
			  "    ctx.fillStyle=c.dim===0?'#f44':c.dim===2?'#4f4':'#fff';\n"
			  "    ctx.beginPath(); ctx.arc(c.x,c.y,2/scale,0,Math.PI*2); ctx.fill();\n"
			  "  }\n"
			  "  ctx.restore();\n"
			  "}\n"
			  "\n"
			  "// pan & zoom\n"
			  "let drag=false, lx=0, ly=0;\n"
			  "cv.onmousedown=e=>{drag=true;lx=e.clientX;ly=e.clientY;};\n"
			  "window.onmouseup=()=>{drag=false;};\n"
			  "cv.onmousemove=e=>{\n"
			  "  if(drag){ox+=e.clientX-lx;oy+=e.clientY-ly;lx=e.clientX;ly=e.clientY;draw();}\n"
			  "  const r=cv.getBoundingClientRect();\n"
			  "  const mx=(e.clientX-r.left-ox)/scale, my=(e.clientY-r.top-oy)/scale;\n"
			  "  info.textContent=`(${mx.toFixed(1)}, ${my.toFixed(1)})`;\n"
			  "};\n"
			  "cv.onwheel=e=>{\n"
			  "  e.preventDefault();\n"
			  "  const r=cv.getBoundingClientRect();\n"
			  "  const mx=e.clientX-r.left, my=e.clientY-r.top;\n"
			  "  const f=e.deltaY<0?1.2:1/1.2;\n"
			  "  ox=mx-f*(mx-ox); oy=my-f*(my-oy); scale*=f; draw();\n"
			  "};\n"
			  "\n"
			  "// click to select nearest separatrix\n"
			  "cv.onclick=e=>{\n"
			  "  const r=cv.getBoundingClientRect();\n"
			  "  const mx=(e.clientX-r.left-ox)/scale, my=(e.clientY-r.top-oy)/scale;\n"
			  "  let best=-1, bestD=Infinity;\n"
			  "  for(let si=0;si<seps.length;si++){\n"
			  "    if(!vis[si]) continue;\n"
			  "    for(const pt of seps[si].p){\n"
			  "      const d=(pt[0]-mx)**2+(pt[1]-my)**2;\n"
			  "      if(d<bestD){bestD=d;best=si;}\n"
			  "    }\n"
			  "  }\n"
			  "  if(bestD<(10/scale)**2){ selected=best; scrollToSep(best); }\n"
			  "  else selected=-1;\n"
			  "  updateList(); draw();\n"
			  "};\n"
			  "\n"
			  "// right-click toggles visibility of nearest\n"
			  "cv.oncontextmenu=e=>{\n"
			  "  e.preventDefault();\n"
			  "  const r=cv.getBoundingClientRect();\n"
			  "  const mx=(e.clientX-r.left-ox)/scale, my=(e.clientY-r.top-oy)/scale;\n"
			  "  let best=-1, bestD=Infinity;\n"
			  "  for(let si=0;si<seps.length;si++){\n"
			  "    for(const pt of seps[si].p){\n"
			  "      const d=(pt[0]-mx)**2+(pt[1]-my)**2;\n"
			  "      if(d<bestD){bestD=d;best=si;}\n"
			  "    }\n"
			  "  }\n"
			  "  if(best>=0 && bestD<(10/scale)**2){\n"
			  "    vis[best]=!vis[best]; updateList(); draw();\n"
			  "  }\n"
			  "};\n"
			  "\n"
			  "// panel list\n"
			  "const listEl=document.getElementById('list');\n"
			  "function buildList(){\n"
			  "  listEl.innerHTML='';\n"
			  "  for(let si=0;si<seps.length;si++){\n"
			  "    const s=seps[si];\n"
			  "    const el=document.createElement('div');\n"
			  "    el.className='sep'+(vis[si]?'':' hidden');\n"
			  "    el.dataset.si=si;\n"
			  "    const sw=document.createElement('span');\n"
			  "    sw.className='swatch';\n"
			  "    sw.style.background=s.a?'#0c0':'#44f';\n"
			  "    el.appendChild(sw);\n"
			  "    const lbl=document.createElement('span');\n"
			  "    lbl.textContent=s.i+' '+(s.a?'R':'V');\n"
			  "    el.appendChild(lbl);\n"
			  "    const tag=document.createElement('span');\n"
			  "    tag.className='tag';\n"
			  "    tag.textContent='d:'+s.d.toFixed(0)+' n:'+s.p.length;\n"
			  "    el.appendChild(tag);\n"
			  "    el.onclick=()=>{\n"
			  "      selected=si; updateList(); draw();\n"
			  "      const mid=s.p[Math.floor(s.p.length/2)];\n"
			  "      ox=cv.width/2-mid[0]*scale;\n"
			  "      oy=cv.height/2-mid[1]*scale;\n"
			  "      draw();\n"
			  "    };\n"
			  "    el.oncontextmenu=ev=>{\n"
			  "      ev.preventDefault(); vis[si]=!vis[si]; updateList(); draw();\n"
			  "    };\n"
			  "    listEl.appendChild(el);\n"
			  "  }\n"
			  "}\n"
			  "function updateList(){\n"
			  "  const els=listEl.children;\n"
			  "  for(let i=0;i<els.length;i++){\n"
			  "    els[i].className='sep'\n"
			  "      +(vis[i]?'':' hidden')\n"
			  "      +(i===selected?' selected':'');\n"
			  "  }\n"
			  "}\n"
			  "function scrollToSep(si){\n"
			  "  const el=listEl.children[si];\n"
			  "  if(el) el.scrollIntoView({block:'center'});\n"
			  "}\n"
			  "\n"
			  "function showAll(){ vis.fill(true); updateList(); draw(); }\n"
			  "function hideAll(){ vis.fill(false); updateList(); draw(); }\n"
			  "function invertAll(){ for(let i=0;i<vis.length;i++) vis[i]=!vis[i]; updateList(); draw(); }\n"
			  "function copyKeep(){\n"
			  "  const arr=seps.filter((_,i)=>vis[i]).map(s=>s.i);\n"
			  "  navigator.clipboard.writeText('keep=['+arr.join(',')+']');\n"
			  "}\n"
			  "function copyRemove(){\n"
			  "  const arr=seps.filter((_,i)=>!vis[i]).map(s=>s.i);\n"
			  "  navigator.clipboard.writeText('remove=['+arr.join(',')+']');\n"
			  "}\n"
			  "\n"
			  "buildList(); resize();\n"
			  "</script></body></html>\n");

			fclose(f);
			fprintf(stderr, "[morse_smale] Wrote: %s (%zu separatrices)\n",
			        html_buf, cmd->separatrices.size());
		}
	}

	/* --- SVG output of simplified mesh --- */
	if (!cmd->mesh_edges.empty()) {
		char svg_buf[512];
		debug_path("morse_smale_mesh.svg", svg_buf, sizeof(svg_buf));
		FILE* f = fopen(svg_buf, "w");
		if (f) {
			/* Per-edge z-test: is the edge normal aligned with the major
			 * eigenvector more than random chance?
			 *
			 * Null hypothesis: edge normal is randomly oriented relative to
			 * the major eigenvector.  For |cos(θ)| with θ ~ Uniform(0,2π):
			 *   E[|cos(θ)|] = 2/π ≈ 0.6366
			 *   Var[|cos(θ)|] = 1/2 - 4/π² ≈ 0.0947
			 *
			 * z = (mean - 2/π) / sqrt(var/N)
			 *   positive z → aligned with major curvature (keep)
			 *   negative z → aligned with minor curvature (prune)
			 *
			 * Map z to color via sigmoid: p = 1/(1+exp(-z)) → [0,1]
			 * where 0.5 = chance, 1.0 = strongly major, 0.0 = strongly minor */
			const float null_mean = 0.6366197723676f; /* 2/π */
			const float null_var  = 0.0947190762749f; /* 1/2 - 4/π² */

			std::vector<float> edge_zscore(cmd->mesh_edges.size(), 0.f);
			std::vector<float> edge_prob(cmd->mesh_edges.size(), 0.5f);

			for (size_t ei = 0; ei < cmd->mesh_edges.size(); ei++) {
				auto& edge = cmd->mesh_edges[ei];
				if (edge.vertex_indices.size() < 2) continue;

				float sum = 0.f;
				int count = 0;
				for (size_t vi = 0; vi + 1 < edge.vertex_indices.size(); vi++) {
					auto& v0 = cmd->mesh_vertices[edge.vertex_indices[vi]];
					auto& v1 = cmd->mesh_vertices[edge.vertex_indices[vi+1]];

					float tx = v1.x - v0.x, ty = v1.y - v0.y;
					float tlen = std::sqrt(tx * tx + ty * ty);
					if (tlen < 1e-6f) continue;
					float enx = -ty / tlen, eny = tx / tlen;

					float mx = (v0.x + v1.x) * 0.5f;
					float my = (v0.y + v1.y) * 0.5f;
					uint32_t px = (uint32_t)mx, py = (uint32_t)my;
					if (px >= W) px = W - 1;
					if (py >= H) py = H - 1;

					HeightEigen eig = height_eigen(cmd->heightmap, px, py, W, H);
					float dot_major = std::abs(enx * eig.major_vec.x + eny * eig.major_vec.y);
					sum += dot_major;
					count++;
				}

				if (count > 0) {
					float mean = sum / (float)count;
					float se = std::sqrt(null_var / (float)count);
					float z = (mean - null_mean) / se;
					edge_zscore[ei] = z;
					/* Sigmoid: 1/(1+exp(-z)) */
					edge_prob[ei] = 1.f / (1.f + std::exp(-z));
				}
			}

			fprintf(f, "<?xml version='1.0' encoding='UTF-8'?>\n"
			  "<svg xmlns='http://www.w3.org/2000/svg' "
			  "width='%u' height='%u' viewBox='0 0 %u %u'>\n"
			  "<rect width='%u' height='%u' fill='#111'/>\n",
			  W * 4, H * 4, W, H, W, H);

			/* Draw edges: R = major alignment probability (sigmoid of z-score),
			 * G = ridge, B = valley.
			 * Bright red = strongly major-aligned (keep).
			 * Dark = minor-aligned or inconclusive (prune candidate). */
			for (size_t ei = 0; ei < cmd->mesh_edges.size(); ei++) {
				auto& edge = cmd->mesh_edges[ei];
				if (edge.vertex_indices.size() < 2) continue;

				/* Map z ∈ [-2, 2] → R ∈ [1, 255] */
				float z_clamped = std::max(-2.f, std::min(2.f, edge_zscore[ei]));
				int r = (int)((z_clamped + 2.f) * (254.f / 4.f) + 1.f + 0.5f);
				int g = edge.ascending ? 200 : 0;
				int b = edge.ascending ? 0 : 200;
				float start_div = cmd->mesh_vertices[edge.vertex_indices.front()].divergence;
				float end_div = cmd->mesh_vertices[edge.vertex_indices.back()].divergence;

				fprintf(f, "<polyline id='edge_%zu' fill='none' stroke='rgb(%d,%d,%d)' "
				  "stroke-width='0.3' stroke-linecap='round' "
				  "data-z='%.2f' data-p='%.3f' "
				  "data-start-div='%.3f' data-end-div='%.3f' points='",
				  ei, r, g, b, edge_zscore[ei], edge_prob[ei], start_div, end_div);

				for (size_t vi = 0; vi < edge.vertex_indices.size(); vi++) {
					auto& v = cmd->mesh_vertices[edge.vertex_indices[vi]];
					if (vi > 0) fprintf(f, " ");
					fprintf(f, "%.2f,%.2f", v.x, v.y);
				}
				fprintf(f, "'/>\n");
			}

			/* Draw vertices as circles */
			/* type: 0=saddle(white), 1=maximum(red), 2=minimum(cyan),
			 *       3=branch(yellow), 4=boundary(gray) */
			const char* vcolors[] = {"#ffffff", "#ff4444", "#44ffff", "#ffff00", "#888888"};
			for (size_t vi = 0; vi < cmd->mesh_vertices.size(); vi++) {
				auto& v = cmd->mesh_vertices[vi];
				if (v.type < 0 || v.type > 4) continue;
				fprintf(f, "<circle cx='%.2f' cy='%.2f' r='0.6' fill='%s' "
				  "data-div='%.3f' data-type='%d'/>\n",
				  v.x, v.y, vcolors[v.type], v.divergence, v.type);
			}

			fprintf(f, "</svg>\n");
			fclose(f);
			fprintf(stderr, "[morse_smale] Wrote: %s (%zu edges, %zu vertices)\n",
			        svg_buf, cmd->mesh_edges.size(), cmd->mesh_vertices.size());
		}
	}

	return 0;
}

#else
int morse_smale_DebugRender(const MorseSmaleCmd*) { return 0; }
#endif
