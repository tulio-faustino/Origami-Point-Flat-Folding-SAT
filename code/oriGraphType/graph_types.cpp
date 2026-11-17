#include "graph_types.hpp"

#include <algorithm>  // std::min, std::ranges::sort
#include <cmath>      // std::atan2, std::fmod, std::hypot, std::isfinite, std::round
#include <cstddef>    // std::size_t
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

namespace {

// Squared distance from (px, py) to the segment `s`.
double distance2_to_segment(double px, double py, const boundary_segment& s) {
    const double vx = s.x2 - s.x1;
    const double vy = s.y2 - s.y1;
    const double wx = px - s.x1;
    const double wy = py - s.y1;

    const double len2 = vx * vx + vy * vy;
    double t = (len2 > 0.0) ? (wx * vx + wy * vy) / len2 : 0.0;
    if (t < 0.0)
        t = 0.0;
    else if (t > 1.0)
        t = 1.0;

    const double dx = px - (s.x1 + t * vx);
    const double dy = py - (s.y1 + t * vy);
    return dx * dx + dy * dy;
}

} // namespace

bool origamiGraph::is_normal_point(int id) const {
    if (id < 0 || id >= static_cast<int>(points.size()))
        return false;

    if (id < static_cast<int>(point_is_normal.size()))
        return point_is_normal[id] != 0;

    // No flag for this point: fall back to the positional convention.
    return id >= num_corners_;
}

void origamiGraph::set_point_is_normal(int id, bool normal) {
    if (id < 0 || id >= static_cast<int>(points.size()))
        return;

    if (static_cast<int>(point_is_normal.size()) <= id)
        point_is_normal.resize(static_cast<std::size_t>(id) + 1, 1);

    point_is_normal[id] = normal ? 1 : 0;
}

void origamiGraph::refresh_boundary_flags() {
    point_is_normal.assign(points.size(), 1);

    const int n = std::min<int>(num_corners_, static_cast<int>(points.size()));
    for (int i = 0; i < n; ++i)
        point_is_normal[i] = 0;
}

double origamiGraph::paper_scale() const {
    const std::vector<point> outline = corners();
    if (outline.empty())
        return 1.0;

    double min_x = outline[0].pos_x, max_x = outline[0].pos_x;
    double min_y = outline[0].pos_y, max_y = outline[0].pos_y;

    for (const point& p : outline) {
        min_x = std::min(min_x, p.pos_x);
        max_x = std::max(max_x, p.pos_x);
        min_y = std::min(min_y, p.pos_y);
        max_y = std::max(max_y, p.pos_y);
    }

    const double diagonal = std::hypot(max_x - min_x, max_y - min_y);
    return diagonal > 1.0 ? diagonal : 1.0;
}

std::vector<boundary_segment> origamiGraph::boundary_segments() const {
    const std::vector<point> outline = corners();

    std::vector<boundary_segment> segments;
    const int n = static_cast<int>(outline.size());
    if (n < kMinCorners)
        return segments;

    segments.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const point& a = outline[i];
        const point& b = outline[(i + 1) % n];
        segments.push_back({a.pos_x, a.pos_y, b.pos_x, b.pos_y});
    }
    return segments;
}

bool origamiGraph::point_on_boundary(const point& p, double eps) const {
    if (eps < 0.0)
        eps = 0.0;

    const double eps2 = eps * eps;
    for (const boundary_segment& s : boundary_segments()) {
        if (distance2_to_segment(p.pos_x, p.pos_y, s) <= eps2)
            return true;
    }
    return false;
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

// Rebuilds the incidence lists from scratch and re-derives the ordering/angles.
// Points and edges are assumed to be consistent already (edge ids equal to their
// index, endpoints inside points[]); stale ids are skipped instead of
// dereferencing out of range.
void origamiGraph::rebuild_incidence() {
    for (point &p : points) {
        p.local_edges.clear();
        p.local_angles.clear();
    }

    const int num_points = static_cast<int>(points.size());
    for (const edge &e : edges) {
        const int u = e.p1.id;
        const int v = e.p2.id;
        if (u < 0 || v < 0 || u >= num_points || v >= num_points)
            continue;

        points[u].local_edges.push_back(e.id);
        points[v].local_edges.push_back(e.id);
    }

    fix();
}
