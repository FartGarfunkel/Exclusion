// gen/zone_dress.cpp — trees, fallen trunks, power lines and street lamps (see zone_dress.h). The trees are what STALKER's
// forests are made of: slim trunks (branch-shaped blocks that reach into their limbs and leaves), crowns of see-through
// leaf blocks in ragged clumps at the ends of limbs, white birches that lean, tall Scots pines bare to a high crown, dark
// spruces, grey dead snags, and in the Red Forest rust-needled pines among burnt stumps. Everything is drawn only where it
// reaches into the chunk, from hashes of the seed and the tree's spot, so every chunk draws its part of the same tree.
#include "gen/zone_dress.h"
#include "gen/zone_world.h"
#include "core/registry.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace dq::zone {
namespace {

const int D8X[8] = {1, 1, 0, -1, -1, -1, 0, 1}, D8Z[8] = {0, 1, 1, 1, 0, -1, -1, -1};

struct Draw {
  Chunk& ch; int ox, oz;
  bool in(int x, int y, int z) const { return (unsigned)(x - ox) < 16 && (unsigned)(z - oz) < 16 && (unsigned)y < 256; }
  BlockId get(int x, int y, int z) const { return in(x, y, z) ? ch.get(x - ox, y, z - oz) : AIR; }
  // soft: only into air or what a plant would give way to
  void put(int x, int y, int z, BlockId id, u8 meta = 0, bool soft = false) {
    if (!in(x, y, z)) return;
    if (soft) { const BlockId b = ch.get(x - ox, y, z - oz); if (b != AIR && !reg().tReplaceable[b]) return; }
    ch.set(x - ox, y, z - oz, id); ch.setMeta(x - ox, y, z - oz, meta);
  }
};

struct Tree {
  Draw& d; const Pal& B; u32 seed; int tx, ty, tz;
  int jogY = 1 << 20, jx = 0, jz = 0;           // the trunk's kink: one block sideways from jogY up
  double r(int a) const { return hashFloat((i32)(seed ^ 0x6d2b79f5u), tx * 3 + a * 101, ty + a, tz * 5 - a * 37); }
  void at(int y, int& x, int& z) const { x = tx + (y >= jogY ? jx : 0); z = tz + (y >= jogY ? jz : 0); }

  void trunk(BlockId id, int H, int thinFrom, bool lean) {
    if (lean) { jogY = ty + 2 + int(r(90) * (H * 0.5)); const int k = int(r(91) * 4) * 2; jx = D8X[k]; jz = D8Z[k]; }
    for (int y = ty; y < ty + H; y++) {
      int x, z; at(y, x, z);
      const u8 m = y - ty >= thinFrom ? 1 : 0;
      if (y == jogY) d.put(tx, y, tz, id, m);
      d.put(x, y, z, id, m);
    }
  }
  // a limb out of the trunk at height y along one of 8 ways, n steps (a diagonal goes x, z, x...), climbing a block every
  // `climb` steps; tip = where it ends
  void limb(BlockId id, int y, int dir, int n, int climb, int& X, int& Y, int& Z) {
    int x, z; at(y, x, z);
    const int dx = D8X[dir & 7], dz = D8Z[dir & 7];
    for (int i = 1; i <= n; i++) {
      if (dx && dz) { if (i & 1) x += dx; else z += dz; } else { x += dx; z += dz; }
      d.put(x, y, z, id, 1, true);
      if (climb && i % climb == 0) { y++; d.put(x, y, z, id, 1, true); }
    }
    X = x; Y = y; Z = z;
  }
  // a ragged ellipsoid of leaves, thinner toward its edge
  void clump(BlockId leaf, int x, int y, int z, float rh, float rv, float dens, int salt) {
    const int R = (int)std::ceil(rh), V = (int)std::ceil(rv);
    for (int dy = -V; dy <= V; dy++) for (int dz = -R; dz <= R; dz++) for (int dx = -R; dx <= R; dx++) {
      const float e = (dx * dx + dz * dz) / (rh * rh) + (dy * dy) / (rv * rv);
      if (e > 1.f) continue;
      if (hashFloat((i32)seed, x + dx, y + dy + salt * 977, z + dz) > dens * (1.2f - 0.55f * e)) continue;
      d.put(x + dx, y + dy, z + dz, leaf, 0, true);
    }
  }

  void birch() {
    const int H = 9 + int(r(1) * 7);
    trunk(B.zBirchTrunk, H, int(H * 0.6f), r(2) < 0.4);
    const int top = ty + H - 1, n = 3 + int(r(3) * 3);
    for (int i = 0; i < n; i++) {
      const int ly = ty + int(H * (0.45 + 0.45 * r(10 + i)));
      int X, Y, Z; limb(B.zBirchTrunk, ly, int(r(20 + i) * 8), 1 + int(r(30 + i) * 2.2), 2, X, Y, Z);
      clump(B.zBirchLeaves, X, Y, Z, 1.4f + float(r(40 + i)) * 0.6f, 1.5f, 0.68f, i);
    }
    int x, z; at(top, x, z);
    clump(B.zBirchLeaves, x, top + 1, z, 1.7f, 1.8f, 0.7f, 9);
  }
  void pine(BlockId needles, float dens) {
    const int H = 14 + int(r(1) * 8);
    trunk(B.zPineTrunk, H, int(H * 0.55f), r(2) < 0.12);
    const int top = ty + H - 1, cb = ty + int(H * (0.6 + 0.08 * r(3)));
    for (int s = 0; s < 2; s++) {   // dead stubs on the bare trunk
      int X, Y, Z; limb(B.zPineTrunk, ty + 3 + int(r(4 + s) * (cb - ty - 4)), int(r(6 + s) * 8), 1, 0, X, Y, Z);
    }
    int k = 0;
    for (int y = cb; y < top - 1; y += 2 + (r(50 + y) < 0.3 ? 1 : 0)) {
      const int n = 2 + int(r(60 + y) * 2.5);
      for (int i = 0; i < n; i++, k++) {
        const int len = 1 + int(r(70 + k) * (y < cb + 3 ? 3 : 2));
        int X, Y, Z; limb(B.zPineTrunk, y, int(r(80 + k) * 8), len, len, X, Y, Z);
        clump(needles, X, Y, Z, 1.3f + float(r(100 + k)) * 0.7f, 1.05f, dens, k);
      }
    }
    int x, z; at(top, x, z);
    clump(needles, x, top + 1, z, 1.5f, 1.4f, dens + 0.05f, 99);
  }
  void spruce() {
    const int H = 11 + int(r(1) * 8);
    trunk(B.zPineTrunk, H, int(H * 0.5f), false);
    const int top = ty + H - 1, rot = int(r(2) * 8);
    int k = 0;
    for (int y = ty + 2 + int(r(3) * 2); y <= top; y += 2) {
      const float rr = std::min(3.4f, (top + 1 - y) * 0.36f + 0.6f);
      const int len = std::max(0, int(rr - 0.4f));
      for (int a = 0; a < 8; a += 2 + (r(200 + y) < 0.4 ? 1 : 0), k++) {
        int X = tx, Y = y, Z = tz;
        if (len) limb(B.zPineTrunk, y, a + rot, len, 0, X, Y, Z);
        // needles along the limb, drooping at its tip
        for (int i = 0; i <= len; i++) {
          const int px = tx + (X - tx) * i / std::max(1, len), pz = tz + (Z - tz) * i / std::max(1, len);
          for (int q = 0; q < 5; q++) {
            const int qx = px + (q == 1) - (q == 2), qz = pz + (q == 3) - (q == 4);
            if (hashFloat((i32)seed, qx, y + k * 31, qz) < 0.72) d.put(qx, y, qz, B.zPineNeedles, 0, true);
          }
          if (i == len && len) d.put(X, y - 1, Z, B.zPineNeedles, 0, true);
          if (r(300 + k * 3 + i) < 0.45) d.put(px, y + 1, pz, B.zPineNeedles, 0, true);
        }
      }
    }
    d.put(tx, top + 1, tz, B.zPineNeedles, 0, true); d.put(tx, top + 2, tz, B.zPineNeedles, 0, true);
  }
  // a Lombardy poplar: tall and straight, a narrow column of leaf clumps up its top half
  void poplar() {
    const int H = 14 + int(r(1) * 7);
    trunk(B.zBirchTrunk, H, int(H * 0.5f), false);
    const int top = ty + H - 1;
    for (int y = ty + int(H * 0.42f), i = 0; y <= top; y += 2, i++) {
      const float f = float(y - ty) / H;
      int x, z; at(y, x, z);
      clump(B.bushLeaves, x + (r(400 + i) < 0.3 ? 1 : 0), y, z, 1.25f + (1.f - std::abs(f - 0.7f) * 2.2f) * 0.7f, 1.4f, 0.8f, 40 + i);
    }
    int x, z; at(top, x, z); clump(B.bushLeaves, x, top + 2, z, 1.f, 1.3f, 0.75f, 59);
  }
  // a young birch of the regrowth: thin, 5..8 tall, a small ragged crown
  void young() {
    const int H = 5 + int(r(1) * 4);
    trunk(B.zBirchTrunk, H, 1, r(2) < 0.35);
    int x, z; at(ty + H - 1, x, z);
    clump(B.zBirchLeaves, x, ty + H - 1, z, 1.3f + float(r(3)) * 0.5f, 1.6f, 0.7f, 3);
  }
  void bush(int kind) {
    const float rh = 1.1f + float(r(1)) * 1.1f, rv = 0.9f + float(r(2)) * 0.8f;
    if (r(3) < 0.6) d.put(tx, ty, tz, kind == 2 ? B.zDeadTrunk : B.zBirchTrunk, 1, true);
    clump(kind == 0 ? B.zBirchLeaves : kind == 1 ? B.bushLeaves : B.zTwigs, tx, ty + (rv > 1.3f ? 1 : 0), tz, rh, rv, kind == 2 ? 0.55f : 0.82f, 7);
  }
  void dead(BlockId id, bool twigs) {
    const bool stump = r(1) < 0.2;
    const int H = stump ? 2 + int(r(2) * 3) : 5 + int(r(2) * 9);
    trunk(id, H, int(H * 0.45f), !stump && r(3) < 0.5);
    if (stump) return;
    const int n = 1 + int(r(4) * 3.5);
    for (int i = 0; i < n; i++) {
      int X, Y, Z; limb(id, ty + int(H * (0.4 + 0.55 * r(10 + i))), int(r(20 + i) * 8), 1 + int(r(30 + i) * 3), 1 + int(r(40 + i) * 2), X, Y, Z);
      if (twigs && r(50 + i) < 0.55) clump(B.zTwigs, X, Y, Z, 1.3f, 1.1f, 0.45f, i);
    }
  }
};

// ---- power lines and lamps: spots along each road line (fixed for every seed: the roads are), indexed by 64-block cell
struct Spot { u16 li, k; u8 lamp; };
constexpr float POLE_GAP_ROAD = 30, POLE_GAP_DIRT = 26, LAMP_GAP = 21;
float gapOf(int li, bool lamp) { return lamp ? LAMP_GAP : roads()[li].kind == 1 ? POLE_GAP_ROAD : POLE_GAP_DIRT; }
const std::unordered_map<i64, std::vector<Spot>>& spotGrid() {
  static const std::unordered_map<i64, std::vector<Spot>> g = [] {
    std::unordered_map<i64, std::vector<Spot>> m;
    const auto& R = roads();
    for (int li = 0; li < (int)R.size(); li++) {
      if (R[li].kind == 0 || R[li].kind == 3 || (R[li].kind == 2 && ((li & 1) || roadLength(li) < 1500))) continue;   // rivers, the railway, short or half the dirt roads
      for (int lamp = 0; lamp <= (R[li].kind == 1 ? 1 : 0); lamp++) {
        const float gap = gapOf(li, lamp);
        for (int k = 0; k * gap <= roadLength(li); k++) {
          float x, z, dx, dz; roadPointAt(li, k * gap, x, z, dx, dz);
          m[cellKey((int)std::floor(x) >> 6, (int)std::floor(z) >> 6)].push_back({(u16)li, (u16)k, (u8)lamp});
        }
      }
    }
    return m;
  }();
  return g;
}

struct Pole { bool ok = false; int x = 0, z = 0, top = 0; u8 meta = 0; float dx = 0, dz = 1; };
bool clearSpot(u32 seed, const Plan& plan, int x, int z, int& H) {
  if (!insideZone(x, z) || std::min({x - MIN_X, MAX_X - x, z - MIN_Z, MAX_Z - z}) < 30) return false;
  const Ground g = groundAt(seed, x, z);
  if (g.water || g.road || g.roadD < 1.f || g.riverD < 2.f) return false;
  if (!plan.near(x, z, 2).empty()) return false;
  WCol c; if (worldColumn(x, z, c) && c.nruns) return false;   // one of the user's own things stands there
  H = (int)std::round(g.h);
  return true;
}
Pole poleAt(u32 seed, const Plan& plan, int li, int k) {
  Pole p;
  if (k < 0 || k * gapOf(li, false) > roadLength(li)) return p;
  if (hashFloat((i32)seed, li, k, 61) < 0.07) return p;   // fallen long ago
  float x, z; roadPointAt(li, k * gapOf(li, false), x, z, p.dx, p.dz);
  const float off = roads()[li].width * 0.5f + (roads()[li].kind == 1 ? 3.f : 2.f);
  p.x = (int)std::floor(x + p.dz * off); p.z = (int)std::floor(z - p.dx * off);   // on the road's left
  int H; if (!clearSpot(seed, plan, p.x, p.z, H)) return p;
  p.top = H + 8; p.meta = std::abs(p.dx) >= std::abs(p.dz) ? 1 : 0;   // the crossarm across the road
  p.ok = true;
  return p;
}
// the two insulators, [0] on the road's left
void insulators(const Pole& p, float out[2][3]) {
  const float cx = p.x + 0.5f, cz = p.z + 0.5f, y = p.top + 0.95f, a = 0.71875f;
  float ax = p.meta ? 0 : a, az = p.meta ? a : 0;
  if (ax * p.dz - az * p.dx < 0) { ax = -ax; az = -az; }   // (+ax,+az) toward the left (dz, -dx)
  out[0][0] = cx + ax; out[0][1] = y; out[0][2] = cz + az;
  out[1][0] = cx - ax; out[1][1] = y; out[1][2] = cz - az;
}
// a sagging wire from a to b: one block per step along its main axis (the mesher joins them, render/mesh_shapes.cpp)
void wire(Draw& d, const Pal& B, const float* a, const float* b, int which) {
  const float ddx = b[0] - a[0], ddz = b[2] - a[2];
  const bool alongZ = std::abs(ddz) > std::abs(ddx);
  const float a0 = alongZ ? a[2] : a[0], a1 = alongZ ? b[2] : b[0];
  if (std::abs(a1 - a0) < 0.5f) return;
  const float sag = 0.035f * std::sqrt(ddx * ddx + ddz * ddz);
  const int i0 = (int)std::ceil(std::min(a0, a1) - 0.5f), i1 = (int)std::floor(std::max(a0, a1) - 0.5f);
  const int lo = alongZ ? d.oz : d.ox;
  for (int i = std::max(i0, lo - 1); i <= std::min(i1, lo + 16); i++) {
    const float t = (i + 0.5f - a0) / (a1 - a0);
    const float m = alongZ ? a[0] + ddx * t : a[2] + ddz * t, y = a[1] + (b[1] - a[1]) * t - sag * 4 * t * (1 - t);
    const int M = (int)std::floor(m), Y = (int)std::floor(y);
    const int X = alongZ ? M : i, Z = alongZ ? i : M;
    if (!d.in(X, Y, Z)) continue;
    const BlockId cur = d.get(X, Y, Z);
    if (cur != AIR && !reg().tLeaves[cur] && !reg().tReplaceable[cur]) continue;
    const u8 meta = (u8)((which << 7) | (std::min(7, int((m - M) * 8)) << 4) | std::min(15, int((y - Y) * 16)));
    d.put(X, Y, Z, alongZ ? B.zCableZ : B.zCableX, meta);
  }
}
bool lampTown(int x, int z) {
  const Region* r = regionAt(x, z);
  if (!r || regionWeight(*r, x, z) < 0.2f) return false;
  return r->kind == K_CITY || r->kind == K_CLOSED_TOWN || r->kind == K_GHOST_TOWN || r->kind == K_HUB || r->kind == K_MILITARY || r->kind == K_INSTITUTE || r->kind == K_HOSPITAL;
}

} // namespace

void zoneTree(Chunk& ch, int ox, int oz, const Pal& B, u32 seed, int tx, int ty, int tz, TreeKind kind) {
  Draw d{ch, ox, oz};
  Tree t{d, B, seed, tx, ty, tz};
  switch (kind) {
    case T_BIRCH: t.birch(); break;
    case T_PINE: t.pine(B.zPineNeedles, 0.72f); break;
    case T_SPRUCE: t.spruce(); break;
    case T_RED_PINE: t.pine(B.zRedNeedles, 0.5f); break;
    case T_BURNT: t.dead(B.zBurntTrunk, false); break;
    case T_POPLAR: t.poplar(); break;
    case T_YOUNG: t.young(); break;
    default: t.dead(B.zDeadTrunk, true); break;
  }
}

void zoneBush(Chunk& ch, int ox, int oz, const Pal& B, u32 seed, int tx, int ty, int tz, int kind) {
  Draw d{ch, ox, oz};
  Tree t{d, B, seed ^ 0x51ed27u, tx, ty, tz};
  t.bush(kind);
}

void zoneFallenTrunk(Chunk& ch, int ox, int oz, const Pal& B, int x, int y, int z, int n, bool alongZ) {
  Draw d{ch, ox, oz};
  for (int i = 0; i < n; i++) d.put(alongZ ? x : x + i, y, alongZ ? z + i : z, B.zFallen, alongZ ? 1 : 0, true);
}

void zoneRoadside(Chunk& ch, int ox, int oz, const Pal& B, u32 seed, const Plan& plan) {
  Draw d{ch, ox, oz};
  const auto& G = spotGrid();
  for (int cz = (oz - 48) >> 6; cz <= (oz + 63) >> 6; cz++) for (int cx = (ox - 48) >> 6; cx <= (ox + 63) >> 6; cx++) {
    auto it = G.find(cellKey(cx, cz)); if (it == G.end()) continue;
    for (const Spot& s : it->second) {
      if (s.lamp) {
        float x, z, dx, dz; roadPointAt(s.li, s.k * LAMP_GAP + 7, x, z, dx, dz);
        const float off = roads()[s.li].width * 0.5f + 1.5f;
        const int lx = (int)std::floor(x - dz * off), lz = (int)std::floor(z + dx * off);   // on the road's right
        if (lx < ox || lx > ox + 15 || lz < oz || lz > oz + 15 || !lampTown(lx, lz)) continue;
        const double h = hashFloat((i32)seed, s.li, s.k, 63);
        int H; if (h < 0.1 || !clearSpot(seed, plan, lx, lz, H)) continue;
        const bool head = h > 0.3;
        for (int y = H + 1; y <= H + (head ? 6 : 3 + int(h * 10)); y++) d.put(lx, y, lz, B.zLampPost);
        // the arm reaches over the road (to the left of the road's way from here): model arm +z, metas turn it -x, -z, +x
        const float rx = dz, rz = -dx;
        const u8 m = std::abs(rz) >= std::abs(rx) ? (rz > 0 ? 0 : 2) : (rx > 0 ? 3 : 1);
        if (head) d.put(lx, H + 7, lz, B.zLampHead, m);
        continue;
      }
      const Pole A = poleAt(seed, plan, s.li, s.k);
      if (!A.ok) continue;
      if (d.in(A.x, A.top, A.z)) {
        for (int y = A.top - 7; y < A.top; y++) d.put(A.x, y, A.z, B.zPole);
        d.put(A.x, A.top, A.z, B.zPoleTop, A.meta);
      }
      const Pole P = poleAt(seed, plan, s.li, s.k + 1);
      if (!P.ok || hashFloat((i32)seed, s.li, s.k, 62) < 0.12) continue;   // a span whose wires are down
      if (std::max(A.x, P.x) + 2 < ox || std::min(A.x, P.x) - 2 > ox + 15 || std::max(A.z, P.z) + 2 < oz || std::min(A.z, P.z) - 2 > oz + 15) continue;
      float ia[2][3], ib[2][3]; insulators(A, ia); insulators(P, ib);
      for (int w = 0; w < 2; w++) wire(d, B, ia[w], ib[w], w);
    }
  }
}

} // namespace dq::zone
