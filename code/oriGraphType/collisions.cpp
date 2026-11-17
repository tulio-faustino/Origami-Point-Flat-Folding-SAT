#include "collisions.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Implementation of the collision resolver.
//
// Pipeline (each phase is O(1) amortised per item unless noted):
//   1. copy the points, merging anything closer than eps (corners first, so the
//      ids of the paper outline stay 0 .. num_corners-1);
//   2. normalise the edges (drop self loops, zero length, duplicates);
//   3. build an AABB tree over the segments (inflated by eps) -- broad phase;
//   4. pass 1: edge x edge, proper crossings only;
//   5. pass 2: point x edge, T-junctions (an existing point on another crease).
//      Pass 2 runs AFTER pass 1 because a crossing may snap onto an existing
//      point, and then every other crease through that point must be split too.
//      Two passes in this order are complete: any point that has to be inserted
//      lies inside a parent segment, and every sub-segment is a subset of its
//      parent, so nothing new can appear after the second pass.
//   6. sort the split parameters of every edge and emit the sub-segments,
//      inheriting the fold type of the parent.
//
// The split lists always contain both endpoints, and every accepted split is
// more than eps away (along the edge) from its neighbours, so no segment
// shorter than the tolerance is ever emitted.
// ---------------------------------------------------------------------------

namespace {

struct vec2 {
    double x = 0.0;
    double y = 0.0;
};

double dot(const vec2& a, const vec2& b) {
    return a.x * b.x + a.y * b.y;
}

double cross(const vec2& a, const vec2& b) {
    return a.x * b.y - a.y * b.x;
}

// Unordered key of an undirected edge, used to drop duplicated segments.
std::uint64_t edge_key(int u, int v) {
    const std::uint32_t lo = static_cast<std::uint32_t>(std::min(u, v));
    const std::uint32_t hi = static_cast<std::uint32_t>(std::max(u, v));
    return (static_cast<std::uint64_t>(lo) << 32) | static_cast<std::uint64_t>(hi);
}

// ---------------------------------------------------------------------------
// Point index: answers "is there already a point within eps of (x, y)?" in
// expected O(1) and doubles as the canonicalisation map used while copying.
//
// The hash works on cells of side eps: llround(v / eps) is monotone and adding
// exactly one cell to a value adds exactly one to its key, so any point within
// eps of the query lies in one of the nine neighbouring cells (plus its own).
// A plain uniform grid of cell size eps would need ~1/eps cells per axis and is
// therefore unusable here; the quantised key has no such problem.
// ---------------------------------------------------------------------------
struct cell_key {
    long long x = 0;
    long long y = 0;

    bool operator==(const cell_key& other) const { return x == other.x && y == other.y; }
};

struct cell_hash {
    std::size_t operator()(const cell_key& key) const noexcept {
        std::uint64_t hx = static_cast<std::uint64_t>(key.x) * 0x9e3779b97f4a7c15ULL;
        std::uint64_t hy = static_cast<std::uint64_t>(key.y) * 0xc2b2ae3d27d4eb4fULL;
        hx ^= hy + 0x9e3779b97f4a7c15ULL + (hx << 6) + (hx >> 2);
        return static_cast<std::size_t>(hx);
    }
};

class PointIndex {
public:
    explicit PointIndex(double eps)
        : eps_(eps > 0.0 ? eps : std::numeric_limits<double>::min()),
          eps2_(eps_ * eps_) {}

    // Closest point within eps, or -1. `ambiguous` reports the (rare) case of
    // two candidates inside the tolerance, which means the input carries a
    // cluster the canonicalisation could not collapse.
    int find(double x, double y, bool* ambiguous = nullptr) const {
        if (ambiguous != nullptr)
            *ambiguous = false;

        if (!std::isfinite(x) || !std::isfinite(y))
            return -1;

        const long long cx = cell_of(x);
        const long long cy = cell_of(y);

        int best = -1;
        double best_d2 = eps2_;
        int hits = 0;

        for (long long dx = -1; dx <= 1; ++dx) {
            for (long long dy = -1; dy <= 1; ++dy) {
                const auto it = cells_.find(cell_key{cx + dx, cy + dy});
                if (it == cells_.end())
                    continue;

                for (const int id : it->second) {
                    const double px = coords_[static_cast<std::size_t>(id)].x - x;
                    const double py = coords_[static_cast<std::size_t>(id)].y - y;
                    const double d2 = px * px + py * py;
                    if (d2 > eps2_)
                        continue;

                    ++hits;
                    if (best < 0 || d2 < best_d2) {
                        best = id;
                        best_d2 = d2;
                    }
                }
            }
        }

        if (ambiguous != nullptr && hits > 1)
            *ambiguous = true;

        return best;
    }

    void insert(int id, double x, double y) {
        const std::size_t index = static_cast<std::size_t>(id);
        if (coords_.size() <= index)
            coords_.resize(index + 1);
        coords_[index] = vec2{x, y};

        cells_[cell_key{cell_of(x), cell_of(y)}].push_back(id);
    }

private:
    long long cell_of(double value) const {
        const double scaled = value / eps_;
        // Keep the conversion inside long long even for absurd inputs.
        if (!(scaled > -9.0e18 && scaled < 9.0e18))
            return 0;
        return std::llround(scaled);
    }

    double eps_;
    double eps2_;
    std::vector<vec2> coords_;
    std::unordered_map<cell_key, std::vector<int>, cell_hash> cells_;
};

// ---------------------------------------------------------------------------
// Broad phase: a flat AABB tree (median split on the longest axis of the
// centroid spread). Deterministic (ties broken by segment id), no tuning
// parameter to pick, and it does not degrade when a crease crosses the whole
// paper -- which is exactly what a uniform grid would do here.
// ---------------------------------------------------------------------------
struct aabb {
    double min_x = 0.0;
    double min_y = 0.0;
    double max_x = 0.0;
    double max_y = 0.0;

    bool overlaps(const aabb& other) const {
        return !(max_x < other.min_x || other.max_x < min_x ||
                 max_y < other.min_y || other.max_y < min_y);
    }

    double center_x() const { return 0.5 * (min_x + max_x); }
    double center_y() const { return 0.5 * (min_y + max_y); }
};

class AabbTree {
public:
    void reset(std::vector<aabb> boxes) {
        boxes_ = std::move(boxes);
        nodes_.clear();
        order_.resize(boxes_.size());
        for (std::size_t i = 0; i < order_.size(); ++i)
            order_[i] = static_cast<int>(i);
        if (!order_.empty())
            build(0, static_cast<int>(order_.size()));
    }

    template <class Visit>
    void query(const aabb& box, Visit&& visit) const {
        if (!nodes_.empty())
            query_node(0, box, visit);
    }

private:
    struct node {
        aabb box;
        int left = -1;
        int right = -1;
        int first = 0;
        int count = 0; // > 0 marks a leaf
    };

    static aabb merged(const aabb& a, const aabb& b) {
        aabb out;
        out.min_x = std::min(a.min_x, b.min_x);
        out.min_y = std::min(a.min_y, b.min_y);
        out.max_x = std::max(a.max_x, b.max_x);
        out.max_y = std::max(a.max_y, b.max_y);
        return out;
    }

    // Builds the node for order_[lo, hi) and returns its index.
    int build(int lo, int hi) {
        const int index = static_cast<int>(nodes_.size());
        nodes_.push_back(node{});

        aabb box = boxes_[static_cast<std::size_t>(order_[static_cast<std::size_t>(lo)])];
        double min_cx = box.center_x();
        double max_cx = min_cx;
        double min_cy = box.center_y();
        double max_cy = min_cy;

        for (int i = lo + 1; i < hi; ++i) {
            const aabb& other = boxes_[static_cast<std::size_t>(order_[static_cast<std::size_t>(i)])];
            box = merged(box, other);
            min_cx = std::min(min_cx, other.center_x());
            max_cx = std::max(max_cx, other.center_x());
            min_cy = std::min(min_cy, other.center_y());
            max_cy = std::max(max_cy, other.center_y());
        }

        nodes_[static_cast<std::size_t>(index)].box = box;

        if (hi - lo == 1) {
            nodes_[static_cast<std::size_t>(index)].first = lo;
            nodes_[static_cast<std::size_t>(index)].count = 1;
            return index;
        }

        const bool split_x = (max_cx - min_cx) >= (max_cy - min_cy);
        const int mid = lo + (hi - lo) / 2;

        std::nth_element(order_.begin() + lo, order_.begin() + mid, order_.begin() + hi,
                         [this, split_x](int lhs, int rhs) {
                             const aabb& a = boxes_[static_cast<std::size_t>(lhs)];
                             const aabb& b = boxes_[static_cast<std::size_t>(rhs)];
                             const double ka = split_x ? a.center_x() : a.center_y();
                             const double kb = split_x ? b.center_x() : b.center_y();
                             if (ka != kb)
                                 return ka < kb;
                             return lhs < rhs; // deterministic for equal keys
                         });

        const int left = build(lo, mid);
        const int right = build(mid, hi);

        nodes_[static_cast<std::size_t>(index)].left = left;
        nodes_[static_cast<std::size_t>(index)].right = right;
        return index;
    }

    template <class Visit>
    static void query_node_impl(const std::vector<node>& nodes,
                                const std::vector<int>& order, int index,
                                const aabb& box, Visit& visit) {
        const node& current = nodes[static_cast<std::size_t>(index)];
        if (!current.box.overlaps(box))
            return;

        if (current.count > 0) {
            for (int i = current.first; i < current.first + current.count; ++i)
                visit(order[static_cast<std::size_t>(i)]);
            return;
        }

        query_node_impl(nodes, order, current.left, box, visit);
        query_node_impl(nodes, order, current.right, box, visit);
    }

    template <class Visit>
    void query_node(int index, const aabb& box, Visit& visit) const {
        query_node_impl(nodes_, order_, index, box, visit);
    }

    std::vector<node> nodes_;
    std::vector<int> order_;
    std::vector<aabb> boxes_;
};

// One point where an edge has to be cut: the parameter along the parent edge
// (0 = first endpoint, 1 = second endpoint) and the vertex id to use there.
struct split {
    double t = 0.0;
    int point_id = -1;
};

// A source edge after canonicalisation of its endpoints.
struct parent_segment {
    int src_id = -1;
    int u = -1;
    int v = -1;
    char type = kBaseEdgeType;
};

// Adds a cut unless that vertex is already a cut of this edge. Returns true when
// something was actually added.
bool add_split(std::vector<split>& list, double t, int point_id) {
    if (point_id < 0)
        return false;

    for (const split& existing : list) {
        if (existing.point_id == point_id)
            return false;
    }

    if (t < 0.0)
        t = 0.0;
    else if (t > 1.0)
        t = 1.0;

    list.push_back(split{t, point_id});
    return true;
}

int append_point(origamiGraph& graph, PointIndex& index, double x, double y) {
    const int id = static_cast<int>(graph.points.size());
    graph.points.emplace_back(x, y, id);
    index.insert(id, x, y);
    return id;
}

std::string describe_segment(const parent_segment& parent) {
    std::ostringstream out;
    out << "edge " << parent.src_id << " (" << parent.u << "-" << parent.v << ")";
    return out.str();
}

} // namespace

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------

double collision_eps(const origamiGraph& graph) {
    return kCollisionEpsFactor * graph.paper_scale();
}

bool resolve_collisions(const origamiGraph& src, origamiGraph& dst,
                        collision_report& report) {
    report = collision_report{};
    dst = origamiGraph{};
    dst.set_num_corners(src.num_corners());

    report.input_points = static_cast<int>(src.points.size());
    report.input_edges = static_cast<int>(src.edges.size());

    if (src.points.empty())
        return true;

    const int num_corners =
        std::min<int>(src.num_corners(), static_cast<int>(src.points.size()));
    if (num_corners < kMinCorners) {
        report.error = "the source graph does not declare a valid paper outline";
        dst = origamiGraph{};
        return false;
    }

    const double eps = collision_eps(src);
    const double eps2 = eps * eps;

    PointIndex index(eps);

    // ---- 1. Points: corners first (their ids must stay 0 .. num_corners-1),
    //         then everything else, merging whatever is within the tolerance.
    std::vector<int> canonical(src.points.size(), -1);

    for (int i = 0; i < num_corners; ++i) {
        const point& p = src.points[static_cast<std::size_t>(i)];

        // Two corners on top of each other cannot be merged without changing
        // which points define the outline, so the pattern is rejected instead.
        if (index.find(p.pos_x, p.pos_y) >= 0) {
            report.error = "two corners of the paper coincide within the tolerance";
            dst = origamiGraph{};
            return false;
        }

        const int id = append_point(dst, index, p.pos_x, p.pos_y);
        dst.set_point_is_normal(id, false);
        canonical[static_cast<std::size_t>(i)] = id;
    }

    for (int i = num_corners; i < static_cast<int>(src.points.size()); ++i) {
        const point& p = src.points[static_cast<std::size_t>(i)];

        bool ambiguous = false;
        const int existing = index.find(p.pos_x, p.pos_y, &ambiguous);
        if (existing >= 0) {
            canonical[static_cast<std::size_t>(i)] = existing;
            ++report.merged_points;
            if (ambiguous)
                ++report.ambiguous_snaps;
            continue;
        }

        canonical[static_cast<std::size_t>(i)] = append_point(dst, index, p.pos_x, p.pos_y);
    }

    // ---- 2. Edges: remap the endpoints, drop the degenerate and duplicated
    //         ones, and remember the fold type to inherit.
    std::vector<parent_segment> parents;
    parents.reserve(src.edges.size());

    std::unordered_set<std::uint64_t> seen;
    seen.reserve(src.edges.size() * 2 + 1);

    for (const edge& e : src.edges) {
        const int a = e.p1.id;
        const int b = e.p2.id;
        if (a < 0 || b < 0 || a >= static_cast<int>(src.points.size()) ||
            b >= static_cast<int>(src.points.size())) {
            ++report.dropped_edges;
            continue;
        }

        const int u = canonical[static_cast<std::size_t>(a)];
        const int v = canonical[static_cast<std::size_t>(b)];
        if (u < 0 || v < 0 || u == v) {
            ++report.dropped_edges; // both endpoints merged: shorter than eps
            continue;
        }

        const double dx = dst.points[static_cast<std::size_t>(v)].pos_x -
                          dst.points[static_cast<std::size_t>(u)].pos_x;
        const double dy = dst.points[static_cast<std::size_t>(v)].pos_y -
                          dst.points[static_cast<std::size_t>(u)].pos_y;
        if (!std::isfinite(dx) || !std::isfinite(dy) || dx * dx + dy * dy <= eps2) {
            ++report.dropped_edges;
            continue;
        }

        if (!seen.insert(edge_key(u, v)).second) {
            ++report.dropped_edges; // duplicated crease
            continue;
        }

        parents.push_back(parent_segment{e.id, u, v, e.type});
    }

    const int num_edges = static_cast<int>(parents.size());

    std::vector<double> seg_length(static_cast<std::size_t>(num_edges), 0.0);
    std::vector<double> t_eps(static_cast<std::size_t>(num_edges), 1.0);
    std::vector<aabb> boxes(static_cast<std::size_t>(num_edges));

    for (int i = 0; i < num_edges; ++i) {
        const parent_segment& parent = parents[static_cast<std::size_t>(i)];
        const double ax = dst.points[static_cast<std::size_t>(parent.u)].pos_x;
        const double ay = dst.points[static_cast<std::size_t>(parent.u)].pos_y;
        const double bx = dst.points[static_cast<std::size_t>(parent.v)].pos_x;
        const double by = dst.points[static_cast<std::size_t>(parent.v)].pos_y;

        seg_length[static_cast<std::size_t>(i)] = std::hypot(bx - ax, by - ay);
        t_eps[static_cast<std::size_t>(i)] =
            seg_length[static_cast<std::size_t>(i)] > 0.0
                ? eps / seg_length[static_cast<std::size_t>(i)]
                : 1.0;

        // Inflated by eps so the same tree answers proximity queries too.
        boxes[static_cast<std::size_t>(i)] = aabb{std::min(ax, bx) - eps, std::min(ay, by) - eps,
                                                  std::max(ax, bx) + eps, std::max(ay, by) + eps};
    }

    // ---- 3. Broad phase.
    AabbTree tree;
    tree.reset(boxes);

    std::vector<std::vector<split>> splits(static_cast<std::size_t>(num_edges));
    for (int i = 0; i < num_edges; ++i) {
        const parent_segment& parent = parents[static_cast<std::size_t>(i)];
        splits[static_cast<std::size_t>(i)].push_back(split{0.0, parent.u});
        splits[static_cast<std::size_t>(i)].push_back(split{1.0, parent.v});
    }

    bool rejected = false;

    // ---- 4. Pass 1: edge x edge.
    const auto test_pair = [&](int i, int j) {
        const parent_segment& first = parents[static_cast<std::size_t>(i)];
        const parent_segment& second = parents[static_cast<std::size_t>(j)];

        const double ax = dst.points[static_cast<std::size_t>(first.u)].pos_x;
        const double ay = dst.points[static_cast<std::size_t>(first.u)].pos_y;
        const double bx = dst.points[static_cast<std::size_t>(first.v)].pos_x;
        const double by = dst.points[static_cast<std::size_t>(first.v)].pos_y;
        const double cx = dst.points[static_cast<std::size_t>(second.u)].pos_x;
        const double cy = dst.points[static_cast<std::size_t>(second.u)].pos_y;
        const double dx = dst.points[static_cast<std::size_t>(second.v)].pos_x;
        const double dy = dst.points[static_cast<std::size_t>(second.v)].pos_y;

        const vec2 ab{bx - ax, by - ay};
        const vec2 cd{dx - cx, dy - cy};

        const double len_a = seg_length[static_cast<std::size_t>(i)];
        const double len_b = seg_length[static_cast<std::size_t>(j)];
        if (len_a <= 0.0 || len_b <= 0.0)
            return;

        const bool shared = first.u == second.u || first.u == second.v ||
                            first.v == second.u || first.v == second.v;

        // sin(theta) * min(len): when the crossing point moves by more than eps
        // for an eps-sized perturbation of the inputs, computing it is
        // meaningless (the error of the intersection formula grows like
        // 1 / sin(theta)), so the pair is classified as parallel instead.
        const double sin_theta = std::abs(cross(ab, cd)) / (len_a * len_b);
        if (sin_theta * std::min(len_a, len_b) <= eps) {
            // Parallel or (nearly) collinear. Only an actual overlap matters:
            // a simple touch is handled by pass 2, and a parallel pair that is
            // apart never collides.
            const double dist_c = cross(ab, vec2{cx - ax, cy - ay}) / len_a;
            const double dist_d = cross(ab, vec2{dx - ax, dy - ay}) / len_a;
            if (std::abs(dist_c) > eps || std::abs(dist_d) > eps)
                return;

            // Both endpoints of the second edge are on the line of the first:
            // project them and look for an overlap of positive length.
            const double t_c = dot(ab, vec2{cx - ax, cy - ay}) / (len_a * len_a);
            const double t_d = dot(ab, vec2{dx - ax, dy - ay}) / (len_a * len_a);
            const double lo = std::min(t_c, t_d) - t_eps[static_cast<std::size_t>(i)];
            const double hi = std::max(t_c, t_d) + t_eps[static_cast<std::size_t>(i)];
            const double overlap = std::min(hi, 1.0) - std::max(lo, 0.0);

            // The +/- eps of slack above makes two creases that merely end at
            // the same vertex produce exactly one eps of "overlap", so a real
            // overlap has to be bigger than that.
            if (overlap > 2.0 * t_eps[static_cast<std::size_t>(i)]) {
                std::ostringstream message;
                message << describe_segment(first) << " overlaps "
                        << describe_segment(second);
                report.overlaps.push_back(message.str());
                rejected = true;
            }
            return;
        }

        if (shared)
            return; // two non-collinear segments sharing a vertex meet only there

        // Signed areas: {c, d} against the line of the first edge, {a, b} against
        // the second one. Opposite signs on both pairs mean a proper crossing.
        const double s1 = cross(cd, vec2{ax - cx, ay - cy});
        const double s2 = cross(cd, vec2{bx - cx, by - cy});
        const double s3 = cross(ab, vec2{cx - ax, cy - ay});
        const double s4 = cross(ab, vec2{dx - ax, dy - ay});

        if (!((s1 > 0.0) != (s2 > 0.0)) || !((s3 > 0.0) != (s4 > 0.0)))
            return; // an endpoint touch is pass 2's job

        // s3/(s3 - s4) is the parameter on the SECOND edge and s1/(s1 - s2) the
        // parameter on the first one (each pair of areas is taken against the
        // line of the other edge).
        const double t = s1 / (s1 - s2); // on `first`, in (0, 1)
        const double s = s3 / (s3 - s4); // on `second`, in (0, 1)

        // A crossing that lands on one of the four endpoints is not a crossing
        // but a touch / T-junction: pass 2 splits the other edge at the existing
        // vertex, which is exactly the right result and avoids creating a
        // second vertex a hair away from the endpoint.
        const double edge_eps_a = t_eps[static_cast<std::size_t>(i)];
        const double edge_eps_b = t_eps[static_cast<std::size_t>(j)];
        if (t <= edge_eps_a || t >= 1.0 - edge_eps_a || s <= edge_eps_b ||
            s >= 1.0 - edge_eps_b)
            return;

        const double px = ax + t * ab.x;
        const double py = ay + t * ab.y;
        if (!std::isfinite(px) || !std::isfinite(py))
            return;

        // Reuse the vertex when the crossing lands on one (or on a point added
        // by a previous crossing), otherwise create it.
        bool ambiguous = false;
        int point_id = index.find(px, py, &ambiguous);
        if (point_id >= 0) {
            ++report.snapped_crossings;
            if (ambiguous)
                ++report.ambiguous_snaps;
        } else {
            point_id = append_point(dst, index, px, py);
            ++report.new_points;
        }

        ++report.crossings;
        add_split(splits[static_cast<std::size_t>(i)], t, point_id);
        add_split(splits[static_cast<std::size_t>(j)], s, point_id);
    };

    for (int i = 0; i < num_edges && !rejected; ++i) {
        tree.query(boxes[static_cast<std::size_t>(i)], [&](int j) {
            if (rejected || j <= i)
                return;
            test_pair(i, j);
        });
    }

    // ---- 5. Pass 2: point x edge (T-junctions). Runs on every point of the
    //         result, including the ones created by pass 1: a crossing may have
    //         snapped onto an existing vertex, and then the crease that vertex
    //         lies on also has to be cut there.
    for (int point_id = 0; point_id < static_cast<int>(dst.points.size()) && !rejected;
         ++point_id) {
        const double px = dst.points[static_cast<std::size_t>(point_id)].pos_x;
        const double py = dst.points[static_cast<std::size_t>(point_id)].pos_y;

        const aabb query_box{px - eps, py - eps, px + eps, py + eps};

        tree.query(query_box, [&](int i) {
            if (rejected)
                return;

            const parent_segment& parent = parents[static_cast<std::size_t>(i)];
            if (point_id == parent.u || point_id == parent.v)
                return;

            const double ax = dst.points[static_cast<std::size_t>(parent.u)].pos_x;
            const double ay = dst.points[static_cast<std::size_t>(parent.u)].pos_y;
            const double bx = dst.points[static_cast<std::size_t>(parent.v)].pos_x;
            const double by = dst.points[static_cast<std::size_t>(parent.v)].pos_y;

            const vec2 ab{bx - ax, by - ay};
            const double len2 = ab.x * ab.x + ab.y * ab.y;
            if (len2 <= 0.0)
                return;

            const double t = dot(ab, vec2{px - ax, py - ay}) / len2;
            const double edge_eps = t_eps[static_cast<std::size_t>(i)];
            if (t <= edge_eps || t >= 1.0 - edge_eps)
                return; // too close to an endpoint: already a vertex there

            const double qx = ax + t * ab.x;
            const double qy = ay + t * ab.y;
            const double dx = px - qx;
            const double dy = py - qy;
            if (dx * dx + dy * dy > eps2)
                return;

            if (add_split(splits[static_cast<std::size_t>(i)], t, point_id))
                ++report.t_junctions;
        });
    }

    if (rejected) {
        dst = origamiGraph{};
        return false;
    }

    // ---- 6. Rebuild the edges from the split lists.
    std::unordered_set<std::uint64_t> emitted;
    emitted.reserve(static_cast<std::size_t>(num_edges) * 2 + 1);

    for (int i = 0; i < num_edges; ++i) {
        std::vector<split>& list = splits[static_cast<std::size_t>(i)];

        std::ranges::sort(list, [](const split& lhs, const split& rhs) {
            if (lhs.t != rhs.t)
                return lhs.t < rhs.t;
            return lhs.point_id < rhs.point_id;
        });

        const double edge_eps = t_eps[static_cast<std::size_t>(i)];
        int segments = 0;

        int last_id = list.front().point_id;
        double last_t = list.front().t;

        for (std::size_t k = 1; k < list.size(); ++k) {
            const split& current = list[k];
            const bool is_last = (k + 1 == list.size());

            if (!is_last && current.t - last_t <= edge_eps) {
                ++report.dropped_splits; // closer than eps along the edge
                continue;
            }

            if (current.point_id != last_id) {
                if (emitted.insert(edge_key(last_id, current.point_id)).second) {
                    const int id = static_cast<int>(dst.edges.size());
                    dst.edges.emplace_back(dst.points[static_cast<std::size_t>(last_id)],
                                           dst.points[static_cast<std::size_t>(current.point_id)],
                                           id);
                    dst.edges.back().type = parents[static_cast<std::size_t>(i)].type;
                } else {
                    ++report.dropped_duplicate_segments;
                }
                ++segments;
            }

            last_id = current.point_id;
            last_t = current.t;
        }

        if (segments >= 2)
            ++report.split_edges;
    }

    // ---- 7. Boundary flags, incidence lists, angles.
    dst.refresh_boundary_flags();

    const double boundary_eps = kBoundaryEpsFactor * dst.paper_scale();
    for (int i = 0; i < static_cast<int>(dst.points.size()); ++i) {
        if (dst.is_normal_point(i) && dst.point_on_boundary(dst.points[static_cast<std::size_t>(i)],
                                                          boundary_eps)) {
            dst.set_point_is_normal(i, false);
        }
    }

    dst.rebuild_incidence();

    report.output_points = static_cast<int>(dst.points.size());
    report.output_edges = static_cast<int>(dst.edges.size());

    const int result_corners =
        std::min<int>(dst.num_corners(), static_cast<int>(dst.points.size()));

    for (const point& p : dst.points) {
        const int degree = static_cast<int>(p.local_edges.size());
        if (degree == 0) {
            // A corner with no crease is normal (partial patterns), anything
            // else with no crease lost all of them and is worth reporting.
            if (p.id >= result_corners)
                ++report.isolated_points;
        } else if (degree % 2 == 1 && dst.is_normal_point(p.id)) {
            ++report.odd_interior_points;
        }
    }

    return true;
}
