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

#include <stdio.h>
#include <math.h>
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

	/* --- 4. Trace separatrices to build skeleton masks --- */
	cmd->ridge_mask.assign(N, 0);
	cmd->valley_mask.assign(N, 0);

	Field::Vectors V = field.V();    /* descending (toward minima) */
	Field::Vectors coV = field.coV(); /* ascending (toward maxima) */

	auto mark_pixels = [&](Cell c, std::vector<uint8_t>& mask) {
		int nv = vertices.count(c);
		for (int i = 0; i < nv; i++) {
			Cell v = vertices(c, i);
			uint32_t x = complex.cellX(v);
			uint32_t y = complex.cellY(v);
			if (x < W && y < H && !nodata[y * W + x])
				mask[y * W + x] = 1;
		}
	};

	for (Cell saddle : saddles) {
		/* Descending flow: saddle → minima = valley lines */
		{
			auto trail = flowTraversal(saddle, V, I);
			mark_pixels(saddle, cmd->valley_mask);
			for (auto& step : trail) {
				Cell paired = V(step.second);
				mark_pixels(step.second, cmd->valley_mask);
				mark_pixels(paired, cmd->valley_mask);
			}
		}

		/* Ascending flow: saddle → maxima = ridge lines */
		{
			auto trail = flowTraversal(saddle, coV, coI);
			mark_pixels(saddle, cmd->ridge_mask);
			for (auto& step : trail) {
				Cell paired = coV(step.second);
				mark_pixels(step.second, cmd->ridge_mask);
				mark_pixels(paired, cmd->ridge_mask);
			}
		}
	}

	/* --- 5. Mark data/no-data boundary as ridge pixels --- */
	int boundary_px = 0;
	for (uint32_t y = 0; y < H; y++) {
		for (uint32_t x = 0; x < W; x++) {
			uint32_t idx = y * W + x;
			if (nodata[idx]) continue;
			/* Check 4-connected neighbors for no-data */
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

	int ridge_px = 0, valley_px = 0;
	for (uint32_t i = 0; i < N; i++) {
		ridge_px += cmd->ridge_mask[i];
		valley_px += cmd->valley_mask[i];
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

	/* Background: heightmap as dark gray */
	for (uint32_t i = 0; i < N; i++) {
		uint8_t gray = (uint8_t)(cmd->heightmap[i] * 100.0f);
		rgb[i*3+0] = gray;
		rgb[i*3+1] = gray;
		rgb[i*3+2] = gray;
	}

	/* Draw valley lines (blue) */
	for (uint32_t i = 0; i < N; i++) {
		if (cmd->valley_mask[i]) {
			rgb[i*3+0] = 40;
			rgb[i*3+1] = 80;
			rgb[i*3+2] = 220;
		}
	}

	/* Draw ridge lines (yellow/orange — drawn after valleys so ridges are visible) */
	for (uint32_t i = 0; i < N; i++) {
		if (cmd->ridge_mask[i]) {
			rgb[i*3+0] = 255;
			rgb[i*3+1] = 200;
			rgb[i*3+2] = 30;
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
	return 0;
}

#else
int morse_smale_DebugRender(const MorseSmaleCmd*) { return 0; }
#endif
