#include "printing_functions.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <system_error>
#include <utility>

namespace {

// Creates the parent directory of `filename` (e.g. plot/instructions) if needed.
bool ensure_parent_dir(const std::string& filename) {
    const std::filesystem::path path(filename);
    if (!path.has_parent_path()) {
        return true;
    }

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        std::cerr << "Error: could not create directory '"
                  << path.parent_path().string() << "': " << ec.message() << "\n";
        return false;
    }
    return true;
}

// Coordinates of the endpoint of `e` that is NOT the point `p`.
std::pair<double, double> neighbor_position(const point& p, const edge& e) {
    const point& other = (e.p1.id == p.id) ? e.p2 : e.p1;
    return {other.pos_x, other.pos_y};
}

// Writes the crease pattern with an explicit type per edge, index-aligned with
// `edges`. Shared body of generate_plot_file() (types taken from edge::type)
// and generate_sort_plot_file() (types taken from the solver assignment).
bool write_plot_file(const std::string& filename,
                     const std::vector<point>& points,
                     const std::vector<edge>& edges,
                     const std::vector<char>& types) {
    // Boundary verification check
    if (points.size() < 4) {
        std::cerr << "Export error: The graph contains only " << points.size()
                  << " point(s). At least 4 points are required to define the plot boundaries.\n";
        return false; // Abort file export
    }

    if (types.size() != edges.size()) {
        std::cerr << "Export error: " << types.size() << " type(s) for "
                  << edges.size() << " edge(s).\n";
        return false;
    }

    if (!ensure_parent_dir(filename)) {
        return false;
    }

    std::ofstream out(filename);
    if (!out.is_open()) {
        std::cerr << "Error: Failed to create file " << filename << ".\n";
        return false;
    }

    // Write points
    out << points.size() << " //number points\n";
    for (const auto& p : points) {
        out << p.pos_x << " " << p.pos_y << "\n";
    }

    // Write edges with their type ('0', 'M', 'V' or 'U')
    out << edges.size() << " //number edges\n";
    for (std::size_t i = 0; i < edges.size(); ++i) {
        out << edges[i].p1.id << " " << edges[i].p2.id << " " << types[i] << "\n";
    }

    out.close();
    std::cout << "Plot file '" << filename << "' successfully generated!\n";
    return true;
}

// Same angle definition used by ccw_sort_point_edges(): sweep measured CCW
// from the +x axis, normalized to [0, 2*pi).
double ccw_angle(const point& p, double nx, double ny) {
    double dx = nx - p.pos_x;
    double dy = ny - p.pos_y;

    // Degenerate edge: keep it deterministic instead of atan2(0, 0) == 0.
    if (dx * dx + dy * dy < 1e-18) {
        return 0.0;
    }

    double angle = std::atan2(dy, dx);
    if (!std::isfinite(angle)) {
        return 0.0;
    }

    angle = std::fmod(angle, 2 * PI);
    if (angle < 0) {
        angle += 2 * PI;
    }
    return angle;
}

} // namespace


void generate_plot_file(const std::string& filename, const std::vector<point>& points, const std::vector<edge>& edges) {
    std::vector<char> types;
    types.reserve(edges.size());
    for (const auto& e : edges) {
        types.push_back(e.type);
    }

    write_plot_file(filename, points, edges, types);
}


void generate_plot_file(const std::vector<point>& points, const std::vector<edge>& edges) {
    generate_plot_file(plot_paths::kOrigamiPlot, points, edges);
}


void generate_sort_plot_file(const std::string& filename,
                             const std::vector<point>& points,
                             const std::vector<edge>& edges,
                             const std::vector<char>& assignment) {
    // solve_mv_assignment() reports UNSAT/UNKNOWN as a single-element {'U'}:
    // there is no per-edge assignment then, but the plot is still useful (it
    // shows the pattern that failed), so every edge is marked 'U'.
    const bool unsat = assignment.empty() || assignment.front() == 'U';

    std::vector<char> types;
    if (unsat) {
        std::cout << "Sort plot: no MV assignment (UNSAT/UNKNOWN), "
                     "marking every edge as 'U'.\n";
        types.assign(edges.size(), 'U');
    } else {
        if (assignment.size() != edges.size()) {
            std::cerr << "Sort plot error: got " << assignment.size()
                      << " assignment(s) for " << edges.size() << " edge(s).\n";
            return;
        }
        types = assignment; // already 'M' / 'V' per edge
    }

    write_plot_file(filename, points, edges, types);
}


void generate_sort_plot_file(const std::vector<point>& points,
                             const std::vector<edge>& edges,
                             const std::vector<char>& assignment) {
    generate_sort_plot_file(plot_paths::kSortPlot, points, edges, assignment);
}


void test_ccw_sort(const std::vector<point>& points,
                   const std::vector<edge>& edges,
                   const std::string& filename,
                   bool with_angles) {
    if (points.empty()) {
        std::cerr << "CCW test error: the graph has no points.\n";
        return;
    }

    if (!ensure_parent_dir(filename)) {
        return;
    }

    std::ofstream out(filename);
    if (!out.is_open()) {
        std::cerr << "Error: Failed to create file " << filename << ".\n";
        return;
    }

    out << std::setprecision(10);
    out << points.size() << " " << (with_angles ? 1 : 0)
        << " //number of points, local angles included (1 = yes, 0 = no)\n";

    std::cout << "CCW test: p.local_edges order for every point ("
              << points.size() << " points)"
              << (with_angles ? " with local angles" : "") << "\n";

    for (const point& p : points) {
        out << p.id << " " << p.pos_x << " " << p.pos_y << " "
            << p.local_edges.size() << " //point_id x y degree\n";

        std::string angle_sequence;
        for (std::size_t k = 0; k < p.local_edges.size(); ++k) {
            const int edge_id = p.local_edges[k];
            if (edge_id < 0 || edge_id >= static_cast<int>(edges.size())) {
                std::cerr << "CCW test error: point " << p.id
                          << " references invalid edge " << edge_id << ".\n";
                continue;
            }

            const auto [nx, ny] = neighbor_position(p, edges[edge_id]);
            const double angle_deg = ccw_angle(p, nx, ny) * 180.0 / PI;

            out << k << " " << edge_id << " " << nx << " " << ny << " "
                << angle_deg << " //order edge_id neighbor_x neighbor_y angle_deg\n";

            if (!angle_sequence.empty()) {
                angle_sequence += ", ";
            }
            angle_sequence += std::to_string(static_cast<int>(std::lround(angle_deg)));
        }

        std::cout << "  point " << p.id << " (" << p.pos_x << ", " << p.pos_y
                  << ") d=" << p.local_edges.size() << ": " << angle_sequence
                  << " deg";

        if (with_angles) {
            // Written in radians; the plotting script converts to degrees.
            out << p.local_angles.size();
            std::string local_sequence;
            for (const double alpha : p.local_angles) {
                out << " " << alpha;
                if (!local_sequence.empty()) {
                    local_sequence += ", ";
                }
                local_sequence += std::to_string(
                    static_cast<int>(std::lround(alpha * 180.0 / PI)));
            }
            out << " //local angles (rad) between local_edges[k] and "
                   "local_edges[k+1]\n";

            std::cout << "  | local: " << local_sequence << " deg";
        }

        std::cout << "\n";
    }

    out.close();
    std::cout << "CCW test file '" << filename << "' successfully generated!\n";
}
