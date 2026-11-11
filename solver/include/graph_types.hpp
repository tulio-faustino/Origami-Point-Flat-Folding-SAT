#pragma once

#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// Shared data model of the crease pattern.
// Kept in its own header so that the reader/geometry code in solver/src/main.cpp,
// the SMT encoder in solver/src/mv_solver.cpp and the printing helpers in
// plot_generator/src/printing_functions.cpp all talk about the same
// point/edge types.
// ---------------------------------------------------------------------------

constexpr double PI = 3.141592653589793;

// The input format has no boundary flag: the corners of the paper are assumed
// to be the first points of the file (ids 0 .. kDefaultCorners - 1). The
// default paper is a square (4 corners); a closed crease pattern needs at
// least a triangle (3).
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
