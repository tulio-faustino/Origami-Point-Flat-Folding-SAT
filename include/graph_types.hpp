#pragma once

#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// Shared data model of the crease pattern.
// Kept in its own header so that both src/main.cpp and the printing helpers in
// src/printing_functions.cpp can talk about the same point/edge types.
// ---------------------------------------------------------------------------

constexpr double PI = 3.141592653589793;

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
