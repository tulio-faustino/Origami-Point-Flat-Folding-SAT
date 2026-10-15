import sys
import matplotlib.pyplot as plt

def main():
    # Read and clean input from standard input (stdin)
    lines = []
    for line in sys.stdin:
        # Strip comments starting with '//' and trim whitespace
        clean_line = line.split('//')[0].strip()
        if clean_line:
            lines.append(clean_line.split())

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

    plt.grid(True, linestyle=':', alpha=0.6)
    plt.title("Graph Visualization")
    plt.xlabel("X")
    plt.ylabel("Y")
    
    # Display plot
    plt.show()

if __name__ == '__main__':
    main()