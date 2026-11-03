// gen/zone_plan.cpp — the Zone's terrain height and its site plan (see zone_plan.h).
#include "gen/zone_plan.h"
#include "gen/zone_layers.h"
#include "gen/zone_world.h"
#include "common/noise.h"
#include <cmath>
#include <algorithm>
#include <mutex>
#include <memory>
#include <map>
#include <atomic>

namespace dq::zone {

static const char* FNAME[] = {"Loners", "Bandits", "Military", "Duty", "Freedom", "Mercenaries", "Monolith", "Ecologists", "Mutants"};
static const char* FKEY[] = {"drifter", "bandit", "army", "bastion", "opensky", "merc", "choir", "scientist", "mutant"};
const char* factionName(int f) { return f >= 0 && f < F_COUNT ? FNAME[f] : "Nobody"; }
const char* factionKey(int f) { return f >= 0 && f < F_COUNT ? FKEY[f] : "none"; }
int factionRelation(int a, int b) {
  if (a == b) return 1;
  if (a == F_MUTANT || b == F_MUTANT || a == F_CHOIR || b == F_CHOIR) return -1;
  static const int R[8][8] = {
    //  dri  ban  arm  bas  opn  mer  cho  ins
    {1, -1, 0, 0, 0, 0, -1, 1},    // drifters
    {-1, 1, -1, -1, -1, 0, -1, -1}, // bandits
    {0, -1, 1, 0, -1, -1, -1, 0},  // army
    {0, -1, 0, 1, -1, -1, -1, 1},  // bastion
    {0, -1, -1, -1, 1, 0, -1, 1},  // open sky
    {0, 0, -1, -1, 0, 1, -1, 0},   // contractors
    {-1, -1, -1, -1, -1, -1, 1, -1},
    {1, -1, 0, 1, 1, 0, -1, 1},    // institute
  };
  return (a < 8 && b < 8) ? R[a][b] : -1;
}

// ---------------------------------------------------------------- lines index
namespace {
struct SegRef { u16 line, seg; };
struct SegIndex {
  std::unordered_map<i64, std::vector<SegRef>> cells;
  const std::vector<Line>* lines = nullptr;
  void build(const std::vector<Line>& L, int pad) {
    lines = &L;
    for (size_t li = 0; li < L.size(); li++) for (size_t s = 0; s + 1 < L[li].pts.size(); s++) {
      const Pt a = L[li].pts[s], b = L[li].pts[s + 1];
      const int x0 = (int)std::floor((std::min(a.x, b.x) - pad) / 128), x1 = (int)std::floor((std::max(a.x, b.x) + pad) / 128);
      const int z0 = (int)std::floor((std::min(a.z, b.z) - pad) / 128), z1 = (int)std::floor((std::max(a.z, b.z) + pad) / 128);
      for (int cz = z0; cz <= z1; cz++) for (int cx = x0; cx <= x1; cx++) cells[cellKey(cx, cz)].push_back({(u16)li, (u16)s});
    }
  }
  // nearest line within the index pad: distance minus the half-width, its index and direction
  float nearest(float x, float z, int& li, float& dx, float& dz, float& raw) const {
    li = -1; float best = 1e30f; raw = 1e30f;
    auto it = cells.find(cellKey((int)std::floor(x / 128), (int)std::floor(z / 128)));
    if (it == cells.end()) return best;
    for (const SegRef& r : it->second) {
      const Line& l = (*lines)[r.line];
      const Pt a = l.pts[r.seg], b = l.pts[r.seg + 1];
      const float ux = b.x - a.x, uz = b.z - a.z, L2 = ux * ux + uz * uz;
      const float t = std::clamp(L2 > 0 ? ((x - a.x) * ux + (z - a.z) * uz) / L2 : 0.f, 0.f, 1.f);
      const float px = a.x + ux * t - x, pz = a.z + uz * t - z;
      const float d = std::sqrt(px * px + pz * pz), e = d - l.width * 0.5f;
      if (e < best) { best = e; raw = d; li = r.line; const float n = std::sqrt(L2) + 1e-6f; dx = ux / n; dz = uz / n; }
    }
    return best;
  }
};
const SegIndex& roadIdx() { static SegIndex s; static std::once_flag f; std::call_once(f, [] { s.build(roads(), 48); }); return s; }
const SegIndex& trackIdx() { static SegIndex s; static std::once_flag f; std::call_once(f, [] { s.build(tracks(), 8); }); return s; }

inline float sstep(float t) { t = std::clamp(t, 0.f, 1.f); return t * t * (3 - 2 * t); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

} // namespace

namespace {
// the plans that are built (groundAt levels the ground round their procedural buildings once there is one; the planner
// itself sees the bare land)
std::atomic<u32> g_readySeed[8]; std::atomic<const Plan*> g_readyPlan[8]; std::atomic<int> g_readyN{0};
const Plan* builtPlan(u32 seed) {
  thread_local u32 lastSeed = 0; thread_local const Plan* last = nullptr;
  if (last && lastSeed == seed) return last;
  const int n = std::min(8, g_readyN.load(std::memory_order_acquire));
  for (int i = 0; i < n; i++) if (g_readySeed[i].load(std::memory_order_acquire) == seed) { last = g_readyPlan[i].load(std::memory_order_acquire); lastSeed = seed; return last; }
  return nullptr;
}

} // namespace
// what kind of road a column's surface is: 1 paved (stone, asphalt-dark soul sand, slabs), 2 gravel or cobbles, 0 none
u8 worldSurfRoad(const WCol& c) {
  auto kindOf = [](const std::string& n) -> u8 {
    const size_t k = n.find(':'); const std::string b = n.substr(0, k);
    if (b == "soul_sand" || b == "stone_slab" || b == "double_stone_slab" || b == "stonebrick") return 1;
    if (b == "stone") return n == "stone:0" || n == "stone:5" || n == "stone:6" ? 1 : 0;
    if (b == "gravel" || b == "cobblestone" || b == "mossy_cobblestone" || b == "grass_path") return 2;
    return 0;
  };
  if (c.nruns == 1 && c.runs[0].y0 == c.g + 1 && c.runs[0].len == 1) return 0;   // (a slab on the ground: the generator lays it)
  return c.surf ? kindOf(*c.surf) : 0;
}

float trackDist(int x, int z) { int li; float dx, dz, raw; const float e = trackIdx().nearest((float)x, (float)z, li, dx, dz, raw); return li >= 0 ? e : 1e9f; }

namespace {
// the ground outside the world's data: the edge's own height easing to a plain, cheap
Ground edgeGround(int x, int z) {
  Ground g{};
  const float cx = (float)std::clamp(x, MIN_X, MAX_X), cz = (float)std::clamp(z, MIN_Z, MAX_Z), d = std::hypot(x - cx, z - cz);
  const float eh = coarseHeight(cx, cz);
  g.h = lerpf(std::max(eh, WATER + 1.f), (float)BASE, sstep(d / 120.f)) + 1.5f * std::sin(x * 0.031f) * std::sin(z * 0.027f) * sstep(d / 60.f);
  g.waterY = WATER; g.riverD = 99; g.roadD = 99;
  return g;
}

// the bare land: the user's world (gen/zone_world.h), its roads (lines for the distance), the distance to water
Ground groundRaw(u32 seed, int x, int z) {
  (void)seed;
  Ground g{};
  WCol c;
  auto t = worldTileAt(x, z);
  if (t && worldColumn(*t, x, z, c)) {
    g.h = (float)c.g;
    g.water = c.w > c.g; g.waterY = g.water ? c.w : WATER;
    g.road = g.water ? 0 : worldSurfRoad(c);
    g.riverD = g.water ? -1.f - (c.w - c.g) : std::max(0.f, coarseAt(CL_WDIST, (float)x, (float)z) * 510.f - 3.f);
  } else g = edgeGround(x, z);
  if (!g.road && !g.water && trackDist(x, z) < 0) g.road = 2;   // a track laid between places (gen/zone_map.cpp)
  const Region* r = regionAt(x, z);
  g.region = r; g.rw = r ? regionWeight(*r, x, z) : 0.f;
  int li; float dx, dz, raw;
  const float od = roadIdx().nearest((float)x, (float)z, li, dx, dz, raw);
  g.roadD = g.road ? std::max(0.f, raw) : li >= 0 ? std::max(0.5f, od) : 99.f;
  return g;
}

} // namespace

// the ground round a building comes to its pad: a level apron, then a slope out to the land (stone-faced where it is steep)
static void padBlend(const Plan& P, int x, int z, Ground& g) {
  auto it = P.grid.find(cellKey(x >> 6, z >> 6)); if (it == P.grid.end()) return;
  float best = 0, target = 0, diff = 0;
  for (int i : it->second) {
    const Placed& p = P.s[i];
    if (!p.pad || p.mc || p.type == S_BARGE) continue;
    const float ex = (float)std::max({p.x0 - x, 0, x - (p.x0 + p.W - 1)}), ez = (float)std::max({p.z0 - z, 0, z - (p.z0 + p.D - 1)});
    const float d = std::sqrt(ex * ex + ez * ez), ty = (float)(p.y - 1), df = std::abs(g.h - ty);
    const float S = std::min(13.f, 1.f + df * 1.1f);
    if (d > S + 1.5f) continue;
    const float wgt = d <= 1.5f ? 1.f : 1.f - sstep((d - 1.5f) / S);
    if (wgt > best) { best = wgt; target = ty; diff = df; }
  }
  if (best <= 0) return;
  g.h = lerpf(g.h, target, best);
  g.water = g.h < g.waterY - 0.5f;
  if (best < 0.999f && diff > 4.f) g.cut = 1;
  if (best > 0.5f) g.road = 0;
}

Ground groundAt(u32 seed, int x, int z) {
  Ground g = groundRaw(seed, x, z);
  if (const Plan* P = builtPlan(seed)) padBlend(*P, x, z, g);
  g.h = std::round(g.h);
  return g;
}

Ground groundCoarse(u32 seed, int x, int z) {
  (void)seed;
  Ground g{};
  if (insideZone(x, z)) {
    const float fx = (float)x, fz = (float)z;
    g.h = std::round(coarseHeight(fx, fz)); g.waterY = WATER;
    const float wf = coarseAt(CL_WATER, fx, fz);
    g.water = wf > 0.5f;
    g.riverD = g.water ? -1.f : std::max(0.f, coarseAt(CL_WDIST, fx, fz) * 510.f - 4.f);
    g.road = coarseAt(CL_ROAD, fx, fz) > 0.6f ? 1 : 0;
  } else g = edgeGround(x, z);
  const Region* r = regionAt(x, z);
  g.region = r; g.rw = r ? regionWeight(*r, x, z) : 0.f;
  int li; float dx, dz, raw;
  const float od = roadIdx().nearest((float)x, (float)z, li, dx, dz, raw);
  if (li >= 0 && od < 0) { g.road = std::max<u8>(g.road, roads()[li].kind); g.roadD = raw; }
  else g.roadD = li >= 0 ? std::max(0.5f, od) : 99.f;
  return g;
}

// distances along each road line (for things spaced along a road: poles, lamps)
static const std::vector<std::vector<float>>& roadCum() {
  static const std::vector<std::vector<float>> c = [] {
    std::vector<std::vector<float>> v;
    for (const Line& l : roads()) {
      std::vector<float> s{0.f};
      for (size_t i = 0; i + 1 < l.pts.size(); i++) s.push_back(s.back() + std::hypot(l.pts[i + 1].x - l.pts[i].x, l.pts[i + 1].z - l.pts[i].z));
      v.push_back(std::move(s));
    }
    return v;
  }();
  return c;
}
float roadLength(int li) { return li >= 0 && li < (int)roadCum().size() ? roadCum()[li].back() : 0.f; }
void roadPointAt(int li, float along, float& x, float& z, float& dx, float& dz) {
  const Line& l = roads()[li]; const auto& s = roadCum()[li];
  const size_t i = std::min<size_t>(std::upper_bound(s.begin(), s.end(), along) - s.begin(), s.size() - 1);
  const size_t a = i ? i - 1 : 0, b = std::min(a + 1, l.pts.size() - 1);
  const float L = std::max(1e-3f, s[b] - s[a]), t = std::clamp((along - s[a]) / L, 0.f, 1.f);
  x = l.pts[a].x + (l.pts[b].x - l.pts[a].x) * t; z = l.pts[a].z + (l.pts[b].z - l.pts[a].z) * t;
  dx = (l.pts[b].x - l.pts[a].x) / L; dz = (l.pts[b].z - l.pts[a].z) / L;
}

// direction of the road at a point (for rail metas); 0 if none
static bool roadDir(float x, float z, float& dx, float& dz) {
  int li; float raw;
  roadIdx().nearest(x, z, li, dx, dz, raw);
  return li >= 0;
}

std::vector<int> Plan::near(int x, int z, int r) const {
  std::vector<int> out;
  for (int cz = (z - r) >> 6; cz <= (z + r) >> 6; cz++) for (int cx = (x - r) >> 6; cx <= (x + r) >> 6; cx++) {
    auto it = grid.find(cellKey(cx, cz)); if (it == grid.end()) continue;
    for (int i : it->second) if (std::find(out.begin(), out.end(), i) == out.end()) {
      const Placed& p = s[i];
      if (x + r >= p.x0 && x - r < p.x0 + p.W && z + r >= p.z0 && z - r < p.z0 + p.D) out.push_back(i);
    }
  }
  return out;
}

void fieldAnomalies(u32 seed, const AnomalyField& f, std::vector<Anomaly>& out) {
  for (int i = 0; i < f.count; i++) {
    const double a = hashFloat((i32)seed, f.x, f.z, i * 3) * 6.2832, d = std::sqrt(hashFloat((i32)seed, f.x, f.z, i * 3 + 1)) * f.r;
    Anomaly A;
    A.x = f.x + (int)std::round(std::cos(a) * d); A.z = f.z + (int)std::round(std::sin(a) * d); A.y = f.y + 1;
    A.r = 1.5f + float(hashFloat((i32)seed, f.x, f.z, i * 3 + 2)) * (f.family == A_GRAVITY ? 3.f : 2.f);
    // a field is mostly its own family, with a stray of another now and then
    A.family = hashFloat((i32)seed, f.z, f.x, i) < 0.85 ? f.family : (u8)(hashFloat((i32)seed, f.x, i, 9) * 4);
    A.tier = f.tier;
    out.push_back(A);
  }
}

// ---------------------------------------------------------------- the planner
// The user's own buildings are the plan's buildings (worldSites(): loot, stashes, camps in them, /site); our builders only
// add what their world hasn't got (the trader's cellar, your hatch, camps, bunkers, checkpoints, the six regions it lacks)
// on clear ground round them.
namespace {
SType clsType(const std::string& c) {
  return c == "hut" ? S_HUT : c == "house" ? S_HOUSE2 : c == "panel" ? S_PANEL : c == "factory" ? S_FACTORY : c == "hangar" ? S_HANGAR : S_OFFICE;
}
void localToWorld(const Placed& p, int lx, int lz, int& wx, int& wz) {
  switch (p.rot & 3) {
    case 0: wx = p.x0 + lx; wz = p.z0 + lz; break;
    case 1: wx = p.x0 + (p.d - 1 - lz); wz = p.z0 + lx; break;
    case 2: wx = p.x0 + (p.w - 1 - lx); wz = p.z0 + (p.d - 1 - lz); break;
    default: wx = p.x0 + lz; wz = p.z0 + (p.w - 1 - lx); break;
  }
}

struct Planner {
  Plan& P; u32 seed; Mulberry32 rnd;
  std::vector<char> used;   // MC sites given a smart terrain already
  Planner(Plan& p, u32 s) : P(p), seed(s), rnd(s ^ 0xC0FFEEu) {}

  int groundY(int x, int z) { return (int)std::round(groundCoarse(seed, x, z).h) + 1; }

  bool clearOf(int x0, int z0, int W, int D, int margin) {
    for (int cz = (z0 - margin) >> 6; cz <= (z0 + D + margin) >> 6; cz++) for (int cx = (x0 - margin) >> 6; cx <= (x0 + W + margin) >> 6; cx++) {
      auto it = P.grid.find(cellKey(cx, cz)); if (it == P.grid.end()) continue;
      for (int i : it->second) {
        const Placed& q = P.s[i];
        if (q.type == S_FENCE_RING || q.type == S_PYLON) continue;   // rings enclose things; pylons are tiny
        const int m = q.mc ? margin + 3 : margin;
        if (x0 - m < q.x0 + q.W && x0 + W + m > q.x0 && z0 - m < q.z0 + q.D && z0 + D + m > q.z0) return false;
      }
    }
    return true;
  }
  bool awayFromLines(int x0, int z0, int W, int D, bool allowRoad) {
    // corners and centre off the roads, out of the water and off the user's built ground
    const int xs[3] = {x0, x0 + W / 2, x0 + W - 1}, zs[3] = {z0, z0 + D / 2, z0 + D - 1};
    float lo = 1e9f, hi = -1e9f;
    for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) {
      const Ground g = groundCoarse(seed, xs[a], zs[b]);
      if (g.water || g.riverD < 6 || !insideZone(xs[a], zs[b])) return false;
      if (!allowRoad && (g.road || g.roadD < 3)) return false;
      if (coarseAt(CL_BUILT, (float)xs[a], (float)zs[b]) > 0.35f) return false;
      lo = std::min(lo, g.h); hi = std::max(hi, g.h);
    }
    return hi - lo < 9;   // not on a cliff
  }
  int add(Placed p) {
    const int idx = (int)P.s.size();
    P.s.push_back(p);
    for (int gz = (p.z0 - 16) >> 6; gz <= (p.z0 + p.D + 16) >> 6; gz++) for (int gx = (p.x0 - 16) >> 6; gx <= (p.x0 + p.W + 16) >> 6; gx++) P.grid[cellKey(gx, gz)].push_back(idx);
    return idx;
  }
  // place a structure centred at (cx, cz); rot 0..3; returns its index or -1
  int place(SType t, int cx, int cz, int w, int d, int h, int rot, int variant, float ruin, u8 danger, bool force = false, bool allowRoad = false, int margin = 3) {
    const int W = (rot & 1) ? d : w, D = (rot & 1) ? w : d;
    const int x0 = cx - W / 2, z0 = cz - D / 2;
    if (!insideZone(x0, z0) || !insideZone(x0 + W, z0 + D)) return -1;
    if (!force && (!clearOf(x0, z0, W, D, margin) || !awayFromLines(x0, z0, W, D, allowRoad))) return -1;
    Placed p;
    p.type = t; p.x0 = x0; p.z0 = z0; p.W = W; p.D = D; p.w = w; p.d = d; p.h = h; p.rot = (u8)(rot & 3);
    p.y = groundY(cx, cz); p.seed = hash32((i32)seed, cx, cz, (i32)t); p.danger = danger; p.variant = (u8)variant; p.ruin = ruin;
    p.pad = !(t == S_FENCE_RING || t == S_WRECK || t == S_HEAP || t == S_CAMP || t == S_STASH || t == S_PYLON || t == S_HELIPAD || t == S_BARGE);
    if (t == S_BARGE) p.y = WATER - 1;
    if (t == S_CAMP) p.y = (int)groundAt(seed, cx, cz).h + 1;   // its seats stand on the real ground (stashes and wrecks: the generator)
    const int idx = add(p);
    if (t == S_CAMP) seatsOf(P.s[idx]);
    return idx;
  }
  // the logs round a camp's fire (gen/zone_build.cpp bCamp: fire at local 6,6; logs at 5..6,4 and 8,5..6)
  void seatsOf(const Placed& p) {
    int fx, fz; localToWorld(p, 6, 6, fx, fz);
    for (auto [lx, lz] : {std::pair{5, 4}, std::pair{6, 4}, std::pair{8, 5}, std::pair{8, 6}}) {
      int x, z; localToWorld(p, lx, lz, x, z);
      P.seats.push_back({x, p.y, z, std::atan2(-(float)(fx - x), -(float)(fz - z))});
    }
  }
  // try a few spots in an annulus around (cx, cz)
  int scatter(SType t, int cx, int cz, int r0, int r1, int w, int d, int h, int variant, float ruin, u8 danger, int tries = 8, bool allowRoad = false) {
    for (int k = 0; k < tries; k++) {
      const double a = rnd() * 6.2832, rr = r0 + std::sqrt(rnd()) * (r1 - r0);
      const int i = place(t, cx + (int)(std::cos(a) * rr), cz + (int)(std::sin(a) * rr), w, d, h, (int)(rnd() * 4), variant, ruin, danger, false, allowRoad);
      if (i >= 0) return i;
    }
    return -1;
  }
  void smart(int x, int z, SmartKind k, u8 fac, u8 slots, u8 danger, int region, std::string name, int y = -1) {
    P.smarts.push_back({x, z, y >= 0 ? y : groundY(x, z), k, fac, slots, danger, region, std::move(name)});
  }
  void smartAt(int idx, SmartKind k, u8 fac, u8 slots, int region, const std::string& name) {
    if (idx < 0) return;
    const Placed& p = P.s[idx];
    if (p.mc) { if (idx < (int)used.size()) used[idx] = 1; smart(p.x0 + p.W / 2, p.z0 + p.D / 2, k, fac, slots, p.danger, region, name, p.y); return; }
    smart(p.x0 + p.W / 2, p.z0 + p.D / 2, k, fac, slots, p.danger, region, name);
  }
  // a place the story needs (sim/zone_story.cpp finds it by its smart terrain's name): the spot asked for, else the nearest
  // free one round it, else forced there, so every seed has it
  int must(SType t, int cx, int cz, int w, int d, int h, int rot, int variant, float ruin, u8 danger) {
    int i = place(t, cx, cz, w, d, h, rot, variant, ruin, danger);
    for (int k = 0; i < 0 && k < 40; k++) { const double a = rnd() * 6.2832, rr = 24 + k * 10; i = place(t, cx + (int)(std::cos(a) * rr), cz + (int)(std::sin(a) * rr), w, d, h, rot, variant, ruin, danger); }
    return i >= 0 ? i : place(t, cx, cz, w, d, h, rot, variant, ruin, danger, true);
  }
  int wreck(int cx, int cz, u8 danger, int kind = -1) {
    static const int dims[6][2] = {{5, 3}, {9, 3}, {12, 4}, {8, 4}, {12, 5}, {6, 3}};
    const int v = kind >= 0 ? kind : (int)(rnd() * 6);
    return place(S_WRECK, cx, cz, dims[v][0], dims[v][1], 5, (int)(rnd() * 4), v, 0, danger, false, true, 1);
  }
  // the user's building nearest (x, z) within maxd with at least minArea m² of floor (and of a kind, if asked)
  int mcNear(int x, int z, int maxd, int minArea, SType only = S_COUNT, bool fresh = true) {
    int best = -1; float bd = (float)maxd;
    for (int i = 0; i < (int)P.s.size(); i++) {
      const Placed& p = P.s[i];
      if (!p.mc || p.W * p.D < minArea || (only != S_COUNT && p.type != only) || (fresh && used[i])) continue;
      const float d = std::hypot(p.x0 + p.W * 0.5f - x, p.z0 + p.D * 0.5f - z);
      if (d < bd) { bd = d; best = i; }
    }
    return best;
  }
  int mcBiggest(int x, int z, int maxd, int exclude = -1) {
    int best = -1, ba = 0;
    for (int i = 0; i < (int)P.s.size(); i++) {
      const Placed& p = P.s[i];
      if (!p.mc || used[i] || i == exclude || std::hypot(p.x0 + p.W * 0.5f - x, p.z0 + p.D * 0.5f - z) > maxd) continue;
      if (p.W * p.D > ba) { ba = p.W * p.D; best = i; }
    }
    return best;
  }
  // a spot on a road (paved first) between r0 and r1 from (x, z), nearest r0; false if none
  bool roadSpot(int x, int z, float r0, float r1, int& ox, int& oz, int kind = 0) {
    float bd = 1e9f;
    for (const Line& l : roads()) {
      if (kind && l.kind != kind) continue;
      for (const Pt& q : l.pts) {
        const float d = std::hypot(q.x - x, q.z - z);
        if (d < r0 || d > r1 || std::abs(d - r0) >= bd) continue;
        bd = std::abs(d - r0); ox = (int)q.x; oz = (int)q.z;
      }
    }
    return bd < 1e9f;
  }

  // the user's buildings: every one of them goes into the plan as it stands
  void mcSites() {
    for (const WSite& s : worldSites()) {
      if (s.cls == "fence") continue;
      const int cx = s.x + s.w / 2, cz = s.z + s.d / 2;
      const Region* r = regionAt(cx, cz);
      const int dg = r ? r->danger : wildDanger(cx, cz);
      Placed p;
      p.type = clsType(s.cls); p.x0 = s.x; p.z0 = s.z; p.W = p.w = s.w; p.D = p.d = s.d; p.h = s.h; p.rot = 0; p.y = s.y;
      p.seed = hash32((i32)seed, cx, cz, 977); p.danger = (u8)dg; p.variant = 0; p.ruin = 0.25f + dg * 0.08f; p.pad = false; p.mc = true;
      add(p);
    }
    used.assign(P.s.size(), 0);
  }

  void region(int ri) {
    const Region& R = regions()[ri];
    const int cx = R.cx, cz = R.cz, rr = R.r;
    const u8 dg = (u8)R.danger;
    const float ruin = 0.12f + dg * 0.08f;
    auto rot = [&]() { return (int)(rnd() * 4); };
    auto name = [&](const char* what) { return std::string(R.name) + " " + what; };
    switch (R.kind) {
      case K_VILLAGE: {
        // the Rookie Village: the rookie village by the railway bridge (the camp fire is the hub), the trader's cellar, your bunker's hatch
        const int hx = SPAWN_X, hz = SPAWN_Z;
        const int camp = must(S_CAMP, hx - 4, hz + 10, 12, 12, 3, 0, 0, 0, 1);
        smartAt(camp, SM_HUB, F_DRIFTER, 8, ri, "Rookie Village");
        const int tr = must(S_TRADER, hx - 34, hz + 14, 9, 9, 8, 0, 0, 0.f, 1);
        if (tr >= 0) { const Placed& t = P.s[tr]; P.traderX = t.x0 + t.W / 2; P.traderZ = t.z0 + t.D - 2; P.traderY = t.y - 6; }
        const int hh = must(S_HOME_HATCH, hx + 16, hz + 26, 5, 5, 2, 0, 0, 0, 1);
        if (hh >= 0) { const Placed& t = P.s[hh]; P.hatchX = t.x0 + 2; P.hatchZ = t.z0 + 2; P.hatchY = t.y; }
        // the army's checkpoint on the paved road south toward the fence, its towers
        int kx = cx, kz = MAX_Z - 120;
        { float bz = -1e9f; for (const Line& l : roads()) if (l.kind == 1) for (const Pt& q : l.pts)
            if (std::hypot(q.x - hx, q.z - hz) < 1500 && q.z > hz + 450 && q.z < MAX_Z - 40 && q.z > bz) { bz = q.z; kx = (int)q.x; kz = (int)q.z; } }
        const int ck = place(S_CHECKPOINT, kx, kz, 14, 10, 5, 0, 0, 0, 1, true, true);
        smartAt(ck, SM_BASE, F_ARMY, 6, ri, "Military checkpoint");
        place(S_TOWER, kx - 16, kz - 10, 4, 4, 10, 0, 1, 0, 1); place(S_TOWER, kx + 16, kz - 10, 4, 4, 10, 0, 1, 0, 1);
        // a Bandit post in the yard south-east of the village
        int bp = mcNear(-4030, 3665, 260, 60);
        if (bp < 0) bp = scatter(S_HOUSE2, hx + 250, hz + 150, 0, 120, 11, 9, 12, 0, 0.4f, 1);
        smartAt(bp, SM_BASE, F_BANDIT, 6, ri, "Bandit post");
        // the village's houses: shelter for the rookies
        for (int k = 0; k < 3; k++) { const int h = mcNear(hx, hz, 260, 40); smartAt(h, SM_RUIN, F_NONE, 3, ri, name("houses")); }
        break;
      }
      case K_MARSH: {
        int ch = mcNear(cx, cz, 450, 40);
        if (ch < 0) ch = place(S_CHURCH, cx, cz, 15, 9, 27, rot(), 0, 0.35f, dg);
        smartAt(ch, SM_CAMP, F_DRIFTER, 5, ri, "Clear Sky base");
        for (int i = 0; i < 3; i++) scatter(S_TOWER, cx, cz, 150, rr - 100, 4, 4, 10, 0, 0.3f, dg);
        break;
      }
      case K_GORGE: {   // (not in the user's world: built here)
        const int a = place(S_HANGAR, cx + 60, cz - 40, 28, 14, 11, rot(), 0, 0.4f, dg);
        place(S_HANGAR, cx - 50, cz + 30, 24, 12, 11, rot(), 0, 0.4f, dg);
        place(S_BARN, cx + 10, cz + 80, 14, 9, 9, rot(), 0, 0.5f, dg);
        place(S_TOWER, cx - 10, cz - 70, 4, 4, 10, 0, 0, 0.2f, dg);
        smartAt(a >= 0 ? a : scatter(S_HANGAR, cx, cz, 0, rr - 60, 28, 14, 11, 0, 0.4f, dg, 20), SM_BASE, F_BANDIT, 6, ri, "Darkscape bandit camp");
        break;
      }
      case K_INSTITUTE: {
        const int b = must(S_BUNKER, cx + 40, cz + 60, 30, 16, 6, 0, 0, 0, dg + 1);
        smartAt(b, SM_LAIR, F_MUTANT, 4, ri, "Agroprom Underground");
        smartAt(mcBiggest(cx, cz, rr), SM_POST, F_ARMY, 4, ri, "Agroprom research institute");
        break;
      }
      case K_SCRAPYARD: {
        int h = mcNear(cx, cz, 300, 300);
        if (h < 0) h = must(S_HANGAR, cx + 40, cz - 120, 34, 16, 12, 0, 0, ruin, dg);
        smartAt(h, SM_CAMP, F_DRIFTER, 5, ri, "Garbage hangar");
        for (int i = 0; i < 25; i++) wreck(cx - 260 + (int)(rnd() * 520), cz - 200 + (int)(rnd() * 400), dg);
        for (int i = 0; i < 8; i++) { const int s = 16 + (int)(rnd() * 18); scatter(S_HEAP, cx, cz, 60, rr - 60, s, s, 7 + (int)(rnd() * 6), 0, 0, dg, 6, true); }
        break;
      }
      case K_VALLEY: {
        smartAt(mcBiggest(cx, cz, rr), SM_BASE, F_BANDIT, 10, ri, "Bandit base");
        const int x = must(S_BUNKER, cx + 160, cz + 120, 30, 16, 6, 0, 0, 0, dg + 2);   // the hatch down into Lab X-18
        smartAt(x, SM_LAIR, F_MUTANT, 5, ri, "Lab X-18");
        break;
      }
      case K_YARD: {   // Wild Territory: the station and the yard, the Mercenaries' snipers
        smartAt(mcBiggest(cx, cz, rr), SM_BASE, F_MERC, 8, ri, "Wild Territory station");
        for (int k = 0; k < 2; k++) smartAt(mcBiggest(cx + (int)((rnd() - 0.5) * rr), cz + (int)((rnd() - 0.5) * rr), rr / 2), SM_POST, F_MERC, 3, ri, "Sniper nest");
        for (int i = 0; i < 6; i++) wreck(cx - 150 + (int)(rnd() * 300), cz - 150 + (int)(rnd() * 300), dg, i & 1 ? 1 : 3);
        break;
      }
      case K_LAKE: {
        int b = mcNear(cx, cz, 160, 200);
        if (b < 0) b = must(S_BUNKER, cx + 80, cz - 60, 30, 16, 6, 0, 0, 0, dg);
        smartAt(b, SM_BASE, F_INSTITUTE, 4, ri, "Ecologists' bunker");
        smartAt(mcBiggest(cx, cz, rr, b), SM_LAIR, F_MUTANT, 6, ri, "Yantar factory");
        const int x = must(S_BUNKER, cx - 150, cz + 160, 30, 16, 6, 0, 0, 0, dg + 2);   // the hatch down into Lab X-16
        smartAt(x, SM_LAIR, F_MUTANT, 5, ri, "Lab X-16");
        break;
      }
      case K_HUB: {
        int bar = mcNear(cx, cz, 320, 400);
        if (bar < 0) bar = must(S_FACTORY, cx - 50, cz - 40, 70, 30, 14, 0, 0, 0.05f, 1);
        smartAt(bar, SM_HUB, F_DRIFTER, 10, ri, "100 Rads Bar");
        int gx, gz;
        if (roadSpot(cx, cz, 180, 420, gx, gz)) {
          const int ck = place(S_CHECKPOINT, gx, gz, 14, 10, 5, 0, 0, 0, 1, false, true);
          if (ck >= 0) smartAt(ck, SM_BASE, F_BASTION, 8, ri, "Duty checkpoint"); else smart(gx, gz, SM_BASE, F_BASTION, 8, 1, ri, "Duty checkpoint");
        } else smartAt(mcBiggest(cx, cz, rr, bar), SM_BASE, F_BASTION, 8, ri, "Duty checkpoint");
        break;
      }
      case K_GRAVEYARD: {   // (built here)
        for (int row = 0; row < 12; row++) for (int c = 0; c < 14; c++)
          if (rnd() < 0.8) wreck(cx - 110 + c * 16, cz - 70 + row * 11, dg, row % 4 == 0 ? 4 : row % 4 == 1 ? 2 : row % 4 == 2 ? 1 : 3);
        place(S_HANGAR, cx + 180, cz - 60, 30, 14, 11, 1, 0, ruin, dg);
        const int t = place(S_TOWER, cx - 150, cz - 90, 4, 4, 12, 0, 1, 0, dg);
        const int camp = scatter(S_CAMP, cx, cz, 160, rr - 40, 12, 12, 3, 0, 0, dg, 12);
        smartAt(camp, SM_CAMP, F_DRIFTER, 5, ri, "Truck Cemetery camp");
        smartAt(t, SM_POST, F_NONE, 3, ri, "Truck Cemetery tower");
        break;
      }
      case K_GHOST_TOWN: case K_CLOSED_TOWN: {
        const bool closed = R.kind == K_CLOSED_TOWN;
        int hall = mcNear(cx, cz, 300, 300);
        if (hall < 0) hall = mcBiggest(cx, cz, rr);
        smartAt(hall, SM_BASE, closed ? F_CHOIR : F_MERC, 8, ri, closed ? "Limansk square" : "Mercenary base");
        break;
      }
      case K_MILITARY: {
        const int g = mcNear(cx, cz, 300, 150);
        smartAt(g, SM_BASE, F_OPENSKY, 10, ri, "Freedom checkpoint");
        smartAt(mcBiggest(cx, cz, rr), SM_POST, F_OPENSKY, 4, ri, "Freedom base");
        if (g < 0) { int gx, gz; if (roadSpot(cx, cz, 150, 400, gx, gz)) smart(gx, gz, SM_BASE, F_OPENSKY, 10, dg, ri, "Freedom checkpoint"); }
        break;
      }
      case K_FARMSTEAD: {   // (built here)
        int b = -1;
        for (int i = 0; i < 6; i++) { const int k = place(S_BARN, cx - 90 + (i % 3) * 60, cz - 40 + (i / 3) * 60, 18, 10, 9, 0, 0, ruin, dg); if (k >= 0) b = k; }
        for (int i = 0; i < 10; i++) scatter(S_HUT, cx, cz, 120, rr - 60, 7, 9, 8, i, ruin + 0.15f, dg);
        place(S_WATERTOWER, cx + 110, cz + 10, 7, 7, 22, 0, 0, 0, dg);
        for (int i = 0; i < 6; i++) wreck(cx - 120 + (int)(rnd() * 240), cz - 120 + (int)(rnd() * 240), dg, 5);
        const int camp = must(S_CAMP, cx, cz + 20, 12, 12, 3, 0, 0, 0, dg);
        smartAt(camp, SM_CAMP, F_DRIFTER, 6, ri, "Meadow camp");
        smartAt(b, SM_RUIN, F_NONE, 3, ri, "Meadow works");
        break;
      }
      case K_RED_FOREST: {   // (built here, in the woods between Pripyat and the plant)
        const int m = must(S_BUNKER, cx + 120, cz - 160, 30, 16, 6, 0, 0, 0, dg + 1);
        smartAt(m, SM_LAIR, F_MUTANT, 6, ri, "The old mine");
        for (int i = 0; i < 8; i++) wreck(cx - 300 + (int)(rnd() * 600), cz - 300 + (int)(rnd() * 600), dg, 3);
        const int h = must(S_HUT, cx - 200, cz + 260, 7, 9, 8, 1, 0, 0.3f, dg);
        smartAt(h, SM_CAMP, F_DRIFTER, 4, ri, "Forester's hut");
        break;
      }
      case K_RADAR: {
        smartAt(mcNear(cx, cz, 500, 0), SM_BASE, F_CHOIR, 10, ri, "Brain Scorcher");
        const int b = must(S_BUNKER, cx, cz + 120, 30, 16, 6, 0, 0, 0, dg + 1);
        smartAt(b, SM_POST, F_CHOIR, 4, ri, "Radar bunker");
        for (int i = 0; i < 3; i++) scatter(S_HANGAR, cx, cz, 150, rr - 80, 30, 14, 11, 0, ruin, dg);
        place(S_WATERTOWER, cx - 220, cz + 140, 7, 7, 22, 0, 0, 0, dg);
        break;
      }
      case K_HOSPITAL: {   // (built here, on the way from Pripyat to the plant: four storeys of wards and stairwells, the Monolith inside)
        const int h = must(S_HOSPITAL, cx, cz, 60, 34, 17, 0, 0, ruin + 0.1f, dg);
        smartAt(h, SM_BASE, F_CHOIR, 8, ri, "Deserted Hospital");
        for (int i = 0; i < 2; i++) scatter(S_OFFICE, cx, cz, 70, rr - 40, 30, 12, 9, 2, ruin, dg);
        break;
      }
      case K_BACKWATER: {
        int bar = mcNear(cx, cz, 320, 200);
        if (bar < 0) bar = must(S_BARGE, cx, cz, 40, 11, 8, 0, 1, 0.2f, dg);
        smartAt(bar, SM_HUB, F_DRIFTER, 8, ri, "Skadovsk");
        for (int i = 0; i < 3; i++) {   // barges beached in the backwaters
          for (int k = 0; k < 40; k++) {
            const int x = cx - 500 + (int)(rnd() * 1000), z = cz - 500 + (int)(rnd() * 1000);
            const Ground g = groundCoarse(seed, x, z);
            if (!g.water || coarseAt(CL_WATER, (float)x + 20, (float)z) < 0.9f || coarseAt(CL_WATER, (float)x - 20, (float)z) < 0.9f) continue;
            if (clearOf(x - 20, z - 6, 40, 11, 4) && place(S_BARGE, x, z, 40, 11, 8, (int)(rnd() * 4), 0, 0.3f, dg, true) >= 0) break;
          }
        }
        break;
      }
      case K_JUPITER: {
        int st = mcNear(cx, cz, 220, 100);
        if (st < 0) st = must(S_STATION, cx + 60, cz, 44, 14, 9, 1, 2, 0.15f, dg);
        smartAt(st, SM_HUB, F_DRIFTER, 10, ri, "Yanov Station");
        smartAt(mcBiggest(cx, cz, rr, st), SM_LAIR, F_MUTANT, 6, ri, "Jupiter factory");
        break;
      }
      case K_CITY: {
        if (std::string(R.id) == "outskirts") {   // the suburbs: the Monolith's lookouts on the high-rises, the school
          smartAt(mcBiggest(cx, cz, rr), SM_BASE, F_CHOIR, 8, ri, "Pripyat school");
          for (int k = 0; k < 3; k++) smartAt(mcNear(cx + (int)((rnd() - 0.5) * rr), cz + (int)((rnd() - 0.5) * rr), rr, 150, S_PANEL), SM_POST, F_CHOIR, 4, ri, "Monolith lookout");
          break;
        }
        smart(-2862, -2312, SM_BASE, F_CHOIR, 10, dg, ri, "Pripyat square");   // the user's own square (the teleport room's "Pripyat Center")
        for (int i = 0; i < 30; i++) wreck(cx - rr + (int)(rnd() * 2 * rr), cz - rr + (int)(rnd() * 2 * rr), dg, i % 7 == 0 ? 4 : i % 3 == 0 ? 3 : -1);   // abandoned cars, BTRs, a helicopter
        for (int k = 0; k < 6; k++) smartAt(mcBiggest(cx + (int)((rnd() - 0.5) * rr), cz + (int)((rnd() - 0.5) * rr), 300), k < 2 ? SM_POST : SM_RUIN, k < 2 ? F_CHOIR : F_NONE, 4, ri, name("blocks"));
        break;
      }
      case K_GENERATORS: {   // (built here, the far end beyond the river among the power-line corridors: the Generators)
        place(S_COOLING, cx - 200, cz - 60, 64, 64, 72, 0, 1, 0.1f, dg); place(S_COOLING, cx + 200, cz + 60, 64, 64, 60, 0, 1, 0.2f, dg);
        int h = -1;
        for (int i = 0; i < 3; i++) { const int k = scatter(S_TURBINE_HALL, cx, cz, 60, rr - 60, 110, 26, 18, 1, ruin, dg, 14); if (k >= 0) h = k; }
        if (h < 0) h = must(S_TURBINE_HALL, cx, cz, 110, 26, 18, 0, 1, ruin, dg);
        for (int i = 0; i < 10; i++) place(S_PYLON, cx - 300 + i * 60, cz + 260, 7, 7, 26, 0, 0, 0, dg);
        // the energy spheres: glowing balls on steel frames, each in an electric field
        for (int i = 0, n = 0; i < 40 && n < 4; i++) {
          const double a = rnd() * 6.2832, d = 60 + rnd() * (rr - 160);
          const int x = cx + (int)(std::cos(a) * d), z = cz + (int)(std::sin(a) * d);
          if (!clearOf(x - 6, z - 6, 12, 12, 6) || !awayFromLines(x - 6, z - 6, 12, 12, true)) continue;
          const int k = place(S_HELIPAD, x, z, 12, 12, 1, 0, 0, 0, dg); if (k < 0) continue;
          P.spheres.push_back({x, groundY(x, z) - 1, z}); n++;
          field(x + 14, z, 10, A_ELECTRIC, 5, 6);
        }
        const int b = must(S_BUNKER, cx + 150, cz + 150, 30, 16, 6, 0, 0, 0, dg);   // the hatch down into the X-lab
        smartAt(h, SM_BASE, F_CHOIR, 10, ri, "The Generators");
        smartAt(b, SM_POST, F_CHOIR, 4, ri, "Lab X-2 hatch");
        break;
      }
      case K_NPP: {
        if (std::string(R.id) == "npp_south") {   // the south side: the approach, the switchyards, the cooling channels
          smartAt(mcBiggest(cx, cz, rr), SM_BASE, F_CHOIR, 10, ri, "NPP south yards");
          int gx, gz; if (roadSpot(cx, cz, 60, rr, gx, gz, 1)) smart(gx, gz, SM_POST, F_CHOIR, 6, dg, ri, "NPP south gate");
          break;
        }
        int re = -1, bh = 0;
        for (int i = 0; i < (int)P.s.size(); i++) { const Placed& p = P.s[i]; if (p.mc && std::hypot(p.x0 + p.W * 0.5f - cx, p.z0 + p.D * 0.5f - cz) < 350 && p.W * p.D > 400 && p.h > bh) { bh = p.h; re = i; } }
        if (re < 0) re = must(S_REACTOR, cx + 60, cz, 60, 44, 46, 0, 0, 0.2f, dg);
        smartAt(re, SM_BASE, F_CHOIR, 12, ri, "The sarcophagus");
        break;
      }
      default: break;
    }
    // every region: the user's other buildings as shelters, camps, stashes, lairs in the woods and the reeds
    for (int k = 0; k < 3; k++) smartAt(mcBiggest(cx + (int)((rnd() - 0.5) * rr), cz + (int)((rnd() - 0.5) * rr), rr / 2), SM_RUIN, F_NONE, 3, ri, name("ruins"));
    for (int i = 0; i < 2; i++) { const int c = scatter(S_CAMP, cx, cz, rr / 4, rr - 60, 12, 12, 3, 0, 0, dg, 10); smartAt(c, SM_CAMP, F_NONE, 4, ri, name("camp")); }
    for (int i = 0; i < 3 + dg; i++) scatter(S_STASH, cx, cz, 40, rr - 30, 1, 1, 2, 0, 0, dg, 6);
    for (int i = 0, n = 0; i < 30 && n < 2 + dg / 2; i++) {
      const double a = rnd() * 6.2832, d = rr * (0.35 + rnd() * 0.6);
      const int x = cx + (int)(std::cos(a) * d), z = cz + (int)(std::sin(a) * d);
      const Ground g = groundCoarse(seed, x, z);
      if (g.water || coarseAt(CL_BUILT, (float)x, (float)z) > 0.2f) continue;
      if (coarseAt(CL_FOREST, (float)x, (float)z) < 0.35f && R.kind != K_MARSH && i < 20) continue;   // in the woods (the marsh: anywhere)
      smart(x, z, SM_LAIR, F_MUTANT, 5, dg, ri, name("lair")); n++;
    }
  }

  void wilds() {
    // between the regions: the user's buildings as shelters, camps, stashes, lairs in the woods, a ruin now and then
    constexpr int G = 360;
    for (int gz = MIN_Z / G - 1; gz <= MAX_Z / G; gz++) for (int gx = MIN_X / G - 1; gx <= MAX_X / G; gx++) {
      const int x = gx * G + 40 + (int)(hashFloat((i32)seed, gx, gz, 1) * (G - 80)), z = gz * G + 40 + (int)(hashFloat((i32)seed, gx, gz, 2) * (G - 80));
      if (!insideZone(x, z) || regionAt(x, z)) continue;
      const int dg = wildDanger(x, z);
      const double r = hashFloat((i32)seed, gx, gz, 3);
      const int m = mcNear(x, z, 150, 60);
      if (m >= 0 && r < 0.6) { smartAt(m, SM_RUIN, F_NONE, 3, -1, "Ruins"); continue; }
      if (r < 0.08) { const int c = place(S_CAMP, x, z, 12, 12, 3, 0, 0, 0, (u8)dg); smartAt(c, SM_CAMP, F_NONE, 4, -1, "Roadside camp"); }
      else if (r < 0.18) place(S_STASH, x, z, 1, 1, 2, 0, 0, 0, (u8)dg);
      else if (r < 0.28 && coarseAt(CL_FOREST, (float)x, (float)z) > 0.3f && !groundCoarse(seed, x, z).water) smart(x, z, SM_LAIR, F_MUTANT, 4, (u8)dg, -1, "Wild lair");
      else if (r < 0.33) place(S_RUIN, x, z, 7, 9, 8, (int)(r * 4), gx & 3, 0.6f, (u8)dg);
    }
    // wrecks along the paved roads
    for (const Line& l : roads()) {
      if (l.kind != 1) continue;
      for (size_t i = 0; i + 1 < l.pts.size(); i++) {
        const Pt a = l.pts[i], b = l.pts[i + 1];
        const float dx = b.x - a.x, dz = b.z - a.z, len = std::sqrt(dx * dx + dz * dz);
        for (float t = 0; t < len; t += 8) {
          const float px = a.x + dx * t / len, pz = a.z + dz * t / len, nx = -dz / len, nz = dx / len;
          if (hashFloat((i32)seed, (int)px, (int)pz, 77) < 0.012) wreck((int)(px + nx * (hashFloat((i32)seed, (int)px, 1, 2) < 0.5 ? -2 : 3)), (int)(pz + nz * 3), (u8)wildDanger((int)px, (int)pz));
        }
      }
    }
  }

  void field(int x, int z, int r, u8 family, u8 tier, u8 count) {
    AnomalyField f;
    f.x = x; f.z = z; f.y = (int)std::round(groundCoarse(seed, x, z).h); f.r = r; f.family = family; f.tier = tier; f.count = count;
    const int idx = (int)P.fields.size();
    P.fields.push_back(f);
    for (int cz = (z - f.r - 8) >> 6; cz <= (z + f.r + 8) >> 6; cz++) for (int cx = (x - f.r - 8) >> 6; cx <= (x + f.r + 8) >> 6; cx++) P.fieldGrid[cellKey(cx, cz)].push_back(idx);
    const Region* rg = regionAt(x, z);
    smart(x, z, SM_FIELD, F_NONE, 3, tier, rg ? (int)(rg - &regions()[0]) : -1, "Anomaly field");
  }
  void anomalyFields() {
    // the first ones a rookie meets: by the railway embankment and the road at the bridge, a short walk from the camp
    field(SPAWN_X + 122, SPAWN_Z - 78, 9, A_ELECTRIC, 1, 4);
    field(SPAWN_X + 92, SPAWN_Z + 96, 9, A_GRAVITY, 1, 4);
    field(SPAWN_X - 92, SPAWN_Z - 52, 8, A_THERMAL, 1, 3);
    // a field per 150-block cell, more and stronger toward the plant; none in the hubs or by the start
    for (int gz = MIN_Z / 150 - 1; gz <= MAX_Z / 150; gz++) for (int gx = MIN_X / 150 - 1; gx <= MAX_X / 150; gx++) {
      const int x = gx * 150 + 20 + (int)(hashFloat((i32)seed, gx, gz, 11) * 110), z = gz * 150 + 20 + (int)(hashFloat((i32)seed, gx, gz, 12) * 110);
      if (!insideZone(x, z) || std::min({x - MIN_X, MAX_X - x, z - MIN_Z, MAX_Z - z}) < 60) continue;
      const Region* r = regionAt(x, z);
      const int dg = r ? r->danger : wildDanger(x, z);
      if (r && r->kind == K_HUB) continue;   // Rostok is safe ground
      if (std::hypot(float(x - SPAWN_X), float(z - SPAWN_Z)) < 220) continue;
      const double chance = (r ? 0.2 : 0.08) + dg * 0.07 + (r && (r->kind == K_RED_FOREST || r->kind == K_GENERATORS || r->kind == K_CLOSED_TOWN || r->kind == K_CITY || r->kind == K_YARD) ? 0.3 : 0);
      if (hashFloat((i32)seed, gx, gz, 13) > chance) continue;
      const Ground g = groundCoarse(seed, x, z);
      if (g.water || g.road || coarseAt(CL_BUILT, (float)x, (float)z) > (r && r->kind == K_CITY ? 0.6f : 0.3f)) continue;
      // the families each area leans to
      double fam = hashFloat((i32)seed, gx, gz, 15);
      u8 F = fam < 0.3 ? A_THERMAL : fam < 0.55 ? A_ELECTRIC : fam < 0.8 ? A_GRAVITY : A_CHEMICAL;
      if (r && r->kind == K_MARSH) F = fam < 0.6 ? A_CHEMICAL : A_ELECTRIC;
      if (r && r->kind == K_RED_FOREST) F = fam < 0.5 ? A_THERMAL : A_GRAVITY;
      if (r && (r->kind == K_RADAR || r->kind == K_NPP || r->kind == K_CITY) && fam < 0.2) F = A_PSY;
      if (r && r->kind == K_GENERATORS) F = fam < 0.5 ? A_ELECTRIC : fam < 0.7 ? A_PSY : A_GRAVITY;
      field(x, z, 8 + (int)(hashFloat((i32)seed, gx, gz, 14) * (10 + dg * 4)), F, (u8)dg, (u8)(3 + hashFloat((i32)seed, gx, gz, 16) * (3 + dg * 2)));
    }
  }
};
} // namespace

const Plan& zonePlan(u32 seed) {
  static std::mutex m; static std::map<u32, std::unique_ptr<Plan>> cache;
  std::lock_guard<std::mutex> g(m);
  auto& p = cache[seed];
  if (!p) {
    p = std::make_unique<Plan>();
    Planner pl(*p, seed);
    pl.mcSites();
    for (int i = 0; i < (int)regions().size(); i++) pl.region(i);
    pl.wilds();
    pl.anomalyFields();
    const int n = g_readyN.load();
    if (n < 8) { g_readySeed[n].store(seed); g_readyPlan[n].store(p.get()); g_readyN.store(n + 1, std::memory_order_release); }
    DQ_LOG("zone plan: %d buildings (%d the user's), %d smart terrains, %d anomaly fields, %d seats; trader at %d %d %d, hatch at %d %d %d", (int)p->s.size(),
           (int)std::count_if(p->s.begin(), p->s.end(), [](const Placed& q) { return q.mc; }), (int)p->smarts.size(), (int)p->fields.size(), (int)p->seats.size(),
           p->traderX, p->traderY, p->traderZ, p->hatchX, p->hatchY, p->hatchZ);
  }
  return *p;
}

} // namespace dq::zone
