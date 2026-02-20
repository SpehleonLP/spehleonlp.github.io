#include "morse_smale_cmd.h"
#include "debug_png.h"
#include "../debug_output.h"

/* diamorse library headers */
#include "CubicalComplex.hpp"
#include "MorseVectorField.hpp"
#include "PackedMap.hpp"
#include "VertexMap.hpp"
#include "vectorFieldExtraction.hpp"
#include "traversals.hpp"
#include "chainComplexExtraction.hpp"
#include "persistence.hpp"
#include "SimpleComplex.hpp"

#include <stdio.h>
#include <math.h>
#include <algorithm>
#include <map>
#include <memory>

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

	/* --- 2b. Compute persistence pairing --- */
	{
		/* Cell value: max scalar over vertices of cell */
		auto cell_value = [&](Cell c) -> float {
			float val = scalars.get(vertices(c, 0));
			for (int i = 1; i < vertices.count(c); i++)
				val = std::max(val, scalars.get(vertices(c, i)));
			return val;
		};

		/* Collect and sort critical cells by value (ascending) */
		std::vector<Cell> sources;
		for (Cell c = 0; c < complex.cellIdLimit(); ++c)
			if (complex.isCell(c) && field.isCritical(c))
				sources.push_back(c);

		std::stable_sort(sources.begin(), sources.end(),
			[&](Cell a, Cell b) {
				float va = cell_value(a), vb = cell_value(b);
				return va < vb || (va == vb && complex.cellDimension(a) < complex.cellDimension(b));
			});

		/* Build SimpleComplex for persistence algorithm */
		size_t n = sources.size();
		std::map<Cell, size_t> cell_to_idx;
		for (size_t i = 0; i < n; i++)
			cell_to_idx[sources[i]] = i;

		typedef std::vector<std::pair<Cell, int>> Boundary;
		std::map<Cell, Boundary> chains = chainComplex(complex, field);

		std::vector<unsigned int> dims;
		std::vector<float> values;
		std::vector<std::vector<Cell>> faceLists;

		for (size_t i = 0; i < n; i++) {
			Cell c = sources[i];
			dims.push_back(complex.cellDimension(c));
			values.push_back(cell_value(c));

			Boundary const& bnd = chains[c];
			std::vector<Cell> faces;
			for (auto& p : bnd)
				for (int k = 0; k < p.second; k++)
					faces.push_back(cell_to_idx[p.first]);
			faceLists.push_back(faces);
		}

		SimpleComplex simple(dims, values, faceLists);
		auto pairs = persistencePairing(simple);

		/* Build Cell → persistence map */
		cmd->persistence_map.clear();
		int n_paired = 0;
		for (size_t i = 0; i < pairs.size(); i++) {
			size_t j = pairs[i].partner;
			float pers;
			if (j >= n) {
				pers = INFINITY; /* unpaired = permanent cycle */
			} else {
				pers = fabsf(pairs[j].value - pairs[i].value);
				n_paired++;
			}
			cmd->persistence_map[sources[i]] = pers;
		}

		fprintf(stderr, "[morse_smale] Persistence: %zu critical cells, %d paired, %zu unpaired\n",
				n, n_paired, n - n_paired);
	}

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
		auto pit = cmd->persistence_map.find(c);
		cp.persistence = (pit != cmd->persistence_map.end()) ? pit->second : INFINITY;
		cmd->critical_points.push_back(cp);

		if (dim == 1)
			saddles.push_back(c);

		if (dim == 0) n_max++;
		else if (dim == 1) n_saddle++;
		else if (dim == 2) n_min++;
	}

	fprintf(stderr, "[morse_smale] %ux%u: %d maxima (dim-0), %d saddles (dim-1), %d minima (dim-2), %d skipped (no-data)\n",
			W, H, n_max, n_saddle, n_min, n_skip);

	/* Log persistence distribution for saddles */
	{
		std::vector<float> saddle_pers;
		for (auto& cp : cmd->critical_points)
			if (cp.dimension == 1 && cp.persistence < INFINITY)
				saddle_pers.push_back(cp.persistence);
		std::sort(saddle_pers.begin(), saddle_pers.end());
		fprintf(stderr, "[morse_smale] Saddle persistence (sorted):");
		for (float p : saddle_pers)
			fprintf(stderr, " %.4f", p);
		fprintf(stderr, "\n");
	}

	/* --- 4. Trace separatrices to build skeleton masks --- */
	cmd->ridge_mask.assign(N, 0.f);
	cmd->valley_mask.assign(N, 0.f);

	Field::Vectors V = field.V();    /* descending (toward minima) */
	Field::Vectors coV = field.coV(); /* ascending (toward maxima) */

	/* Mark pixels with the persistence of the saddle that spawned them.
	 * If multiple separatrices overlap, keep max persistence. */
	auto mark_pixels = [&](Cell c, std::vector<float>& mask, float pers) {
		int nv = vertices.count(c);
		for (int i = 0; i < nv; i++) {
			Cell v = vertices(c, i);
			uint32_t x = complex.cellX(v);
			uint32_t y = complex.cellY(v);
			if (x < W && y < H && !nodata[y * W + x])
				if (pers > mask[y * W + x])
					mask[y * W + x] = pers;
		}
	};

	for (Cell saddle : saddles) {
		/* Look up this saddle's persistence */
		auto pit = cmd->persistence_map.find(saddle);
		float pers = (pit != cmd->persistence_map.end()) ? pit->second : 0.f;
		/* Clamp nodata-border persistence to a visible but distinct value */
		if (pers > 1e5f) pers = 1e5f;

		/* Descending flow: saddle → minima = valley lines */
		{
			auto trail = flowTraversal(saddle, V, I);
			mark_pixels(saddle, cmd->valley_mask, pers);
			for (auto& step : trail) {
				Cell paired = V(step.second);
				mark_pixels(step.second, cmd->valley_mask, pers);
				mark_pixels(paired, cmd->valley_mask, pers);
			}
		}

		/* Ascending flow: saddle → maxima = ridge lines */
		{
			auto trail = flowTraversal(saddle, coV, coI);
			mark_pixels(saddle, cmd->ridge_mask, pers);
			for (auto& step : trail) {
				Cell paired = coV(step.second);
				mark_pixels(step.second, cmd->ridge_mask, pers);
				mark_pixels(paired, cmd->ridge_mask, pers);
			}
		}
	}

	/* --- 5. Mark data/no-data boundary as ridge pixels --- */
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
			if (on_boundary && cmd->ridge_mask[idx] == 0.f) {
				cmd->ridge_mask[idx] = -1.f; /* negative = boundary, not a separatrix */
				boundary_px++;
			}
		}
	}

	int ridge_px = 0, valley_px = 0;
	for (uint32_t i = 0; i < N; i++) {
		ridge_px += (cmd->ridge_mask[i] != 0.f) ? 1 : 0;
		valley_px += (cmd->valley_mask[i] != 0.f) ? 1 : 0;
	}
	fprintf(stderr, "[morse_smale] Skeleton: %d ridge pixels (%d boundary), %d valley pixels\n",
			ridge_px, boundary_px, valley_px);

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

	/* Find max finite persistence for log-scale normalization */
	float max_pers = 0.001f;
	for (uint32_t i = 0; i < N; i++) {
		float rp = cmd->ridge_mask[i], vp = cmd->valley_mask[i];
		if (rp > 0 && rp < 1e5f && rp > max_pers) max_pers = rp;
		if (vp > 0 && vp < 1e5f && vp > max_pers) max_pers = vp;
	}
	/* log scale: t = log(1 + pers) / log(1 + max_pers), maps [0,max] → [0,1] */
	float log_denom = logf(1.f + max_pers);

	fprintf(stderr, "[morse_smale] DebugRender: max persistence = %.4f\n", max_pers);

	/* Persistence → [0,1] via log scale */
	auto pers_to_t = [&](float p) -> float {
		if (p <= 0.f) return 0.f;
		if (p >= 1e5f) return 1.f; /* nodata-border saddles */
		return logf(1.f + p) / log_denom;
	};

	/* Background: heightmap as dark gray */
	for (uint32_t i = 0; i < N; i++) {
		uint8_t gray = (uint8_t)(cmd->heightmap[i] * 100.0f);
		rgb[i*3+0] = gray;
		rgb[i*3+1] = gray;
		rgb[i*3+2] = gray;
	}

	/* Draw valley lines: low persistence = dark blue, high = bright cyan */
	for (uint32_t i = 0; i < N; i++) {
		float vp = cmd->valley_mask[i];
		if (vp > 0.f) {
			float t = pers_to_t(vp);
			rgb[i*3+0] = (uint8_t)(30 + 30 * t);
			rgb[i*3+1] = (uint8_t)(40 + 180 * t);
			rgb[i*3+2] = (uint8_t)(80 + 175 * t);
		}
	}

	/* Draw ridge lines: low persistence = dark red, high = bright yellow */
	for (uint32_t i = 0; i < N; i++) {
		float rp = cmd->ridge_mask[i];
		if (rp > 0.f) {
			float t = pers_to_t(rp);
			rgb[i*3+0] = (uint8_t)(80 + 175 * t);
			rgb[i*3+1] = (uint8_t)(30 + 200 * t);
			rgb[i*3+2] = (uint8_t)(10 + 20 * t);
		} else if (rp < 0.f) {
			/* Boundary pixels: dim gray */
			rgb[i*3+0] = 100;
			rgb[i*3+1] = 100;
			rgb[i*3+2] = 100;
		}
	}

	/* Draw critical points as small markers */
	for (auto& cp : cmd->critical_points) {
		int cx = (int)(cp.x + 0.5f);
		int cy = (int)(cp.y + 0.5f);

		uint8_t r, g, b;
		int radius;
		if (cp.dimension == 0) {
			r = 255; g = 0; b = 0;
			radius = 2;
		} else if (cp.dimension == 2) {
			r = 0; g = 255; b = 0;
			radius = 2;
		} else {
			r = 255; g = 255; b = 255;
			radius = 1;
		}

		for (int dy = -radius; dy <= radius; dy++) {
			for (int dx = -radius; dx <= radius; dx++) {
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
	return 0;
}

#else
int morse_smale_DebugRender(const MorseSmaleCmd*) { return 0; }
#endif
