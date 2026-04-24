#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef RT_HAVE_OPENGL_BACKEND

#include "renderer.h"
#include "sphere.h"
#include "plane.h"
#include "disc.h"
#include "cylinder.h"
#include "triangle.h"
#include "box.h"
#include "sprite.h"
#include "heightfield.h"
#include "mesh.h"
#include "cpu/bvh.h"   /* rt_bvh_node — shared BVH node format across backends */

#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -- Shader source ---------------------------------------------------- */

static const char *RAYTRACE_SHADER_SOURCE =
"#version 430\n"
"layout(local_size_x = 16, local_size_y = 16) in;\n"
"layout(rgba8, binding = 0) uniform writeonly image2D outputImage;\n"
"\n"
"uniform vec3  u_cam_origin;\n"
"uniform vec3  u_cam_forward;\n"
"uniform vec3  u_cam_right;\n"
"uniform vec3  u_cam_up;\n"
"uniform float u_fov;\n"
"uniform float u_ambient;\n"
"uniform int   u_sphere_count;\n"
"uniform int   u_plane_count;\n"
"uniform int   u_disc_count;\n"
"uniform int   u_cylinder_count;\n"
"uniform int   u_triangle_count;\n"
"uniform int   u_box_count;\n"
"uniform int   u_sprite_count;\n"
"uniform int   u_heightfield_count;\n"
"uniform int   u_light_count;\n"
"uniform int   u_mesh_count;\n"
"\n"
"struct Sphere   { vec4 center_radius; ivec4 mat; };\n"
"struct Plane    { vec4 point; vec4 normal; ivec4 mat; };\n"
"struct Disc     { vec4 center_radius; vec4 normal; ivec4 mat; };\n"
"struct Cylinder { vec4 center_hh; vec4 axis_radius; ivec4 mat; };\n"
"struct Triangle { vec4 v0; vec4 v1; vec4 v2; ivec4 mat; };\n"
"struct Box      { vec4 minp; vec4 maxp; ivec4 mat; };\n"
"struct Sprite   { vec4 position_w; vec4 direction_h; ivec4 frame_info; };\n"
"struct Light    { vec4 direction_intensity; };\n"
"/* Per-mesh descriptor for BVH traversal. The triangle SSBO layout is:\n"
" * [0 .. u_triangle_count)       explicit scene triangles (no BVH)\n"
" * [u_triangle_count .. end)     per-mesh triangles, mesh.tri_start points at\n"
" *                               each mesh's run. Leaf tri_start is LOCAL to\n"
" *                               the mesh (absolute index = mesh.tri_start +\n"
" *                               leaf.tri_start + k). */\n"
"struct Mesh     { vec4 bounds;  ivec4 info; };   /* bounds: xyz center, w radius.\n"
"                                                    info: (bvh_start, bvh_count,\n"
"                                                           tri_start, tri_count) */\n"
"struct BvhNode  { vec4 aabb_min; vec4 aabb_max; ivec4 info; };\n"
"                                                /* info: (tri_start_local,\n"
"                                                          tri_count,\n"
"                                                          second_child_offset, _) */\n"
"struct Material {\n"
"    vec4  albedo;   /* tile A / base color */\n"
"    vec4  albedo2;  /* tile B (checker) */\n"
"    ivec4 kind;     /* .x = scene_tex_kind, .y = tex_index */\n"
"    vec4  scale;    /* .x = tex_scale, .y = reflectivity */\n"
"};\n"
"struct Texture { ivec4 size; };  /* .x = real_w, .y = real_h */\n"
"struct Heightfield {\n"
"    vec4  origin_world;   /* origin_x, origin_z, world_w, world_d */\n"
"    vec4  grid;           /* rows, cols, max_h, 0 */\n"
"    ivec4 offsets;        /* heights_off, normals_off, colors_off, 0 */\n"
"};\n"
"\n"
"layout(std430, binding = 1)  readonly buffer SphereBuf   { Sphere   spheres[];    };\n"
"layout(std430, binding = 2)  readonly buffer PlaneBuf    { Plane    planes[];     };\n"
"layout(std430, binding = 3)  readonly buffer DiscBuf     { Disc     discs[];      };\n"
"layout(std430, binding = 4)  readonly buffer CylBuf      { Cylinder cylinders[];  };\n"
"layout(std430, binding = 5)  readonly buffer TriBuf      { Triangle triangles[];  };\n"
"layout(std430, binding = 6)  readonly buffer BoxBuf      { Box      boxes[];      };\n"
"layout(std430, binding = 7)  readonly buffer SpriteBuf   { Sprite   sprites[];    };\n"
"layout(std430, binding = 8)  readonly buffer LightBuf    { Light    lights[];     };\n"
"layout(std430, binding = 9)  readonly buffer HfBuf       { Heightfield heightfields[]; };\n"
"layout(std430, binding = 10) readonly buffer HfHeights   { float hf_heights[];    };\n"
"layout(std430, binding = 11) readonly buffer HfNormals   { float hf_normals[];    };\n"
"layout(std430, binding = 12) readonly buffer HfColors    { uint  hf_colors[];     };\n"
"layout(std430, binding = 13) readonly buffer MatBuf      { Material materials[];  };\n"
"layout(std430, binding = 14) readonly buffer TexBuf      { Texture  textures[];   };\n"
"layout(std430, binding = 15) readonly buffer MeshBuf     { Mesh     meshes[];     };\n"
"layout(std430, binding = 16) readonly buffer BvhBuf      { BvhNode  bvh_nodes[];  };\n"
"\n"
"/* Sampler for sprite frames: all frames packed into one texture array */\n"
"uniform sampler2DArray u_sprite_atlas;\n"
"/* Sampler for image textures: one layer per material image texture */\n"
"uniform sampler2DArray u_tex_atlas;\n"
"\n"
"/* ---- Ray-primitive intersections ------------------------------------- */\n"
"\n"
"float isect_sphere(vec3 ro, vec3 rd, Sphere s) {\n"
"    vec3 oc = ro - s.center_radius.xyz;\n"
"    float r = s.center_radius.w;\n"
"    float b = 2.0 * dot(oc, rd);\n"
"    float c = dot(oc, oc) - r * r;\n"
"    float disc = b * b - 4.0 * c;\n"
"    if (disc < 0.0) return -1.0;\n"
"    float sq = sqrt(disc);\n"
"    float t0 = (-b - sq) * 0.5;\n"
"    if (t0 > 0.0) return t0;\n"
"    float t1 = (-b + sq) * 0.5;\n"
"    return (t1 > 0.0) ? t1 : -1.0;\n"
"}\n"
"\n"
"float isect_plane(vec3 ro, vec3 rd, Plane p) {\n"
"    float denom = dot(rd, p.normal.xyz);\n"
"    if (abs(denom) < 1e-6) return -1.0;\n"
"    float t = dot(p.point.xyz - ro, p.normal.xyz) / denom;\n"
"    return (t > 0.0) ? t : -1.0;\n"
"}\n"
"\n"
"float isect_disc(vec3 ro, vec3 rd, Disc d) {\n"
"    float denom = dot(rd, d.normal.xyz);\n"
"    if (abs(denom) < 1e-6) return -1.0;\n"
"    float t = dot(d.center_radius.xyz - ro, d.normal.xyz) / denom;\n"
"    if (t <= 0.0) return -1.0;\n"
"    vec3 hp = ro + rd * t;\n"
"    vec3 diff = hp - d.center_radius.xyz;\n"
"    if (dot(diff, diff) > d.center_radius.w * d.center_radius.w) return -1.0;\n"
"    return t;\n"
"}\n"
"\n"
"float isect_cylinder(vec3 ro, vec3 rd, Cylinder c, out vec3 hp_out) {\n"
"    vec3  center = c.center_hh.xyz;\n"
"    float hh     = c.center_hh.w;\n"
"    vec3  axis   = c.axis_radius.xyz;\n"
"    float radius = c.axis_radius.w;\n"
"    vec3 oc = ro - center;\n"
"    float rd_dot_a = dot(rd, axis);\n"
"    float oc_dot_a = dot(oc, axis);\n"
"    vec3 rd_perp = rd - axis * rd_dot_a;\n"
"    vec3 oc_perp = oc - axis * oc_dot_a;\n"
"    float a = dot(rd_perp, rd_perp);\n"
"    float b = 2.0 * dot(oc_perp, rd_perp);\n"
"    float cc = dot(oc_perp, oc_perp) - radius * radius;\n"
"    float disc = b * b - 4.0 * a * cc;\n"
"    if (disc < 0.0) return -1.0;\n"
"    float sq = sqrt(disc);\n"
"    float t0 = (-b - sq) / (2.0 * a);\n"
"    float t1 = (-b + sq) / (2.0 * a);\n"
"    for (int i = 0; i < 2; i++) {\n"
"        float t = (i == 0) ? t0 : t1;\n"
"        if (t < 0.0) continue;\n"
"        float h = oc_dot_a + t * rd_dot_a;\n"
"        if (h >= -hh && h <= hh) { hp_out = ro + rd * t; return t; }\n"
"    }\n"
"    return -1.0;\n"
"}\n"
"\n"
"vec3 normal_cylinder(vec3 hp, Cylinder c) {\n"
"    vec3 diff = hp - c.center_hh.xyz;\n"
"    float proj = dot(diff, c.axis_radius.xyz);\n"
"    vec3 on_axis = c.center_hh.xyz + c.axis_radius.xyz * proj;\n"
"    return normalize(hp - on_axis);\n"
"}\n"
"\n"
"float isect_triangle(vec3 ro, vec3 rd, Triangle tri) {\n"
"    vec3 e1 = tri.v1.xyz - tri.v0.xyz;\n"
"    vec3 e2 = tri.v2.xyz - tri.v0.xyz;\n"
"    vec3 pvec = cross(rd, e2);\n"
"    float det = dot(e1, pvec);\n"
"    if (abs(det) < 1e-6) return -1.0;\n"
"    float inv_det = 1.0 / det;\n"
"    vec3 tvec = ro - tri.v0.xyz;\n"
"    float u = dot(tvec, pvec) * inv_det;\n"
"    if (u < 0.0 || u > 1.0) return -1.0;\n"
"    vec3 qvec = cross(tvec, e1);\n"
"    float v = dot(rd, qvec) * inv_det;\n"
"    if (v < 0.0 || u + v > 1.0) return -1.0;\n"
"    float t = dot(e2, qvec) * inv_det;\n"
"    return (t > 0.0) ? t : -1.0;\n"
"}\n"
"\n"
"vec3 normal_triangle(Triangle tri) {\n"
"    return normalize(cross(tri.v1.xyz - tri.v0.xyz, tri.v2.xyz - tri.v0.xyz));\n"
"}\n"
"\n"
"float isect_box(vec3 ro, vec3 rd, Box b, out vec3 hp_out) {\n"
"    float tmin = -1e30;\n"
"    float tmax =  1e30;\n"
"    for (int i = 0; i < 3; i++) {\n"
"        float rdi = rd[i];\n"
"        float roi = ro[i];\n"
"        float mn  = b.minp[i];\n"
"        float mx  = b.maxp[i];\n"
"        if (abs(rdi) > 1e-6) {\n"
"            float t0 = (mn - roi) / rdi;\n"
"            float t1 = (mx - roi) / rdi;\n"
"            if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }\n"
"            if (t0 > tmin) tmin = t0;\n"
"            if (t1 < tmax) tmax = t1;\n"
"        } else if (roi < mn || roi > mx) return -1.0;\n"
"    }\n"
"    if (tmin > tmax) return -1.0;\n"
"    float t = (tmin > 0.0) ? tmin : tmax;\n"
"    if (t <= 0.0) return -1.0;\n"
"    hp_out = ro + rd * t;\n"
"    return t;\n"
"}\n"
"\n"
"vec3 normal_box(vec3 hp, Box b) {\n"
"    float eps = 1e-4;\n"
"    if (abs(hp.x - b.minp.x) < eps) return vec3(-1.0, 0.0, 0.0);\n"
"    if (abs(hp.x - b.maxp.x) < eps) return vec3( 1.0, 0.0, 0.0);\n"
"    if (abs(hp.y - b.minp.y) < eps) return vec3( 0.0,-1.0, 0.0);\n"
"    if (abs(hp.y - b.maxp.y) < eps) return vec3( 0.0, 1.0, 0.0);\n"
"    if (abs(hp.z - b.minp.z) < eps) return vec3( 0.0, 0.0,-1.0);\n"
"    return vec3(0.0, 0.0, 1.0);\n"
"}\n"
"\n"
"/* Sprite: billboard facing camera, axis-aligned-up. */\n"
"float isect_sprite(vec3 ro, vec3 rd, Sprite s,\n"
"                   out vec3 hp_out, out vec3 right_out,\n"
"                   out vec3 up_out, out vec3 normal_out) {\n"
"    vec3 to_cam = u_cam_origin - s.position_w.xyz;\n"
"    vec3 n = normalize(to_cam);\n"
"    float denom = dot(rd, n);\n"
"    if (abs(denom) < 1e-6) return -1.0;\n"
"    float t = dot(s.position_w.xyz - ro, n) / denom;\n"
"    if (t < 0.0) return -1.0;\n"
"    vec3 world_up = vec3(0.0, 1.0, 0.0);\n"
"    vec3 r = cross(n, world_up);\n"
"    if (length(r) < 1e-3) r = cross(n, vec3(0.0, 0.0, 1.0));\n"
"    r = normalize(r);\n"
"    vec3 u = cross(r, n);\n"
"    vec3 hp = ro + rd * t;\n"
"    vec3 diff = hp - s.position_w.xyz;\n"
"    float lx = dot(diff, r);\n"
"    float ly = dot(diff, u);\n"
"    float hw = s.position_w.w * 0.5;\n"
"    float hh = s.direction_h.w * 0.5;\n"
"    if (lx < -hw || lx > hw || ly < -hh || ly > hh) return -1.0;\n"
"    hp_out = hp;\n"
"    right_out = r;\n"
"    up_out = u;\n"
"    normal_out = n;\n"
"    return t;\n"
"}\n"
"\n"
"int sprite_select_frame(Sprite s) {\n"
"    int fc = s.frame_info.x;\n"
"    if (fc <= 1) return 0;\n"
"    vec3 to_cam = u_cam_origin - s.position_w.xyz;\n"
"    if (length(to_cam.xz) < 1e-4) return 0;\n"
"    float a = atan(to_cam.x, to_cam.z) - atan(s.direction_h.x, s.direction_h.z);\n"
"    const float TAU = 6.28318530718;\n"
"    a = mod(a + TAU, TAU);\n"
"    float sector = TAU / float(fc);\n"
"    int idx = int(floor(a / sector + 0.5)) % fc;\n"
"    return idx;\n"
"}\n"
"\n"
"vec4 sprite_sample(Sprite s, vec3 hp, vec3 r, vec3 u, int frame) {\n"
"    vec3 diff = hp - s.position_w.xyz;\n"
"    float lx = dot(diff, r);\n"
"    float ly = dot(diff, u);\n"
"    float tu = (lx / s.position_w.w) + 0.5;\n"
"    float tv = 0.5 - (ly / s.direction_h.w);\n"
"    int layer = s.frame_info.y + frame;\n"
"    /* texture() with sampler2DArray does bilinear; we want nearest to\n"
"     * match CPU sampling. Use textureLod with integer coords. */\n"
"    ivec2 sz = ivec2(s.frame_info.z, s.frame_info.w);\n"
"    int px = clamp(int(tu * float(sz.x)), 0, sz.x - 1);\n"
"    int py = clamp(int(tv * float(sz.y)), 0, sz.y - 1);\n"
"    return texelFetch(u_sprite_atlas, ivec3(px, py, layer), 0);\n"
"}\n"
"\n"
"/* Heightfield: AABB + DDA grid traversal with triangulated cells */\n"
"int hf_aabb_test(Heightfield hf, vec3 ro, vec3 rd, out float t_enter, out float t_exit) {\n"
"    float xmin = hf.origin_world.x;\n"
"    float xmax = hf.origin_world.x + hf.origin_world.z;\n"
"    float ymin = 0.0;\n"
"    float ymax = hf.grid.z;\n"
"    float zmin = hf.origin_world.y;\n"
"    float zmax = hf.origin_world.y + hf.origin_world.w;\n"
"    float tmin = -1e30;\n"
"    float tmax =  1e30;\n"
"    float mn[3] = float[3](xmin, ymin, zmin);\n"
"    float mx[3] = float[3](xmax, ymax, zmax);\n"
"    for (int i = 0; i < 3; i++) {\n"
"        float rdi = rd[i];\n"
"        float roi = ro[i];\n"
"        if (abs(rdi) > 1e-6) {\n"
"            float t0 = (mn[i] - roi) / rdi;\n"
"            float t1 = (mx[i] - roi) / rdi;\n"
"            if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }\n"
"            if (t0 > tmin) tmin = t0;\n"
"            if (t1 < tmax) tmax = t1;\n"
"        } else if (roi < mn[i] || roi > mx[i]) return 0;\n"
"    }\n"
"    if (tmin > tmax) return 0;\n"
"    if (tmax < 0.0) return 0;\n"
"    t_enter = max(tmin, 0.0);\n"
"    t_exit  = tmax;\n"
"    return 1;\n"
"}\n"
"\n"
"vec3 hf_vertex_pos(Heightfield hf, int r, int c) {\n"
"    int rows = int(hf.grid.x);\n"
"    int cols = int(hf.grid.y);\n"
"    float cw = hf.origin_world.z / float(cols - 1);\n"
"    float cd = hf.origin_world.w / float(rows - 1);\n"
"    float h = hf_heights[hf.offsets.x + r * cols + c];\n"
"    return vec3(hf.origin_world.x + float(c) * cw, h, hf.origin_world.y + float(r) * cd);\n"
"}\n"
"\n"
"vec3 hf_vertex_normal(Heightfield hf, int r, int c) {\n"
"    int cols = int(hf.grid.y);\n"
"    int base = hf.offsets.y + (r * cols + c) * 3;\n"
"    return vec3(hf_normals[base], hf_normals[base + 1], hf_normals[base + 2]);\n"
"}\n"
"\n"
"float hf_tri(vec3 ro, vec3 rd, vec3 v0, vec3 v1, vec3 v2, out float uu, out float vv) {\n"
"    vec3 e1 = v1 - v0;\n"
"    vec3 e2 = v2 - v0;\n"
"    vec3 pvec = cross(rd, e2);\n"
"    float det = dot(e1, pvec);\n"
"    if (abs(det) < 1e-6) return -1.0;\n"
"    float inv_det = 1.0 / det;\n"
"    vec3 tvec = ro - v0;\n"
"    float u = dot(tvec, pvec) * inv_det;\n"
"    if (u < 0.0 || u > 1.0) return -1.0;\n"
"    vec3 qvec = cross(tvec, e1);\n"
"    float v = dot(rd, qvec) * inv_det;\n"
"    if (v < 0.0 || u + v > 1.0) return -1.0;\n"
"    float t = dot(e2, qvec) * inv_det;\n"
"    if (t < 0.0) return -1.0;\n"
"    uu = u; vv = v;\n"
"    return t;\n"
"}\n"
"\n"
"int isect_heightfield(Heightfield hf, vec3 ro, vec3 rd,\n"
"                      out float best_t, out vec3 best_n,\n"
"                      out int best_cr, out int best_cc) {\n"
"    float te, tx;\n"
"    if (hf_aabb_test(hf, ro, rd, te, tx) == 0) return 0;\n"
"    int rows = int(hf.grid.x);\n"
"    int cols = int(hf.grid.y);\n"
"    int cells_x = cols - 1;\n"
"    int cells_z = rows - 1;\n"
"    float cw = hf.origin_world.z / float(cells_x);\n"
"    float cd = hf.origin_world.w / float(cells_z);\n"
"    float eps = 1e-4;\n"
"    vec3 entry = ro + rd * (te + eps);\n"
"    float gx = (entry.x - hf.origin_world.x) / cw;\n"
"    float gz = (entry.z - hf.origin_world.y) / cd;\n"
"    int cx = int(floor(gx));\n"
"    int cz = int(floor(gz));\n"
"    cx = clamp(cx, 0, cells_x - 1);\n"
"    cz = clamp(cz, 0, cells_z - 1);\n"
"    int step_x = (rd.x >= 0.0) ? 1 : -1;\n"
"    int step_z = (rd.z >= 0.0) ? 1 : -1;\n"
"    float t_delta_x = (abs(rd.x) > 1e-6) ? abs(cw / rd.x) : 1e30;\n"
"    float t_delta_z = (abs(rd.z) > 1e-6) ? abs(cd / rd.z) : 1e30;\n"
"    float next_xb = hf.origin_world.x + float((rd.x >= 0.0) ? (cx + 1) : cx) * cw;\n"
"    float next_zb = hf.origin_world.y + float((rd.z >= 0.0) ? (cz + 1) : cz) * cd;\n"
"    float t_max_x = (abs(rd.x) > 1e-6) ? (next_xb - ro.x) / rd.x : 1e30;\n"
"    float t_max_z = (abs(rd.z) > 1e-6) ? (next_zb - ro.z) / rd.z : 1e30;\n"
"    best_t = 1e30;\n"
"    best_n = vec3(0.0, 1.0, 0.0);\n"
"    best_cr = -1; best_cc = -1;\n"
"    int max_steps = cells_x + cells_z + 2;\n"
"    for (int step = 0; step < max_steps; step++) {\n"
"        if (cx < 0 || cx >= cells_x || cz < 0 || cz >= cells_z) break;\n"
"        vec3 v00 = hf_vertex_pos(hf, cz,     cx);\n"
"        vec3 v10 = hf_vertex_pos(hf, cz + 1, cx);\n"
"        vec3 v01 = hf_vertex_pos(hf, cz,     cx + 1);\n"
"        vec3 v11 = hf_vertex_pos(hf, cz + 1, cx + 1);\n"
"        vec3 n00 = hf_vertex_normal(hf, cz,     cx);\n"
"        vec3 n10 = hf_vertex_normal(hf, cz + 1, cx);\n"
"        vec3 n01 = hf_vertex_normal(hf, cz,     cx + 1);\n"
"        vec3 n11 = hf_vertex_normal(hf, cz + 1, cx + 1);\n"
"        float u, v;\n"
"        float t = hf_tri(ro, rd, v00, v10, v01, u, v);\n"
"        if (t > 0.0 && t < best_t) {\n"
"            float w = 1.0 - u - v;\n"
"            best_t = t;\n"
"            best_n = normalize(w * n00 + u * n10 + v * n01);\n"
"            best_cr = cz; best_cc = cx;\n"
"        }\n"
"        t = hf_tri(ro, rd, v10, v11, v01, u, v);\n"
"        if (t > 0.0 && t < best_t) {\n"
"            float w = 1.0 - u - v;\n"
"            best_t = t;\n"
"            best_n = normalize(w * n10 + u * n11 + v * n01);\n"
"            best_cr = cz; best_cc = cx;\n"
"        }\n"
"        if (best_t < 1e30 && best_t <= min(t_max_x, t_max_z) + eps) break;\n"
"        if (t_max_x < t_max_z) { cx += step_x; t_max_x += t_delta_x; }\n"
"        else                   { cz += step_z; t_max_z += t_delta_z; }\n"
"    }\n"
"    return (best_t < 1e30) ? 1 : 0;\n"
"}\n"
"\n"
"/* ---- UV computation per primitive type ------------------------------ */\n"
"\n"
"const float RT_PI = 3.14159265358979323846;\n"
"\n"
"void rt_tangent_basis(vec3 n, out vec3 t, out vec3 b) {\n"
"    vec3 up = (abs(n.y) < 0.999) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);\n"
"    t = normalize(cross(up, n));\n"
"    b = cross(n, t);\n"
"}\n"
"\n"
"vec2 uv_sphere(vec3 hp, vec3 center) {\n"
"    vec3 n = normalize(hp - center);\n"
"    return vec2(atan(n.z, n.x) / (2.0 * RT_PI) + 0.5,\n"
"                acos(clamp(n.y, -1.0, 1.0)) / RT_PI);\n"
"}\n"
"\n"
"vec2 uv_planar(vec3 hp, vec3 anchor, vec3 normal) {\n"
"    vec3 t, b;\n"
"    rt_tangent_basis(normal, t, b);\n"
"    vec3 d = hp - anchor;\n"
"    return vec2(dot(d, t), dot(d, b));\n"
"}\n"
"\n"
"vec2 uv_cyl(vec3 hp, Cylinder c) {\n"
"    vec3 axis = normalize(c.axis_radius.xyz);\n"
"    vec3 ohp = hp - c.center_hh.xyz;\n"
"    float h = dot(ohp, axis);\n"
"    vec3 radial = ohp - axis * h;\n"
"    vec3 ta, tb;\n"
"    rt_tangent_basis(axis, ta, tb);\n"
"    return vec2(atan(dot(radial, tb), dot(radial, ta)) / (2.0 * RT_PI) + 0.5,\n"
"                (h + c.center_hh.w) / (2.0 * c.center_hh.w));\n"
"}\n"
"\n"
"vec2 uv_box_face(vec3 hp, vec3 normal) {\n"
"    float ax = abs(normal.x), ay = abs(normal.y), az = abs(normal.z);\n"
"    if (ax >= ay && ax >= az)      return vec2(hp.z, hp.y);\n"
"    else if (ay >= ax && ay >= az) return vec2(hp.x, hp.z);\n"
"    else                           return vec2(hp.x, hp.y);\n"
"}\n"
"\n"
"/* ---- Procedural noise (matches libs/raytrace/cpu/render_chunk.c) ---- */\n"
"\n"
"uint noise_hash(int x, int y, int z) {\n"
"    uint n = (uint(x) * 73856093u)\n"
"           ^ (uint(y) * 19349663u)\n"
"           ^ (uint(z) * 83492791u);\n"
"    n = n * 2654435761u;\n"
"    n = n ^ (n >> 13);\n"
"    n = n * 2654435761u;\n"
"    return n;\n"
"}\n"
"\n"
"float noise_rand(int x, int y, int z) {\n"
"    return float(noise_hash(x, y, z) & 0xFFFFFFu) / 16777216.0;\n"
"}\n"
"\n"
"float noise_smooth(float t) { return t * t * (3.0 - 2.0 * t); }\n"
"float noise_lerp(float a, float b, float t) { return a + (b - a) * t; }\n"
"float noise_smoothstep(float e0, float e1, float x) {\n"
"    float t = clamp((x - e0) / (e1 - e0), 0.0, 1.0);\n"
"    return t * t * (3.0 - 2.0 * t);\n"
"}\n"
"\n"
"float noise3d(vec3 p) {\n"
"    int ix = int(floor(p.x));\n"
"    int iy = int(floor(p.y));\n"
"    int iz = int(floor(p.z));\n"
"    float fx = p.x - float(ix);\n"
"    float fy = p.y - float(iy);\n"
"    float fz = p.z - float(iz);\n"
"    float u = noise_smooth(fx);\n"
"    float v = noise_smooth(fy);\n"
"    float w = noise_smooth(fz);\n"
"    float n000 = noise_rand(ix,     iy,     iz);\n"
"    float n100 = noise_rand(ix + 1, iy,     iz);\n"
"    float n010 = noise_rand(ix,     iy + 1, iz);\n"
"    float n110 = noise_rand(ix + 1, iy + 1, iz);\n"
"    float n001 = noise_rand(ix,     iy,     iz + 1);\n"
"    float n101 = noise_rand(ix + 1, iy,     iz + 1);\n"
"    float n011 = noise_rand(ix,     iy + 1, iz + 1);\n"
"    float n111 = noise_rand(ix + 1, iy + 1, iz + 1);\n"
"    float nx00 = noise_lerp(n000, n100, u);\n"
"    float nx10 = noise_lerp(n010, n110, u);\n"
"    float nx01 = noise_lerp(n001, n101, u);\n"
"    float nx11 = noise_lerp(n011, n111, u);\n"
"    float nxy0 = noise_lerp(nx00, nx10, v);\n"
"    float nxy1 = noise_lerp(nx01, nx11, v);\n"
"    return noise_lerp(nxy0, nxy1, w);\n"
"}\n"
"\n"
"void voronoi3d(vec3 p, out float f1_out, out float f2_out) {\n"
"    ivec3 base = ivec3(int(floor(p.x)), int(floor(p.y)), int(floor(p.z)));\n"
"    float f1 = 1e20;\n"
"    float f2 = 1e20;\n"
"    for (int dz = -1; dz <= 1; dz++) {\n"
"    for (int dy = -1; dy <= 1; dy++) {\n"
"    for (int dx = -1; dx <= 1; dx++) {\n"
"        int cx = base.x + dx;\n"
"        int cy = base.y + dy;\n"
"        int cz = base.z + dz;\n"
"        float ox = float(cx) + noise_rand(cx,        cy,        cz);\n"
"        float oy = float(cy) + noise_rand(cx + 1013, cy + 1013, cz + 1013);\n"
"        float oz = float(cz) + noise_rand(cx + 2027, cy + 2027, cz + 2027);\n"
"        float rx = ox - p.x;\n"
"        float ry = oy - p.y;\n"
"        float rz = oz - p.z;\n"
"        float d  = sqrt(rx*rx + ry*ry + rz*rz);\n"
"        if (d < f1)      { f2 = f1; f1 = d; }\n"
"        else if (d < f2) { f2 = d; }\n"
"    }}}\n"
"    f1_out = f1;\n"
"    f2_out = f2;\n"
"}\n"
"\n"
"float turbulence(vec3 p, int octaves) {\n"
"    float total = 0.0;\n"
"    float amp = 1.0;\n"
"    float sum = 0.0;\n"
"    for (int i = 0; i < octaves; i++) {\n"
"        float f = float(1 << i);\n"
"        total += amp * noise3d(p * f);\n"
"        sum += amp;\n"
"        amp *= 0.5;\n"
"    }\n"
"    return total / sum;\n"
"}\n"
"\n"
"vec3 color_lerp(vec3 a, vec3 b, float t) {\n"
"    t = clamp(t, 0.0, 1.0);\n"
"    return a + (b - a) * t;\n"
"}\n"
"\n"
"/* ---- Material sampling ---------------------------------------------- */\n"
"\n"
"vec3 material_sample(int midx, vec3 p, vec2 uv) {\n"
"    Material m = materials[midx];\n"
"    if (m.kind.x == 1) {\n"
"        float s = (m.scale.x > 0.0) ? m.scale.x : 1.0;\n"
"        const float eps = 1e-4;\n"
"        int ix = int(floor(p.x / s + eps));\n"
"        int iy = int(floor(p.y / s + eps));\n"
"        int iz = int(floor(p.z / s + eps));\n"
"        return ((ix + iy + iz) & 1) != 0 ? m.albedo2.rgb : m.albedo.rgb;\n"
"    }\n"
"    if (m.kind.x == 2 && m.kind.y >= 0) {\n"
"        int tidx = m.kind.y;\n"
"        Texture tx = textures[tidx];\n"
"        float s = (m.scale.x > 0.0) ? m.scale.x : 1.0;\n"
"        float uu = uv.x / s; uu = uu - floor(uu);\n"
"        float vv = uv.y / s; vv = vv - floor(vv);\n"
"        int sx = tx.size.x, sy = tx.size.y;\n"
"        int px = clamp(int(uu * float(sx)), 0, sx - 1);\n"
"        int py = clamp(int(vv * float(sy)), 0, sy - 1);\n"
"        return texelFetch(u_tex_atlas, ivec3(px, py, tidx), 0).rgb;\n"
"    }\n"
"    if (m.kind.x == 3) {\n"
"        float s = (m.scale.x > 0.0) ? m.scale.x : 1.0;\n"
"        return color_lerp(m.albedo.rgb, m.albedo2.rgb, p.y / s);\n"
"    }\n"
"    if (m.kind.x == 4) {\n"
"        float s = (m.scale.x > 0.0) ? m.scale.x : 1.0;\n"
"        float t = noise3d(p / s);\n"
"        return color_lerp(m.albedo.rgb, m.albedo2.rgb, t);\n"
"    }\n"
"    if (m.kind.x == 5) {\n"
"        float s = (m.scale.x > 0.0) ? m.scale.x : 1.0;\n"
"        float turb = turbulence(p * 0.5, 4) * 0.8;\n"
"        float r = length(vec2(p.x, p.z)) / s;\n"
"        float rings = 0.5 + 0.5 * sin((r + turb) * 6.2831853);\n"
"        float t = rings * rings;\n"
"        return color_lerp(m.albedo.rgb, m.albedo2.rgb, t);\n"
"    }\n"
"    if (m.kind.x == 6) {\n"
"        float s = (m.scale.x > 0.0) ? m.scale.x : 1.0;\n"
"        float turb = turbulence(p, 5) * 5.0;\n"
"        float t = 0.5 + 0.5 * sin(p.x / s + turb);\n"
"        t = t * t * t * t;\n"
"        return color_lerp(m.albedo.rgb, m.albedo2.rgb, t);\n"
"    }\n"
"    if (m.kind.x == 7) {\n"
"        float s = (m.scale.x > 0.0) ? m.scale.x : 1.0;\n"
"        float f1; float f2; voronoi3d(p / s, f1, f2);\n"
"        return color_lerp(m.albedo.rgb, m.albedo2.rgb, f1);\n"
"    }\n"
"    if (m.kind.x == 8) {\n"
"        float s = (m.scale.x > 0.0) ? m.scale.x : 1.0;\n"
"        float f1; float f2; voronoi3d(p / s, f1, f2);\n"
"        float edge = f2 - f1;\n"
"        float e = clamp(edge / 0.12, 0.0, 1.0);\n"
"        float t = 1.0 - e * e * (3.0 - 2.0 * e);\n"
"        return color_lerp(m.albedo.rgb, m.albedo2.rgb, t);\n"
"    }\n"
"    if (m.kind.x == 9) {\n"
"        float s = (m.scale.x > 0.0) ? m.scale.x : 1.0;\n"
"        int ix = int(floor(p.x / s + 1e-4));\n"
"        float t = ((ix & 1) != 0) ? 1.0 : 0.0;\n"
"        return color_lerp(m.albedo.rgb, m.albedo2.rgb, t);\n"
"    }\n"
"    if (m.kind.x == 10) {\n"
"        float s = (m.scale.x > 0.0) ? m.scale.x : 1.0;\n"
"        float ux = p.x / s; float uz = p.z / s;\n"
"        float lx = ux - floor(ux) - 0.5;\n"
"        float lz = uz - floor(uz) - 0.5;\n"
"        float d = sqrt(lx * lx + lz * lz);\n"
"        float t = 1.0 - noise_smoothstep(0.26, 0.30, d);\n"
"        return color_lerp(m.albedo.rgb, m.albedo2.rgb, t);\n"
"    }\n"
"    if (m.kind.x == 11) {\n"
"        float s = (m.scale.x > 0.0) ? m.scale.x : 1.0;\n"
"        float bz = p.z / s;\n"
"        int row = int(floor(bz));\n"
"        float offset = ((row & 1) != 0) ? 0.5 : 0.0;\n"
"        float bx = p.x / (2.0 * s) + offset;\n"
"        float lx = bx - floor(bx);\n"
"        float lz = bz - floor(bz);\n"
"        float mortar = 0.04;\n"
"        bool in_mortar = (lx < mortar) || (lx > 1.0 - mortar)\n"
"                      || (lz < mortar) || (lz > 1.0 - mortar);\n"
"        float t = in_mortar ? 1.0 : 0.0;\n"
"        return color_lerp(m.albedo.rgb, m.albedo2.rgb, t);\n"
"    }\n"
"    if (m.kind.x == 12) {\n"
"        float s = (m.scale.x > 0.0) ? m.scale.x : 1.0;\n"
"        float t = turbulence(p / s, 4);\n"
"        t = noise_smoothstep(0.40, 0.70, t);\n"
"        return color_lerp(m.albedo.rgb, m.albedo2.rgb, t);\n"
"    }\n"
"    if (m.kind.x == 13) {\n"
"        float s = (m.scale.x > 0.0) ? m.scale.x : 1.0;\n"
"        float n = noise3d(p / s);\n"
"        float t = noise_smoothstep(0.55, 0.60, n);\n"
"        return color_lerp(m.albedo.rgb, m.albedo2.rgb, t);\n"
"    }\n"
"    return m.albedo.rgb;\n"
"}\n"
"\n"
"/* ---- Closest-hit + bounce loop -------------------------------------- */\n"
"\n"
"struct HitInfo {\n"
"    bool  hit;\n"
"    vec3  point;\n"
"    vec3  normal;\n"
"    vec3  albedo;\n"
"    float reflectivity;\n"
"};\n"
"\n"
"HitInfo closest_hit(vec3 ro, vec3 rd) {\n"
"    HitInfo h;\n"
"    h.hit = false;\n"
"    h.point = vec3(0.0);\n"
"    h.normal = vec3(0.0);\n"
"    h.albedo = vec3(0.0);\n"
"    h.reflectivity = 0.0;\n"
"    float closest_t = 1e30;\n"
"\n"
"    for (int i = 0; i < u_sphere_count; i++) {\n"
"        float t = isect_sphere(ro, rd, spheres[i]);\n"
"        if (t > 0.0 && t < closest_t) {\n"
"            closest_t = t;\n"
"            vec3 hp = ro + rd * t;\n"
"            h.point  = hp;\n"
"            h.normal = normalize(hp - spheres[i].center_radius.xyz);\n"
"            vec2 uv  = uv_sphere(hp, spheres[i].center_radius.xyz);\n"
"            int midx = spheres[i].mat.x;\n"
"            h.albedo = material_sample(midx, hp, uv);\n"
"            h.reflectivity = materials[midx].scale.y;\n"
"            h.hit = true;\n"
"        }\n"
"    }\n"
"    for (int i = 0; i < u_plane_count; i++) {\n"
"        float t = isect_plane(ro, rd, planes[i]);\n"
"        if (t > 0.0 && t < closest_t) {\n"
"            closest_t = t;\n"
"            vec3 hp = ro + rd * t;\n"
"            h.point  = hp;\n"
"            h.normal = planes[i].normal.xyz;\n"
"            vec2 uv  = uv_planar(hp, planes[i].point.xyz, h.normal);\n"
"            int midx = planes[i].mat.x;\n"
"            h.albedo = material_sample(midx, hp, uv);\n"
"            h.reflectivity = materials[midx].scale.y;\n"
"            h.hit = true;\n"
"        }\n"
"    }\n"
"    for (int i = 0; i < u_disc_count; i++) {\n"
"        float t = isect_disc(ro, rd, discs[i]);\n"
"        if (t > 0.0 && t < closest_t) {\n"
"            closest_t = t;\n"
"            vec3 hp = ro + rd * t;\n"
"            h.point  = hp;\n"
"            h.normal = discs[i].normal.xyz;\n"
"            vec2 uv  = uv_planar(hp, discs[i].center_radius.xyz, h.normal);\n"
"            int midx = discs[i].mat.x;\n"
"            h.albedo = material_sample(midx, hp, uv);\n"
"            h.reflectivity = materials[midx].scale.y;\n"
"            h.hit = true;\n"
"        }\n"
"    }\n"
"    for (int i = 0; i < u_cylinder_count; i++) {\n"
"        vec3 hp;\n"
"        float t = isect_cylinder(ro, rd, cylinders[i], hp);\n"
"        if (t > 0.0 && t < closest_t) {\n"
"            closest_t = t;\n"
"            h.point  = hp;\n"
"            h.normal = normal_cylinder(hp, cylinders[i]);\n"
"            vec2 uv  = uv_cyl(hp, cylinders[i]);\n"
"            int midx = cylinders[i].mat.x;\n"
"            h.albedo = material_sample(midx, hp, uv);\n"
"            h.reflectivity = materials[midx].scale.y;\n"
"            h.hit = true;\n"
"        }\n"
"    }\n"
"    for (int i = 0; i < u_triangle_count; i++) {\n"
"        float t = isect_triangle(ro, rd, triangles[i]);\n"
"        if (t > 0.0 && t < closest_t) {\n"
"            closest_t = t;\n"
"            vec3 hp = ro + rd * t;\n"
"            h.point  = hp;\n"
"            h.normal = normal_triangle(triangles[i]);\n"
"            vec2 uv  = uv_planar(hp, triangles[i].v0.xyz, h.normal);\n"
"            int midx = triangles[i].mat.x;\n"
"            h.albedo = material_sample(midx, hp, uv);\n"
"            h.reflectivity = materials[midx].scale.y;\n"
"            h.hit = true;\n"
"        }\n"
"    }\n"
"    /* --- Mesh triangles via per-mesh BVH ------------------------------- */\n"
"    if (u_mesh_count > 0) {\n"
"        vec3 inv_rd;\n"
"        inv_rd.x = (abs(rd.x) > 1e-20) ? 1.0 / rd.x : (rd.x >= 0.0 ? 1e30 : -1e30);\n"
"        inv_rd.y = (abs(rd.y) > 1e-20) ? 1.0 / rd.y : (rd.y >= 0.0 ? 1e30 : -1e30);\n"
"        inv_rd.z = (abs(rd.z) > 1e-20) ? 1.0 / rd.z : (rd.z >= 0.0 ? 1e30 : -1e30);\n"
"        for (int mi = 0; mi < u_mesh_count; mi++) {\n"
"            Mesh mesh = meshes[mi];\n"
"            /* Bounding-sphere outer reject (skip if radius <= 0). */\n"
"            if (mesh.bounds.w > 0.0) {\n"
"                vec3 oc = ro - mesh.bounds.xyz;\n"
"                float b = dot(oc, rd);\n"
"                float cs = dot(oc, oc) - mesh.bounds.w * mesh.bounds.w;\n"
"                if (cs > 0.0 && b > 0.0) continue;\n"
"                if (b * b - cs < 0.0) continue;\n"
"            }\n"
"            int bvh_base  = mesh.info.x;\n"
"            int bvh_count = mesh.info.y;\n"
"            int tri_base  = mesh.info.z;\n"
"            int tri_count = mesh.info.w;\n"
"            if (bvh_count <= 0) {\n"
"                /* No BVH — linear scan this mesh's triangle range. */\n"
"                int end = tri_base + tri_count;\n"
"                for (int ti = tri_base; ti < end; ti++) {\n"
"                    float t = isect_triangle(ro, rd, triangles[ti]);\n"
"                    if (t > 0.0 && t < closest_t) {\n"
"                        closest_t = t;\n"
"                        vec3 hp = ro + rd * t;\n"
"                        h.point  = hp;\n"
"                        h.normal = normal_triangle(triangles[ti]);\n"
"                        vec2 uv  = uv_planar(hp, triangles[ti].v0.xyz, h.normal);\n"
"                        int midx = triangles[ti].mat.x;\n"
"                        h.albedo = material_sample(midx, hp, uv);\n"
"                        h.reflectivity = materials[midx].scale.y;\n"
"                        h.hit = true;\n"
"                    }\n"
"                }\n"
"                continue;\n"
"            }\n"
"            int stack[32];\n"
"            int sp = 0;\n"
"            stack[sp++] = 0;\n"
"            while (sp > 0) {\n"
"                int idx = stack[--sp];\n"
"                BvhNode node = bvh_nodes[bvh_base + idx];\n"
"                vec3 t0 = (node.aabb_min.xyz - ro) * inv_rd;\n"
"                vec3 t1 = (node.aabb_max.xyz - ro) * inv_rd;\n"
"                vec3 tmin3 = min(t0, t1);\n"
"                vec3 tmax3 = max(t0, t1);\n"
"                float tmn = max(max(tmin3.x, tmin3.y), tmin3.z);\n"
"                float tmx = min(min(tmax3.x, tmax3.y), tmax3.z);\n"
"                if (tmx < 0.0 || tmn > tmx) continue;\n"
"                float t_enter = max(tmn, 0.0);\n"
"                if (t_enter > closest_t) continue;\n"
"                int ncount = node.info.y;\n"
"                if (ncount > 0) {\n"
"                    int start = tri_base + node.info.x;\n"
"                    int end   = start + ncount;\n"
"                    for (int ti = start; ti < end; ti++) {\n"
"                        float t = isect_triangle(ro, rd, triangles[ti]);\n"
"                        if (t > 0.0 && t < closest_t) {\n"
"                            closest_t = t;\n"
"                            vec3 hp = ro + rd * t;\n"
"                            h.point  = hp;\n"
"                            h.normal = normal_triangle(triangles[ti]);\n"
"                            vec2 uv  = uv_planar(hp, triangles[ti].v0.xyz, h.normal);\n"
"                            int midx = triangles[ti].mat.x;\n"
"                            h.albedo = material_sample(midx, hp, uv);\n"
"                            h.reflectivity = materials[midx].scale.y;\n"
"                            h.hit = true;\n"
"                        }\n"
"                    }\n"
"                } else {\n"
"                    if (sp + 2 <= 32) {\n"
"                        stack[sp++] = idx + 1;\n"
"                        stack[sp++] = idx + node.info.z;\n"
"                    }\n"
"                }\n"
"            }\n"
"        }\n"
"    }\n"
"    for (int i = 0; i < u_box_count; i++) {\n"
"        vec3 hp;\n"
"        float t = isect_box(ro, rd, boxes[i], hp);\n"
"        if (t > 0.0 && t < closest_t) {\n"
"            closest_t = t;\n"
"            h.point  = hp;\n"
"            h.normal = normal_box(hp, boxes[i]);\n"
"            vec2 uv  = uv_box_face(hp, h.normal);\n"
"            int midx = boxes[i].mat.x;\n"
"            h.albedo = material_sample(midx, hp, uv);\n"
"            h.reflectivity = materials[midx].scale.y;\n"
"            h.hit = true;\n"
"        }\n"
"    }\n"
"    for (int i = 0; i < u_sprite_count; i++) {\n"
"        vec3 hp, sright, sup, snormal;\n"
"        float t = isect_sprite(ro, rd, sprites[i], hp, sright, sup, snormal);\n"
"        if (t > 0.0 && t < closest_t) {\n"
"            int frame = sprite_select_frame(sprites[i]);\n"
"            vec4 px = sprite_sample(sprites[i], hp, sright, sup, frame);\n"
"            if (px.a > 0.0) {\n"
"                closest_t = t;\n"
"                h.point  = hp;\n"
"                h.normal = snormal;\n"
"                h.albedo = px.rgb;\n"
"                h.reflectivity = 0.0;\n"
"                h.hit = true;\n"
"            }\n"
"        }\n"
"    }\n"
"    for (int i = 0; i < u_heightfield_count; i++) {\n"
"        float t; vec3 hn; int cr, cc;\n"
"        if (isect_heightfield(heightfields[i], ro, rd, t, hn, cr, cc) == 1) {\n"
"            if (t > 0.0 && t < closest_t) {\n"
"                closest_t = t;\n"
"                h.point  = ro + rd * t;\n"
"                h.normal = hn;\n"
"                int cells_per_row = int(heightfields[i].grid.y) - 1;\n"
"                int ci = heightfields[i].offsets.z + (cr * cells_per_row + cc) * 3;\n"
"                h.albedo = vec3(\n"
"                    float(hf_colors[ci])     / 255.0,\n"
"                    float(hf_colors[ci + 1]) / 255.0,\n"
"                    float(hf_colors[ci + 2]) / 255.0\n"
"                );\n"
"                h.reflectivity = 0.0;\n"
"                h.hit = true;\n"
"            }\n"
"        }\n"
"    }\n"
"    return h;\n"
"}\n"
"\n"
"const int RT_MAX_BOUNCES = 4;\n"
"\n"
"/* ---- Main ------------------------------------------------------------ */\n"
"\n"
"void main() {\n"
"    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);\n"
"    ivec2 size  = imageSize(outputImage);\n"
"    if (pixel.x >= size.x || pixel.y >= size.y) return;\n"
"\n"
"    float half_w = float(size.x) * 0.5;\n"
"    float half_h = float(size.y) * 0.5;\n"
"    float fov_factor = float(size.y) / (2.0 * tan(u_fov * 0.5));\n"
"    float sx = (float(pixel.x) - half_w) / fov_factor;\n"
"    float sy = -(float(pixel.y) - half_h) / fov_factor;\n"
"    vec3 ro = u_cam_origin;\n"
"    vec3 rd = normalize(u_cam_forward + u_cam_right * sx + u_cam_up * sy);\n"
"\n"
"    vec3 result     = vec3(0.0);\n"
"    vec3 throughput = vec3(1.0);\n"
"\n"
"    for (int bounce = 0; bounce < RT_MAX_BOUNCES; bounce++) {\n"
"        HitInfo h = closest_hit(ro, rd);\n"
"        if (!h.hit) break;\n"
"\n"
"        float shade = u_ambient;\n"
"        for (int i = 0; i < u_light_count; i++) {\n"
"            float d = dot(h.normal, lights[i].direction_intensity.xyz);\n"
"            if (d > 0.0) shade += d * lights[i].direction_intensity.w;\n"
"        }\n"
"        shade = min(shade, 1.0);\n"
"\n"
"        vec3 direct = h.albedo * shade;\n"
"        result += throughput * (1.0 - h.reflectivity) * direct;\n"
"\n"
"        if (h.reflectivity <= 0.0) break;\n"
"\n"
"        throughput *= h.reflectivity;\n"
"        rd = reflect(rd, h.normal);\n"
"        ro = h.point + rd * 1e-4;\n"
"    }\n"
"\n"
"    imageStore(outputImage, pixel, vec4(min(result, vec3(1.0)), 1.0));\n"
"}\n";

/* -- GPU struct layouts (std430) -------------------------------------- */

typedef struct { float cr[4]; int32_t mat[4]; } gpu_sphere;
typedef struct { float point[4]; float normal[4]; int32_t mat[4]; } gpu_plane;
typedef struct { float cr[4]; float normal[4]; int32_t mat[4]; } gpu_disc;
typedef struct { float center_hh[4]; float axis_r[4]; int32_t mat[4]; } gpu_cylinder;
typedef struct { float v0[4]; float v1[4]; float v2[4]; int32_t mat[4]; } gpu_triangle;
typedef struct { float minp[4]; float maxp[4]; int32_t mat[4]; } gpu_box;
typedef struct { float position_w[4]; float direction_h[4]; int32_t frame_info[4]; } gpu_sprite;
typedef struct { float dir_int[4]; } gpu_light;
typedef struct {
    float   albedo[4];
    float   albedo2[4];
    int32_t kind[4];
    float   scale[4];
} gpu_material;
typedef struct { int32_t size[4]; } gpu_texture;
typedef struct {
    float   origin_world[4];  /* origin_x, origin_z, world_w, world_d */
    float   grid[4];          /* rows, cols, max_h, 0 */
    int32_t offsets[4];       /* heights_off, normals_off, colors_off, 0 */
} gpu_heightfield;

typedef struct {
    float   bounds[4];        /* bounds_center.xyz, bounds_radius */
    int32_t info[4];          /* bvh_start, bvh_count, tri_start (absolute), tri_count */
} gpu_mesh;

typedef struct {
    float   aabb_min[4];
    float   aabb_max[4];
    int32_t info[4];          /* tri_start (local), tri_count, second_child_offset, 0 */
} gpu_bvh_node;

/* -- Backend state ---------------------------------------------------- */

typedef struct {
    GLuint program;
    GLuint output_tex;
    int tex_w, tex_h;

    /* SSBOs */
    GLuint ssbo[17];
    /* bindings:
     * [0] unused  [1] spheres  [2] planes  [3] discs  [4] cylinders
     * [5] triangles [6] boxes  [7] sprites [8] lights [9] heightfields
     * [10] hf heights [11] hf normals [12] hf colors [13] materials
     * [14] textures (metadata) [15] meshes [16] bvh nodes */

    /* Sprite texture atlas: one 2D texture array, all sprite frames */
    GLuint sprite_atlas;
    int atlas_w, atlas_h, atlas_layers;

    /* Image texture atlas: one 2D texture array, one layer per scene_texture */
    GLuint tex_atlas;
    int tex_atlas_w, tex_atlas_h, tex_atlas_layers;

    /* Cached uniform locations */
    GLint u_cam_origin, u_cam_forward, u_cam_right, u_cam_up;
    GLint u_fov, u_ambient;
    GLint u_sphere_count, u_plane_count, u_disc_count, u_cylinder_count;
    GLint u_triangle_count, u_box_count, u_sprite_count;
    GLint u_heightfield_count, u_light_count, u_mesh_count;
    GLint u_sprite_atlas, u_tex_atlas;
} opengl_backend_data;

/* -- GL helpers ------------------------------------------------------- */

static int check_gl_context(void) {
    const GLubyte *version = glGetString(GL_VERSION);
    if (!version) {
        fprintf(stderr, "rt_opengl: no current GL context\n");
        return 0;
    }
    int major = 0, minor = 0;
    sscanf((const char *)version, "%d.%d", &major, &minor);
    if (major < 4 || (major == 4 && minor < 3)) {
        fprintf(stderr, "rt_opengl: needs GL 4.3+, got %d.%d (%s)\n",
                major, minor, (const char *)version);
        return 0;
    }
    return 1;
}

static GLuint compile_shader(GLenum type, const char *source) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &source, NULL);
    glCompileShader(sh);
    GLint ok;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        fprintf(stderr, "rt_opengl: shader compile error:\n%s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint create_compute_program(const char *source) {
    GLuint sh = compile_shader(GL_COMPUTE_SHADER, source);
    if (!sh) return 0;
    GLuint prog = glCreateProgram();
    glAttachShader(prog, sh);
    glLinkProgram(prog);
    glDeleteShader(sh);
    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "rt_opengl: program link error:\n%s\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

static void cache_uniform_locs(opengl_backend_data *d) {
    d->u_cam_origin         = glGetUniformLocation(d->program, "u_cam_origin");
    d->u_cam_forward        = glGetUniformLocation(d->program, "u_cam_forward");
    d->u_cam_right          = glGetUniformLocation(d->program, "u_cam_right");
    d->u_cam_up             = glGetUniformLocation(d->program, "u_cam_up");
    d->u_fov                = glGetUniformLocation(d->program, "u_fov");
    d->u_ambient            = glGetUniformLocation(d->program, "u_ambient");
    d->u_sphere_count       = glGetUniformLocation(d->program, "u_sphere_count");
    d->u_plane_count        = glGetUniformLocation(d->program, "u_plane_count");
    d->u_disc_count         = glGetUniformLocation(d->program, "u_disc_count");
    d->u_cylinder_count     = glGetUniformLocation(d->program, "u_cylinder_count");
    d->u_triangle_count     = glGetUniformLocation(d->program, "u_triangle_count");
    d->u_box_count          = glGetUniformLocation(d->program, "u_box_count");
    d->u_sprite_count       = glGetUniformLocation(d->program, "u_sprite_count");
    d->u_heightfield_count  = glGetUniformLocation(d->program, "u_heightfield_count");
    d->u_light_count        = glGetUniformLocation(d->program, "u_light_count");
    d->u_mesh_count         = glGetUniformLocation(d->program, "u_mesh_count");
    d->u_sprite_atlas       = glGetUniformLocation(d->program, "u_sprite_atlas");
    d->u_tex_atlas          = glGetUniformLocation(d->program, "u_tex_atlas");
}

static void ensure_output_tex(opengl_backend_data *d, int w, int h) {
    if (d->output_tex && d->tex_w == w && d->tex_h == h) return;
    if (d->output_tex) glDeleteTextures(1, &d->output_tex);
    glGenTextures(1, &d->output_tex);
    glBindTexture(GL_TEXTURE_2D, d->output_tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
    d->tex_w = w; d->tex_h = h;
}

/* Upload a vector+scalar or vector+0 into a vec4 slot. */
static void set_vec4(float *out, float x, float y, float z, float w) {
    out[0] = x; out[1] = y; out[2] = z; out[3] = w;
}

static void set_mat_ivec4(int32_t *out, int mat) {
    out[0] = (int32_t)mat;
    out[1] = 0;
    out[2] = 0;
    out[3] = 0;
}

/* Upload SSBO and bind it. If count==0, uploads a 1-element zero buffer
 * so the binding is still valid (shader guarded by count uniform). */
static void upload_ssbo(GLuint buf, int binding, const void *data,
                        size_t elem_size, int count) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf);
    if (count > 0) {
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     (GLsizeiptr)(elem_size * (size_t)count),
                     data, GL_DYNAMIC_DRAW);
    } else {
        /* 1-byte placeholder so binding is valid */
        static const char zero[16] = {0};
        glBufferData(GL_SHADER_STORAGE_BUFFER, 16, zero, GL_DYNAMIC_DRAW);
    }
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, buf);
}

/* -- Scene upload ----------------------------------------------------- */

static void upload_spheres(opengl_backend_data *d, const scene *s) {
    int n = s->sphere_count;
    gpu_sphere *buf = NULL;
    if (n > 0) {
        buf = malloc(sizeof(gpu_sphere) * (size_t)n);
        for (int i = 0; i < n; i++) {
            set_vec4(buf[i].cr, s->spheres[i].center.x, s->spheres[i].center.y,
                     s->spheres[i].center.z, s->spheres[i].radius);
            set_mat_ivec4(buf[i].mat, s->spheres[i].material);
        }
    }
    upload_ssbo(d->ssbo[1], 1, buf, sizeof(gpu_sphere), n);
    free(buf);
}

static void upload_planes(opengl_backend_data *d, const scene *s) {
    int n = s->plane_count;
    gpu_plane *buf = NULL;
    if (n > 0) {
        buf = malloc(sizeof(gpu_plane) * (size_t)n);
        for (int i = 0; i < n; i++) {
            set_vec4(buf[i].point, s->planes[i].point.x, s->planes[i].point.y,
                     s->planes[i].point.z, 0.0f);
            set_vec4(buf[i].normal, s->planes[i].normal.x, s->planes[i].normal.y,
                     s->planes[i].normal.z, 0.0f);
            set_mat_ivec4(buf[i].mat, s->planes[i].material);
        }
    }
    upload_ssbo(d->ssbo[2], 2, buf, sizeof(gpu_plane), n);
    free(buf);
}

static void upload_discs(opengl_backend_data *d, const scene *s) {
    int n = s->disc_count;
    gpu_disc *buf = NULL;
    if (n > 0) {
        buf = malloc(sizeof(gpu_disc) * (size_t)n);
        for (int i = 0; i < n; i++) {
            set_vec4(buf[i].cr, s->discs[i].center.x, s->discs[i].center.y,
                     s->discs[i].center.z, s->discs[i].radius);
            set_vec4(buf[i].normal, s->discs[i].normal.x, s->discs[i].normal.y,
                     s->discs[i].normal.z, 0.0f);
            set_mat_ivec4(buf[i].mat, s->discs[i].material);
        }
    }
    upload_ssbo(d->ssbo[3], 3, buf, sizeof(gpu_disc), n);
    free(buf);
}

static void upload_cylinders(opengl_backend_data *d, const scene *s) {
    int n = s->cylinder_count;
    gpu_cylinder *buf = NULL;
    if (n > 0) {
        buf = malloc(sizeof(gpu_cylinder) * (size_t)n);
        for (int i = 0; i < n; i++) {
            set_vec4(buf[i].center_hh, s->cylinders[i].center.x,
                     s->cylinders[i].center.y, s->cylinders[i].center.z,
                     s->cylinders[i].half_height);
            set_vec4(buf[i].axis_r, s->cylinders[i].axis.x,
                     s->cylinders[i].axis.y, s->cylinders[i].axis.z,
                     s->cylinders[i].radius);
            set_mat_ivec4(buf[i].mat, s->cylinders[i].material);
        }
    }
    upload_ssbo(d->ssbo[4], 4, buf, sizeof(gpu_cylinder), n);
    free(buf);
}

static void upload_triangles(opengl_backend_data *d, const scene *s) {
    /* Explicit triangles + mesh triangles share the same SSBO. The GPU
     * shader is mesh-oblivious: it just loops a flat triangle array.
     * Per-vertex normals are discarded here — the shader computes face
     * normals, so meshes render flat-shaded on the GPU (fine for
     * low-poly / retro looks; matches the existing triangle path). */
    int mesh_tri_total = 0;
    for (int i = 0; i < s->mesh_count; i++) {
        if (s->meshes[i].index_count >= 3) {
            mesh_tri_total += s->meshes[i].index_count / 3;
        }
    }
    int n = s->triangle_count + mesh_tri_total;

    gpu_triangle *buf = NULL;
    if (n > 0) {
        buf = malloc(sizeof(gpu_triangle) * (size_t)n);
        int w = 0;
        for (int i = 0; i < s->triangle_count; i++, w++) {
            set_vec4(buf[w].v0, s->triangles[i].v0.x, s->triangles[i].v0.y, s->triangles[i].v0.z, 0.0f);
            set_vec4(buf[w].v1, s->triangles[i].v1.x, s->triangles[i].v1.y, s->triangles[i].v1.z, 0.0f);
            set_vec4(buf[w].v2, s->triangles[i].v2.x, s->triangles[i].v2.y, s->triangles[i].v2.z, 0.0f);
            set_mat_ivec4(buf[w].mat, s->triangles[i].material);
        }
        for (int i = 0; i < s->mesh_count; i++) {
            const scene_mesh *m = &s->meshes[i];
            if (m->index_count < 3 || !m->indices || !m->vertices) continue;
            int tri_count = m->index_count / 3;
            for (int t = 0; t < tri_count; t++, w++) {
                uint32_t i0 = m->indices[t * 3 + 0];
                uint32_t i1 = m->indices[t * 3 + 1];
                uint32_t i2 = m->indices[t * 3 + 2];
                if ((int)i0 >= m->vertex_count ||
                    (int)i1 >= m->vertex_count ||
                    (int)i2 >= m->vertex_count) {
                    /* Skip malformed triangle; zero it out so the shader's
                     * degenerate-det test rejects it. */
                    set_vec4(buf[w].v0, 0, 0, 0, 0);
                    set_vec4(buf[w].v1, 0, 0, 0, 0);
                    set_vec4(buf[w].v2, 0, 0, 0, 0);
                    set_mat_ivec4(buf[w].mat, 0);
                    continue;
                }
                vector p0 = m->vertices[i0].position;
                vector p1 = m->vertices[i1].position;
                vector p2 = m->vertices[i2].position;
                set_vec4(buf[w].v0, p0.x, p0.y, p0.z, 0.0f);
                set_vec4(buf[w].v1, p1.x, p1.y, p1.z, 0.0f);
                set_vec4(buf[w].v2, p2.x, p2.y, p2.z, 0.0f);
                set_mat_ivec4(buf[w].mat, m->material_index);
            }
        }
    }
    upload_ssbo(d->ssbo[5], 5, buf, sizeof(gpu_triangle), n);
    free(buf);
}

/* Per-mesh BVH + descriptor upload. Assumes upload_triangles has the same
 * per-mesh layout: explicit triangles [0..triangle_count), then each mesh's
 * triangles concatenated in mesh iteration order. Mesh BVHs are built by
 * the CPU (rt_scene_build_accel) and stored in mesh->accel — we just
 * concatenate them into a single SSBO and record per-mesh offsets. */
static void upload_meshes(opengl_backend_data *d, const scene *s) {
    int n = s->mesh_count;

    int bvh_total = 0;
    for (int i = 0; i < n; i++) {
        bvh_total += s->meshes[i].accel_count;
    }

    gpu_mesh     *meta  = NULL;
    gpu_bvh_node *nodes = NULL;
    if (n > 0) {
        meta = malloc(sizeof(gpu_mesh) * (size_t)n);
        if (bvh_total > 0) {
            nodes = malloc(sizeof(gpu_bvh_node) * (size_t)bvh_total);
        }
        int tri_cursor = s->triangle_count;   /* mesh tris follow explicit tris */
        int node_cursor = 0;
        for (int i = 0; i < n; i++) {
            const scene_mesh *m = &s->meshes[i];
            int tri_count = (m->index_count >= 3) ? (m->index_count / 3) : 0;

            meta[i].bounds[0] = m->bounds_center.x;
            meta[i].bounds[1] = m->bounds_center.y;
            meta[i].bounds[2] = m->bounds_center.z;
            meta[i].bounds[3] = m->bounds_radius;
            meta[i].info[0]   = node_cursor;
            meta[i].info[1]   = m->accel_count;
            meta[i].info[2]   = tri_cursor;
            meta[i].info[3]   = tri_count;

            if (m->accel && m->accel_count > 0) {
                const rt_bvh_node *src = (const rt_bvh_node *)m->accel;
                for (int k = 0; k < m->accel_count; k++) {
                    gpu_bvh_node *dst = &nodes[node_cursor + k];
                    dst->aabb_min[0] = src[k].aabb_min[0];
                    dst->aabb_min[1] = src[k].aabb_min[1];
                    dst->aabb_min[2] = src[k].aabb_min[2];
                    dst->aabb_min[3] = 0.0f;
                    dst->aabb_max[0] = src[k].aabb_max[0];
                    dst->aabb_max[1] = src[k].aabb_max[1];
                    dst->aabb_max[2] = src[k].aabb_max[2];
                    dst->aabb_max[3] = 0.0f;
                    dst->info[0] = src[k].tri_start;
                    dst->info[1] = src[k].tri_count;
                    dst->info[2] = src[k].second_child_offset;
                    dst->info[3] = 0;
                }
                node_cursor += m->accel_count;
            }
            tri_cursor += tri_count;
        }
    }

    upload_ssbo(d->ssbo[15], 15, meta,  sizeof(gpu_mesh),     n);
    upload_ssbo(d->ssbo[16], 16, nodes, sizeof(gpu_bvh_node), bvh_total);
    free(meta); free(nodes);
}

static void upload_boxes(opengl_backend_data *d, const scene *s) {
    int n = s->box_count;
    gpu_box *buf = NULL;
    if (n > 0) {
        buf = malloc(sizeof(gpu_box) * (size_t)n);
        for (int i = 0; i < n; i++) {
            set_vec4(buf[i].minp, s->boxes[i].min.x, s->boxes[i].min.y, s->boxes[i].min.z, 0.0f);
            set_vec4(buf[i].maxp, s->boxes[i].max.x, s->boxes[i].max.y, s->boxes[i].max.z, 0.0f);
            set_mat_ivec4(buf[i].mat, s->boxes[i].material);
        }
    }
    upload_ssbo(d->ssbo[6], 6, buf, sizeof(gpu_box), n);
    free(buf);
}

static void upload_lights(opengl_backend_data *d, const scene *s) {
    int n = s->light_count;
    gpu_light *buf = NULL;
    if (n > 0) {
        buf = malloc(sizeof(gpu_light) * (size_t)n);
        for (int i = 0; i < n; i++) {
            set_vec4(buf[i].dir_int, s->lights[i].direction.x,
                     s->lights[i].direction.y, s->lights[i].direction.z,
                     s->lights[i].intensity);
        }
    }
    upload_ssbo(d->ssbo[8], 8, buf, sizeof(gpu_light), n);
    free(buf);
}

static void upload_materials(opengl_backend_data *d, const scene *s) {
    int n = s->material_count;
    gpu_material *buf = NULL;
    if (n > 0) {
        buf = malloc(sizeof(gpu_material) * (size_t)n);
        for (int i = 0; i < n; i++) {
            const scene_material *m = &s->materials[i];
            buf[i].albedo[0]  = (float)m->albedo.r  / 255.0f;
            buf[i].albedo[1]  = (float)m->albedo.g  / 255.0f;
            buf[i].albedo[2]  = (float)m->albedo.b  / 255.0f;
            buf[i].albedo[3]  = 0.0f;
            buf[i].albedo2[0] = (float)m->albedo2.r / 255.0f;
            buf[i].albedo2[1] = (float)m->albedo2.g / 255.0f;
            buf[i].albedo2[2] = (float)m->albedo2.b / 255.0f;
            buf[i].albedo2[3] = 0.0f;
            buf[i].kind[0] = (int32_t)m->tex_kind;
            buf[i].kind[1] = (int32_t)m->tex_index;
            buf[i].kind[2] = 0;
            buf[i].kind[3] = 0;
            buf[i].scale[0] = m->tex_scale;
            buf[i].scale[1] = m->reflectivity;
            buf[i].scale[2] = 0.0f;
            buf[i].scale[3] = 0.0f;
        }
    }
    upload_ssbo(d->ssbo[13], 13, buf, sizeof(gpu_material), n);
    free(buf);
}

/* Textures: metadata SSBO + 2D texture array atlas (one layer per texture).
 * Smaller textures are padded within their layer; shader uses ivec size
 * from SSBO to clamp integer sampling. */
static void upload_textures(opengl_backend_data *d, const scene *s) {
    int n = s->texture_count;

    int max_w = 1, max_h = 1;
    for (int i = 0; i < n; i++) {
        if (s->textures[i].width  > max_w) max_w = s->textures[i].width;
        if (s->textures[i].height > max_h) max_h = s->textures[i].height;
    }
    int total = (n > 0) ? n : 1;

    gpu_texture *buf = NULL;
    if (n > 0) {
        buf = malloc(sizeof(gpu_texture) * (size_t)n);
        for (int i = 0; i < n; i++) {
            buf[i].size[0] = s->textures[i].width;
            buf[i].size[1] = s->textures[i].height;
            buf[i].size[2] = 0;
            buf[i].size[3] = 0;
        }
    }
    upload_ssbo(d->ssbo[14], 14, buf, sizeof(gpu_texture), n);
    free(buf);

    /* All atlas binds happen on unit 1 so we don't clobber the sprite
     * atlas that upload_sprites just bound to unit 0. */
    glActiveTexture(GL_TEXTURE1);

    if (!d->tex_atlas || d->tex_atlas_w != max_w || d->tex_atlas_h != max_h
        || d->tex_atlas_layers != total) {
        if (d->tex_atlas) glDeleteTextures(1, &d->tex_atlas);
        glGenTextures(1, &d->tex_atlas);
        glBindTexture(GL_TEXTURE_2D_ARRAY, d->tex_atlas);
        glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, max_w, max_h, total);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        d->tex_atlas_w = max_w;
        d->tex_atlas_h = max_h;
        d->tex_atlas_layers = total;
    } else {
        glBindTexture(GL_TEXTURE_2D_ARRAY, d->tex_atlas);
    }

    for (int i = 0; i < n; i++) {
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i,
                        s->textures[i].width, s->textures[i].height, 1,
                        GL_BGRA, GL_UNSIGNED_BYTE, s->textures[i].pixels);
    }

    glUniform1i(d->u_tex_atlas, 1);
}

/* Sprites: (re)build a texture array containing all frames, and an SSBO
 * describing which frames belong to each sprite. All frames are stored
 * at a common size (the max width/height across all frames); smaller
 * frames are padded with transparent. */
static void upload_sprites(opengl_backend_data *d, const scene *s) {
    int n = s->sprite_count;

    /* Build SSBO + atlas */
    int max_w = 1, max_h = 1, total_frames = 0;
    for (int i = 0; i < n; i++) {
        for (int f = 0; f < s->sprites[i].frame_count; f++) {
            const scene_frame *fr = &s->sprites[i].frames[f];
            if (fr->width  > max_w) max_w = fr->width;
            if (fr->height > max_h) max_h = fr->height;
        }
        total_frames += s->sprites[i].frame_count;
    }
    if (total_frames < 1) total_frames = 1;

    gpu_sprite *buf = NULL;
    if (n > 0) {
        buf = malloc(sizeof(gpu_sprite) * (size_t)n);
        int layer = 0;
        for (int i = 0; i < n; i++) {
            set_vec4(buf[i].position_w,
                     s->sprites[i].position.x, s->sprites[i].position.y,
                     s->sprites[i].position.z, s->sprites[i].width);
            set_vec4(buf[i].direction_h,
                     s->sprites[i].direction.x, s->sprites[i].direction.y,
                     s->sprites[i].direction.z, s->sprites[i].height);
            buf[i].frame_info[0] = s->sprites[i].frame_count;
            buf[i].frame_info[1] = layer;
            buf[i].frame_info[2] = (s->sprites[i].frame_count > 0)
                ? s->sprites[i].frames[0].width  : 1;
            buf[i].frame_info[3] = (s->sprites[i].frame_count > 0)
                ? s->sprites[i].frames[0].height : 1;
            layer += s->sprites[i].frame_count;
        }
    }
    upload_ssbo(d->ssbo[7], 7, buf, sizeof(gpu_sprite), n);
    free(buf);

    /* Atlas: reallocate if size grew */
    if (!d->sprite_atlas || d->atlas_w != max_w || d->atlas_h != max_h
        || d->atlas_layers != total_frames) {
        if (d->sprite_atlas) glDeleteTextures(1, &d->sprite_atlas);
        glGenTextures(1, &d->sprite_atlas);
        glBindTexture(GL_TEXTURE_2D_ARRAY, d->sprite_atlas);
        glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, max_w, max_h, total_frames);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        d->atlas_w = max_w;
        d->atlas_h = max_h;
        d->atlas_layers = total_frames;
    } else {
        glBindTexture(GL_TEXTURE_2D_ARRAY, d->sprite_atlas);
    }

    /* Upload frames. ARGB8888 (uint32) matches GL_BGRA on little-endian. */
    int layer = 0;
    for (int i = 0; i < n; i++) {
        for (int f = 0; f < s->sprites[i].frame_count; f++) {
            const scene_frame *fr = &s->sprites[i].frames[f];
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer,
                            fr->width, fr->height, 1,
                            GL_BGRA, GL_UNSIGNED_BYTE, fr->pixels);
            layer++;
        }
    }
    /* Bind atlas to texture unit 0 */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, d->sprite_atlas);
    glUniform1i(d->u_sprite_atlas, 0);
}

/* Heightfields: pack metadata + concatenated heights/normals/colors
 * into four SSBOs. */
static void upload_heightfields(opengl_backend_data *d, const scene *s) {
    int n = s->heightfield_count;

    int total_verts = 0;   /* sum of rows*cols */
    int total_cells = 0;   /* sum of (rows-1)*(cols-1) */
    for (int i = 0; i < n; i++) {
        total_verts += s->heightfields[i].rows * s->heightfields[i].cols;
        total_cells += (s->heightfields[i].rows - 1) * (s->heightfields[i].cols - 1);
    }

    gpu_heightfield *meta = NULL;
    float *heights = NULL;
    float *normals = NULL;
    uint32_t *colors = NULL;

    if (n > 0) {
        meta = malloc(sizeof(gpu_heightfield) * (size_t)n);
        if (total_verts > 0) {
            heights = malloc(sizeof(float) * (size_t)total_verts);
            normals = malloc(sizeof(float) * (size_t)total_verts * 3);
        }
        if (total_cells > 0) {
            colors  = malloc(sizeof(uint32_t) * (size_t)total_cells * 3);
        }
        int h_off = 0, n_off = 0, c_off = 0;
        for (int i = 0; i < n; i++) {
            const scene_heightfield *hf = &s->heightfields[i];
            int verts = hf->rows * hf->cols;
            int cells = (hf->rows - 1) * (hf->cols - 1);

            set_vec4(meta[i].origin_world, hf->origin_x, hf->origin_z,
                     hf->world_width, hf->world_depth);
            set_vec4(meta[i].grid, (float)hf->rows, (float)hf->cols,
                     hf->max_height, 0.0f);
            meta[i].offsets[0] = h_off;
            meta[i].offsets[1] = n_off;
            meta[i].offsets[2] = c_off;
            meta[i].offsets[3] = 0;

            memcpy(&heights[h_off], hf->heights, sizeof(float) * (size_t)verts);
            memcpy(&normals[n_off], hf->normals, sizeof(float) * (size_t)verts * 3);
            /* Expand uint8 color bytes to uint32 for std430 alignment */
            for (int k = 0; k < cells * 3; k++) {
                colors[c_off + k] = (uint32_t)hf->colors[k];
            }

            h_off += verts;
            n_off += verts * 3;
            c_off += cells * 3;
        }
    }

    upload_ssbo(d->ssbo[9],  9,  meta,    sizeof(gpu_heightfield), n);
    upload_ssbo(d->ssbo[10], 10, heights, sizeof(float),           total_verts);
    upload_ssbo(d->ssbo[11], 11, normals, sizeof(float),           total_verts * 3);
    upload_ssbo(d->ssbo[12], 12, colors,  sizeof(uint32_t),        total_cells * 3);

    free(meta); free(heights); free(normals); free(colors);
}

/* -- Vtable ----------------------------------------------------------- */

static void opengl_destroy(rt_renderer *r) {
    opengl_backend_data *d = r->backend_data;
    if (d) {
        if (d->program)      glDeleteProgram(d->program);
        if (d->output_tex)   glDeleteTextures(1, &d->output_tex);
        if (d->sprite_atlas) glDeleteTextures(1, &d->sprite_atlas);
        if (d->tex_atlas)    glDeleteTextures(1, &d->tex_atlas);
        for (int i = 0; i < 17; i++) {
            if (d->ssbo[i]) glDeleteBuffers(1, &d->ssbo[i]);
        }
        free(d);
    }
    free(r);
}

static void opengl_render(rt_renderer *r,
                          const scene *scene,
                          const scene_camera *camera,
                          const rt_viewport *viewport,
                          uint32_t *pixels) {
    opengl_backend_data *d = r->backend_data;
    int w = viewport->width;
    int h = viewport->height;

    ensure_output_tex(d, w, h);

    glUseProgram(d->program);
    glBindImageTexture(0, d->output_tex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

    upload_spheres(d, scene);
    upload_planes(d, scene);
    upload_discs(d, scene);
    upload_cylinders(d, scene);
    upload_triangles(d, scene);
    upload_meshes(d, scene);        /* mesh descriptors + BVH nodes */
    upload_boxes(d, scene);
    upload_sprites(d, scene);       /* must run before sprite atlas bind */
    upload_lights(d, scene);
    upload_heightfields(d, scene);
    upload_materials(d, scene);
    upload_textures(d, scene);

    vector origin, forward, right, up;
    scene_camera_get_basis(camera, &origin, &forward, &right, &up);

    glUniform3f(d->u_cam_origin,  origin.x,  origin.y,  origin.z);
    glUniform3f(d->u_cam_forward, forward.x, forward.y, forward.z);
    glUniform3f(d->u_cam_right,   right.x,   right.y,   right.z);
    glUniform3f(d->u_cam_up,      up.x,      up.y,      up.z);
    glUniform1f(d->u_fov,         viewport->fov);
    glUniform1f(d->u_ambient,     scene->ambient);
    glUniform1i(d->u_sphere_count,      scene->sphere_count);
    glUniform1i(d->u_plane_count,       scene->plane_count);
    glUniform1i(d->u_disc_count,        scene->disc_count);
    glUniform1i(d->u_cylinder_count,    scene->cylinder_count);
    /* The triangle SSBO still holds explicit + mesh triangles, but only
     * explicit ones are scanned linearly here; mesh triangles are reached
     * through the per-mesh BVH traversal below. */
    glUniform1i(d->u_triangle_count,    scene->triangle_count);
    glUniform1i(d->u_mesh_count,        scene->mesh_count);
    glUniform1i(d->u_box_count,         scene->box_count);
    glUniform1i(d->u_sprite_count,      scene->sprite_count);
    glUniform1i(d->u_heightfield_count, scene->heightfield_count);
    glUniform1i(d->u_light_count,       scene->light_count);

    glDispatchCompute((GLuint)((w + 15) / 16), (GLuint)((h + 15) / 16), 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                    GL_TEXTURE_UPDATE_BARRIER_BIT);

    glBindTexture(GL_TEXTURE_2D, d->output_tex);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
}

static const char *opengl_name(const rt_renderer *r) {
    (void)r;
    return "OpenGL";
}

rt_renderer *rt_opengl_renderer_create(void) {
    if (!check_gl_context()) return NULL;

    rt_renderer *r = calloc(1, sizeof(*r));
    if (!r) return NULL;

    opengl_backend_data *d = calloc(1, sizeof(*d));
    if (!d) { free(r); return NULL; }

    d->program = create_compute_program(RAYTRACE_SHADER_SOURCE);
    if (!d->program) { free(d); free(r); return NULL; }

    cache_uniform_locs(d);
    glGenBuffers(17, d->ssbo);

    r->destroy_fn   = opengl_destroy;
    r->render_fn    = opengl_render;
    r->name_fn      = opengl_name;
    r->backend_data = d;
    return r;
}

#endif /* RT_HAVE_OPENGL_BACKEND */
