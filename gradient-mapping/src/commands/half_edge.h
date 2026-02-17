#ifndef HALF_EDGE_H
#define HALF_EDGE_H
#define GLM_ENABLE_EXPERIMENTAL 1

#include <stdint.h>
#include <vector>

enum DCELVertexType : uint8_t {
	DCEL_VERTEX_MAXIMUM,
	DCEL_VERTEX_MINIMUM,
	DCEL_VERTEX_JUNCTION,
	DCEL_VERTEX_ENDPOINT,
	DCEL_VERTEX_PATH         // intermediate pixel along a Dijkstra path
};

enum DCELEdgeType : uint8_t {
	DCEL_EDGE_RIDGE,
	DCEL_EDGE_VALLEY
};

enum DCELFeatureType : uint8_t {
	DCEL_FEATURE_CLOSED,
	DCEL_FEATURE_OPEN
};

struct DCELVertex {
	float x, y;
	float height, divergence;
	DCELVertexType type;
	int32_t edge;           // any outgoing half-edge (-1 if isolated)
};

struct DCELHalfEdge {
	int32_t origin, twin, next, prev, face;
	DCELEdgeType type;
	float tangent_x, tangent_y, energy, length;
};

struct AABB {
	float min_x, min_y, max_x, max_y;
};

struct DCELFeature {
	DCELFeatureType type;
	int32_t first_edge, edge_count;
	int32_t parent;         // smallest enclosing closed feature (-1 for top-level)
	AABB bbox;
	float area_signed;      // positive=CCW, negative=CW; largest negative=infinite face
};

struct DCELMesh {
	std::vector<DCELVertex> vertices;
	std::vector<DCELHalfEdge> half_edges;
	std::vector<DCELFeature> features;
};

/* Destination vertex of a half-edge */
static inline int32_t dcel_dest(const DCELMesh& mesh, int32_t he) {
	return mesh.half_edges[mesh.half_edges[he].twin].origin;
}

/* Next half-edge around the origin vertex (CCW rotation) */
static inline int32_t dcel_next_around_vertex(const DCELMesh& mesh, int32_t he) {
	return mesh.half_edges[mesh.half_edges[he].twin].next;
}

/* Inward-facing normal of a half-edge (90 deg CCW rotation of tangent) */
static inline void dcel_inside_normal(const DCELHalfEdge& he, float* nx, float* ny) {
	*nx = -he.tangent_y;
	*ny = he.tangent_x;
}

#endif /* HALF_EDGE_H */
