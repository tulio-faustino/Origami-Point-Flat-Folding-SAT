#include <iostream>
#include <vector>
#include <cmath>
#include <fstream>
#include <string>
#include <limits>
#include <algorithm>   // std::ranges::sort
#include <ranges>      // std::views::zip (C++23)
#include <functional>  // std::less
#include <tuple>       // std::get over zip_view's tuple
#include <utility>     // std::make_pair

#include "printing_functions.hpp"

// I need to add a colision detector, since intersecting edges create another point.

void ccw_sort_point_edges(point &p, std::vector<edge> &e);

void find_angles(point &p, std::vector<edge> &e);

int main() {

    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);

    int num_points = 0;
    if (!(std::cin >> num_points)) {
        std::cerr << "Error: Failed to read number of points.\n";
        return 1;
    }

    std::vector<point> points;
    points.reserve(num_points);

    for (int i = 0; i < num_points; ++i) {
        double x, y;
        std::cin >> x >> y;
        points.emplace_back(x, y, i); // ID synchronized with the index i
    }

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

    for (int i = 0; i < num_points; i++){
        ccw_sort_point_edges(points[i], edges);
    }

    // Local angles between consecutive edges of each point. Must run after
    // ccw_sort_point_edges, since it relies on the counter-clockwise order of
    // local_edges to know which edge comes after which.
    for (int i = 0; i < num_points; i++){
        find_angles(points[i], edges);
    }

    if (points.size() < 4) {
        std::cerr << "Fatal error: The crease pattern generated fewer than 4 points. "
                  << "Plot boundaries are undefined.\n";
        return 1; // Terminate with error
    }

    // Generate output file (plot/instructions/origami.plot)
    std::cout << "got to here \n";
    generate_plot_file(points, edges);

    // Debug the CCW ordering + local angles of every point
    // (plot/instructions/ccw_test.plot)
    test_ccw_sort(points, edges, plot_paths::kCcwTestPlot, /*with_angles=*/true);

    return 0;
}

// Sorts the edges in a point counter clockwise
void ccw_sort_point_edges(point &p, std::vector<edge> &e){

    std::vector<double> base_angles;
    // Anchor point 1 unit to the right to calculate the angles.
    point angle_anchor = point(p.pos_x + 1, p.pos_y, -1);

    // p = point
    // a = anchor
    // e = edge

    // Difference between the anchor and the point (always the same)
    double pa_x = 1, pa_y = 0;

    for(int i = 0; i < p.local_edges.size(); i++){

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
