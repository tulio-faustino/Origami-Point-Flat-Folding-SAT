#include <iostream>
#include <vector>
#include <cmath>
#include <fstream>
#include <string>
#include <limits>

struct point {
    int id;
    int pos_x;
    int pos_y;
    point(int x, int y, int _id) : id(_id), pos_x(x), pos_y(y) {}
};

struct edge {
    int id;
    point p1;
    point p2;
    float size;
    char type;

    edge(point origin, point destination, int _id) 
        : id(_id), p1(origin), p2(destination), type('u') {
        float dx = static_cast<float>(destination.pos_x - origin.pos_x);
        float dy = static_cast<float>(destination.pos_y - origin.pos_y);
        size = std::hypot(dx, dy);
    }
};

void generate_plot_file(const std::string& filename, const std::vector<point>& points, const std::vector<edge>& edges);

int main() {
    // Fast I/O operations
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
        int x, y;
        std::cin >> x >> y;
        points.emplace_back(x, y, i); // ID synchronized with the index i
    }

    char test_assignment[3] = {'0', 'M', 'V'};
    int num_edges = 0;
    if (!(std::cin >> num_edges)) {
        std::cerr << "Error: Failed to read number of edges.\n";
        return 1;
    }

    std::vector<edge> edges;
    edges.reserve(num_edges);

    for (int i = 0; i < num_edges; ++i) {
        int u, v;
        std::cin >> u >> v;
        // u and v correspond to point indices in the 'points' vector
        edges.emplace_back(points[u], points[v], i); // ID synchronized with the index i
        edges[i].type = test_assignment[i % 3];
    }

    if (points.size() < 4) {
        std::cerr << "Fatal error: The crease pattern generated fewer than 4 points. "
                  << "Plot boundaries are undefined.\n";
        return 1; // Terminate with error
    }

    // Generate output file
    std::cout << "got to here \n";
    generate_plot_file("origami.plot", points, edges);
    
    return 0;
}

void generate_plot_file(const std::string& filename, const std::vector<point>& points, const std::vector<edge>& edges) {
    // Boundary verification check
    if (points.size() < 4) {
        std::cerr << "Export error: The graph contains only " << points.size() 
                  << " point(s). At least 4 points are required to define the plot boundaries.\n";
        return; // Abort file export
    }

    std::ofstream out(filename);
    if (!out.is_open()) {
        std::cerr << "Error: Failed to create file " << filename << ".\n";
        return;
    }

    // Write points
    out << points.size() << " //number points\n";
    for (const auto& p : points) {
        out << p.pos_x << " " << p.pos_y << "\n";
    }

    // Write edges
    out << edges.size() << " //number edges\n";
    for (const auto& e : edges) {
        // Output origin, destination, and edge type ('0', 'M', or 'V')
        out << e.p1.id << " " << e.p2.id << " " << e.type << "\n";
    }

    out.close();
    std::cout << "Plot file '" << filename << "' successfully generated!\n";
}