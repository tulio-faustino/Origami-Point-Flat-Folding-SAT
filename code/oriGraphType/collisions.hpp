#pragma once

#include <string>
#include <vector>

#include "graph_types.hpp"

// ---------------------------------------------------------------------------
// Collision resolution: turn every place where two creases collide into a
// vertex of the graph.
//
// The graph format assumes that every crease starts and ends at a known point.
// Two edges crossing in their interiors break that assumption: the crossing is
// a vertex with four angles around it, and the foldability rules applied to the
// two original endpoints would be applied to the wrong set of angles. The same
// happens when the endpoint of one crease lands inside another one (T-junction).
// resolve_collisions() copies the pattern into a new graph with those vertices
// inserted and the edges split accordingly, so `mv_solve` sees a planar graph.
//
// Three collision classes are handled:
//   1. proper crossing   -> a new vertex, both edges split there;
//   2. T-junction        -> an existing point lying on another edge splits it,
//                           no new vertex is created;
//   3. collinear overlap -> the pattern is REJECTED (see below), because two
//                           creases lying on top of each other have no canonical
//                           interpretation: merging them into one crease would
//                           silently delete input, and keeping both would put
//                           two coincident edges in the same CCW fan and break
//                           Maekawa/Kawasaki.
//
// Tolerances. The only source of imprecision accepted here is floating point
// arithmetic (including the decimal rounding already present in the input), so
// the tolerance is kCollisionEpsFactor * paper_scale() -- see graph_types.hpp.
// Every candidate coordinate goes through a point index: if an existing point
// is within that distance it is reused instead of creating a near-duplicate
// vertex, and two points closer than the tolerance are merged during the copy.
// Two edges closer than the tolerance are therefore treated as touching, and
// anything farther apart is a genuine geometric feature.
//
// Relationship with extend_edges(): both are graph producers that follow the
// same recipe (copy the points, rebuild the edges from point ids, refresh the
// boundary flags, rebuild the incidence and call fix()). Extending a crease to
// the border can create new crossings, so the recommended pipeline is
// intersections -> extend -> intersections.
// ---------------------------------------------------------------------------

// Distance (in paper units) under which two coordinates are considered the same
// point by resolve_collisions().
double collision_eps(const origamiGraph& graph);

// What resolve_collisions() found and did, for the console report.
struct collision_report {
    int input_points = 0;
    int input_edges = 0;

    // Normalisation of the input.
    int merged_points = 0;   // coincident input points collapsed into one id
    int dropped_edges = 0;   // self loop, length <= eps, or duplicate (u, v)

    // Collisions.
    int crossings = 0;          // proper interior crossings found
    int snapped_crossings = 0;  // crossings that landed on an existing point
    int new_points = 0;         // points created by crossings
    int t_junctions = 0;        // existing points snapped onto another edge
    int split_edges = 0;        // input edges that ended up with 2+ segments
    int dropped_splits = 0;     // splits too close to the previous one (< eps)
    int dropped_duplicate_segments = 0;
    int ambiguous_snaps = 0;    // two or more candidates within eps of a coordinate

    // Result.
    int output_points = 0;
    int output_edges = 0;
    int isolated_points = 0;         // resulting non-corner points with degree 0
    int odd_interior_points = 0;     // odd degree inside the paper (crease ends mid-paper)

    // Non-empty when the pattern was rejected (dst stays empty).
    std::string error;
    std::vector<std::string> overlaps;
};

// Copies `src` into `dst` with every collision resolved and returns true.
//
// `dst` is a brand new graph: points and edges are reindexed (the ids are NOT
// the input ids), the corner count is preserved and the first num_corners()
// points are still the corners, `local_edges` / `local_angles` are rebuilt and
// the boundary flags are refreshed. `sort_assignment` is left empty: the edge
// count changes, so an existing MV assignment cannot be reused.
//
// Returns false when the pattern is rejected (collinear overlap, two coincident
// corners, no valid outline). `report.error` and `report.overlaps` explain why,
// `dst` is left empty and the caller must not create the graph.
//
// Complexity: O(E log E + V log E + C1 + C2 + K log K), where C1/C2 are the
// candidate pairs/point-edge pairs surviving the AABB broad phase and K is the
// number of split events; degenerately clustered input falls back to O(E^2),
// the same as the plain brute force.
bool resolve_collisions(const origamiGraph& src, origamiGraph& dst, collision_report& report);
