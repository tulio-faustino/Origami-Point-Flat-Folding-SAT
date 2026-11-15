#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>   // std::ranges::sort
#include <ranges>      // std::views::zip (C++23)
#include <functional>  // std::less
#include <tuple>       // std::get over zip_view's tuple
#include <utility>     // std::make_pair / std::move

#include "graph_types.hpp"
#include "mv_solver.hpp"
#include "printing_functions.hpp"

// Everything that talks to cvc5 (Kawasaki-Justin check, Maekawa-Justin and
// Big-Little-Big encodings, model read-back and its independent re-check) lives
// in MVAssignmentSolver (solver/src/mv_solver.cpp). This file only reads the
// pattern, orders the edges of every point counter-clockwise, computes the
// local angles that the encoder needs, and calls the solver.

// I need to add a colision detector, since intersecting edges create another point.

void ccw_sort_point_edges(point &p, std::vector<edge> &e);

void find_angles(point &p, std::vector<edge> &e);

void make_intersections(std::vector<point> &o_Points, std::vector<edge> &o_Edges,
                            std::vector<point> &new_points, std::vector<edge> &new_edges){};

void extend_edges(std::vector<point> &o_Points, std::vector<edge> &o_Edges,
                    std::vector<point> &new_points, std::vector<edge> &new_edges){};

// Tolerance used when comparing corner coordinates: two heights closer than
// this count as a tie.
constexpr double kCornerEps = 1e-9;

// Index of the corner that has to be the base of the angles: the lowest one,
// and the leftmost one when two corners share the same height.
int find_base_corner(const std::vector<point> &corners);

// The paper outline must arrive counter-clockwise, starting from the base
// corner. Ordering the corners is the responsibility of whoever generates the
// crease pattern, so this only checks (and reports) the problem, it never fixes
// the order.
bool check_corners_ccw(const std::vector<point> &corners);

int main() {

    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);

    // The first line is "num_corners num_points": the number of corners comes
    // right before the number of points. The corners are the points that define
    // the paper boundary and are assumed to be the first ones of the file
    // (ids 0 .. num_corners - 1).
    int num_corners = 0;
    int num_points = 0;
    if (!(std::cin >> num_corners >> num_points)) {
        std::cerr << "Error: Failed to read the number of corners and points.\n";
        return 1;
    }

    // Sanity checks on the paper outline: a closed crease pattern needs at least
    // three corners (a triangle) and there can never be more corners than
    // points, since every corner is one of the points.
    if (num_corners < kMinCorners) {
        std::cerr << "Error: The crease pattern needs at least " << kMinCorners
                  << " corners (the points that define the paper boundary), got "
                  << num_corners << ".\n";
        return 1;
    }

    if (num_corners > num_points) {
        std::cerr << "Error: The number of corners (" << num_corners
                  << ") cannot be larger than the number of points ("
                  << num_points << ").\n";
        return 1;
    }

    std::vector<point> points;
    points.reserve(num_points);

    for (int i = 0; i < num_points; ++i) {
        double x, y;
        std::cin >> x >> y;
        points.emplace_back(x, y, i); // ID synchronized with the index i
    }

    // The corners are also handed over as their own vector: a copy of the first
    // num_corners points, which are the ones that define the paper boundary.
    std::vector<point> corners(points.begin(), points.begin() + num_corners);

    // Only the corners are checked for the counter-clockwise order; whatever
    // order the remaining (interior) points arrive in is irrelevant.
    if (!check_corners_ccw(corners))
        return 1;

    char test_assignment[3] = {'0', 'M', 'V'};
    int num_edges = 0;
    if (!(std::cin >> num_edges)) {
        std::cerr << "Error: Failed to read number of edges.\n";
        return 1;
    }

    std::vector<edge> edges; // May need to change this to a map, If I decide to make the edges breakable
    edges.reserve(num_edges);

    // Inserts edges
    for (int i = 0; i < num_edges; i++) {
        int u, v;
        std::cin >> u >> v;
        // u and v correspond to point indices in the 'points' vector
        edges.emplace_back(points[u], points[v], i); // ID synchronized with the index i
        edges[i].type = test_assignment[i % 3];
        // Pushes the edge id to the points
        points[u].local_edges.push_back(i);
        points[v].local_edges.push_back(i);

    }

    // Sorts the edges counter-clockwise and finds the angles (also counter clock-wise) for every point
    for (int i = 0; i < num_points; i++){
        ccw_sort_point_edges(points[i], edges);
        find_angles(points[i], edges);
    }

    if (points.size() < 4) {
        std::cerr << "Fatal error: The crease pattern generated fewer than 4 points. "
                  << "Plot boundaries are undefined.\n";
        return 1; // Terminate with error
    }

    // Hand the finished pattern over: local_edges is already sorted CCW and
    // local_angles is already filled, which is exactly what the encoder needs.
    // The graph is moved into the solver, and points() / edges() below give the
    // plotting code read access to it after solve().
    MVAssignmentSolver solver(std::move(points), std::move(edges), num_corners);

    // solve() is where the SMT instance is actually created and decided.
    const bool sat = solver.solve();
    const std::vector<char> &assignment = solver.assignment();

    bool model_ok = true;
    if (sat) {
        std::cout << "MV assignment from the solver, re-checked independently:\n";
        model_ok = solver.validate_assignment();
        if (!model_ok)
            std::cerr << "INTERNAL ERROR: the solver model violates Maekawa-Justin.\n";
    }

    // Generate output file (plot/instructions/origami.plot)
    generate_plot_file(solver.points(), solver.edges());

    // Same pattern, but with the MV assignment coming from the solver
    // (plot/instructions/sort.plot). On UNSAT/UNKNOWN every edge is written
    // as 'U' and the plotter titles the image UNSAT.
    generate_sort_plot_file(solver.points(), solver.edges(), assignment);

    // Debug the CCW ordering + local angles of every point
    // (plot/instructions/ccw_test.plot)
    test_ccw_sort(solver.points(), solver.edges(), plot_paths::kCcwTestPlot, /*with_angles=*/true);

    if (!sat)
        return 2; // no MV assignment exists / the solver could not decide

    return model_ok ? 0 : 3; // 3: the encoder produced a model that fails the check
}

// Sorts the edges in a point counter-clockwise
void ccw_sort_point_edges(point &p, std::vector<edge> &e){

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
        if(e[p.local_edges[i]].p1.id == p.id){
            other_x = e[p.local_edges[i]].p2.pos_x;
            other_y = e[p.local_edges[i]].p2.pos_y;
        }else{
            other_x = e[p.local_edges[i]].p1.pos_x;
            other_y = e[p.local_edges[i]].p1.pos_y;
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
void find_angles(point &p, std::vector<edge> &e){
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
        if(e[p.local_edges[i]].p1.id == p.id){
            A_x = e[p.local_edges[i]].p2.pos_x;
            A_y = e[p.local_edges[i]].p2.pos_y;
        }else{
            A_x = e[p.local_edges[i]].p1.pos_x;
            A_y = e[p.local_edges[i]].p1.pos_y;
        }
        if(e[p.local_edges[k]].p1.id == p.id){
            B_x = e[p.local_edges[k]].p2.pos_x;
            B_y = e[p.local_edges[k]].p2.pos_y;
        }else{
            B_x = e[p.local_edges[k]].p1.pos_x;
            B_y = e[p.local_edges[k]].p1.pos_y;
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

// The base of the angles is always the lowest corner of the paper. When two
// corners share the same height, the leftmost one wins.
int find_base_corner(const std::vector<point> &corners){
    int base = 0;

    for (int i = 1; i < static_cast<int>(corners.size()); ++i) {
        const double dy = corners[i].pos_y - corners[base].pos_y;
        const double dx = corners[i].pos_x - corners[base].pos_x;

        if (dy < -kCornerEps) {
            base = i;                               // strictly lower corner
        } else if (std::abs(dy) <= kCornerEps && dx < -kCornerEps) {
            base = i;                               // same height, further left
        }
    }

    return base;
}

// Checks the outline only (the `corners` vector), never `points`:
//   1. corner 0 is the base (lowest, leftmost on a height tie);
//   2. walking 1 -> 2 -> ... the angle measured from the base on the positive
//      x axis only grows, i.e. the outline turns counter-clockwise;
//   3. the signed area is positive (guards against a folded over outline that
//      happens to have increasing angles).
bool check_corners_ccw(const std::vector<point> &corners){
    const int n = static_cast<int>(corners.size());

    // The base corner must be the first one, since it is the origin of every
    // angle used to describe the paper.
    const int base = find_base_corner(corners);
    if (base != 0) {
        std::cerr << "Error: The corners are not organized counter-clockwise. "
                  << "Corner 0 (" << corners[0].pos_x << ", " << corners[0].pos_y
                  << ") must be the lowest one (leftmost on a height tie), but the base "
                  << "corner is actually corner " << base << " ("
                  << corners[base].pos_x << ", " << corners[base].pos_y << ").\n";
        return false;
    }

    double previous_angle = -1.0;
    for (int i = 1; i < n; ++i) {
        double dx = corners[i].pos_x - corners[0].pos_x;
        double dy = corners[i].pos_y - corners[0].pos_y;

        if (std::abs(dy) <= kCornerEps)
            dy = 0.0; // flush rounding noise on the base height

        if (dx * dx + dy * dy <= kCornerEps * kCornerEps) {
            std::cerr << "Error: The corners are not organized counter-clockwise: "
                      << "corner " << i << " (" << corners[i].pos_x << ", "
                      << corners[i].pos_y << ") is at the same position as corner 0 ("
                      << corners[0].pos_x << ", " << corners[0].pos_y << ").\n";
            return false;
        }

        // Every other corner is at or above the base, so this angle is in
        // [0, pi]: a counter-clockwise walk around the base must strictly
        // increase it.
        const double angle = std::atan2(dy, dx);
        if (angle <= previous_angle) {
            std::cerr << "Error: The corners are not organized counter-clockwise "
                      << "around the base corner 0 (" << corners[0].pos_x << ", "
                      << corners[0].pos_y << "). Corner " << i << " ("
                      << corners[i].pos_x << ", " << corners[i].pos_y
                      << ")" << " does not continue the rotation: the corners must be "
                      << "listed counter-clockwise, starting from the lowest one "
                      << "(leftmost on a height tie).\n";
            return false;
        }
        previous_angle = angle;
    }

    // Shoelace formula, twice the signed area: positive means counter-clockwise.
    double twice_area = 0.0;
    for (int i = 0; i < n; ++i) {
        const point &a = corners[i];
        const point &b = corners[(i + 1) % n];
        twice_area += a.pos_x * b.pos_y - b.pos_x * a.pos_y;
    }

    if (twice_area <= 0.0) {
        std::cerr << "Error: The corners are not organized counter-clockwise: the "
                  << "outline they describe is clockwise or degenerate (signed area = "
                  << twice_area / 2.0 << ").\n";
        return false;
    }

    return true;
}
