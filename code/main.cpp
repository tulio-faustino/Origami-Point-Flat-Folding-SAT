#include <algorithm>  // std::count, std::max, std::min
#include <cmath>      // std::abs, std::hypot, used by the geometry
#include <cstdlib>    // std::system, to run the plotting script
#include <filesystem> // std::filesystem::path, to place the instruction files
#include <fstream>    // std::ifstream, to read the .in files from the disk
#include <iostream>
#include <sstream>    // std::istringstream, to split a command line
#include <string>
#include <utility>    // std::move
#include <vector>

#include "collisions.hpp"
#include "graph_types.hpp"
#include "mv_solver.hpp"
#include "printing_functions.hpp"

// ---------------------------------------------------------------------------
// main: a console over a set of NAMED graphs.
//
// Every pattern read from the disk stays in memory under a name, and the user
// chooses what to do with each of them:
//
//   insert <name> <file.in>    read a crease pattern into a named graph
//   create <name> <n_corners>  create an EMPTY graph with n_corners corners
//   mv_solve <name>            solve the MV assignment, keep it in
//                              graph.sort_assignment (no file is written)
//   extend <src> [dst]         extend every edge of <src> to the paper border
//   intersections <src> [dst]  split the colliding creases of <src> into a
//                              planar graph (see collisions.hpp)
//   print <type> [grafo] [file] write + render the instructions of a graph
//   graphs | list              list the graphs loaded so far
//   clear                      clear the terminal screen
//   help / quit
//
// The commands arrive on stdin, one per line, so the whole console can be
// scripted ("printf 'insert g test_files/graph.in\nquit\n' | ./bin/main").
// ---------------------------------------------------------------------------

// Tolerance used when comparing corner coordinates: two heights closer than
// this count as a tie.
constexpr double kCornerEps = 1e-9;

// The plotting script, relative to the repository root (the directory the
// program is expected to run from, the same path the Makefile uses).
constexpr const char* kPython = "python3";
constexpr const char* kPlotterScript = "code/plot_generator/scripts/graph_plotter.py";

// ---------------------------------------------------------------------------
// Named graph registry
// ---------------------------------------------------------------------------
struct NamedGraph {
    std::string name;
    origamiGraph graph;
};

// Graphs in insertion order, so `graphs` / `list` prints a stable list.
std::vector<NamedGraph> graphs;

// Last graph the user touched (insert / create / mv_solve). `print` without an
// explicit graph name works on it.
std::string current_graph;

// The kinds of instruction file the console can print.
enum class PrintType { Origami, Sort, Ccw, CcwAngles };

// --- helpers ---------------------------------------------------------------

NamedGraph* find_graph(const std::string& name) {
    for (NamedGraph& entry : graphs) {
        if (entry.name == name)
            return &entry;
    }
    return nullptr;
}

// Names end up inside commands and as identifiers on the console, so they are
// restricted to a conservative set ([A-Za-z0-9_.-]).
bool valid_name(const std::string& name) {
    if (name.empty())
        return false;

    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok)
            return false;
    }
    return true;
}

bool parse_int(const std::string& text, int& value) {
    std::istringstream iss(text);
    int parsed = 0;
    if (!(iss >> parsed))
        return false;

    std::string rest;
    if (iss >> rest)
        return false; // trailing garbage

    value = parsed;
    return true;
}

// Single-quotes `text` for the shell, so paths with spaces survive. Fails when
// the text itself carries a quote, which cannot be escaped this way.
bool shell_quote(const std::string& text, std::string& quoted) {
    if (text.find('\'') != std::string::npos)
        return false;

    quoted = "'" + text + "'";
    return true;
}

// --- geometry (same code as the old main) ----------------------------------

int find_base_corner(const std::vector<point>& corners);
bool check_corners_ccw(const std::vector<point>& corners);

// --- input -----------------------------------------------------------------

// Reads one crease pattern from `in`, exactly like the old main did: the first
// line is "num_corners num_points", then the points, then the number of edges
// and one "u v" line per edge. The corners are the first points of the file and
// are validated (counter-clockwise outline, base corner first); `fix()` then
// sorts the edges of every point CCW and fills the local angles.
//
// On failure returns false and `error` explains why (the graph is left empty).
bool load_graph(std::istream& in, origamiGraph& graph, std::string& error);

// --- solving ---------------------------------------------------------------

// Same pipeline as the old solve: hand the pattern over to the SMT encoder,
// re-check the model and STORE the assignment in graph.sort_assignment.
// Returns 0 SAT, 2 UNSAT / UNKNOWN, 3 the model failed the re-check.
int run_solve(origamiGraph& graph);

// Geometry steps. `original` is read-only and `new_graph` receives the
// generated points/edges.
//
// extend_edges(): copies the pattern and, for every edge endpoint that does not
// already lie on the paper border, casts a ray along the edge's own line until
// it hits the outline. The hit becomes a new boundary point (snapped to an
// existing point when within tolerance) and a stub edge with the base (dummy)
// type kBaseEdgeType is added. Collisions with the other creases are NOT handled
// here: that is make_intersections()'s job, and extending can create new
// crossings, so the recommended order is intersections -> extend -> intersections.
bool make_intersections(const origamiGraph& original, origamiGraph& new_graph);
void extend_edges(const origamiGraph& original, origamiGraph& new_graph);

// --- printing --------------------------------------------------------------

bool parse_print_type(const std::string& text, PrintType& type);
const char* default_instruction_file(PrintType type);

// Full path of a custom instruction file. A bare name (no directory) is taken
// relative to plot/instructions/, so `print sort g ori1` writes
// plot/instructions/ori1.txt instead of ./ori1.txt. An explicit path wins.
std::string instruction_path(const std::string& filename);

// Every printing path goes through these corner checks: the plotting code takes
// the corners for granted, so a graph whose outline is unknown, degenerate or
// clockwise is refused before any file is written.
bool graph_can_be_printed(const NamedGraph& entry);

// Writes the instruction file and then renders it with the python script. The
// default file name of the type is used when `filename` is empty (it is
// overwritten, which is the intended default).
int print_graph(NamedGraph& entry, PrintType type, const std::string& filename);

// --- console ---------------------------------------------------------------

void print_commands();
void print_print_help();
void print_graphs();

int command_mv_solve(const std::vector<std::string>& tokens);
int command_print(const std::vector<std::string>& tokens);
void command_extend(const std::vector<std::string>& tokens, bool intersections);

int main() {
    std::ios_base::sync_with_stdio(false);

    std::cout << "Origami graph console: named crease patterns + MV solving.\n";
    print_commands();

    // Status of the last mv_solve, so the process still reports 0 / 2 / 3.
    int status = 0;
    std::string line;

    while (true) {
        std::cout << "> " << std::flush;

        if (!std::getline(std::cin, line))
            break; // Ctrl-D or a read error ends the console

        std::vector<std::string> tokens;
        {
            std::istringstream iss(line);
            std::string token;
            while (iss >> token)
                tokens.push_back(token);
        }

        if (tokens.empty())
            continue; // just Enter: nothing to do

        const std::string& command = tokens[0];

        if (command == "insert") {
            if (tokens.size() != 3u) {
                std::cerr << "Usage: insert <name> <file.in>\n";
                continue;
            }

            const std::string& name = tokens[1];
            const std::string& path = tokens[2];

            if (!valid_name(name)) {
                std::cerr << "Error: '" << name
                          << "' is not a valid graph name (use letters, digits, '_', '-' or '.').\n";
                continue;
            }

            if (find_graph(name)) {
                std::cerr << "Error: a graph named '" << name
                          << "' already exists (nothing was changed).\n";
                continue;
            }

            std::ifstream in(path);
            if (!in.is_open()) {
                std::cerr << "Error: could not open '" << path << "'.\n";
                continue;
            }

            origamiGraph graph;
            std::string error;
            if (!load_graph(in, graph, error)) {
                std::cerr << "Error: '" << path << "': " << error << "\n";
                continue;
            }

            std::cout << "Graph '" << name << "': " << graph.points.size() << " points, "
                      << graph.edges.size() << " edges, " << graph.num_corners()
                      << " corners.\n";

            graphs.push_back({name, std::move(graph)});
            current_graph = name;

        } else if (command == "create") {
            if (tokens.size() != 3u) {
                std::cerr << "Usage: create <name> <n_corners>\n";
                continue;
            }

            const std::string& name = tokens[1];

            if (!valid_name(name)) {
                std::cerr << "Error: '" << name
                          << "' is not a valid graph name (use letters, digits, '_', '-' or '.').\n";
                continue;
            }

            int num_corners = 0;
            if (!parse_int(tokens[2], num_corners)) {
                std::cerr << "Error: '" << tokens[2] << "' is not an integer number of corners.\n";
                continue;
            }

            if (num_corners < kMinCorners) {
                std::cerr << "Error: the crease pattern needs at least " << kMinCorners
                          << " corners (the points that define the paper boundary), got "
                          << num_corners << ".\n";
                continue;
            }

            if (find_graph(name)) {
                std::cerr << "Error: a graph named '" << name
                          << "' already exists (nothing was changed).\n";
                continue;
            }

            origamiGraph graph;
            graph.set_num_corners(num_corners);

            graphs.push_back({name, std::move(graph)});
            current_graph = name;

            std::cout << "Graph '" << name << "' created: empty, " << num_corners
                      << " corners. It has no points yet, so 'print' will refuse it until "
                         "extend/intersections fill it.\n";

        } else if (command == "mv_solve") {
            const int solve_status = command_mv_solve(tokens);
            if (solve_status >= 0)
                status = solve_status;

        } else if (command == "extend") {
            command_extend(tokens, /*intersections=*/false);

        } else if (command == "intersections") {
            command_extend(tokens, /*intersections=*/true);

        } else if (command == "print") {
            command_print(tokens);

        } else if (command == "graphs" || command == "list") {
            print_graphs();

        } else if (command == "clear") {
            // ANSI escape: erase the whole screen and move the cursor home, so
            // the console keeps working right after it (no shell involved).
            std::cout << "\x1b[2J\x1b[H" << std::flush;

        } else if (command == "help") {
            print_commands();

        } else if (command == "quit" || command == "exit") {
            break;

        } else {
            std::cerr << "Unknown command '" << command
                      << "'. Type 'help' to see the available commands.\n";
        }
    }

    return status;
}

// ---------------------------------------------------------------------------
// Console commands
// ---------------------------------------------------------------------------

void print_commands() {
    std::cout
        << "Commands:\n"
        << "  insert <name> <file.in>    read a crease pattern into a named graph\n"
        << "  create <name> <n_corners>  create an empty graph with n_corners corners\n"
        << "  mv_solve <name>            solve the MV assignment (kept in graph.sort_assignment)\n"
        << "  extend <src> [dst]         extend every edge of <src> to the paper border\n"
        << "  intersections <src> [dst]  split the creases of <src> that collide (crossings,\n"
        << "                             T-junctions) into a planar graph\n"
        << "  print <type> [graph] [file]  write + render the instructions (print --help)\n"
        << "  graphs | list              list the graphs loaded so far\n"
        << "  clear                      clear the terminal screen\n"
        << "  help                       show this list again\n"
        << "  quit                       leave the program (Ctrl-D works too)\n";
}

void print_print_help() {
    std::cout
        << "print <type> [graph] [file]\n"
        << "  Writes the instruction .txt with the C++ printing functions and then renders\n"
        << "  it with " << kPlotterScript << " (no window is opened).\n"
        << "  graph : omitted => the current graph ('"
        << (current_graph.empty() ? "<none>" : current_graph) << "')\n"
        << "  file  : omitted => the default path of the type (it is overwritten);\n"
        << "           a bare name is written into " << plot_paths::kInstructionsDir << "/\n"
        << "           (an explicit path is used as given)\n"
        << "  types\n"
        << "    origami   points + edges with their current type     -> "
        << plot_paths::kOrigamiTxt << "\n"
        << "    sort      graph.sort_assignment (needs mv_solve)     -> "
        << plot_paths::kSortTxt << "\n"
        << "    ccw       p.local_edges order of every point         -> "
        << plot_paths::kCcwTestTxt << "\n"
        << "    ccw_ang   ccw + the local angles in degrees          -> "
        << plot_paths::kCcwTestTxt << "\n"
        << "  the image goes to plot/img/<instruction file name>.png\n"
        << "  (ccw_ang with the default file keeps plot/img/ccw_test_angles.png)\n"
        << "  a graph that fails the corner checks (count, counter-clockwise outline,\n"
        << "  base corner first) is refused: nothing is written.\n";
}

void print_graphs() {
    if (graphs.empty()) {
        std::cout << "No graphs loaded yet (use 'insert' or 'create').\n";
        return;
    }

    std::cout << "Graphs (" << graphs.size() << "):\n";
    for (const NamedGraph& entry : graphs) {
        const bool solved = !entry.graph.sort_assignment.empty();
        std::cout << "  " << (entry.name == current_graph ? "* " : "  ") << entry.name
                  << "  points=" << entry.graph.points.size()
                  << "  edges=" << entry.graph.edges.size()
                  << "  corners=" << entry.graph.num_corners()
                  << "  solved=" << (solved ? "yes" : "no") << "\n";
    }
}

int command_mv_solve(const std::vector<std::string>& tokens) {
    if (tokens.size() != 2u) {
        std::cerr << "Usage: mv_solve <name>\n";
        return -1;
    }

    NamedGraph* entry = find_graph(tokens[1]);
    if (entry == nullptr) {
        std::cerr << "Error: there is no graph named '" << tokens[1]
                  << "' (use 'graphs' to list them).\n";
        return -1;
    }

    if (entry->graph.points.empty()) {
        std::cerr << "Error: graph '" << entry->name
                  << "' is empty; there is nothing to solve.\n";
        return -1;
    }

    current_graph = entry->name;
    return run_solve(entry->graph);
}

void command_extend(const std::vector<std::string>& tokens, bool intersections) {
    const char* command = intersections ? "intersections" : "extend";
    const char* suffix = intersections ? "_intersections" : "_extended";

    if (tokens.size() < 2u || tokens.size() > 3u) {
        std::cerr << "Usage: " << command << " <source> [destination]\n";
        return;
    }

    NamedGraph* source = find_graph(tokens[1]);
    if (source == nullptr) {
        std::cerr << "Error: there is no graph named '" << tokens[1]
                  << "' (use 'graphs' to list them).\n";
        return;
    }

    // No destination given (the user just pressed Enter): the generated graph is
    // named after the source.
    std::string destination = tokens.size() == 3u ? tokens[2] : source->name + suffix;

    if (!valid_name(destination)) {
        std::cerr << "Error: '" << destination
                  << "' is not a valid graph name (use letters, digits, '_', '-' or '.').\n";
        return;
    }

    if (find_graph(destination)) {
        std::cerr << "Error: a graph named '" << destination
                  << "' already exists (nothing was changed).\n";
        return;
    }

    origamiGraph generated;
    if (intersections) {
        // The resolver reports its own diagnostics and rejects patterns it
        // cannot represent (collinear overlap), in which case no graph exists.
        if (!make_intersections(source->graph, generated))
            return;
    } else {
        extend_edges(source->graph, generated);
    }

    if (generated.points.empty()) {
        std::cerr << "Error: '" << command << "' produced no graph; '"
                  << destination << "' was not created.\n";
        return;
    }

    const auto boundary_points =
        std::count(generated.point_is_normal.begin(), generated.point_is_normal.end(), 0);

    std::cout << "Graph '" << destination << "': " << generated.points.size()
              << " points (" << boundary_points << " on the paper boundary), "
              << generated.edges.size() << " edges.\n";

    graphs.push_back({destination, std::move(generated)});
    current_graph = destination;
}

int command_print(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2u || tokens.size() > 4u) {
        std::cerr << "Usage: print <type> [graph] [file]   (print --help for the types)\n";
        return 1;
    }

    if (tokens[1] == "--help" || tokens[1] == "-h") {
        print_print_help();
        return 0;
    }

    PrintType type;
    if (!parse_print_type(tokens[1], type)) {
        std::cerr << "Error: unknown print type '" << tokens[1]
                  << "'. Use 'print --help' to see the types.\n";
        return 1;
    }

    std::string name;
    if (tokens.size() >= 3u) {
        name = tokens[2];
    } else if (!current_graph.empty()) {
        name = current_graph;
    } else {
        std::cerr << "Error: no graph given and no current graph yet. "
                     "Usage: print <type> [graph] [file]\n";
        return 1;
    }

    NamedGraph* entry = find_graph(name);
    if (entry == nullptr) {
        std::cerr << "Error: there is no graph named '" << name
                  << "' (use 'graphs' to list them).\n";
        return 1;
    }

    const std::string filename = tokens.size() == 4u ? tokens[3] : std::string();
    return print_graph(*entry, type, filename);
}

// ---------------------------------------------------------------------------
// Printing
// ---------------------------------------------------------------------------

bool parse_print_type(const std::string& text, PrintType& type) {
    if (text == "origami") {
        type = PrintType::Origami;
    } else if (text == "sort") {
        type = PrintType::Sort;
    } else if (text == "ccw") {
        type = PrintType::Ccw;
    } else if (text == "ccw_ang" || text == "ccw_angles") {
        type = PrintType::CcwAngles;
    } else {
        return false;
    }
    return true;
}

const char* default_instruction_file(PrintType type) {
    switch (type) {
        case PrintType::Origami:
            return plot_paths::kOrigamiTxt;
        case PrintType::Sort:
            return plot_paths::kSortTxt;
        case PrintType::Ccw:
        case PrintType::CcwAngles:
            return plot_paths::kCcwTestTxt;
    }
    return plot_paths::kOrigamiTxt;
}

std::string instruction_path(const std::string& filename) {
    const std::filesystem::path given(filename);
    if (given.has_parent_path())
        return filename; // the user chose the directory explicitly

    return (std::filesystem::path(plot_paths::kInstructionsDir) / given).string();
}

bool graph_can_be_printed(const NamedGraph& entry) {
    const origamiGraph& graph = entry.graph;

    if (graph.points.size() < 4) {
        std::cerr << "Error: graph '" << entry.name << "' has only " << graph.points.size()
                  << " point(s); at least 4 points are required to define the plot "
                     "boundaries.\n";
        return false;
    }

    const int num_corners = graph.num_corners();
    if (num_corners < kMinCorners) {
        std::cerr << "Error: graph '" << entry.name << "' declares " << num_corners
                  << " corner(s); the crease pattern needs at least " << kMinCorners
                  << " (the points that define the paper boundary).\n";
        return false;
    }

    if (num_corners > static_cast<int>(graph.points.size())) {
        std::cerr << "Error: graph '" << entry.name << "' declares " << num_corners
                  << " corners for " << graph.points.size() << " point(s).\n";
        return false;
    }

    // Outline rules: corner 0 is the base (lowest, leftmost on a height tie), the
    // angles measured from it grow strictly and the signed area is positive.
    // check_corners_ccw() reports the exact problem on stderr.
    if (!check_corners_ccw(graph.corners())) {
        std::cerr << "Error: graph '" << entry.name
                  << "' does not satisfy the corner checks; nothing was printed.\n";
        return false;
    }

    return true;
}

int print_graph(NamedGraph& entry, PrintType type, const std::string& filename) {
    if (!graph_can_be_printed(entry))
        return 1;

    if (type == PrintType::Sort && entry.graph.sort_assignment.empty()) {
        std::cerr << "Error: graph '" << entry.name
                  << "' has no MV assignment yet; run 'mv_solve " << entry.name
                  << "' first.\n";
        return 1;
    }

    const bool default_file = filename.empty();
    const std::string path = default_file ? default_instruction_file(type)
                                         : instruction_path(filename);

    std::string quoted_path;
    if (!shell_quote(path, quoted_path)) {
        std::cerr << "Error: the file name '" << path << "' contains a quote, which is not "
                     "supported.\n";
        return 1;
    }

    // 1. Write the instruction .txt with the C++ printing helpers (the same
    //    files the old main wrote, in plot/instructions/).
    switch (type) {
        case PrintType::Origami:
            generate_plot_file(entry.graph, path);
            break;
        case PrintType::Sort:
            generate_sort_plot_file(entry.graph, entry.graph.sort_assignment, path);
            break;
        case PrintType::Ccw:
            test_ccw_sort(entry.graph, path, /*with_angles=*/false);
            break;
        case PrintType::CcwAngles:
            test_ccw_sort(entry.graph, path, /*with_angles=*/true);
            break;
    }

    // 2. Render it. --no-show keeps the script from opening a window (and from
    //    blocking a scripted run).
    std::string command = std::string(kPython) + " " + kPlotterScript;
    switch (type) {
        case PrintType::Ccw:
            command += " --ccw " + quoted_path;
            break;
        case PrintType::CcwAngles:
            command += " --ccw " + quoted_path + " --angles";
            break;
        case PrintType::Origami:
        case PrintType::Sort:
            command += " " + quoted_path;
            break;
    }
    command += " --no-show";

    // The old `make plot-ccw-angles` naming, kept for the default file only.
    if (type == PrintType::CcwAngles && default_file)
        command += " -o plot/img/ccw_test_angles.png";

    std::cout << "$ " << command << "\n";

    const int result = std::system(command.c_str());
    if (result != 0) {
        std::cerr << "Error: the plotting script failed (status " << result << ").\n";
        return 1;
    }

    return 0;
}

int run_solve(origamiGraph& graph) {
    // Hand the finished pattern over: local_edges is already sorted CCW and
    // local_angles is already filled, which is exactly what the encoder needs.
    MVAssignmentSolver solver(graph);

    // solve() is where the SMT instance is actually created and decided.
    const bool sat = solver.solve();
    const std::vector<char>& assignment = solver.assignment();

    // The console keeps the model inside the graph: `print sort` uses it
    // instead of re-encoding the pattern.
    graph.sort_assignment = assignment;

    bool model_ok = true;
    if (sat) {
        model_ok = solver.validate_assignment();
        if (!model_ok)
            std::cerr << "INTERNAL ERROR: the solver model violates Maekawa-Justin.\n";
    }

    if (!sat)
        return 2; // no MV assignment exists / the solver could not decide

    return model_ok ? 0 : 3; // 3: the encoder produced a model that fails the check
}

bool load_graph(std::istream& in, origamiGraph& graph, std::string& error) {
    // The first line is "num_corners num_points": the number of corners comes
    // right before the number of points. The corners are the points that define
    // the paper boundary and are assumed to be the first ones of the file
    // (ids 0 .. num_corners - 1).
    int num_corners = 0;
    int num_points = 0;
    if (!(in >> num_corners >> num_points)) {
        error = "failed to read the number of corners and points";
        return false;
    }

    if (num_corners < kMinCorners) {
        std::ostringstream message;
        message << "the crease pattern needs at least " << kMinCorners
                << " corners (the points that define the paper boundary), got "
                << num_corners;
        error = message.str();
        return false;
    }

    if (num_corners > num_points) {
        std::ostringstream message;
        message << "the number of corners (" << num_corners
                << ") cannot be larger than the number of points (" << num_points << ")";
        error = message.str();
        return false;
    }

    graph = origamiGraph{};
    graph.set_num_corners(num_corners);
    graph.points.reserve(num_points);

    for (int i = 0; i < num_points; ++i) {
        double x = 0.0;
        double y = 0.0;
        if (!(in >> x >> y)) {
            std::ostringstream message;
            message << "failed to read point " << i << " of " << num_points;
            error = message.str();
            return false;
        }
        graph.points.emplace_back(x, y, i); // ID synchronized with the index i
    }

    // Boundary flags: the corners are the only boundary points the input
    // declares, everything else is normal. extend_edges() marks the additional
    // points it creates on the border (and any input point sitting on it).
    graph.refresh_boundary_flags();

    // Only the corners are checked for the counter-clockwise order; whatever
    // order the remaining (interior) points arrive in is irrelevant.
    if (!check_corners_ccw(graph.corners())) {
        error = "the corners are not organized counter-clockwise (see the message above)";
        return false;
    }

    int num_edges = 0;
    if (!(in >> num_edges)) {
        error = "failed to read the number of edges";
        return false;
    }

    graph.edges.reserve(num_edges);

    // Inserts edges
    for (int i = 0; i < num_edges; ++i) {
        int u = 0;
        int v = 0;
        if (!(in >> u >> v)) {
            std::ostringstream message;
            message << "failed to read edge " << i << " of " << num_edges;
            error = message.str();
            return false;
        }

        if (u < 0 || u >= num_points || v < 0 || v >= num_points) {
            std::ostringstream message;
            message << "edge " << i << " (" << u << ", " << v
                    << ") references a point outside 0.." << (num_points - 1);
            error = message.str();
            return false;
        }

        // u and v correspond to point indices in the 'points' vector
        graph.edges.emplace_back(graph.points[u], graph.points[v], i); // ID synchronized with the index i
        graph.edges[i].type = kBaseEdgeType;
        // Pushes the edge id to the points
        graph.points[u].local_edges.push_back(i);
        graph.points[v].local_edges.push_back(i);
    }

    // Sorts the edges counter-clockwise and finds the angles (also counter
    // clock-wise) for every point.
    graph.fix();

    if (graph.points.size() < 4) {
        error = "the crease pattern generated fewer than 4 points, so the plot boundaries "
                "are undefined";
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Geometry: edge extension to the paper border
// ---------------------------------------------------------------------------

namespace {

// A hit of a ray against the paper outline. `t` is a distance because the ray
// direction is always a unit vector.
struct ray_hit {
    bool found = false;
    double t = 0.0;
    double x = 0.0;
    double y = 0.0;
};

// Intersection of the ray (ox, oy) + t*(dx, dy), t > 0, with the segment `s`.
// The direction is a unit vector, so `t` is a distance. Collinear overlaps are
// reported at the segment endpoint closest to the ray origin.
bool ray_segment_hit(double ox, double oy, double dx, double dy,
                     const boundary_segment& s, double& t) {
    const double sx = s.x2 - s.x1;
    const double sy = s.y2 - s.y1;
    const double seg_len = std::hypot(sx, sy);
    if (seg_len <= 0.0)
        return false; // degenerate outline segment

    const double qx = s.x1 - ox;
    const double qy = s.y1 - oy;

    const double denom = dx * sy - dy * sx;
    const double parallel_eps = 1e-12 * seg_len;

    if (std::abs(denom) > parallel_eps) {
        const double t_ray = (qx * sy - qy * sx) / denom;
        const double u_seg = (qx * dy - qy * dx) / denom;
        if (t_ray <= 0.0 || u_seg < -1e-12 || u_seg > 1.0 + 1e-12)
            return false;
        t = t_ray;
        return true;
    }

    // Parallel: only a collinear ray can reach the segment.
    if (std::abs(qx * dy - qy * dx) > 1e-12 * seg_len)
        return false;

    const double t1 = qx * dx + qy * dy; // (s1 - origin) . direction
    const double t2 = (s.x2 - ox) * dx + (s.y2 - oy) * dy;
    const double lo = std::min(t1, t2);
    const double hi = std::max(t1, t2);
    if (hi <= 0.0)
        return false;

    t = lo > 0.0 ? lo : hi;
    return t > 0.0;
}

// First (closest) hit of the ray with the closed paper outline, ignoring hits
// at a distance <= min_t (the ray origin may be a hair from the border).
bool first_boundary_hit(double ox, double oy, double dx, double dy,
                        const std::vector<boundary_segment>& segments,
                        double min_t, ray_hit& hit) {
    hit = ray_hit{};

    for (const boundary_segment& s : segments) {
        double t = 0.0;
        if (!ray_segment_hit(ox, oy, dx, dy, s, t) || t <= min_t)
            continue;

        if (!hit.found || t < hit.t) {
            hit.found = true;
            hit.t = t;
            hit.x = ox + t * dx;
            hit.y = oy + t * dy;
        }
    }

    return hit.found;
}

// Reuses the point at (x, y) when it is within snap_eps; otherwise appends a
// new point. Any point created here lives on the paper border.
int find_or_add_point(origamiGraph& graph, double x, double y, double snap_eps) {
    const double snap2 = snap_eps * snap_eps;

    for (const point& p : graph.points) {
        const double dx = p.pos_x - x;
        const double dy = p.pos_y - y;
        if (dx * dx + dy * dy <= snap2)
            return p.id;
    }

    const int id = static_cast<int>(graph.points.size());
    graph.points.emplace_back(x, y, id);
    graph.set_point_is_normal(id, false); // new points sit on the border
    return id;
}

// True when some edge incident to `from` already leaves it in the direction
// (dx, dy) (a unit vector). Extending along it would retrace an existing crease
// with a second, overlapping edge; the crease line already continues from this
// vertex, and that neighbour is extended on its own turn.
bool has_collinear_edge_ahead(const origamiGraph& graph, int from, int toward,
                              double dx, double dy) {
    for (const edge& e : graph.edges) {
        int other = -1;
        if (e.p1.id == from)
            other = e.p2.id;
        else if (e.p2.id == from)
            other = e.p1.id;

        if (other < 0 || other == toward)
            continue;

        const double ex = graph.points[other].pos_x - graph.points[from].pos_x;
        const double ey = graph.points[other].pos_y - graph.points[from].pos_y;
        const double len = std::hypot(ex, ey);
        if (len <= 0.0)
            continue;

        const double ux = ex / len;
        const double uy = ey / len;
        const double dot = ux * dx + uy * dy;
        const double cross = ux * dy - uy * dx;
        if (dot > 0.0 && std::abs(cross) <= 1e-9)
            return true;
    }
    return false;
}

bool has_edge_between(const origamiGraph& graph, int a, int b) {
    for (const edge& e : graph.edges) {
        const int u = e.p1.id;
        const int v = e.p2.id;
        if ((u == a && v == b) || (u == b && v == a))
            return true;
    }
    return false;
}

void add_stub(origamiGraph& graph, int from, int to) {
    if (from == to || has_edge_between(graph, from, to))
        return;

    const int id = static_cast<int>(graph.edges.size());
    graph.edges.emplace_back(graph.points[from], graph.points[to], id);
    graph.edges.back().type = kBaseEdgeType; // extension has no fold assigned yet
}

} // namespace

bool make_intersections(const origamiGraph& original, origamiGraph& new_graph) {
    collision_report report;

    if (!resolve_collisions(original, new_graph, report)) {
        if (report.error.empty())
            std::cerr << "Error: the pattern was rejected.\n";
        else
            std::cerr << "Error: the pattern was rejected: " << report.error << "\n";
        for (const std::string& overlap : report.overlaps)
            std::cerr << "  collinear overlap: " << overlap << "\n";
        std::cerr << "Two creases lying on top of each other have no unambiguous "
                     "interpretation, so no graph was created.\n";
        return false;
    }

    std::cout << "Collision scan (tolerance " << collision_eps(original) << "): ";
    if (new_graph.points.empty()) {
        std::cout << "the source pattern has no points, nothing to do.\n";
        return true;
    }
    std::cout << report.crossings << " crossing(s)";
    if (report.snapped_crossings > 0)
        std::cout << " (" << report.snapped_crossings << " snapped onto an existing point)";
    std::cout << ", " << report.t_junctions << " T-junction(s), " << report.new_points
              << " new point(s), " << report.split_edges << " edge(s) split.\n";

    if (report.merged_points > 0)
        std::cout << "  normalised: " << report.merged_points
                  << " coincident point(s) merged";
    if (report.dropped_edges > 0)
        std::cout << ", " << report.dropped_edges << " degenerate/duplicate edge(s) dropped";
    if (report.merged_points > 0 || report.dropped_edges > 0)
        std::cout << "\n";

    if (report.ambiguous_snaps > 0)
        std::cerr << "warning: " << report.ambiguous_snaps
                  << " coordinate(s) had more than one point within the tolerance; the "
                     "nearest one was used.\n";

    if (report.dropped_splits > 0)
        std::cerr << "warning: " << report.dropped_splits
                  << " split(s) were closer than the tolerance to the previous one and were "
                     "absorbed.\n";

    if (report.isolated_points > 0)
        std::cerr << "warning: " << report.isolated_points
                  << " point(s) ended up with no edge.\n";

    if (report.odd_interior_points > 0)
        std::cerr << "warning: " << report.odd_interior_points
                  << " interior vertex(es) have an odd number of creases (a crease ends "
                     "inside the paper); Maekawa/Kawasaki skip them.\n";

    return true;
}

void extend_edges(const origamiGraph& original, origamiGraph& new_graph) {
    new_graph = origamiGraph{};
    new_graph.set_num_corners(original.num_corners());

    if (original.points.empty())
        return;

    if (original.num_corners() < kMinCorners ||
        original.num_corners() > static_cast<int>(original.points.size())) {
        std::cerr << "extend: the source graph does not declare a valid paper outline "
                     "(it needs at least " << kMinCorners << " corners).\n";
        return;
    }

    // 1. Copy the pattern. The edges are rebuilt on top of the copied points so
    //    that the "edge id == index" / "point id == index" invariant holds.
    new_graph.points = original.points;
    for (point& p : new_graph.points) {
        p.local_edges.clear();
        p.local_angles.clear();
    }

    const int num_points = static_cast<int>(new_graph.points.size());
    new_graph.edges.reserve(original.edges.size());
    for (const edge& e : original.edges) {
        const int u = e.p1.id;
        const int v = e.p2.id;
        if (u < 0 || v < 0 || u >= num_points || v >= num_points)
            continue;

        new_graph.edges.emplace_back(new_graph.points[u], new_graph.points[v],
                                     static_cast<int>(new_graph.edges.size()));
        new_graph.edges.back().type = e.type;
    }

    // 2. Boundary flags: corners first, then the input points that happen to
    //    lie on the outline (they must not be extended either).
    new_graph.refresh_boundary_flags();

    const double scale = new_graph.paper_scale();
    const double eps = kBoundaryEpsFactor * scale;
    const double nudge = eps * kBoundaryNudgeFraction;
    const std::vector<boundary_segment> segments = new_graph.boundary_segments();

    for (const point& p : original.points) {
        if (new_graph.is_normal_point(p.id) && new_graph.point_on_boundary(p, eps))
            new_graph.set_point_is_normal(p.id, false);
    }

    // 3. Extend every endpoint that is not already on the border along its own
    //    line. Collisions with the other creases are deliberately ignored.
    const auto extend_endpoint = [&](int from, int toward) {
        if (new_graph.is_boundary_point(from))
            return;

        // Local copies: find_or_add_point() below can reallocate points.
        const double fx = new_graph.points[from].pos_x;
        const double fy = new_graph.points[from].pos_y;
        const double tx = new_graph.points[toward].pos_x;
        const double ty = new_graph.points[toward].pos_y;

        double dx = fx - tx;
        double dy = fy - ty;
        const double length = std::hypot(dx, dy);
        if (length <= 0.0)
            return;
        dx /= length;
        dy /= length;

        // The crease already continues in this direction from `from` (e.g. two
        // collinear input edges sharing the vertex): that neighbour reaches the
        // border on its own turn, so adding a stub would duplicate the crease.
        if (has_collinear_edge_ahead(new_graph, from, toward, dx, dy))
            return;

        ray_hit hit;
        if (!first_boundary_hit(fx, fy, dx, dy, segments, eps, hit)) {
            std::cerr << "extend: no paper boundary found from point " << from
                      << " along (" << dx << ", " << dy << "); edge left as is.\n";
            return;
        }

        // A hit on a corner (or on a previously created boundary point) reuses
        // the existing vertex instead of creating a nearly duplicate one.
        int to = -1;
        const double snap2 = eps * eps;
        for (const point& p : new_graph.points) {
            const double px = p.pos_x - hit.x;
            const double py = p.pos_y - hit.y;
            if (px * px + py * py <= snap2) {
                to = p.id;
                break;
            }
        }

        if (to < 0) {
            // The exact intersection is on the border; store the point a hair
            // inside it so floating point noise cannot put it outside.
            to = find_or_add_point(new_graph, hit.x - dx * nudge,
                                   hit.y - dy * nudge, eps);
        }

        add_stub(new_graph, from, to);
    };

    for (const edge& e : original.edges) {
        const int a = e.p1.id;
        const int b = e.p2.id;
        if (a == b || a < 0 || b < 0 || a >= num_points || b >= num_points)
            continue;

        extend_endpoint(a, b);
        extend_endpoint(b, a);
    }

    // 4. Rebuild the incidence lists and the CCW ordering/angles of the result.
    new_graph.rebuild_incidence();
}

// ---------------------------------------------------------------------------
// Corner checks 
// ---------------------------------------------------------------------------

// The base of the angles is always the lowest corner of the paper. When two
// corners share the same height, the leftmost one wins.
int find_base_corner(const std::vector<point>& corners) {
    int base = 0;

    for (int i = 1; i < static_cast<int>(corners.size()); ++i) {
        const double dy = corners[i].pos_y - corners[base].pos_y;
        const double dx = corners[i].pos_x - corners[base].pos_x;

        if (dy < -kCornerEps) {
            base = i; // strictly lower corner
        } else if (std::abs(dy) <= kCornerEps && dx < -kCornerEps) {
            base = i; // same height, further left
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
bool check_corners_ccw(const std::vector<point>& corners) {
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
        const point& a = corners[i];
        const point& b = corners[(i + 1) % n];
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
