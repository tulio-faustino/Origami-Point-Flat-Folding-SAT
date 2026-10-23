#pragma once

#include <string>
#include <vector>

#include "graph_types.hpp"

// ---------------------------------------------------------------------------
// Directory layout (relative to the working directory the program runs from):
//   plot/instructions/  <- .plot files written here by the printing functions
//   plot/img/           <- images rendered here by scripts/graph_plotter.py
// ---------------------------------------------------------------------------
namespace plot_paths {
inline constexpr const char* kInstructionsDir = "plot/instructions";
inline constexpr const char* kOrigamiPlot    = "plot/instructions/origami.plot";
inline constexpr const char* kCcwTestPlot    = "plot/instructions/ccw_test.plot";
} // namespace plot_paths

// Writes the whole crease pattern (points + typed edges) to `filename`, in the
// format consumed by scripts/graph_plotter.py.
// Missing parent directories are created automatically.
// Aborts (with a message on stderr) if the pattern has fewer than 4 points,
// because the plot boundaries are undefined in that case.
void generate_plot_file(const std::string& filename,
                        const std::vector<point>& points,
                        const std::vector<edge>& edges);

// Same as above, writing to plot_paths::kOrigamiPlot.
void generate_plot_file(const std::vector<point>& points,
                        const std::vector<edge>& edges);

// Debug helper for ccw_sort_point_edges:
// writes, for EVERY point of the pattern, its p.local_edges vector *in the
// current order*, one block per point.
//
// Implemented in src/printing_functions.cpp.
//
// Output format (whitespace separated, '//' starts a comment):
//
//   <num_points> <with_angles>
//   <point_id> <x> <y> <degree>            // block header, repeated per point
//   <order> <edge_id> <nx> <ny> <angle>    // degree lines, in CCW order
//   <n> <a0> <a1> ... <an-1>               // only when with_angles == true
//
// where <order> is the index inside p.local_edges (0, 1, 2, ...),
// <nx> <ny> are the coordinates of the edge endpoint opposite to that point and
// <angle> is the edge's angle measured CCW from the +x axis, in degrees.
// If the sort worked, <angle> must be non-decreasing inside each block.
//
// The extra line carries p.local_angles in RADIANS (the gaps between
// consecutive edges); scripts/graph_plotter.py converts them to degrees.
//
// The resulting file is plotted by plot_ccw_sequence() in scripts/graph_plotter.py.
void test_ccw_sort(const std::vector<point>& points,
                   const std::vector<edge>& edges,
                   const std::string& filename = plot_paths::kCcwTestPlot,
                   bool with_angles = false);
