// assets/zone_model_kit.h — tools for building the Zone's 3D item models from code (CPU only, thread-safe):
// colours (a plain 0xRRGGBB, or a function of the voxel; -1 cuts a hole, + GLOW makes it glow), the Canvas
// (box / cylinder / cone / ellipsoid / capsule rasterisers on a voxel grid, x right, y up, z toward the viewer)
// and finishCanvas (crop, scale into the item-model frame: the biggest of x, y spans `size`, centred on the origin).
// The gun builder (render/zone_gun_models.cpp) has its own box list and shares Col and toVox.
#pragma once
#include "assets/item_models.h"
#include <cmath>
#include <functional>
#include <type_traits>

namespace dq::zk {

constexpr int GLOW = 0x1000000;   // added to a colour: the voxel glows (unlit)
inline int jr(double v) { return (int)std::floor(v + 0.5); }   // JS Math.round
inline int shadeC(int c, double k) {
  if (c < 0) return c;
  auto f = [&](int v) { return std::min(255, std::max(0, jr(v * k))); };
  return (c & GLOW) | f((c >> 16) & 255) << 16 | f((c >> 8) & 255) << 8 | f(c & 255);
}
inline int mixC(int a, int b, double t) {   // blend a -> b
  auto f = [&](int s) { return jr(((a >> s) & 255) * (1 - t) + ((b >> s) & 255) * t); };
  return f(16) << 16 | f(8) << 8 | f(0);
}
inline double hash3(int x, int y, int z, int s) {
  unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + (unsigned)z * 2147483647u + (unsigned)s * 1274126177u;
  h = (h ^ (h >> 13)) * 1274126177u; h ^= h >> 16;
  return (h & 0xFFFFFF) / 16777216.0;
}
inline u32 toVox(int c) {
  if (c < 0) return 0;
  return voxRGBA((c >> 16) & 255, (c >> 8) & 255, c & 255, (c & GLOW) ? VOXEL_GLOW : VOXEL_SOLID);
}

struct Col {
  int c = 0;
  std::function<int(int, int, int)> f;
  Col(int v = 0) : c(v) {}
  template <class F, class = std::enable_if_t<!std::is_arithmetic_v<std::decay_t<F>> && std::is_invocable_r_v<int, F, int, int, int>>>
  Col(F fn) : f(std::move(fn)) {}
  int at(int x, int y, int z) const { return f ? f(x, y, z) : c; }
};
#define ZCF [=](int x, int y, int z) -> int

// crop `g` to what is solid and put it in `out` as a Flat item model: biggest of x/y = `size` (model units), centred; `stand`:
// the foot on y = -0.5 (bottles, cans). The mesh is built here (no GL).
inline bool finishCanvas(VoxelModel& out, const VoxelGrid& g, float size, bool stand = false) {
  ivec3 lo, hi;
  if (!g.bounds(lo, hi)) return false;
  VoxelGrid c(hi - lo);
  for (int z = lo.z; z < hi.z; z++) for (int y = lo.y; y < hi.y; y++) for (int x = lo.x; x < hi.x; x++) c.set(x - lo.x, y - lo.y, z - lo.z, g.at(x, y, z));
  const ivec3 e = hi - lo;
  const float k = size / (float)std::max(1, std::max(e.x, e.y));
  out.frame.voxel = k;
  out.frame.origin = vec3(-e.x * k * 0.5f, stand ? -0.5f : -e.y * k * 0.5f, -e.z * k * 0.5f);
  out.size = c.size; out.kind = VoxelModel::Flat; out.stand = stand; out.depth = std::max(0.02f, e.z * k * 0.5f);
  out.built = true;
  buildVoxelMesh(c, out.data);
  return true;
}

// a model built in its own frame (guns: origin at the grip) -> the item-model frame: biggest of x/y = size, centred
inline void normalizeModel(VoxelModel& m, float size) {
  const vec3 e = vec3(m.size) * m.frame.voxel;
  const float k = size / std::max(1e-4f, std::max(e.x, e.y));
  m.frame.voxel *= k; m.frame.origin = -e * k * 0.5f;
  m.kind = VoxelModel::Flat; m.depth = std::max(0.02f, e.z * k * 0.5f); m.built = true;
}

struct Canvas {
  VoxelGrid g;
  Canvas(int sx, int sy, int sz) : g(ivec3(sx, sy, sz)) {}
  void put(int x, int y, int z, const Col& c) { g.set(x, y, z, toVox(c.at(x, y, z))); }
  template <class In> void fill(vec3 lo, vec3 hi, In inside, const Col& c) {
    const int x0 = std::max(0, (int)std::floor(lo.x)), x1 = std::min(g.size.x - 1, (int)std::ceil(hi.x));
    const int y0 = std::max(0, (int)std::floor(lo.y)), y1 = std::min(g.size.y - 1, (int)std::ceil(hi.y));
    const int z0 = std::max(0, (int)std::floor(lo.z)), z1 = std::min(g.size.z - 1, (int)std::ceil(hi.z));
    for (int z = z0; z <= z1; z++) for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++)
      if (inside(vec3(x + 0.5f, y + 0.5f, z + 0.5f))) put(x, y, z, c);
  }
  // inclusive voxel box
  void box(int x0, int y0, int z0, int x1, int y1, int z1, const Col& c) {
    for (int z = z0; z <= z1; z++) for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) put(x, y, z, c);
  }
  // elliptic cylinders: centre (a, b) on the two cross axes, radii (ra, rb), the span on the axis
  void cylY(float cx, float cz, float rx, float rz, int y0, int y1, const Col& c) {
    fill(vec3(cx - rx, y0, cz - rz), vec3(cx + rx, y1 + 1, cz + rz), [&](vec3 p) { float a = (p.x - cx) / rx, b = (p.z - cz) / rz; return a * a + b * b <= 1.f && p.y >= y0 && p.y <= y1 + 1; }, c);
  }
  void cylX(float cy, float cz, float ry, float rz, int x0, int x1, const Col& c) {
    fill(vec3(x0, cy - ry, cz - rz), vec3(x1 + 1, cy + ry, cz + rz), [&](vec3 p) { float a = (p.y - cy) / ry, b = (p.z - cz) / rz; return a * a + b * b <= 1.f && p.x >= x0 && p.x <= x1 + 1; }, c);
  }
  void cylZ(float cx, float cy, float rx, float ry, int z0, int z1, const Col& c) {
    fill(vec3(cx - rx, cy - ry, z0), vec3(cx + rx, cy + ry, z1 + 1), [&](vec3 p) { float a = (p.x - cx) / rx, b = (p.y - cy) / ry; return a * a + b * b <= 1.f && p.z >= z0 && p.z <= z1 + 1; }, c);
  }
  void ell(float cx, float cy, float cz, float rx, float ry, float rz, const Col& c) {
    fill(vec3(cx - rx, cy - ry, cz - rz), vec3(cx + rx, cy + ry, cz + rz), [&](vec3 p) { float a = (p.x - cx) / rx, b = (p.y - cy) / ry, d = (p.z - cz) / rz; return a * a + b * b + d * d <= 1.f; }, c);
  }
  // a frustum along y: radius r0 at y0 .. r1 at y1 (round)
  void coneY(float cx, float cz, float r0, float r1, int y0, int y1, const Col& c) {
    const float R = std::max(r0, r1);
    fill(vec3(cx - R, y0, cz - R), vec3(cx + R, y1 + 1, cz + R), [&](vec3 p) {
      float t = (p.y - y0) / std::max(1.f, (float)(y1 + 1 - y0)); float r = r0 + (r1 - r0) * t;
      return p.y >= y0 && p.y <= y1 + 1 && (p.x - cx) * (p.x - cx) + (p.z - cz) * (p.z - cz) <= r * r; }, c);
  }
  void coneX(float cy, float cz, float r0, float r1, int x0, int x1, const Col& c) {
    const float R = std::max(r0, r1);
    fill(vec3(x0, cy - R, cz - R), vec3(x1 + 1, cy + R, cz + R), [&](vec3 p) {
      float t = (p.x - x0) / std::max(1.f, (float)(x1 + 1 - x0)); float r = r0 + (r1 - r0) * t;
      return p.x >= x0 && p.x <= x1 + 1 && (p.y - cy) * (p.y - cy) + (p.z - cz) * (p.z - cz) <= r * r; }, c);
  }
  // a flat ring in the x-y plane (outer radius R, inner r), z0..z1
  void ringZ(float cx, float cy, float R, float r, int z0, int z1, const Col& c) {
    fill(vec3(cx - R, cy - R, z0), vec3(cx + R, cy + R, z1 + 1), [&](vec3 p) { float d = (p.x - cx) * (p.x - cx) + (p.y - cy) * (p.y - cy); return d <= R * R && d >= r * r && p.z >= z0 && p.z <= z1 + 1; }, c);
  }
  // a tapered rod (capsule) from a to b, radius r0 -> r1
  void rod(vec3 a, vec3 b, float r0, float r1, const Col& c) {
    const float R = std::max(r0, r1);
    const vec3 ab = b - a; const float L2 = std::max(1e-4f, glm::dot(ab, ab));
    fill(glm::min(a, b) - vec3(R), glm::max(a, b) + vec3(R), [&](vec3 p) {
      float t = clampv(glm::dot(p - a, ab) / L2, 0.f, 1.f); float r = r0 + (r1 - r0) * t;
      vec3 q = a + ab * t - p; return glm::dot(q, q) <= r * r; }, c);
  }
  // a hexagonal prism along z (flat-to-flat radius R), z0..z1
  void hexZ(float cx, float cy, float R, int z0, int z1, const Col& c) {
    fill(vec3(cx - R * 1.16f, cy - R * 1.16f, z0), vec3(cx + R * 1.16f, cy + R * 1.16f, z1 + 1), [&](vec3 p) {
      float x = std::abs(p.x - cx), y = std::abs(p.y - cy);
      return y <= R && (x * 0.866f + y * 0.5f) <= R && p.z >= z0 && p.z <= z1 + 1; }, c);
  }
};

} // namespace dq::zk
