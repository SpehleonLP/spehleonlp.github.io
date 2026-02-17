#include "dcel_build_cmd.h"
#include "skeleton_graph_cmd.h"
#include <math.h>
#include <stdio.h>
#include <algorithm>
#include <vector>

int dcel_build_Execute(DCELBuildCmd* cmd)
{
    if (!cmd || !cmd->edges) {
        fprintf(stderr, "[dcel_build] Error: NULL input\n");
        return -1;
    }

    const std::vector<UndirectedEdge>& edges = *cmd->edges;
    size_t num_edges = edges.size();

    /* Step 1: Move vertices into mesh */
    cmd->mesh.vertices = std::move(cmd->vertices);
    cmd->mesh.half_edges.clear();
    cmd->mesh.features.clear();

    /* Step 2: Create two half-edges per undirected edge */
    cmd->mesh.half_edges.resize(num_edges * 2);

    for (size_t i = 0; i < num_edges; i++) {
        const UndirectedEdge& e = edges[i];

        int32_t fwd = (int32_t)(2 * i);
        int32_t twn = (int32_t)(2 * i + 1);

        int32_t origin_fwd = e.v0;
        int32_t origin_twn = e.v1;

        /* Compute tangent and length from vertex positions */
        float dx_fwd = (float)cmd->mesh.vertices[origin_twn].x - (float)cmd->mesh.vertices[origin_fwd].x;
        float dy_fwd = (float)cmd->mesh.vertices[origin_twn].y - (float)cmd->mesh.vertices[origin_fwd].y;
        float len = sqrtf(dx_fwd * dx_fwd + dy_fwd * dy_fwd);

        float tx_fwd, ty_fwd;
        if (len > 1e-6f) {
            tx_fwd = dx_fwd / len;
            ty_fwd = dy_fwd / len;
        } else {
            tx_fwd = 0.0f;
            ty_fwd = 0.0f;
        }

        /* Forward half-edge: v0 -> v1 */
        DCELHalfEdge& he_fwd = cmd->mesh.half_edges[fwd];
        he_fwd.origin = origin_fwd;
        he_fwd.twin = twn;
        he_fwd.next = -1;
        he_fwd.prev = -1;
        he_fwd.face = -1;
        he_fwd.type = e.type;
        he_fwd.tangent_x = tx_fwd;
        he_fwd.tangent_y = ty_fwd;
        he_fwd.energy = 0.0f;
        he_fwd.length = len;

        /* Twin half-edge: v1 -> v0 */
        DCELHalfEdge& he_twn = cmd->mesh.half_edges[twn];
        he_twn.origin = origin_twn;
        he_twn.twin = fwd;
        he_twn.next = -1;
        he_twn.prev = -1;
        he_twn.face = -1;
        he_twn.type = e.type;
        he_twn.tangent_x = -tx_fwd;
        he_twn.tangent_y = -ty_fwd;
        he_twn.energy = 0.0f;
        he_twn.length = len;
    }

    /* Step 3: Build per-vertex outgoing edge lists */
    size_t num_verts = cmd->mesh.vertices.size();
    std::vector<std::vector<int32_t>> outgoing(num_verts);

    for (size_t i = 0; i < cmd->mesh.half_edges.size(); i++) {
        int32_t origin = cmd->mesh.half_edges[i].origin;
        if (origin >= 0 && origin < (int32_t)num_verts)
            outgoing[origin].push_back((int32_t)i);
    }

    /* Step 4: Sort each outgoing list by atan2(tangent_y, tangent_x) in CCW order */
    for (size_t v = 0; v < num_verts; v++) {
        std::vector<int32_t>& out = outgoing[v];
        if (out.size() < 2) continue;

        std::sort(out.begin(), out.end(), [&](int32_t a, int32_t b) {
            float angle_a = atan2f(cmd->mesh.half_edges[a].tangent_y,
                                   cmd->mesh.half_edges[a].tangent_x);
            float angle_b = atan2f(cmd->mesh.half_edges[b].tangent_y,
                                   cmd->mesh.half_edges[b].tangent_x);
            return angle_a < angle_b;
        });
    }

    /* Step 5: Link next/prev around each vertex
     *
     * For each vertex with outgoing edges sorted CCW as (e_0, e_1, ..., e_{n-1}):
     *   twin(e_i).next = e_{(i+1) mod n}
     *   e_{(i+1) mod n}.prev = twin(e_i)
     */
    for (size_t v = 0; v < num_verts; v++) {
        const std::vector<int32_t>& out = outgoing[v];
        size_t n = out.size();
        if (n == 0) continue;

        for (size_t i = 0; i < n; i++) {
            int32_t e_i = out[i];
            int32_t e_next = out[(i + 1) % n];

            int32_t twin_i = cmd->mesh.half_edges[e_i].twin;

            cmd->mesh.half_edges[twin_i].next = e_next;
            cmd->mesh.half_edges[e_next].prev = twin_i;
        }
    }

    /* Step 6: Set vertex.edge to first outgoing half-edge (or -1 if none) */
    for (size_t v = 0; v < num_verts; v++) {
        if (!outgoing[v].empty())
            cmd->mesh.vertices[v].edge = outgoing[v][0];
        else
            cmd->mesh.vertices[v].edge = -1;
    }

    printf("[dcel_build] %zu half-edges from %zu undirected edges\n",
           cmd->mesh.half_edges.size(), num_edges);

    return 0;
}
