#pragma once

#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// Shared data model of the crease pattern.
// Kept in its own header so that the graph/geometry code in
// solver/src/graph_types.cpp, the SMT encoder in solver/src/mv_solver.cpp and
// the printing helpers in plot_generator/src/printing_functions.cpp all talk
// about the same point/edge types.
// ---------------------------------------------------------------------------

constexpr double PI = 3.141592653589793;

// The input format carries the number of corners on its first line (right
// before the number of points), and the corners of the paper are assumed to be
// the first points of the file (ids 0 .. num_corners - 1). kDefaultCorners is
// only the fallback when the count is not given; a closed crease pattern needs
// at least a triangle (kMinCorners).
inline constexpr int kDefaultCorners = 4;
inline constexpr int kMinCorners = 3;

struct point {
    int id;
    double pos_x;
    double pos_y;
    std::vector<int> local_edges; // Edges that intercede this point (ordered counter-clockwise)
    std::vector<double> local_angles; // Angles between the edges, starting from local_edges[0] onwards
    point(double x, double y, int _id) : id(_id), pos_x(x), pos_y(y) {}
};

struct edge {
    int id;
    point p1;
    point p2;
    double size;
    char type;

    edge(point origin, point destination, int _id)
        : id(_id), p1(origin), p2(destination), type('u') {
        double dx = destination.pos_x - origin.pos_x;
        double dy = destination.pos_y - origin.pos_y;
        size = std::hypot(dx, dy);
    }
};

// ---------------------------------------------------------------------------
// The whole crease pattern: the points and the edges that connect them.
//
// The geometry that used to live in main.cpp is a set of methods now, because
// it needs both the points and the edge list:
//   - ccw_sort_point_edges() orders the edges incident to one point CCW,
//   - find_angles() fills local_angles for that point,
//   - fix() runs both for every point of the graph.
//
// The structs stay free-standing (and public) so the reader, the solver and the
// printing helpers keep sharing the exact same point/edge definitions.
// ---------------------------------------------------------------------------
class origamiGraph {
public:
    std::vector<point> points;
    std::vector<edge> edges;
    std::vector<char> sort_assignment;

    origamiGraph() = default;

    // Number of leading points that define the paper boundary: ids
    // 0 .. num_corners() - 1, the convention used by the input format.
    int num_corners() const { return num_corners_; }
    void set_num_corners(int n) { num_corners_ = n; }

    // Copy of the first num_corners() points (clamped to the graph size, so an
    // inconsistent count can never read past the vector).
    std::vector<point> corners() const;

    // Orders the edges incident to `p` counter-clockwise and, in the same pass,
    // finds the angles between consecutive edges.
    void ccw_sort_point_edges(point &p);
    void find_angles(point &p);

    // Applies ccw_sort_point_edges() and find_angles() to every point.
    void fix();

private:
    int num_corners_ = kDefaultCorners;
};
