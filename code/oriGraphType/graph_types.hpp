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

// Tolerance of the paper-boundary helpers. It is RELATIVE to the size of the
// paper (origamiGraph::paper_scale()), so the same pattern quoted in
// millimetres and in metres gets the same treatment; paper_scale() has a floor
// of one unit, so a degenerate outline never produces a zero tolerance.
inline constexpr double kBoundaryEpsFactor = 1e-9;

// Fraction of that tolerance used to pull a point created on the border
// slightly INTO the paper: the exact intersection lies on the border, but
// floating point arithmetic can place it a hair outside, so the stored point is
// nudged inside by a distance well below the tolerance. It still counts as a
// boundary point and cannot be mistaken for "outside the paper".
inline constexpr double kBoundaryNudgeFraction = 0.1;

// One side of the paper outline, used only to detect where an extended edge
// hits the paper. These are NOT creases and are never stored in
// origamiGraph::edges.
struct boundary_segment {
    double x1;
    double y1;
    double x2;
    double y2;
};

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

    // One flag per point, indexed by point id: '1' means a normal (interior)
    // point, '0' means a point on the paper boundary. Corners and the points
    // created by extend_edges() are '0'. When the vector does not cover a point
    // (empty graph, or a producer that did not fill it), is_normal_point()
    // falls back to the positional convention id < num_corners().
    std::vector<char> point_is_normal;

    origamiGraph() = default;

    // Boundary flags. A point on the paper boundary takes the paper's own
    // angle, so the Maekawa-Justin count and the extension logic must tell it
    // apart from an interior vertex; the id range test id < num_corners() alone
    // is not enough once extend_edges() adds new points on the border.
    bool is_normal_point(int id) const;
    bool is_boundary_point(int id) const { return !is_normal_point(id); }
    void set_point_is_normal(int id, bool normal);

    // Rebuilds point_is_normal from scratch: every point normal, the first
    // num_corners() points on the boundary. Geometric detection of non-corner
    // points that happen to lie on an outline segment is NOT done here; that is
    // extend_edges()'s job.
    void refresh_boundary_flags();

    // Number of leading points that define the paper boundary: ids
    // 0 .. num_corners() - 1, the convention used by the input format.
    int num_corners() const { return num_corners_; }
    void set_num_corners(int n) { num_corners_ = n; }

    // Copy of the first num_corners() points (clamped to the graph size, so an
    // inconsistent count can never read past the vector).
    std::vector<point> corners() const;

    // Length used to scale the geometric tolerances: the diagonal of the
    // bounding box of the corners, with a floor of 1.0.
    double paper_scale() const;

    // The paper outline as a closed polygon: one segment per pair of
    // consecutive corners (the last corner connects back to the first). Empty
    // when there are fewer than kMinCorners corners. Built on demand; these
    // segments are not creases.
    std::vector<boundary_segment> boundary_segments() const;

    // True when `p` is at most `eps` away from any outline segment.
    bool point_on_boundary(const point& p, double eps) const;

    // Orders the edges incident to `p` counter-clockwise and, in the same pass,
    // finds the angles between consecutive edges.
    void ccw_sort_point_edges(point &p);
    void find_angles(point &p);

    // Applies ccw_sort_point_edges() and find_angles() to every point.
    void fix();

private:
    int num_corners_ = kDefaultCorners;
};
