import argparse
import math
import os
import sys

import matplotlib.pyplot as plt
from matplotlib.patches import Arc

# ---------------------------------------------------------------------------
# Directory layout, resolved from the repository root (parent of scripts/):
#   plot/instructions/  <- .plot files written by the C++ printing functions
#   plot/img/           <- images rendered by this script
# ---------------------------------------------------------------------------
ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INSTRUCTIONS_DIR = os.path.join(ROOT_DIR, 'plot', 'instructions')
IMG_DIR = os.path.join(ROOT_DIR, 'plot', 'img')

ORIGAMI_INSTRUCTIONS = os.path.join(INSTRUCTIONS_DIR, 'origami.plot')
CCW_INSTRUCTIONS = os.path.join(INSTRUCTIONS_DIR, 'ccw_test.plot')

# Size of the local-angle arcs, as a fraction of the shortest edge of the gap.
ARC_SCALE = 0.33          # gaps up to 180 degrees
ARC_SCALE_REFLEX = 0.22   # reflex gaps (> 180 degrees) sweep around the vertex
ARC_LABEL_SCALE = 0.72    # label radius, also as a fraction of the arc radius


def _save_figure(fig, source_path=None, out_path=None, show=True):
    """Save `fig` as a PNG inside plot/img/ (named after the instruction file)."""
    if out_path is None:
        if source_path is None:
            stem = 'graph'
        else:
            stem = os.path.splitext(os.path.basename(source_path))[0]
        out_path = os.path.join(IMG_DIR, f"{stem}.png")

    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    fig.savefig(out_path, dpi=150, bbox_inches='tight')
    print(f"Saved plot to '{out_path}'")

    if show:
        plt.show()
    plt.close(fig)
    return out_path


def _clean_lines(stream):
    """Read a stream and return a list of whitespace-split tokens per line."""
    lines = []
    for line in stream:
        # Strip comments starting with '//' and trim whitespace
        clean_line = line.split('//')[0].strip()
        if clean_line:
            lines.append(clean_line.split())
    return lines


def main(path=None, out_path=None, show=True, use_stdin=False):
    """Plot the crease pattern read from `path` (default: plot/instructions/origami.plot).

    With use_stdin=True (or when the default instruction file is missing) the
    pattern is read from standard input instead.
    """
    source = path
    if source is None and not use_stdin and os.path.exists(ORIGAMI_INSTRUCTIONS):
        source = ORIGAMI_INSTRUCTIONS

    if source is None:
        # Read and clean input from standard input (stdin)
        lines = _clean_lines(sys.stdin)
    else:
        with open(source, 'r') as handle:
            lines = _clean_lines(handle)
        print(f"Reading instructions from '{source}'")

    if not lines:
        return

    iterator = iter(lines)

    # Read points
    num_points = int(next(iterator)[0])
    points = []
    for _ in range(num_points):
        parts = next(iterator)
        points.append((float(parts[0]), float(parts[1])))

    # Read edges
    num_edges = int(next(iterator)[0])
    edges = []
    for _ in range(num_edges):
        parts = next(iterator)
        u = int(parts[0])
        v = int(parts[1])
        t = parts[2]
        edges.append((u, v, t))

    # Plot configuration
    fig, ax = plt.subplots()

    # Draw edges (points remain invisible, acting solely as segment endpoints)
    for u, v, t in edges:
        p1, p2 = points[u], points[v]
        x_coords = [p1[0], p2[0]]
        y_coords = [p1[1], p2[1]]

        # Determine line style based on edge type
        if t == '0':
            c, ls, lw = 'black', 'solid', 1.5
        elif t == 'M':
            c, ls, lw = 'red', 'solid', 1.5
        elif t == 'V':
            c, ls, lw = 'darkblue', 'dashed', 1.5
        else:
            c, ls, lw = 'gray', 'dotted', 1.0  # Fallback for unrecognized types

        ax.plot(x_coords, y_coords, color=c, linestyle=ls, linewidth=lw)

    # Set boundaries based on the first 4 points
    if len(points) >= 4:
        boundary_points = points[:4]
        bound_x = [p[0] for p in boundary_points]
        bound_y = [p[1] for p in boundary_points]
        
        ax.set_xlim(min(bound_x), max(bound_x))
        ax.set_ylim(min(bound_y), max(bound_y))
    elif points:
        # Fallback if file has fewer than 4 points
        bound_x = [p[0] for p in points]
        bound_y = [p[1] for p in points]
        ax.set_xlim(min(bound_x) - 10, max(bound_x) + 10)
        ax.set_ylim(min(bound_y) - 10, max(bound_y) + 10)

    # Enforce 1:1 aspect ratio
    ax.set_aspect('equal', adjustable='box')

    ax.grid(True, linestyle=':', alpha=0.6)
    ax.set_title("Graph Visualization")
    ax.set_xlabel("X")
    ax.set_ylabel("Y")

    # Save into plot/img/ (and optionally show it)
    _save_figure(fig, source, out_path, show)


def _parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Plot crease-pattern instructions from plot/instructions/ "
                    "into plot/img/.")
    parser.add_argument(
        'instruction', nargs='?', default=None,
        help="instruction file (default: plot/instructions/origami.plot; "
             "use '-' to read from stdin)")
    parser.add_argument(
        '-c', '--ccw', nargs='?', const=CCW_INSTRUCTIONS, default=None,
        metavar='FILE',
        help="plot the ccw ordering test (default: "
             "plot/instructions/ccw_test.plot)")
    parser.add_argument(
        '-a', '--angles', action='store_true',
        help="with --ccw, also print the local angles in degrees "
             "(alternating black and red)")
    parser.add_argument(
        '-o', '--out', default=None,
        help="output image path (default: plot/img/<instruction name>.png)")
    parser.add_argument(
        '--no-show', action='store_true',
        help="only save the image, do not open the interactive window")
    return parser.parse_args(argv)


# ---------------------------------------------------------------------------
# CCW test plot (counterpart of test_ccw_sort in src/printing_functions.cpp)
# ---------------------------------------------------------------------------

def parse_ccw_file(path):
    """
    Parse the file written by test_ccw_sort().

    Format (one block per point):
        <num_points> <has_angles>
        <point_id> <x> <y> <degree>
        <order> <edge_id> <neighbor_x> <neighbor_y> <angle_deg>   (degree lines)
        <n> <a0> <a1> ... <an-1>          (local angles in RADIANS, optional)

    Every angle is measured counter clockwise from the +x axis, so a correct
    ccw_sort_point_edges() produces a non-decreasing angle column inside each
    block. The optional extra line holds p.local_angles (gaps between
    consecutive edges); it is converted to degrees here, at print time.

    Returns (blocks, has_angles), where each block is
    {'id', 'point': (x, y), 'entries': [...], 'angles': [radians]}.
    """
    with open(path, 'r') as handle:
        lines = _clean_lines(handle)

    if not lines:
        raise ValueError(f"'{path}' is empty.")

    num_points = int(float(lines[0][0]))
    has_angles = len(lines[0]) > 1 and int(float(lines[0][1])) != 0
    cursor = 1
    blocks = []

    for _ in range(num_points):
        if cursor >= len(lines):
            raise ValueError(f"'{path}': expected {num_points} point blocks, "
                             f"found only {len(blocks)}.")

        header = lines[cursor]
        cursor += 1
        point_id = int(float(header[0]))
        position = (float(header[1]), float(header[2]))
        degree = int(float(header[3]))

        entries = []
        for _ in range(degree):
            tokens = lines[cursor]
            cursor += 1
            entries.append({
                'order': int(float(tokens[0])),
                'edge_id': int(float(tokens[1])),
                'point': (float(tokens[2]), float(tokens[3])),
                'angle': float(tokens[4]),
            })

        angles = []
        if has_angles:
            tokens = lines[cursor]
            cursor += 1
            count = int(float(tokens[0]))
            angles = [float(value) for value in tokens[1:1 + count]]

        blocks.append({'id': point_id, 'point': position,
                       'entries': entries, 'angles': angles})

    return blocks, has_angles


def _angles_ok(entries):
    """True if the CCW angle column is non-decreasing."""
    angles = [entry['angle'] for entry in entries]
    return all(b >= a - 1e-9 for a, b in zip(angles, angles[1:]))


def _edge_colors(edge_ids):
    """Map every edge id to its own stable color."""
    count = max(len(edge_ids), 1)
    if count <= 20:
        cmap = plt.get_cmap('tab20')
        return {eid: cmap(i % 20) for i, eid in enumerate(edge_ids)}

    cmap = plt.get_cmap('hsv')
    return {eid: cmap(i / count) for i, eid in enumerate(edge_ids)}


def _draw_local_angles(ax, block):
    """Draw the local angles (in degrees) of one vertex.

    Each gap gets an arc swept counter-clockwise from one edge to the next, a
    small arrow head showing the sweep direction and the value in the middle of
    the gap. Colors alternate between black and red so consecutive gaps are easy
    to tell apart.
    """
    hub = block['point']
    entries = block['entries']
    angles = block.get('angles') or []
    total = len(entries)

    if total < 2 or len(angles) < total:
        return

    for i, alpha in enumerate(angles):
        nx, ny = entries[i]['point']
        fx, fy = entries[(i + 1) % total]['point']

        ux, uy = nx - hub[0], ny - hub[1]
        vx, vy = fx - hub[0], fy - hub[1]
        lu, lv = math.hypot(ux, uy), math.hypot(vx, vy)
        if lu == 0 or lv == 0:
            continue

        ux, uy = ux / lu, uy / lu
        color = 'black' if i % 2 == 0 else 'red'
        alpha_deg = math.degrees(alpha)
        min_len = min(lu, lv)
        # Reflex gaps sweep around most of the vertex, so a smaller radius keeps
        # the arc from swallowing its neighbours.
        span = (ARC_SCALE if alpha_deg <= 180.0 else ARC_SCALE_REFLEX) * min_len
        label_r = ARC_LABEL_SCALE * span   # radius where the value is written

        # Arc of the swept gap: theta grows counter-clockwise, exactly like the
        # angles themselves, so the arc goes from the current edge to the next.
        start_deg = math.degrees(math.atan2(uy, ux))
        end_deg = start_deg + alpha_deg
        ax.add_patch(Arc(hub, 2 * span, 2 * span, theta1=start_deg,
                         theta2=end_deg, color=color, linewidth=1.1,
                         alpha=0.9, zorder=2))

        # Little arrow head marking the counter-clockwise direction of the sweep
        head = min(8.0, 0.5 * alpha_deg)
        tip = math.radians(end_deg)
        tail = math.radians(end_deg - head)
        ax.annotate(
            "", xy=(hub[0] + span * math.cos(tip), hub[1] + span * math.sin(tip)),
            xytext=(hub[0] + span * math.cos(tail),
                    hub[1] + span * math.sin(tail)),
            arrowprops=dict(arrowstyle='-|>', color=color, linewidth=1.0,
                            shrinkA=0, shrinkB=0, mutation_scale=8),
            zorder=5)

        # Value in the middle of the gap, between the vertex and the arc
        half = alpha / 2.0
        bx = ux * math.cos(half) - uy * math.sin(half)
        by = ux * math.sin(half) + uy * math.cos(half)
        lx, ly = hub[0] + label_r * bx, hub[1] + label_r * by

        ax.annotate(f"{alpha_deg:.1f}\u00b0", (lx, ly), ha='center',
                    va='center', fontsize=7, color=color, zorder=5,
                    bbox=dict(boxstyle='round,pad=0.12', fc='white',
                              ec='none', alpha=0.8))


def plot_ccw_sequence(path=None, out_path=None, show=True, show_angles=False):
    """Draw the p.local_edges order of EVERY vertex in a single graph.

    Each edge is drawn once with its own color (so it looks the same from both
    of its endpoints), and the order index that the edge occupies in the
    ordering of a vertex is written right next to that vertex, along the edge.
    Since an edge only ever joins two vertices, each endpoint gets exactly one
    number per incident edge - no information is lost even though everything is
    drawn on top of each other.

    Reads plot/instructions/ccw_test.plot by default (written by C++
    test_ccw_sort) and saves the image into plot/img/.

    With show_angles=True the local angles (p.local_angles) are also written in
    the plot, in degrees, alternating black and red.
    """
    if path is None:
        path = CCW_INSTRUCTIONS

    blocks, has_angles = parse_ccw_file(path)

    if not blocks:
        print(f"No points to plot in '{path}'.")
        return

    # The same edge shows up in the block of both endpoints, so collect the
    # segment only once (first occurrence wins) and draw it a single time.
    segments = {}
    for block in blocks:
        hub = block['point']
        for entry in block['entries']:
            segments.setdefault(entry['edge_id'], (hub, entry['point']))

    colors = _edge_colors(sorted(segments))

    fig, ax = plt.subplots(figsize=(11, 9))

    # Draw every edge once, with its own color
    for edge_id, (a, b) in segments.items():
        ax.plot([a[0], b[0]], [a[1], b[1]], color=colors[edge_id],
                linewidth=2.2, zorder=1, label=f"e{edge_id}")

    # Sequential order index near the ORIGIN of each edge (the vertex that owns
    # the ordering). Placing it a bit after the vertex, along the edge itself,
    # spreads the numbers of different edges instead of piling them on the
    # vertex.
    for block in blocks:
        hub = block['point']
        for entry in block['entries']:
            nx, ny = entry['point']
            lx = hub[0] + 0.22 * (nx - hub[0])
            ly = hub[1] + 0.22 * (ny - hub[1])
            ax.annotate(str(entry['order']), (lx, ly), ha='center',
                        va='center', fontsize=9, fontweight='bold',
                        color=colors.get(entry['edge_id'], 'black'), zorder=3,
                        bbox=dict(boxstyle='round,pad=0.15', fc='white',
                                  ec='none', alpha=0.65))

    # Vertices (drawn on top of everything)
    for block in blocks:
        x, y = block['point']
        ax.plot([x], [y], marker='X', markersize=10, color='black',
                linestyle='None', zorder=4)
        ax.annotate(f"P{block['id']}", (x, y), textcoords="offset points",
                    xytext=(0, -17), ha='center', fontsize=8,
                    color='dimgray', zorder=4)

    # Optional: local angles in degrees, alternating black and red
    if show_angles:
        if has_angles:
            for block in blocks:
                _draw_local_angles(ax, block)
        else:
            print("Warning: the instruction file has no local angles; re-run "
                  "the C++ with with_angles=true.")

    broken = [block['id'] for block in blocks
              if block['entries'] and not _angles_ok(block['entries'])]
    ok = not broken
    status = ("all vertices CCW OK" if ok
              else f"CCW broken at point(s): {', '.join(map(str, broken))}")

    xs = [block['point'][0] for block in blocks]
    ys = [block['point'][1] for block in blocks]
    margin = 0.10 * max(max(xs) - min(xs), max(ys) - min(ys), 1.0)
    ax.set_xlim(min(xs) - margin, max(xs) + margin)
    ax.set_ylim(min(ys) - margin, max(ys) + margin)
    ax.set_aspect('equal', adjustable='box')
    ax.grid(True, linestyle=':', alpha=0.5)
    ax.set_title(f"p.local_edges order for all {len(blocks)} vertices - "
                 f"{status}\n"
                 f"color = edge, number = index of that edge in the "
                 f"p.local_edges of the vertex it is drawn next to"
                 + ("\nlocal angle arcs (deg), alternating black / red"
                    if show_angles and has_angles else ""),
                 fontsize=10, color='green' if ok else 'red')
    ax.set_xlabel("X")
    ax.set_ylabel("Y")
    ax.legend(title="edges", fontsize=7, title_fontsize=8, ncol=2,
              loc='center left', bbox_to_anchor=(1.01, 0.5))

    print(f"CCW check: {status}")
    for block in blocks:
        sequence = ", ".join(
            f"{entry['angle']:.1f}" for entry in block['entries'])
        line = (f"  P{block['id']} d={len(block['entries'])}: "
                f"[{sequence}] deg")
        if show_angles and block['angles']:
            local = ", ".join(f"{math.degrees(a):.1f}"
                              for a in block['angles'])
            line += f"  | local: [{local}] deg"
        print(line)

    # Save into plot/img/ (and optionally show it)
    _save_figure(fig, path, out_path, show)


if __name__ == '__main__':
    options = _parse_args(sys.argv[1:])
    want_show = not options.no_show

    if options.ccw is not None:
        plot_ccw_sequence(options.ccw, options.out, want_show, options.angles)
    elif options.instruction == '-':
        main(None, options.out, want_show, use_stdin=True)
    else:
        main(options.instruction, options.out, want_show)