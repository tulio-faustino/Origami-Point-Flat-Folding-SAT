#pragma once

#include <vector>

#include <cvc5/cvc5.h>

#include "graph_types.hpp"

// ---------------------------------------------------------------------------
// MV assignment solver 
//
// Everything that builds or reads the SMT instance lives in this class:
//   - the Kawasaki-Justin check (decided in plain C++, before the solver is
//     even constructed: the angles are constants of the input),
//   - the Maekawa-Justin encoding,
//   - the Big-Little-Big encoding,
//   - the read-back of the model, and
//   - an independent re-check of that model.
//
// The pattern stays owned by the object after solve(); graph() (and the
// points() / edges() shortcuts) give the plotting code read access to it.
// ---------------------------------------------------------------------------

// Tolerance (in RADIANS) for the static geometric checks. The gaps come from
// atan2() on doubles, so exact equality is never meaningful.
inline constexpr double kAngleEpsRad = 1e-9;

class MVAssignmentSolver {
private:
    // -----------------------------------------------------------------------
    // Kawasaki-Justin theorem, checked OUTSIDE the solver.
    //
    // At an interior vertex the gaps must be splittable into two groups of
    // equal total, i.e. the sum of every other gap is pi:
    //
    //     alpha_0 + alpha_2 + alpha_4 + ... = pi
    //
    // For EVEN degree the condition does not depend on which gap is alpha_0:
    // the complementary half sums to 2*pi - pi = pi as well.
    //
    // Odd-degree vertices are boundary vertices (corners included): the paper's
    // exterior angle is not part of the graph, so their Kawasaki form cannot be
    // evaluated here and they are skipped (Maekawa handles them as boundary).
    //
    // Returns true when every evaluable vertex passes. Offending vertices are
    // reported on stderr (in degrees) and `eps` is the tolerance in radians.
    // -----------------------------------------------------------------------
    bool verify_kawasaki(double eps = kAngleEpsRad) const;

    // -----------------------------------------------------------------------
    // Maekawa-Justin theorem, checked at ONE vertex.
    //
    // With x_e == true meaning "edge e is a mountain fold" and |M| / |V| the
    // number of true / false literals of the vertex, an INTERIOR vertex of even
    // degree d must satisfy
    //
    //     ||M| - |V|| = 2   =>   |M| = (d - 2) / 2   or   |M| = (d + 2) / 2
    //
    // so the constraint is the disjunction of those two counts: the geometry
    // alone does not tell which of the two sides is the mountain-heavy one.
    //
    // Vertices on the paper boundary follow the ODD version ||M| - |V|| = 1
    // (=> |M| = (d - 1) / 2 or (d + 1) / 2) instead. The boundary is taken from
    // origamiGraph::point_is_normal (corners and the points created by
    // extend_edges), plus the odd-degree rule (an interior vertex of a
    // flat-foldable pattern always has an even number of creases). Boundary
    // vertices of even degree are skipped: their paper angle is not in the
    // graph, so there is nothing sound to assert.
    // -----------------------------------------------------------------------
    void assert_maekawa(cvc5::Solver& solver, cvc5::TermManager& tm,
                        const std::vector<cvc5::Term>& x,
                        const point& pt) const;

    // -----------------------------------------------------------------------
    // Big-Little-Big (Justin), checked at ONE vertex.
    //
    // For every gap that is strictly smaller than both of its neighbours
    //
    //     alpha_j < alpha_{j-1}  and  alpha_j < alpha_{j+1}
    //
    // the pairs of creases around it may not all share the same fold type: the
    // clause asserted is
    //
    //     (x_prev == x_next) -> (x_cur != x_next)
    //
    // where prev/j/next are the gaps j-1, j, j+1 and cur is the edge owned by
    // gap j. `local_edges[k]` already stores an edge id (ids are kept equal to
    // the vector indices), so it indexes `x` directly.
    // -----------------------------------------------------------------------
    void assert_big_little_big(cvc5::Solver& solver, cvc5::TermManager& tm,
                               const std::vector<cvc5::Term>& x,
                               const point& pt) const;

    origamiGraph graph_;
    std::vector<char> assignment_; // filled by solve()
    int num_points_ = 0;
    int num_edges_ = 0;
    int num_corners_ = kDefaultCorners;

public:
    // The graph must already carry the boundary count (num_corners), plus
    // local_edges (CCW sorted) and local_angles for every point; the class does
    // not touch the geometry, it only encodes it. The graph is copied, so the
    // caller's pattern stays intact and solve() can be repeated.
    //
    // num_corners: the first num_corners points of the graph (ids
    // 0 .. num_corners - 1) are the corners of the paper. This positional
    // convention is the fallback when the graph carries no per-point boundary
    // flags; with flags present, is_boundary_point() (corners plus the points
    // created by extend_edges) is the authoritative source (see assert_maekawa).
    explicit MVAssignmentSolver(const origamiGraph& graph);

    // Builds the cvc5 instance and decides it.
    //
    // Returns true on SAT, with assignment() holding one 'M' / 'V' per edge.
    // On UNSAT or UNKNOWN returns false, assignment() becomes {'U'} and the
    // reason is reported on stderr.
    bool solve();

    // Result of the last solve():
    //   SAT            -> num_edges() characters, 'M' or 'V', indexed by edge id
    //   UNSAT/UNKNOWN  -> single-element {'U'}
    //   before solve() -> empty
    const std::vector<char>& assignment() const { return assignment_; }

    // Independent check of the finished assignment, in plain C++ (no cvc5).
    // The encoder is easy to get wrong, so the model is re-verified from the
    // 'M' / 'V' characters instead of trusting the solver.
    //
    // It is silent when the model is valid: only the offending vertices are
    // reported on stderr, together with the expected count (||M|-|V|| = 2 at
    // interior vertices, = 1 on the paper boundary, whose own angle closes the
    // fan). Returns false if any Maekawa-Justin count is violated.
    bool validate_assignment() const;

    int num_points() const { return num_points_; }
    int num_edges() const { return num_edges_; }
    int num_corners() const { return num_corners_; }

    // Read access to the pattern the solver was built from.
    const origamiGraph& graph() const { return graph_; }
    const std::vector<point>& points() const { return graph_.points; }
    const std::vector<edge>& edges() const { return graph_.edges; }

};
