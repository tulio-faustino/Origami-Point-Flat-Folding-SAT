#include "mv_solver.hpp"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <utility>

// ---------------------------------------------------------------------------
// Construction: the class simply copies the pattern main() already prepared
// (local_edges in CCW order and local_angles filled in), including the corner
// count that now lives in the graph.
// ---------------------------------------------------------------------------
MVAssignmentSolver::MVAssignmentSolver(const origamiGraph& graph)
    : graph_(graph),
      num_points_(static_cast<int>(graph_.points.size())),
      num_edges_(static_cast<int>(graph_.edges.size())),
      num_corners_(graph_.num_corners()) {
    // Configuration error, not a property of a single vertex: fail loudly
    // instead of silently encoding the wrong side of the theorem.
    if (num_corners_ < kMinCorners) {
        std::cerr << "MVAssignmentSolver: a crease pattern needs at least "
                  << kMinCorners << " corners, got " << num_corners_ << ".\n";
        throw std::invalid_argument("MVAssignmentSolver: num_corners < kMinCorners");
    }
}

// ---------------------------------------------------------------------------
// Decides the MV assignment.
//
// Returns true when the instance is satisfiable. `assignment_` then holds one
// character per edge -- 'M' (mountain) or 'V' (valley) -- and is left as a
// single-element vector {'U'} otherwise: UNSAT means the crease pattern has no
// valid assignment, UNKNOWN means the solver could not decide. Callers must
// check solve()'s return value (or assignment().size() == num_edges()) before
// indexing into it.
// ---------------------------------------------------------------------------
bool MVAssignmentSolver::solve()
{
    assignment_.clear();

    // Kawasaki-Justin is decided in C++ and NOT asserted in the solver: the
    // angles are constants of the input, known before any search starts, so a
    // violation means "no MV assignment exists" for any solver.
    //
    // Deciding it here instead of asserting a constant `false` in cvc5 gives a
    // diagnostic naming the offending vertex, avoids building an instance that
    // is doomed anyway, and keeps floating point arithmetic out of the term.
    if (!verify_kawasaki()) {
        std::cerr << "UNSAT: Kawasaki-Justin fails, no MV assignment can fold "
                     "this pattern flat.\n";
        assignment_.push_back('U');
        return false;
    }

    cvc5::TermManager tm;
    cvc5::Solver solver(tm);
    solver.setOption("produce-models", "true");

    // x_e == true  <=>  edge e is mountain(M)
    std::vector<cvc5::Term> x(num_edges_);
    for (int i = 0; i < num_edges_; i++)
        x[i] = tm.mkConst(tm.getBooleanSort(), "x" + std::to_string(i));

    // First property being verified is Maekawa ||V| - |M|| = 2: one call per
    // vertex.
    for (const point& pt : graph_.points)
        assert_maekawa(solver, tm, x, pt);

    // Then sum En must be 360: Kawasaki is decided statically, in
    // verify_kawasaki() above, so there is nothing left to assert for it.

    // Big little big lemma (uses the angles): one call per vertex.
    for (const point& pt : graph_.points)
        assert_big_little_big(solver, tm, x, pt);

    cvc5::Result res = solver.checkSat();
    if (!res.isSat()) {
        // 'U' marks "no usable assignment". UNSAT means the pattern cannot be
        // folded flat at every vertex; UNKNOWN means the solver gave up
        // (timeout / incomplete theory combination).
        std::cerr << (res.isUnsat()
                          ? "UNSAT: no valid MV assignment exists.\n"
                          : "UNKNOWN: the solver could not decide.\n");
        assignment_.push_back('U');
        return false;
    }

    assignment_.resize(num_edges_);
    for (int i = 0; i < num_edges_; i++) {
        // getBooleanValue() reads the truth value out of the model; comparing
        // Terms with == would test structural equality of expressions instead.
        assignment_[i] = solver.getValue(x[i]).getBooleanValue() ? 'M' : 'V';
    }
    return true;
}

// ---------------------------------------------------------------------------
// Independent check of a finished assignment.
//
// The encoder is easy to get wrong, so the model is re-verified in plain C++
// from the 'M' / 'V' characters instead of trusting the solver. Silent when the
// assignment is valid; reports the offending vertices on stderr and returns
// false otherwise.
// ---------------------------------------------------------------------------
bool MVAssignmentSolver::validate_assignment() const
{
    if (assignment_.size() != static_cast<std::size_t>(num_edges_)) {
        std::cerr << "validate_assignment: expected " << num_edges_
                  << " assignment(s), got " << assignment_.size() << ".\n";
        return false;
    }

    bool ok = true;

    for (const point& pt : graph_.points) {
        const int deg = static_cast<int>(pt.local_edges.size());
        if (deg == 0)
            continue;

        int mountains = 0;
        int valleys = 0;
        for (int id : pt.local_edges) {
            if (id < 0 || id >= static_cast<int>(assignment_.size())) {
                std::cerr << "validate_assignment: edge " << id
                          << " has no type in the assignment.\n";
                return false;
            }
            (assignment_[id] == 'M' ? mountains : valleys)++;
        }

        // Boundary-ness comes from the per-point flag (corners plus the points
        // created by extend_edges); an ODD degree is still a geometric hint of
        // a boundary vertex, because an interior vertex always has even degree.
        const bool is_boundary = graph_.is_boundary_point(pt.id) || (deg % 2 != 0);
        if (is_boundary && deg % 2 == 0)
            continue; // skipped by the encoding as well

        const int k = is_boundary ? 1 : 2;
        const int diff = mountains - valleys;
        const bool vacuous = (deg - k) / 2 <= 0 && (deg + k) / 2 >= deg;
        const bool good = vacuous || std::abs(diff) == k;

        if (!good) {
            std::cerr << "Maekawa-Justin violated at point " << pt.id << " (d=" << deg
                      << (is_boundary ? ", boundary" : ", interior") << "): "
                      << mountains << "M " << valleys << "V -> |M|-|V| = " << diff
                      << ", expected " << k << ".\n";
            std::cerr << "  (the paper boundary takes the missing angle, so its rule is "
                         "||M|-|V|| = 1; interior vertices need 2)\n";
        }

        ok = ok && good;
    }

    return ok;
}

// ---------------------------------------------------------------------------
// Static check of the Kawasaki-Justin theorem; see the declaration in
// mv_solver.hpp for the reasoning (sum of every other gap = pi at even-degree
// vertices).
// ---------------------------------------------------------------------------
bool MVAssignmentSolver::verify_kawasaki(double eps) const
{
    // How many offenders get a detailed line before the list is truncated: a
    // broken pattern can have hundreds of them.
    constexpr int kMaxReported = 8;

    bool all_ok = true;
    int offenders = 0;

    for (const point& pt : graph_.points) {
        const int deg = static_cast<int>(pt.local_angles.size());

        // Degree < 2 has no alternating sum; odd degree is a boundary vertex
        // whose exterior (paper) angle is not represented in the graph.
        if (deg < 2 || deg % 2 != 0)
            continue;

        double alternate = 0.0;
        for (int i = 0; i < deg; i += 2)
            alternate += pt.local_angles[i];

        if (std::abs(alternate - PI) <= eps)
            continue;

        all_ok = false;

        if (offenders++ < kMaxReported) {
            std::cerr << "Kawasaki violated at point " << pt.id
                      << ": sum of alternate angles = " << alternate * 180.0 / PI
                      << " deg, expected 180 deg (deviation "
                      << (alternate - PI) * 180.0 / PI << " deg).\n";
        }
    }

    if (offenders > kMaxReported)
        std::cerr << "  ... and " << (offenders - kMaxReported)
                  << " more vertex(es) violate Kawasaki-Justin.\n";

    return all_ok;
}

// ---------------------------------------------------------------------------
// Maekawa-Justin at one vertex; see the declaration in mv_solver.hpp.
// ---------------------------------------------------------------------------
void MVAssignmentSolver::assert_maekawa(cvc5::Solver& solver,
                                        cvc5::TermManager& tm,
                                        const std::vector<cvc5::Term>& x,
                                        const point& pt) const
{
    const int deg = static_cast<int>(pt.local_edges.size());
    if (deg == 0)
        return;

    // The graph carries the boundary information per point (corners, plus the
    // points created by extend_edges). An ODD degree also means boundary: an
    // interior vertex of a flat-foldable pattern always has an even degree.
    const bool on_paper_boundary = graph_.is_boundary_point(pt.id);
    const bool is_boundary = on_paper_boundary || (deg % 2 != 0);

    // A boundary vertex with even degree would need the paper's own angle at
    // that point, which the graph does not carry, so there is nothing sound to
    // assert. Reporting a false UNSAT here would be worse than skipping.
    if (is_boundary && deg % 2 == 0) {
        std::cerr << "Maekawa: point " << pt.id << " looks like a boundary vertex "
                  << "but has even degree " << deg << "; paper angle unknown, "
                  << "skipping its count constraint.\n";
        return;
    }

    // |M| = (d - k) / 2 or |M| = (d + k) / 2, with k = 1 on the boundary and
    // k = 2 in the interior. Parity above guarantees these are integers.
    const int k = is_boundary ? 1 : 2;
    const int low = (deg - k) / 2;
    const int high = (deg + k) / 2;

    // Degree 1 corners satisfy ||M| - |V|| = 1 for free, so the disjunction
    // below would be a tautology; skip it instead of feeding the solver junk.
    if (low <= 0 && high >= deg)
        return;

    // count = sum of ite(x_e, 1, 0) over the incident edges (integer term).
    cvc5::Term zero = tm.mkInteger(0);
    cvc5::Term one = tm.mkInteger(1);

    std::vector<cvc5::Term> parts;
    parts.reserve(deg);
    for (int id : pt.local_edges)
        parts.push_back(tm.mkTerm(cvc5::Kind::ITE,
                                  std::vector<cvc5::Term>{x[id], one, zero}));

    cvc5::Term count = tm.mkTerm(cvc5::Kind::ADD, parts);

    cvc5::Term low_ok = tm.mkTerm(cvc5::Kind::EQUAL,
                                  std::vector<cvc5::Term>{count, tm.mkInteger(low)});
    cvc5::Term high_ok = tm.mkTerm(cvc5::Kind::EQUAL,
                                   std::vector<cvc5::Term>{count, tm.mkInteger(high)});

    solver.assertFormula(
        tm.mkTerm(cvc5::Kind::OR, std::vector<cvc5::Term>{low_ok, high_ok}));
}

// ---------------------------------------------------------------------------
// Big-Little-Big at one vertex; see the declaration in mv_solver.hpp.
// ---------------------------------------------------------------------------
void MVAssignmentSolver::assert_big_little_big(cvc5::Solver& solver,
                                               cvc5::TermManager& tm,
                                               const std::vector<cvc5::Term>& x,
                                               const point& pt) const
{
    const int deg = static_cast<int>(pt.local_angles.size());

    // A triple of gaps only makes sense from degree 3 on.
    if (deg < 3)
        return;

    for (int j = 0; j < deg; j++) {

        // Logic for protecting going over the limits
        const int prev = (j + deg - 1) % deg;
        const int next = (j + 1) % deg;

        // Then the middle edge must be different from the previous and the next
        if (!(pt.local_angles[j] < pt.local_angles[prev] &&
              pt.local_angles[j] < pt.local_angles[next]))
            continue;

        const int id_prev = pt.local_edges[prev];
        const int id_next = pt.local_edges[next];
        const int id_cur = pt.local_edges[j];

        // Degenerate triple: the clause would relate a literal to itself
        // (e.g. x -> !x) and could make an otherwise satisfiable pattern
        // UNSAT. Skip those instead of asserting nonsense.
        if (id_prev == id_next || id_cur == id_prev || id_cur == id_next)
            continue;

        // Add clause: if e[prev] == e[next] -> e[j] = NOT(e[next])
        cvc5::Term same_neighbours =
            tm.mkTerm(cvc5::Kind::EQUAL,
                      std::vector<cvc5::Term>{x[id_prev], x[id_next]});

        cvc5::Term cur_differs =
            tm.mkTerm(cvc5::Kind::NOT,
                      std::vector<cvc5::Term>{
                          tm.mkTerm(cvc5::Kind::EQUAL,
                                    std::vector<cvc5::Term>{x[id_cur], x[id_next]})});

        solver.assertFormula(
            tm.mkTerm(cvc5::Kind::IMPLIES,
                      std::vector<cvc5::Term>{same_neighbours, cur_differs}));
    }
}
