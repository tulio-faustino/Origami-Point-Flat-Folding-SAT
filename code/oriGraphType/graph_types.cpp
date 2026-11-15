#include "graph_types.hpp"

#include <algorithm>  // std::ranges::sort
#include <cmath>      // std::atan2, std::fmod, std::isfinite, std::round
#include <functional> // std::less
#include <ranges>     // std::views::zip (C++23)
#include <tuple>      // std::get over zip_view's tuple
#include <utility>    // std::make_pair
#include <vector>

// ---------------------------------------------------------------------------
// origamiGraph: the crease pattern plus the geometry that used to live in
// main.cpp. The bodies are the same as before; the only change is that the
// edge list is reached through the member instead of a parameter.
// ---------------------------------------------------------------------------

std::vector<point> origamiGraph::corners() const {
    int n = num_corners_;
    if (n < 0)
        n = 0;
    if (n > static_cast<int>(points.size()))
        n = static_cast<int>(points.size());

    return std::vector<point>(points.begin(), points.begin() + n);
}

// Sorts the edges in a point counter-clockwise
void origamiGraph::ccw_sort_point_edges(point &p){

    std::vector<double> base_angles;
    // Anchor point 1 unit to the right to calculate the angles.
    point angle_anchor = point(p.pos_x + 1, p.pos_y, -1);

    // p = point
    // a = anchor
    // e = edge

    // Difference between the anchor and the point (always the same)
    double pa_x = 1, pa_y = 0;

    const int degree = static_cast<int>(p.local_edges.size());
    for(int i = 0; i < degree; i++){

        // to not pick up the same point as the destination point
        double other_x, other_y;
        if(edges[p.local_edges[i]].p1.id == p.id){
            other_x = edges[p.local_edges[i]].p2.pos_x;
            other_y = edges[p.local_edges[i]].p2.pos_y;
        }else{
            other_x = edges[p.local_edges[i]].p1.pos_x;
            other_y = edges[p.local_edges[i]].p1.pos_y;
        }

        double pe_x = other_x - p.pos_x, pe_y  = other_y - p.pos_y;

        // Edge case: degenerate edge (both endpoints at the same position).
        // atan2(0, 0) silently returns 0, which would look like a valid angle,
        // so it is fixed explicitly to keep the ordering deterministic. *
        if (pe_x * pe_x + pe_y * pe_y < 1e-18) {
            base_angles.push_back(0.0);
            continue;
        }

        double cross = pa_x * pe_y - pa_y * pe_x;
        double dot = pa_x * pe_x + pa_y * pe_y;
        double angle = std::atan2(cross, dot);

        // Edge case: NaN/Inf (from NaN coordinates) must never reach the
        // comparator, otherwise the strict weak ordering required by sort breaks. *
        if (!std::isfinite(angle)) {
            base_angles.push_back(0.0);
            continue;
        }

        angle = std::fmod(angle, 2 * PI);
        if (angle < 0)
            angle += 2 * PI;
        if (angle == 0.0)
            angle = 0.0; // normalizes -0.0 to +0.0

        base_angles.push_back(angle);
    }

    // This part is AI code, I need to read docs on these --------
    auto zipped = std::views::zip(base_angles, p.local_edges);

    // Sorting the zipped view reorders both vectors in place, so index i of
    // base_angles keeps describing index i of p.local_edges after the sort.
    //
    // - ANGLE_EPS: angles closer than this are treated as equal (rounding noise
    //   on nearly collinear edges), and the bucket also makes the comparison a
    //   valid strict weak ordering instead of a raw epsilon comparison.
    //
    //   The edge id breaks ties
    constexpr double ANGLE_EPS = 1e-12;

    std::ranges::sort(zipped, std::less<>{}, [](auto t) {
        return std::make_pair(std::round(std::get<0>(t) / ANGLE_EPS) * ANGLE_EPS,
                              std::get<1>(t));
    });
    // ------------------------------------------------------------
};

// Basically the same code as before, I need to create a three_point_angle function later.
// local_angles[i] is the counter-clockwise angle between local_edges[i] and
// local_edges[(i + 1) % degree], so the whole vector describes one full turn
// around the point (Kawasaki-Justin needs these gaps).
void origamiGraph::find_angles(point &p){
    const int degree = static_cast<int>(p.local_edges.size());

    // Makes the function idempotent: calling it twice must not duplicate values.
    p.local_angles.clear();

    if (degree == 0)
        return;

    // A single edge has no pair to measure against: going all the way back to
    // itself is a full turn.
    if (degree == 1) {
        p.local_angles.push_back(2 * PI);
        return;
    }

    for(int i = 0; i < degree; i++){
        // The edge that follows i, wrapping back to the first one
        const int k = (i + 1) % degree;

        double A_x, A_y, B_x, B_y;
        // Figures out which of the edge points is the correct one
        if(edges[p.local_edges[i]].p1.id == p.id){
            A_x = edges[p.local_edges[i]].p2.pos_x;
            A_y = edges[p.local_edges[i]].p2.pos_y;
        }else{
            A_x = edges[p.local_edges[i]].p1.pos_x;
            A_y = edges[p.local_edges[i]].p1.pos_y;
        }
        if(edges[p.local_edges[k]].p1.id == p.id){
            B_x = edges[p.local_edges[k]].p2.pos_x;
            B_y = edges[p.local_edges[k]].p2.pos_y;
        }else{
            B_x = edges[p.local_edges[k]].p1.pos_x;
            B_y = edges[p.local_edges[k]].p1.pos_y;
        }

        double pa_x = A_x - p.pos_x, pa_y = A_y - p.pos_y;
        double pb_x = B_x - p.pos_x, pb_y = B_y - p.pos_y;

        double cross = pa_x * pb_y - pa_y * pb_x;
        double dot = pa_x * pb_x + pa_y * pb_y;
        double angle = std::atan2(cross, dot);

        // Edge case: degenerate edge (both endpoints at the same position).
        // atan2(0, 0) silently returns 0, which would look like a valid angle,
        // so it is fixed explicitly to keep the values deterministic.
        if (pa_x * pa_x + pa_y * pa_y < 1e-18 ||
            pb_x * pb_x + pb_y * pb_y < 1e-18 ||
            !std::isfinite(angle)) {
            p.local_angles.push_back(0.0);
            continue;
        }

        angle = std::fmod(angle, 2 * PI);
        if (angle < 0)
            angle += 2 * PI;
        if (angle == 0.0)
            angle = 0.0; // normalizes -0.0 to +0.0

        p.local_angles.push_back(angle);
    }
}

// Convenience that applies the whole per-point pipeline to the graph.
void origamiGraph::fix(){
    for (point &p : points) {
        ccw_sort_point_edges(p);
        find_angles(p);
    }
}
