// gen/zone_plan.cpp — the Zone's terrain height and its site plan (see zone_plan.h).
#include "gen/zone_plan.h"
#include "gen/zone_layers.h"
#include "common/noise.h"
#include <cmath>
#include <algorithm>
#include <mutex>
#include <memory>
#include <map>

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
const SegIndex& riverIdx() { static SegIndex s; static std::once_flag f; std::call_once(f, [] { s.build(rivers(), 130); }); return s; }

struct Noises {
  SimplexNoise a, b, c;
  explicit Noises(u32 s) : a(s ^ 0x1234567u), b(s ^ 0x9876543u), c(s ^ 0x5555AAAu) {}
};
const Noises& noisesSlow(u32 seed) {
  static std::mutex m; static std::map<u32, std::unique_ptr<Noises>> cache;
  std::lock_guard<std::mutex> g(m);
  auto& p = cache[seed]; if (!p) p = std::make_unique<Noises>(seed);
  return *p;
}
// the hot path (every column, on every worker): remember the last seed's noises per thread, no lock
const Noises& noises(u32 seed) {
  thread_local u32 lastSeed = 0; thread_local const Noises* last = nullptr;
  if (!last || lastSeed != seed) { last = &noisesSlow(seed); lastSeed = seed; }
  return *last;
}
inline float sstep(float t) { t = std::clamp(t, 0.f, 1.f); return t * t * (3 - 2 * t); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

} // namespace

Ground groundAt(u32 seed, int x, int z) {
  const Noises& N = noises(seed);
  Ground g{};
  float h = BASE + float(N.a.fbm2(x / 1100.0, z / 1100.0, 3)) * 4.5f + float(N.b.fbm2(x / 230.0, z / 230.0, 2)) * 1.8f;
  const Region* r = regionAt(x, z);
  const float w = r ? regionWeight(*r, x, z) : 0.f;
  g.region = r; g.rw = w;
  if (r) {
    switch (r->kind) {
      case K_MARSH: h = lerpf(h, 63.4f + float(N.b.fbm2(x / 90.0, z / 90.0, 2)) * 1.8f, sstep(w * 2.5f)); break;
      case K_RED_FOREST: h += sstep(w * 2) * float(N.c.fbm2(x / 260.0, z / 260.0, 3)) * 7.f; break;
      case K_GORGE: h += sstep(w * 2) * 9.f; break;
      case K_VALLEY: h -= sstep(w * 2) * 2.5f; break;
      default: break;
    }
    if (r->kind != K_MARSH && r->kind != K_RED_FOREST && r->kind != K_GORGE) h = lerpf(h, (float)BASE, sstep((w - 0.15f) / 0.35f));
    if (r->kind == K_BACKWATER) {
      const float c = float(N.c.fbm2(x / 240.0, z / 240.0, 2));
      if (c > 0.2f) h = std::min(h, lerpf(h, 60.f, sstep((c - 0.2f) / 0.12f) * sstep(w * 3)));
    }
  }
  // the water of the traced map (gen/zone_layers.h): ponds, lakes and rivers with soft banks
  const float wf = layerAt(ML_WATER, (float)x, (float)z), wd = waterDistAt((float)x, (float)z);
  if (wf > 0.5f) h = std::min(h, WATER - 1.1f - 4.f * sstep((wf - 0.5f) / 0.45f));
  else h = std::min(h, lerpf(WATER + 0.15f, h, sstep(std::max(wd - 4.f, 0.f) / 45.f)));
  g.riverD = wf > 0.5f ? -1.f - (wf - 0.5f) * 20.f : std::max(0.f, wd - 3.f);
  int li; float dx, dz, raw;
  // the hand-traced rivers (zone_map rivers()): a channel up to 4 deep with soft banks
  if (const float e = riverIdx().nearest((float)x, (float)z, li, dx, dz, raw); li >= 0) {   // e: metres from the bank, < 0 in the water
    if (e < 0) h = std::min(h, WATER - 1.1f - std::min(3.f, -e * 0.35f));
    else h = std::min(h, lerpf(WATER + 0.15f, h, sstep(std::max(e - 2.f, 0.f) / 30.f)));
    g.riverD = std::min(g.riverD, e < 0 ? e - 1.f : std::max(0.f, e - 1.f));
  }
  // roads: nearest road and how far
  const float od = roadIdx().nearest((float)x, (float)z, li, dx, dz, raw);
  if (li >= 0 && od < 0) { g.road = roads()[li].kind; g.roadD = raw; }
  else { g.road = 0; g.roadD = od; }
  g.h = h;
  g.water = h < WATER - 0.5f;
  g.waterY = WATER;
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
namespace {
struct Planner {
  Plan& P; u32 seed; Mulberry32 rnd;
  Planner(Plan& p, u32 s) : P(p), seed(s), rnd(s ^ 0xC0FFEEu) {}

  int groundY(int x, int z) { return (int)std::round(groundAt(seed, x, z).h) + 1; }

  bool clearOf(int x0, int z0, int W, int D, int margin) {
    for (int cz = (z0 - margin) >> 6; cz <= (z0 + D + margin) >> 6; cz++) for (int cx = (x0 - margin) >> 6; cx <= (x0 + W + margin) >> 6; cx++) {
      auto it = P.grid.find(cellKey(cx, cz)); if (it == P.grid.end()) continue;
      for (int i : it->second) {
        const Placed& q = P.s[i];
        if (q.type == S_FENCE_RING || q.type == S_PYLON) continue;   // rings enclose things; pylons are tiny
        if (x0 - margin < q.x0 + q.W && x0 + W + margin > q.x0 && z0 - margin < q.z0 + q.D && z0 + D + margin > q.z0) return false;
      }
    }
    return true;
  }
  bool awayFromLines(int x0, int z0, int W, int D, bool allowRoad) {
    // corners and centre off the roads and out of the rivers
    const int xs[3] = {x0, x0 + W / 2, x0 + W - 1}, zs[3] = {z0, z0 + D / 2, z0 + D - 1};
    for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) {
      const Ground g = groundAt(seed, xs[a], zs[b]);
      if (g.water || g.riverD < 6) return false;
      if (!allowRoad && (g.road || g.roadD < 3)) return false;
    }
    return true;
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
    const int idx = (int)P.s.size();
    P.s.push_back(p);
    for (int gz = (z0 - 8) >> 6; gz <= (z0 + D + 8) >> 6; gz++) for (int gx = (x0 - 8) >> 6; gx <= (x0 + W + 8) >> 6; gx++) P.grid[cellKey(gx, gz)].push_back(idx);
    return idx;
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
  void smart(int x, int z, SmartKind k, u8 fac, u8 slots, u8 danger, int region, std::string name) {
    P.smarts.push_back({x, z, groundY(x, z), k, fac, slots, danger, region, std::move(name)});
  }
  void smartAt(int idx, SmartKind k, u8 fac, u8 slots, int region, const std::string& name) {
    if (idx < 0) return;
    const Placed& p = P.s[idx];
    smart(p.x0 + p.W / 2, p.z0 + p.D / 2, k, fac, slots, p.danger, region, name);
  }
  // a place the story needs (sim/zone_story.cpp finds it by its smart terrain's name): the spot asked for, else the nearest
  // free one round it, else forced there, so every seed has it
  int must(SType t, int cx, int cz, int w, int d, int h, int rot, int variant, float ruin, u8 danger) {
    int i = place(t, cx, cz, w, d, h, rot, variant, ruin, danger);
    for (int k = 0; i < 0 && k < 30; k++) { const double a = rnd() * 6.2832, rr = 24 + k * 10; i = place(t, cx + (int)(std::cos(a) * rr), cz + (int)(std::sin(a) * rr), w, d, h, rot, variant, ruin, danger); }
    return i >= 0 ? i : place(t, cx, cz, w, d, h, rot, variant, ruin, danger, true);
  }
  int wreck(int cx, int cz, u8 danger, int kind = -1) {
    static const int dims[6][2] = {{5, 3}, {9, 3}, {12, 4}, {8, 4}, {12, 5}, {6, 3}};
    const int v = kind >= 0 ? kind : (int)(rnd() * 6);
    return place(S_WRECK, cx, cz, dims[v][0], dims[v][1], 5, (int)(rnd() * 4), v, 0, danger, false, true, 1);
  }

  void region(int ri) {
    const Region& R = regions()[ri];
    const int cx = R.cx, cz = R.cz, rr = R.r;
    const u8 dg = (u8)R.danger;
    const float ruin = 0.12f + dg * 0.08f;
    auto rot = [&]() { return (int)(rnd() * 4); };
    switch (R.kind) {
      case K_VILLAGE: {
        // Old Hamlet: two rows of huts on a lane, the trader's cellar, the camp fire, your bunker's hatch
        const int hx = SPAWN_X, hz = SPAWN_Z;
        for (int i = 0; i < 7; i++) {
          place(S_HUT, hx - 66 + i * 19, hz - 24, 7, 9, 8, 2, i, 0.2f, 1, true);
          if (i != 3) place(S_HUT, hx - 66 + i * 19, hz + 16, 7, 9, 8, 0, i + 1, 0.25f, 1, true);
        }
        const int tr = place(S_TRADER, hx - 9, hz + 16, 9, 9, 8, 0, 0, 0.f, 1, true);
        if (tr >= 0) { const Placed& t = P.s[tr]; P.traderX = t.x0 + t.W / 2; P.traderZ = t.z0 + t.D - 2; P.traderY = t.y - 6; }
        const int hh = place(S_HOME_HATCH, hx + 12, hz - 4, 5, 5, 2, 0, 0, 0, 1, true);
        if (hh >= 0) { const Placed& t = P.s[hh]; P.hatchX = t.x0 + 2; P.hatchZ = t.z0 + 2; P.hatchY = t.y; }
        const int camp = place(S_CAMP, hx - 14, hz - 4, 12, 12, 3, 0, 0, 0, 1, true);
        smartAt(camp, SM_HUB, F_DRIFTER, 8, ri, "Old Hamlet");
        // the army's checkpoint on the highway at the fence, its towers and barracks
        const int ck = place(S_CHECKPOINT, hx - 28, hz + 1090, 14, 10, 5, 0, 0, 0, 1, true, true);
        smartAt(ck, SM_BASE, F_ARMY, 6, ri, "Perimeter checkpoint");
        place(S_TOWER, hx - 70, hz + 1070, 4, 4, 10, 0, 1, 0, 1, true); place(S_TOWER, hx + 8, hz + 1070, 4, 4, 10, 0, 1, 0, 1, true);
        place(S_HANGAR, hx + 30, hz + 1160, 26, 12, 10, 1, 0, 0, 1);
        for (int i = 0; i < 5; i++) scatter(S_BARN, cx, cz, 250, rr - 200, 14, 9, 9, 0, 0.4f, dg);
        break;
      }
      case K_MARSH: {
        const int ch = place(S_CHURCH, cx, cz, 15, 9, 27, rot(), 0, 0.35f, dg, true);
        smartAt(ch, SM_CAMP, F_DRIFTER, 5, ri, "Drowned church");
        for (int i = 0; i < 18; i++) scatter(S_HUT, cx, cz, 120, rr - 150, 7, 9, 8, i, 0.55f, dg);
        for (int i = 0; i < 3; i++) scatter(S_TOWER, cx, cz, 200, rr - 300, 4, 4, 10, 0, 0.3f, dg);
        break;
      }
      case K_GORGE: {
        const int a = place(S_HANGAR, cx + 60, cz - 40, 28, 14, 11, rot(), 0, 0.4f, dg);
        place(S_HANGAR, cx - 50, cz + 30, 24, 12, 11, rot(), 0, 0.4f, dg);
        place(S_BARN, cx + 10, cz + 80, 14, 9, 9, rot(), 0, 0.5f, dg);
        place(S_TOWER, cx - 10, cz - 70, 4, 4, 10, 0, 0, 0.2f, dg);
        smartAt(a, SM_BASE, F_BANDIT, 6, ri, "Quarry sheds");
        break;
      }
      case K_INSTITUTE: {
        place(S_FENCE_RING, cx, cz, 240, 170, 3, 0, 1, 0.3f, dg, true);
        int last = -1;
        for (int i = 0; i < 4; i++) last = place(S_OFFICE, cx - 60 + (i % 2) * 110, cz - 40 + (i / 2) * 70, 32, 14, 13, i < 2 ? 2 : 0, 3, ruin, dg);
        place(S_HANGAR, cx + 80, cz + 50, 30, 14, 11, 1, 0, ruin, dg);
        place(S_GREENHOUSE, cx - 20, cz + 60, 30, 10, 9, 0, 0, 0.5f, dg); place(S_GREENHOUSE, cx - 20, cz + 74, 30, 10, 9, 0, 0, 0.5f, dg);
        place(S_WATERTOWER, cx + 95, cz - 60, 7, 7, 22, 0, 0, 0, dg);
        const int b = must(S_BUNKER, cx, cz, 30, 16, 6, 0, 0, 0, dg + 1);
        smartAt(b, SM_LAIR, F_MUTANT, 4, ri, "Institute sewers");
        smartAt(last, SM_POST, F_ARMY, 4, ri, "Institute labs");
        break;
      }
      case K_SCRAPYARD: {
        for (int i = 0; i < 22; i++) { const int s = 18 + (int)(rnd() * 26); scatter(S_HEAP, cx, cz, 0, rr - 250, s, s, 8 + (int)(rnd() * 8), 0, 0, dg, 6, true); }
        const int h = must(S_HANGAR, cx + 40, cz - 120, 34, 16, 12, 0, 0, ruin, dg);
        place(S_HANGAR, cx - 70, cz - 110, 28, 14, 11, 1, 0, ruin, dg);
        place(S_HANGAR, cx + 120, cz + 60, 28, 14, 11, 0, 0, ruin, dg);
        for (int i = 0; i < 45; i++) wreck(cx - 260 + (int)(rnd() * 520), cz - 200 + (int)(rnd() * 400), dg);
        smartAt(h, SM_CAMP, F_DRIFTER, 5, ri, "Scrapyard hangar");
        break;
      }
      case K_VALLEY: {
        place(S_FENCE_RING, cx, cz, 180, 130, 3, 0, 1, 0.3f, dg, true);
        const int f = place(S_FACTORY, cx, cz - 20, 64, 30, 14, 0, 0, ruin, dg);
        place(S_CHIMNEY, cx + 40, cz - 20, 7, 7, 46, 0, 0, 0, dg);
        for (int i = 0; i < 3; i++) place(S_HANGAR, cx - 60 + i * 50, cz + 40, 26, 12, 11, 0, 0, ruin, dg);
        for (int i = 0; i < 6; i++) scatter(S_HUT, cx, cz, 120, 360, 7, 9, 8, i, 0.45f, dg);
        place(S_WATERTOWER, cx - 80, cz - 50, 7, 7, 22, 0, 0, 0, dg);
        smartAt(f, SM_BASE, F_BANDIT, 10, ri, "Wolves' brick works");
        break;
      }
      case K_LAKE: {
        const int b = must(S_BUNKER, cx + 520, cz - 170, 30, 16, 6, 0, 0, 0, dg);
        smartAt(b, SM_BASE, F_INSTITUTE, 4, ri, "Kalina research bunker");
        const int f = place(S_FACTORY, cx + 200, cz + 120, 44, 24, 13, 1, 0, 0.45f, dg);
        smartAt(f, SM_LAIR, F_MUTANT, 6, ri, "Kalina works");
        for (int i = 0; i < 3; i++) scatter(S_BARN, cx, cz, 300, 700, 14, 9, 9, 0, 0.5f, dg);
        break;
      }
      case K_HUB: {
        place(S_FENCE_RING, cx, cz, 320, 230, 4, 0, 1, 0.05f, dg, true);
        const int bar = must(S_FACTORY, cx - 50, cz - 40, 70, 30, 14, 0, 0, 0.05f, 1);
        place(S_FACTORY, cx + 70, cz + 50, 60, 28, 14, 0, 1, 0.1f, 1);
        place(S_CHIMNEY, cx + 10, cz - 60, 7, 7, 50, 0, 0, 0, 1); place(S_CHIMNEY, cx + 22, cz - 60, 7, 7, 44, 0, 0, 0, 1);
        for (int i = 0; i < 3; i++) place(S_HANGAR, cx - 120 + i * 45, cz + 70, 26, 12, 11, 0, 0, 0.1f, 1);
        place(S_OFFICE, cx + 100, cz - 60, 30, 14, 13, 2, 3, 0.05f, 1);
        for (int i = 0; i < 4; i++) place(S_TOWER, cx + (i & 1 ? 150 : -150), cz + (i & 2 ? 105 : -105), 4, 4, 11, 0, 1, 0, 1, true);
        const int ck = place(S_CHECKPOINT, cx, cz + 125, 14, 10, 5, 0, 0, 0, 1, true, true);
        smartAt(bar, SM_HUB, F_DRIFTER, 10, ri, "The Furnace bar");
        smartAt(ck, SM_BASE, F_BASTION, 8, ri, "Duty gate");
        break;
      }
      case K_GRAVEYARD: {
        for (int row = 0; row < 12; row++) for (int c = 0; c < 14; c++)
          if (rnd() < 0.8) wreck(cx - 110 + c * 16, cz - 70 + row * 11, dg, row % 4 == 0 ? 4 : row % 4 == 1 ? 2 : row % 4 == 2 ? 1 : 3);
        place(S_HANGAR, cx + 180, cz - 60, 30, 14, 11, 1, 0, ruin, dg);
        const int t = place(S_TOWER, cx - 150, cz - 90, 4, 4, 12, 0, 1, 0, dg);
        const int camp = scatter(S_CAMP, cx, cz, 200, 380, 12, 12, 3, 0, 0, dg);
        smartAt(camp, SM_CAMP, F_DRIFTER, 5, ri, "Graveyard camp");
        smartAt(t, SM_POST, F_NONE, 3, ri, "Graveyard tower");
        break;
      }
      case K_GHOST_TOWN: case K_CLOSED_TOWN: {
        // streets in a grid; panel blocks along them, small houses in the gaps
        const bool closed = R.kind == K_CLOSED_TOWN;
        int hall = -1;
        for (int gz = -3; gz <= 3; gz++) for (int gx = -3; gx <= 3; gx++) {
          const int bx = cx + gx * 70, bz = cz + gz * 56;
          const double r = rnd();
          if (gx == 0 && gz == 0) { hall = place(S_PUBLIC, bx, bz, 40, 22, 16, 0, 3, ruin, dg); continue; }
          if (r < (closed ? 0.45 : 0.3)) place(S_PANEL, bx, bz, 48, 12, 22, (gx + gz) & 1 ? 0 : 2, 5, ruin, dg);
          else if (r < 0.75) { place(S_HOUSE2, bx - 14, bz, 11, 9, 12, rot(), 0, ruin + 0.1f, dg); place(S_HOUSE2, bx + 14, bz, 11, 9, 12, rot(), 0, ruin + 0.1f, dg); }
          else if (r < 0.85) place(S_PUBLIC, bx, bz, 34, 20, 12, 0, 2, ruin, dg);
        }
        place(S_CHURCH, cx + 260, cz - 200, 15, 9, 27, 1, 0, 0.4f, dg);
        smartAt(hall, SM_BASE, closed ? F_CHOIR : F_MERC, 8, ri, closed ? "Zarechny square" : "Sulfur Town hall");
        break;
      }
      case K_MILITARY: {
        place(S_FENCE_RING, cx, cz, 380, 270, 3, 0, 1, 0.1f, dg, true);
        int wh = -1;
        for (int i = 0; i < 6; i++) wh = place(S_HANGAR, cx - 130 + (i % 3) * 90, cz - 70 + (i / 3) * 70, 40, 18, 13, 0, 0, 0.15f, dg);
        for (int i = 0; i < 3; i++) place(S_OFFICE, cx + 140, cz - 90 + i * 45, 32, 12, 9, 1, 2, 0.15f, dg);
        place(S_HELIPAD, cx - 140, cz + 90, 22, 22, 1, 0, 0, 0, dg);
        for (int i = 0; i < 4; i++) place(S_TOWER, cx + (i & 1 ? 182 : -182), cz + (i & 2 ? 127 : -127), 4, 4, 12, 0, 1, 0, dg, true);
        const int ck = place(S_CHECKPOINT, cx, cz + 140, 14, 10, 5, 0, 0, 0, dg, true, true);
        place(S_BUNKER, cx + 60, cz + 80, 30, 16, 6, 0, 0, 0, dg);
        for (int i = 0; i < 12; i++) wreck(cx - 160 + (int)(rnd() * 320), cz + 20 + (int)(rnd() * 90), dg, i % 2 ? 3 : 1);
        smartAt(ck, SM_BASE, F_OPENSKY, 10, ri, "Depot 17 gate");
        smartAt(wh, SM_POST, F_OPENSKY, 4, ri, "Depot warehouses");
        break;
      }
      case K_FARMSTEAD: {
        int b = -1;
        for (int i = 0; i < 6; i++) b = place(S_BARN, cx - 90 + (i % 3) * 60, cz - 40 + (i / 3) * 60, 18, 10, 9, 0, 0, ruin, dg);
        for (int i = 0; i < 10; i++) scatter(S_HUT, cx, cz, 120, 400, 7, 9, 8, i, ruin + 0.15f, dg);
        place(S_WATERTOWER, cx + 110, cz + 10, 7, 7, 22, 0, 0, 0, dg);
        for (int i = 0; i < 6; i++) wreck(cx - 120 + (int)(rnd() * 240), cz - 120 + (int)(rnd() * 240), dg, 5);
        const int camp = place(S_CAMP, cx, cz + 20, 12, 12, 3, 0, 0, 0, dg);
        smartAt(camp, SM_CAMP, F_DRIFTER, 6, ri, "Lesnoe camp");
        smartAt(b, SM_RUIN, F_NONE, 3, ri, "Lesnoe barns");
        break;
      }
      case K_RED_FOREST: {
        const int m = must(S_BUNKER, cx + 120, cz - 160, 30, 16, 6, 0, 0, 0, dg + 1);
        smartAt(m, SM_LAIR, F_MUTANT, 6, ri, "The old mine");
        for (int i = 0; i < 8; i++) wreck(cx - 300 + (int)(rnd() * 600), cz - 300 + (int)(rnd() * 600), dg, 3);
        const int h = place(S_HUT, cx - 200, cz + 260, 7, 9, 8, 1, 0, 0.3f, dg);
        smartAt(h, SM_CAMP, F_DRIFTER, 4, ri, "Forester's hut");
        break;
      }
      case K_RADAR: {
        place(S_RADAR, cx, cz - 220, 380, 12, 140, 0, 0, 0, dg, true);
        place(S_FENCE_RING, cx, cz + 40, 520, 320, 3, 0, 1, 0.2f, dg, true);
        int o = -1;
        for (int i = 0; i < 4; i++) o = place(S_OFFICE, cx - 150 + i * 100, cz + 20, 32, 14, 13, 0, 3, ruin, dg);
        for (int i = 0; i < 3; i++) place(S_HANGAR, cx - 100 + i * 100, cz + 110, 30, 14, 11, 0, 0, ruin, dg);
        for (int i = 0; i < 3; i++) place(S_PANEL, cx + 200, cz - 60 + i * 60, 48, 12, 22, 1, 5, ruin, dg);
        place(S_WATERTOWER, cx - 220, cz + 140, 7, 7, 22, 0, 0, 0, dg);
        const int b = must(S_BUNKER, cx, cz - 120, 30, 16, 6, 0, 0, 0, dg + 1);
        smartAt(o, SM_BASE, F_CHOIR, 10, ri, "Array control");
        smartAt(b, SM_POST, F_CHOIR, 4, ri, "Array bunker");
        break;
      }
      case K_HOSPITAL: {
        const int h = place(S_HOSPITAL, cx, cz, 60, 34, 17, 0, 0, ruin, dg);
        place(S_OFFICE, cx + 80, cz - 30, 30, 12, 9, 1, 2, ruin, dg);
        for (int i = 0; i < 4; i++) scatter(S_HUT, cx, cz, 120, 300, 7, 9, 8, i, 0.6f, dg);
        smartAt(h, SM_BASE, F_MERC, 6, ri, "Sanatorium");
        break;
      }
      case K_BACKWATER: {
        // barges beached along the Veresnya where it meets the backwaters
        int bar = -1;
        for (int i = 0; i < 6; i++) {
          for (int k = 0; k < 40; k++) {
            const int x = cx - 500 + (int)(rnd() * 1000), z = cz - 500 + (int)(rnd() * 1000);
            const Ground g = groundAt(seed, x, z);
            if (!g.water) continue;
            const int idx = place(S_BARGE, x, z, 40, 11, 8, (int)(rnd() * 4), i == 0, 0.2f, dg, true);
            if (idx >= 0) { if (i == 0) bar = idx; break; }
          }
        }
        if (bar < 0) bar = place(S_BARGE, cx, cz, 40, 11, 8, 0, 1, 0.2f, dg, true);
        for (int i = 0; i < 8; i++) scatter(S_HUT, cx, cz, 200, 700, 7, 9, 8, i, 0.4f, dg);
        place(S_WATERTOWER, cx + 300, cz + 250, 7, 7, 22, 0, 0, 0, dg);
        smartAt(bar, SM_HUB, F_DRIFTER, 8, ri, "The Volga barge");
        break;
      }
      case K_JUPITER: {
        place(S_FENCE_RING, cx - 160, cz + 40, 240, 170, 3, 0, 1, 0.3f, dg, true);
        const int f = place(S_FACTORY, cx - 170, cz + 20, 70, 30, 15, 0, 0, ruin, dg);
        place(S_FACTORY, cx - 160, cz + 80, 50, 26, 13, 0, 1, ruin, dg);
        place(S_CHIMNEY, cx - 110, cz - 20, 7, 7, 46, 0, 0, 0, dg);
        const int st = must(S_STATION, cx + 230, cz, 44, 14, 9, 1, 2, 0.15f, dg);
        for (int i = 0; i < 4; i++) scatter(S_HANGAR, cx, cz, 150, 450, 26, 12, 11, 0, ruin, dg);
        smartAt(st, SM_HUB, F_DRIFTER, 10, ri, "Yanov Station");
        smartAt(f, SM_LAIR, F_MUTANT, 6, ri, "Jupiter plant");
        break;
      }
      case K_CITY: {
        int hub = -1;
        for (int gz = -5; gz <= 5; gz++) for (int gx = -4; gx <= 4; gx++) {
          const int bx = cx + gx * 84, bz = cz + gz * 64;
          const double r = rnd();
          if (gx == 0 && gz == 0) { hub = must(S_PUBLIC, bx, bz, 50, 26, 16, 0, 3, ruin, dg); continue; }
          if (gx == 1 && gz == 0) { place(S_FERRIS, bx, bz, 30, 7, 28, 0, 0, 0, dg); continue; }
          if (gx == -1 && gz == 1) { place(S_HOSPITAL, bx, bz, 60, 34, 17, 0, 0, ruin, dg); continue; }
          if (r < 0.12) place(S_PANEL, bx, bz, 18, 18, 66, 0, 16, ruin, dg);           // 16-storey towers
          else if (r < 0.7) place(S_PANEL, bx, bz, 48, 13, 38, (gx + gz) & 1 ? 0 : 2, 9, ruin, dg);
          else if (r < 0.85) place(S_PUBLIC, bx, bz, 36, 22, 12, 0, 2, ruin, dg);
        }
        smartAt(hub, SM_BASE, F_CHOIR, 10, ri, "Pripyat square");
        break;
      }
      case K_GENERATORS: {
        place(S_COOLING, cx - 200, cz - 100, 64, 64, 72, 0, 1, 0.1f, dg); place(S_COOLING, cx + 120, cz - 160, 64, 64, 60, 0, 1, 0.2f, dg);
        int h = -1;
        for (int i = 0; i < 3; i++) h = place(S_TURBINE_HALL, cx - 80 + i * 10, cz + 60 + i * 50, 110, 26, 18, 0, 1, ruin, dg);
        for (int i = 0; i < 4; i++) scatter(S_HANGAR, cx, cz, 250, 600, 30, 14, 11, 0, ruin, dg);
        for (int i = 0; i < 14; i++) place(S_PYLON, cx - 400 + i * 60, cz + 250, 7, 7, 26, 0, 0, 0, dg);
        const int b = place(S_BUNKER, cx + 200, cz + 200, 30, 16, 6, 0, 0, 0, dg);
        smartAt(h, SM_BASE, F_CHOIR, 8, ri, "Generator halls");
        smartAt(b, SM_LAIR, F_MUTANT, 4, ri, "Generator bunker");
        break;
      }
      case K_NPP: {
        place(S_FENCE_RING, cx, cz, 640, 420, 4, 0, 1, 0.15f, dg, true);
        const int re = place(S_REACTOR, cx + 60, cz, 60, 44, 46, 0, 0, 0.2f, dg, true);
        place(S_CHIMNEY, cx + 130, cz - 8, 7, 7, 75, 0, 1, 0, dg, true);
        place(S_TURBINE_HALL, cx - 100, cz + 50, 200, 30, 20, 0, 1, 0.2f, dg, true);
        for (int i = 0; i < 3; i++) place(S_OFFICE, cx - 200, cz - 120 + i * 50, 34, 14, 17, 0, 4, ruin, dg);
        for (int i = 0; i < 4; i++) place(S_HANGAR, cx + 200, cz - 150 + i * 60, 30, 14, 11, 1, 0, ruin, dg);
        place(S_COOLING, cx + 600, cz + 1150, 64, 64, 72, 0, 0, 0.05f, dg, true); place(S_COOLING, cx + 680, cz + 1620, 64, 64, 72, 0, 0, 0.05f, dg, true);
        for (int i = 0; i < 10; i++) place(S_PYLON, cx - 320 + i * 64, cz + 230, 7, 7, 26, 0, 0, 0, dg);
        smartAt(re, SM_BASE, F_CHOIR, 12, ri, "The sarcophagus");
        break;
      }
      default: break;
    }
    // every region: ruins, camps, stashes, lairs and the odd tower in its wider area
    const int extra = std::max(6, rr / 90);
    for (int i = 0; i < extra; i++) scatter(S_RUIN, cx, cz, rr / 3, rr - 60, 7, 9, 8, i, 0.5f + dg * 0.07f, dg, 4);
    for (int i = 0; i < 2; i++) { const int c = scatter(S_CAMP, cx, cz, rr / 3, rr - 100, 12, 12, 3, 0, 0, dg, 6); smartAt(c, SM_CAMP, F_NONE, 4, ri, std::string(R.name) + " camp"); }
    for (int i = 0; i < 3 + dg; i++) scatter(S_STASH, cx, cz, 60, rr - 40, 1, 1, 2, 0, 0, dg, 4);
    for (int i = 0; i < 2 + dg / 2; i++) { const double a = rnd() * 6.2832, d = rr * (0.45 + rnd() * 0.45); smart(cx + (int)(std::cos(a) * d), cz + (int)(std::sin(a) * d), SM_LAIR, F_MUTANT, 5, dg, ri, std::string(R.name) + " lair"); }
    scatter(S_TOWER, cx, cz, rr / 2, rr - 80, 4, 4, 10, 0, 0.3f, dg, 4);
  }

  // the villages and sites of the traced map: a building on (near) each of its marks, styled by the region it is in
  void sites() {
    int k = 0;
    for (const MapSite& st : mapSites()) {
      const Region* r = regionAt((int)st.x, (int)st.z);
      const int ri = r ? (int)(r - &regions()[0]) : -1;
      const Kind kind = r ? r->kind : K_WILD;
      const u8 dg = (u8)(r ? r->danger : std::clamp(1 + (int)((MAX_Z - st.z) / 6500), 1, 5));
      const bool town = kind == K_CITY || kind == K_CLOSED_TOWN || kind == K_GHOST_TOWN;
      const bool works = kind == K_NPP || kind == K_GENERATORS || kind == K_JUPITER || kind == K_MILITARY || kind == K_HUB || st.built > 0.3f;
      const float ruin = 0.25f + dg * 0.08f;
      int first = -1;
      for (const Pt& d : st.dots) {
        const double q = rnd();
        int i;
        if (works) i = place(q < 0.5 ? S_HANGAR : S_OFFICE, (int)d.x, (int)d.z, q < 0.5 ? 26 : 30, q < 0.5 ? 12 : 13, 11, (int)(q * 8) & 3, 2, ruin, dg);
        else if (town) i = q < 0.25 ? place(S_PANEL, (int)d.x, (int)d.z, 40, 12, 18, (int)(q * 8) & 3, 4, ruin, dg) : place(S_HOUSE2, (int)d.x, (int)d.z, 11, 9, 12, (int)(q * 16) & 3, 0, ruin, dg);
        else i = q < 0.68 ? place(S_HUT, (int)d.x, (int)d.z, 7, 9, 8, (int)(q * 16) & 3, (int)(q * 100), ruin + 0.15f, dg)
               : q < 0.88 ? place(S_HOUSE2, (int)d.x, (int)d.z, 11, 9, 12, (int)(q * 16) & 3, 0, ruin, dg)
               : place(S_BARN, (int)d.x, (int)d.z, 14, 9, 9, (int)(q * 16) & 3, 0, ruin + 0.1f, dg);
        if (first < 0) first = i;
      }
      // a church for the big villages, a camp at the middle of each
      if (!works && !town && st.n >= 18) scatter(S_CHURCH, (int)st.x, (int)st.z, 0, (int)st.r / 2, 15, 9, 27, 0, 0.4f, dg, 8);
      const int c = scatter(S_CAMP, (int)st.x, (int)st.z, 0, std::max(60, (int)st.r / 2), 12, 12, 3, 0, 0, dg, 8);
      const std::string nm = (r ? std::string(r->name) + ": " : std::string()) + (works ? "works" : town ? "quarter" : st.n >= 18 ? "village" : "hamlet") + " " + std::to_string(++k);
      smartAt(c >= 0 ? c : first, SM_CAMP, F_NONE, (u8)std::clamp(st.n / 5, 3, 8), ri, nm);
    }
  }

  void wilds() {
    // between the regions: ruined huts, wrecks by the roads, camps, stashes; pylons along the highway
    for (int gz = MIN_Z / 320; gz <= MAX_Z / 320; gz++) for (int gx = MIN_X / 320; gx <= MAX_X / 320; gx++) {
      const int x = gx * 320 + 40 + (int)(hashFloat((i32)seed, gx, gz, 1) * 240), z = gz * 320 + 40 + (int)(hashFloat((i32)seed, gx, gz, 2) * 240);
      if (regionAt(x, z)) continue;
      const int dg = std::clamp(1 + (int)((MAX_Z - z) / 6500), 1, 5);
      const double r = hashFloat((i32)seed, gx, gz, 3);
      if (layerAt(ML_BUILT, (float)x, (float)z) > 0.4f) { if (r < 0.5) place(r < 0.25 ? S_HANGAR : r < 0.4 ? S_OFFICE : S_HEAP, x, z, r < 0.4 ? 28 : 20, r < 0.4 ? 13 : 20, r < 0.4 ? 11 : 8, (int)(r * 8) & 3, 2, 0.5f, (u8)dg); continue; }
      if (r < 0.22) place(S_RUIN, x, z, 7, 9, 8, (int)(r * 4), gx & 3, 0.6f, (u8)dg);
      else if (r < 0.27) { const int c = place(S_CAMP, x, z, 12, 12, 3, 0, 0, 0, (u8)dg); smartAt(c, SM_CAMP, F_NONE, 4, -1, "Roadside camp"); }
      else if (r < 0.36) place(S_STASH, x, z, 1, 1, 2, 0, 0, 0, (u8)dg);
      else if (r < 0.40) place(S_TOWER, x, z, 4, 4, 10, 0, 0, 0.4f, (u8)dg);
      else if (r < 0.46) smart(x, z, SM_LAIR, F_MUTANT, 4, (u8)dg, -1, "Wild lair");
    }
    // wrecks and pylons along the asphalt roads
    for (const Line& l : roads()) {
      if (l.kind != 1) continue;
      float acc = 0;
      for (size_t i = 0; i + 1 < l.pts.size(); i++) {
        const Pt a = l.pts[i], b = l.pts[i + 1];
        const float dx = b.x - a.x, dz = b.z - a.z, len = std::sqrt(dx * dx + dz * dz);
        for (float t = 0; t < len; t += 8) {
          acc += 8;
          const float px = a.x + dx * t / len, pz = a.z + dz * t / len, nx = -dz / len, nz = dx / len;
          if (std::fmod(acc, 96.f) < 8) place(S_PYLON, (int)(px + nx * 22), (int)(pz + nz * 22), 7, 7, 24, 0, 0, 0, 1);
          if (hashFloat((i32)seed, (int)px, (int)pz, 77) < 0.035) wreck((int)(px + nx * (hashFloat((i32)seed, (int)px, 1, 2) < 0.5 ? -2 : 6)), (int)(pz + nz * 3), 2);
        }
      }
    }
  }

  void anomalyFields() {
    // a field per 150-block cell, more and stronger toward the plant; none in the hubs
    for (int gz = MIN_Z / 150; gz <= MAX_Z / 150; gz++) for (int gx = MIN_X / 150; gx <= MAX_X / 150; gx++) {
      const int x = gx * 150 + 20 + (int)(hashFloat((i32)seed, gx, gz, 11) * 110), z = gz * 150 + 20 + (int)(hashFloat((i32)seed, gx, gz, 12) * 110);
      const Region* r = regionAt(x, z);
      const int dg = r ? r->danger : std::clamp(1 + (int)((MAX_Z - z) / 6500), 1, 5);
      if (r && r->kind == K_HUB) continue;
      if (r && r->kind == K_VILLAGE && std::abs(x - SPAWN_X) < 500 && std::abs(z - SPAWN_Z) < 500) continue;
      const double chance = 0.1 + dg * 0.07 + (r && (r->kind == K_RED_FOREST || r->kind == K_GENERATORS || r->kind == K_CLOSED_TOWN) ? 0.25 : 0);
      if (hashFloat((i32)seed, gx, gz, 13) > chance) continue;
      const Ground g = groundAt(seed, x, z);
      if (g.water || g.road) continue;
      AnomalyField f;
      f.x = x; f.z = z; f.y = (int)std::round(g.h);
      f.r = 8 + (int)(hashFloat((i32)seed, gx, gz, 14) * (10 + dg * 4));
      // the families each area leans to
      double fam = hashFloat((i32)seed, gx, gz, 15);
      u8 F = fam < 0.3 ? A_THERMAL : fam < 0.55 ? A_ELECTRIC : fam < 0.8 ? A_GRAVITY : A_CHEMICAL;
      if (r && r->kind == K_MARSH) F = fam < 0.6 ? A_CHEMICAL : A_ELECTRIC;
      if (r && r->kind == K_RED_FOREST) F = fam < 0.5 ? A_THERMAL : A_GRAVITY;
      if (r && (r->kind == K_RADAR || r->kind == K_NPP) && fam < 0.25) F = A_PSY;
      f.family = F; f.tier = (u8)dg;
      f.count = (u8)(3 + hashFloat((i32)seed, gx, gz, 16) * (3 + dg * 2));
      const int idx = (int)P.fields.size();
      P.fields.push_back(f);
      for (int cz = (z - f.r - 8) >> 6; cz <= (z + f.r + 8) >> 6; cz++) for (int cx = (x - f.r - 8) >> 6; cx <= (x + f.r + 8) >> 6; cx++) P.fieldGrid[cellKey(cx, cz)].push_back(idx);
      smart(x, z, SM_FIELD, F_NONE, 3, (u8)dg, r ? (int)(r - &regions()[0]) : -1, "Anomaly field");
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
    for (int i = 0; i < (int)regions().size(); i++) pl.region(i);
    pl.sites();
    pl.wilds();
    pl.anomalyFields();
    DQ_LOG("zone plan: %d buildings, %d smart terrains, %d anomaly fields; trader at %d %d %d, hatch at %d %d %d", (int)p->s.size(), (int)p->smarts.size(), (int)p->fields.size(), p->traderX, p->traderY, p->traderZ, p->hatchX, p->hatchY, p->hatchZ);
  }
  return *p;
}

} // namespace dq::zone
