#!/usr/bin/env python3
"""Generate procedural OBJ meshes with proper vertex normals.

Emits real smooth-surface meshes so the CPU raytracer's per-vertex
normal interpolation has something to actually interpolate. (A cube
looks identical under smooth and flat shading — a sphere does not.)

Usage:
    gen_mesh.py sphere   [--segments N] [--rings N]   > sphere.obj
    gen_mesh.py torus    [--segments N] [--rings N]   > torus.obj
    gen_mesh.py icosphere [--subdivisions N]          > icosphere.obj
"""

import argparse
import math
import sys


def write_obj(out, verts, normals, tris, name):
    out.write(f"# {name}\n")
    out.write(f"# {len(verts)} vertices, {len(tris)} triangles\n\n")
    for x, y, z in verts:
        out.write(f"v {x:.6f} {y:.6f} {z:.6f}\n")
    out.write("\n")
    for nx, ny, nz in normals:
        out.write(f"vn {nx:.6f} {ny:.6f} {nz:.6f}\n")
    out.write("\n")
    # OBJ is 1-indexed; pair each vertex with the normal at the same index.
    for a, b, c in tris:
        out.write(f"f {a+1}//{a+1} {b+1}//{b+1} {c+1}//{c+1}\n")


def uv_sphere(segments, rings):
    verts, normals, tris = [], [], []
    for r in range(rings + 1):
        phi = math.pi * r / rings                # 0 .. pi (pole to pole)
        y = math.cos(phi)
        sp = math.sin(phi)
        for s in range(segments + 1):
            theta = 2 * math.pi * s / segments   # 0 .. 2pi
            x = sp * math.cos(theta)
            z = sp * math.sin(theta)
            verts.append((x, y, z))
            normals.append((x, y, z))            # on unit sphere normal == position

    def idx(r, s):
        return r * (segments + 1) + s

    for r in range(rings):
        for s in range(segments):
            a = idx(r, s)
            b = idx(r + 1, s)
            c = idx(r + 1, s + 1)
            d = idx(r, s + 1)
            # Winding chosen so (b-a) x (c-a) points outward.
            tris.append((a, c, b))
            tris.append((a, d, c))
    return verts, normals, tris


def torus(segments, rings, major_r=1.0, minor_r=0.35):
    verts, normals, tris = [], [], []
    for r in range(rings):
        u = 2 * math.pi * r / rings
        cu, su = math.cos(u), math.sin(u)
        for s in range(segments):
            v = 2 * math.pi * s / segments
            cv, sv = math.cos(v), math.sin(v)
            x = (major_r + minor_r * cv) * cu
            y = minor_r * sv
            z = (major_r + minor_r * cv) * su
            verts.append((x, y, z))
            # Normal points outward from the ring's center to the point.
            cx = major_r * cu
            cz = major_r * su
            nx, ny, nz = x - cx, y, z - cz
            mag = math.sqrt(nx*nx + ny*ny + nz*nz) or 1.0
            normals.append((nx/mag, ny/mag, nz/mag))

    def idx(r, s):
        return (r % rings) * segments + (s % segments)

    for r in range(rings):
        for s in range(segments):
            a = idx(r,     s)
            b = idx(r + 1, s)
            c = idx(r + 1, s + 1)
            d = idx(r,     s + 1)
            # Winding chosen so face normal points outward from ring center.
            tris.append((a, c, b))
            tris.append((a, d, c))
    return verts, normals, tris


def icosphere(subdivisions):
    # Start from a regular icosahedron.
    t = (1.0 + math.sqrt(5.0)) / 2.0
    verts = [
        (-1,  t,  0), ( 1,  t,  0), (-1, -t,  0), ( 1, -t,  0),
        ( 0, -1,  t), ( 0,  1,  t), ( 0, -1, -t), ( 0,  1, -t),
        ( t,  0, -1), ( t,  0,  1), (-t,  0, -1), (-t,  0,  1),
    ]

    def normalize(v):
        m = math.sqrt(v[0]**2 + v[1]**2 + v[2]**2)
        return (v[0]/m, v[1]/m, v[2]/m)

    verts = [normalize(v) for v in verts]
    tris = [
        (0,11,5),(0,5,1),(0,1,7),(0,7,10),(0,10,11),
        (1,5,9),(5,11,4),(11,10,2),(10,7,6),(7,1,8),
        (3,9,4),(3,4,2),(3,2,6),(3,6,8),(3,8,9),
        (4,9,5),(2,4,11),(6,2,10),(8,6,7),(9,8,1),
    ]

    # Subdivide each triangle: midpoints projected back to unit sphere.
    cache = {}
    def midpoint(i, j):
        key = (min(i, j), max(i, j))
        if key in cache: return cache[key]
        mx = (verts[i][0] + verts[j][0]) / 2
        my = (verts[i][1] + verts[j][1]) / 2
        mz = (verts[i][2] + verts[j][2]) / 2
        verts.append(normalize((mx, my, mz)))
        cache[key] = len(verts) - 1
        return cache[key]

    for _ in range(subdivisions):
        new_tris = []
        for a, b, c in tris:
            ab = midpoint(a, b)
            bc = midpoint(b, c)
            ca = midpoint(c, a)
            new_tris.append((a, ab, ca))
            new_tris.append((b, bc, ab))
            new_tris.append((c, ca, bc))
            new_tris.append((ab, bc, ca))
        tris = new_tris

    normals = list(verts)  # on unit sphere, normal == position
    return verts, normals, tris


def main():
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="shape", required=True)

    sp = sub.add_parser("sphere")
    sp.add_argument("--segments", type=int, default=24)
    sp.add_argument("--rings",    type=int, default=16)

    tp = sub.add_parser("torus")
    tp.add_argument("--segments", type=int, default=20)
    tp.add_argument("--rings",    type=int, default=32)

    ip = sub.add_parser("icosphere")
    ip.add_argument("--subdivisions", type=int, default=2)

    args = p.parse_args()

    if args.shape == "sphere":
        v, n, t = uv_sphere(args.segments, args.rings)
        write_obj(sys.stdout, v, n, t, f"UV sphere {args.segments}x{args.rings}")
    elif args.shape == "torus":
        v, n, t = torus(args.segments, args.rings)
        write_obj(sys.stdout, v, n, t, f"Torus {args.segments}x{args.rings}")
    else:
        v, n, t = icosphere(args.subdivisions)
        write_obj(sys.stdout, v, n, t, f"Icosphere sub={args.subdivisions}")


if __name__ == "__main__":
    main()
