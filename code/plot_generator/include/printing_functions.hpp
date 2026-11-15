#pragma once

#include <string>
#include <vector>

#include "graph_types.hpp"

// ---------------------------------------------------------------------------
// Directory layout (relative to the working directory the program runs from):
//   plot/instructions/  <- .txt files written here by the printing functions
//   plot/img/           <- images rendered here by plot_generator/scripts/graph_plotter.py
// ---------------------------------------------------------------------------
namespace plot_paths {
inline constexpr const char* kInstructionsDir = "plot/instructions";
inline constexpr const char* kOrigamiTxt     = "plot/instructions/origami.txt";
inline constexpr const char* kCcwTestTxt     = "plot/instructions/ccw_test.txt";
inline constexpr const char* kSortTxt        = "plot/instructions/sort.txt";
} // namespace plot_paths

// Writes the whole crease pattern (points + typed edges) of `graph` to
// `filename`, in the format consumed by
// plot_generator/scripts/graph_plotter.py. When `filename` is omitted the
// default plot_paths::kOrigamiTxt is used.
// Missing parent directories are created automatically.
// Aborts (with a message on stderr) if the pattern has fewer than 4 points,
// because the plot boundaries are undefined in that case.
void generate_plot_file(const origamiGraph& graph,
                        const std::string& filename = plot_paths::kOrigamiTxt);

// Same layout as generate_plot_file, but the edge type column comes from the
// MV assignment returned by MVAssignmentSolver::solve() instead of edge::type:
//
//   <num_points>
//   <x> <y>                  (per point)
//   <num_edges>
//   <p1> <p2> <'0'|'M'|'V'|'U'>
//
// The solver answers UNSAT/UNKNOWN with a single-element vector {'U'}. In that
// case there is no per-edge assignment, but the file is still written (it shows
// the pattern that could not be folded) with 'U' on EVERY edge. An empty
// assignment is treated the same way. When `filename` is omitted the default
// plot_paths::kSortTxt is used.
//
// The 'U' edges are drawn in purple by plot_generator/scripts/graph_plotter.py,
// which titles the resulting image UNSAT.
void generate_sort_plot_file(const origamiGraph& graph,
                             const std::vector<char>& assignment,
                             const std::string& filename = plot_paths::kSortTxt);

// Debug helper for origamiGraph::ccw_sort_point_edges:
// writes, for EVERY point of the pattern, its p.local_edges vector *in the
// current order*, one block per point.
//
// Implemented in plot_generator/src/printing_functions.cpp.
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
// consecutive edges); plot_generator/scripts/graph_plotter.py converts them to
// degrees.
//
// The resulting file is plotted by plot_ccw_sequence() in
// plot_generator/scripts/graph_plotter.py. When `filename` is omitted the
// default plot_paths::kCcwTestTxt is used.
void test_ccw_sort(const origamiGraph& graph,
                   const std::string& filename = plot_paths::kCcwTestTxt,
                   bool with_angles = false);
