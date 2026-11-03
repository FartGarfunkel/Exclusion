// gen/zone_map.cpp — the Zone's layout (see zone_map.h). The ground (water, forest, fields, built-up land), the roads and
// the villages are traced from a satellite map of the real Zone (tools/zone/trace_map.py -> data/zone_map, read by
// gen/zone_layers.cpp), 1 block = 7 m of it, ~13.4 x 34.3 km; our 21 regions sit on the matching places: the plant and
// its cooling pond at the top, the city beside them, the forests, the backwater lake, the villages, down to the south.
#include "gen/zone_map.h"
#include "gen/zone_layers.h"
#include "common/noise.h"
#include <cmath>
#include <algorithm>

namespace dq::zone {

static const std::vector<Region> REGIONS = {
  // ---- the south: where everyone starts ----
  {"perimeter", "Southern Perimeter", -3400, 13800, 1700, K_VILLAGE, 1, "The fence, the Military checkpoint and Old Hamlet, where rookies sleep and the trader lives in a cellar."},
  {"marsh", "Reedwater Marsh", -5100, 15600, 1400, K_MARSH, 1, "Reeds, peat and sunken huts round a drowned church. Fog sits on the water all day."},
  {"gorge", "Blackwater Gorge", 1700, 9800, 1300, K_GORGE, 2, "A quarry cut down to black water. Bandits watch the bridge."},
  // ---- the outer ring ----
  {"institute", "Agrarian Institute", -4180, 4229, 1200, K_INSTITUTE, 2, "Research halls and greenhouses over a flooded sewer. The Military lost a squad here."},
  {"scrapyard", "The Scrapyard", 3900, 15000, 1300, K_SCRAPYARD, 2, "Hills of junk from the liquidation, hot with radiation. Packs of dogs den in the hangars."},
  {"hollow", "Hollow Valley", 4400, 11200, 1500, K_VALLEY, 2, "A brick works in a damp valley, held by a Bandit gang."},
  // ---- the middle ----
  {"lake", "Lake Kalina", 3805, 6430, 1100, K_LAKE, 3, "A still lake by an old institute bunker. Something walks the factory floors at night."},
  {"molot", "Molot Works", -2100, 2900, 1300, K_HUB, 1, "A fortified steel works: the Furnace bar, a technician and Duty's guns. No shooting inside."},
  {"graveyard", "Rassokha Vehicle Graveyard", 2100, -2849, 1200, K_GRAVEYARD, 3, "Rows of trucks, buses and helicopters that cleaned up the first disaster. Most still glow."},
  {"sulfur", "Sulfur Town", -3752, 7887, 1300, K_GHOST_TOWN, 3, "An empty district town. Mercenaries run their business from the town hall."},
  {"depot", "Depot 17", -5700, 2400, 1100, K_MILITARY, 3, "Military warehouses, barracks and a helipad. Freedom keeps the gate."},
  {"lesnoe", "Lesnoe Farmstead", 5000, 2400, 1300, K_FARMSTEAD, 3, "A collective farm on the road north. The last stop before the deep Zone."},
  // ---- the deep Zone ----
  {"zarechny", "Zarechny", -5000, -4000, 1500, K_CLOSED_TOWN, 4, "A closed science town across the river, never on the maps. The streets are full of anomalies."},
  {"redforest", "Red Forest", -3900, -9300, 1500, K_RED_FOREST, 4, "Pines killed in one night, still rust-red. The ground is hot and the old mine goes deep."},
  {"array", "Duga Array", 4400, -300, 1500, K_RADAR, 4, "The over-horizon radar: a steel wall 150 m high. Its hum turns minds; the Monolith guard it."},
  {"sanatorium", "Abandoned Sanatorium", -1800, -8700, 1000, K_HOSPITAL, 4, "A children's sanatorium in the pines, roofs fallen in."},
  {"zaton", "Zaton Backwater", 6000, -5650, 1100, K_BACKWATER, 3, "River backwaters full of rusting barges. A trader runs a bar in a beached ship."},
  {"jupiter", "Jupiter Plant & Yanov Station", 5300, -3100, 1200, K_JUPITER, 3, "A secret factory and the railway station beside it, a camp for stalkers going to the city."},
  {"pripyat", "Pripyat", 2200, -7900, 1900, K_CITY, 5, "The city of the plant workers, empty since the evacuation. The Monolith hold the rooftops."},
  // ---- the centre ----
  {"turbines", "Turbine Fields", -5000, -14900, 1500, K_GENERATORS, 5, "Unfinished power blocks and generator yards wrapped in the strongest anomalies."},
  {"npp", "Chernobyl NPP", -100, -13500, 1700, K_NPP, 5, "The plant, the sarcophagus and the cooling pond. Where the Zone began."},
};

const std::vector<Region>& regions() { return REGIONS; }

const Region* regionById(const std::string& id) { for (auto& r : REGIONS) if (id == r.id) return &r; return nullptr; }

float regionWeight(const Region& r, int x, int z) {
  const float d = std::sqrt(float(x - r.cx) * (x - r.cx) + float(z - r.cz) * (z - r.cz)) / r.r;
  return d >= 1 ? 0.f : 1.f - d;
}

const Region* regionAt(int x, int z) {
  const Region* best = nullptr; float bd = 1e30f;
  for (auto& r : REGIONS) {
    const float d = (float(x - r.cx) * (x - r.cx) + float(z - r.cz) * (z - r.cz)) / (float(r.r) * r.r);
    if (d < 1 && d < bd) { bd = d; best = &r; }
  }
  return best;
}

// rivers and lakes are in the traced water layer (gen/zone_layers.h); roads are the traced ones
static std::vector<Line> makeRivers() { return mapRivers(); }   // the narrow ones the tracer misses, hand-traced

// a trunk road: `pre` (the bend round the region it leaves), then on from the last of them to b with gentle bends, stepping
// round water where it can
static void trunk(std::vector<Line>& L, std::vector<Pt> pre, Pt b, u8 kind) {
  const Pt a = pre.back();
  float dx = b.x - a.x, dz = b.z - a.z; const float len = std::hypot(dx, dz);
  if (len < 40) return;
  dx /= len; dz /= len;
  const int k = std::max(2, (int)(len / 200));
  const float amp = std::min(150.f, len * 0.05f), f = 0.6f + float(hashFloat(7, (int)a.x, (int)b.z, 1)), ph = float(hashFloat(7, (int)b.x, (int)a.z, 2)) * 6.2832f;
  Line l; l.kind = kind; l.width = kind == 1 ? 7.f : 4.f; l.pts = std::move(pre);
  for (int i = 1; i <= k; i++) {
    const float t = (float)i / k, off = amp * std::sin(t * 3.14159f) * std::sin(t * 6.2832f * f + ph);
    float px = a.x + dx * len * t - dz * off, pz = a.z + dz * len * t + dx * off;
    if (i < k && layerAt(ML_WATER, px, pz) > 0.3f)   // round the pond if a dry way is near
      for (int s = 1; s <= 10; s++) {
        const float o = (s & 1 ? 1 : -1) * 40.f * ((s + 1) / 2);
        if (layerAt(ML_WATER, px - dz * o, pz + dx * o) < 0.3f) { px -= dz * o; pz += dx * o; break; }
      }
    l.pts.push_back({std::clamp(px, (float)MIN_X + 20, (float)MAX_X - 20), std::clamp(pz, (float)MIN_Z + 20, (float)MAX_Z - 20)});
  }
  L.push_back(std::move(l));
}

// the traced roads come in pieces (the tracer loses them under the trees and at the junctions): lay trunk roads between
// the regions first (a tree over their centres from Old Hamlet; the way from the fence through the checkpoint to the
// plant is asphalt), then run every loose end on to the nearest road within reach, so the places are all on one network
static std::vector<Line> makeRoads() {
  std::vector<Line> L = mapRoads();
  if (L.empty()) return L;
  for (Line& r : mapRails()) L.push_back(std::move(r));   // the railway (hand-traced)
  const float hx = SPAWN_X, hz = SPAWN_Z;
  // the highway in: the fence, the checkpoint (zone_plan puts it astride the road), into Old Hamlet's lane and out north
  L.push_back({{{hx - 28, MAX_Z - 25.f}, {hx - 28, hz + 1090}, {hx - 23, hz + 640}, {hx - 19, hz + 60}, {hx - 19, hz + 9}}, 7.f, 1});
  L.push_back({{{hx - 19, hz - 12}, {hx - 19, hz - 70}}, 7.f, 1});
  struct Node { Pt p; float stop; };
  std::vector<Node> N{{{hx - 19, hz - 70}, 0.f}};
  int npp = -1;
  for (const Region& r : REGIONS) {
    if (std::string(r.id) == "perimeter") continue;   // Old Hamlet stands for it
    if (std::string(r.id) == "npp") npp = (int)N.size();
    N.push_back({{(float)r.cx, (float)r.cz}, std::clamp(r.r * 0.22f, 150.f, 420.f)});
  }
  const int n = (int)N.size();
  std::vector<int> par(n, -1); std::vector<float> best(n, 1e30f); std::vector<char> in(n, 0);
  best[0] = 0;
  for (int it = 0; it < n; it++) {   // Prim, rooted at Old Hamlet
    int u = -1;
    for (int i = 0; i < n; i++) if (!in[i] && (u < 0 || best[i] < best[u])) u = i;
    in[u] = 1;
    for (int v = 0; v < n; v++) if (!in[v]) { const float d = std::hypot(N[u].p.x - N[v].p.x, N[u].p.z - N[v].p.z); if (d < best[v]) { best[v] = d; par[v] = u; } }
  }
  std::vector<char> spine(n, 0);
  for (int v = npp; v > 0; v = par[v]) spine[v] = 1;
  // each region's roads meet at its junction (on a ring `stop` out from the centre, on the side of the road in) and the
  // ones going on bend round the ring from there, clear of the compound in the middle
  auto ang = [&](int u, int v) { return std::atan2(N[v].p.z - N[u].p.z, N[v].p.x - N[u].p.x); };
  auto ring = [&](int u, float a) { return Pt{N[u].p.x + std::cos(a) * N[u].stop, N[u].p.z + std::sin(a) * N[u].stop}; };
  for (int v = 1; v < n; v++) {
    const int u = par[v];
    std::vector<Pt> pre;
    const float a1 = ang(u, v);
    if (u > 0) {
      const float a0 = ang(u, par[u]);
      float d = std::remainder(a1 - a0, 6.2832f);
      const int steps = (int)std::ceil(std::abs(d) / 0.3f);
      for (int i = 0; i <= steps; i++) pre.push_back(ring(u, a0 + d * i / std::max(1, steps)));
    } else pre.push_back(N[u].p);
    trunk(L, std::move(pre), ring(v, ang(v, u)), spine[v] ? 1 : 2);
  }
  // loose ends: run on to the nearest road within reach (not over water)
  const float REACH = 170;
  std::vector<float> bb(L.size() * 4);
  for (size_t j = 0; j < L.size(); j++) {
    float a = 1e30f, b = -1e30f, c = 1e30f, d = -1e30f;
    for (const Pt& q : L[j].pts) { a = std::min(a, q.x); b = std::max(b, q.x); c = std::min(c, q.z); d = std::max(d, q.z); }
    bb[j * 4] = a - REACH; bb[j * 4 + 1] = b + REACH; bb[j * 4 + 2] = c - REACH; bb[j * 4 + 3] = d + REACH;
  }
  for (size_t i = 0; i < L.size(); i++) for (int end = 0; end < 2; end++) {
    if (L[i].kind == 3) break;   // the railway's ends stay where they are
    const Pt p = end ? L[i].pts.back() : L[i].pts.front();
    float bd = REACH; Pt q{};
    for (size_t j = 0; j < L.size(); j++) {
      if (j == i || p.x < bb[j * 4] || p.x > bb[j * 4 + 1] || p.z < bb[j * 4 + 2] || p.z > bb[j * 4 + 3]) continue;
      for (size_t s = 0; s + 1 < L[j].pts.size(); s++) {
        const Pt a = L[j].pts[s], b = L[j].pts[s + 1];
        const float ux = b.x - a.x, uz = b.z - a.z, L2 = ux * ux + uz * uz;
        const float t = std::clamp(L2 > 0 ? ((p.x - a.x) * ux + (p.z - a.z) * uz) / L2 : 0.f, 0.f, 1.f);
        const Pt c{a.x + ux * t, a.z + uz * t};
        const float d = std::hypot(c.x - p.x, c.z - p.z);
        if (d < bd) { bd = d; q = c; }
      }
    }
    if (bd >= REACH || bd < 3) continue;
    bool wet = false;
    for (int k = 1; k < 4; k++) wet |= layerAt(ML_WATER, p.x + (q.x - p.x) * k / 4, p.z + (q.z - p.z) * k / 4) > 0.4f;
    if (wet) continue;
    if (end) L[i].pts.push_back(q); else L[i].pts.insert(L[i].pts.begin(), q);
  }
  return L;
}

const std::vector<Line>& rivers() { static const std::vector<Line> v = makeRivers(); return v; }
const std::vector<Line>& roads() { static const std::vector<Line> v = makeRoads(); return v; }

float distToLine(const Line& l, float x, float z) {
  float best = 1e30f;
  for (size_t i = 0; i + 1 < l.pts.size(); i++) {
    const Pt a = l.pts[i], b = l.pts[i + 1];
    const float dx = b.x - a.x, dz = b.z - a.z, L2 = dx * dx + dz * dz;
    float t = L2 > 0 ? ((x - a.x) * dx + (z - a.z) * dz) / L2 : 0;
    t = std::clamp(t, 0.f, 1.f);
    const float px = a.x + dx * t - x, pz = a.z + dz * t - z;
    best = std::min(best, px * px + pz * pz);
  }
  return std::sqrt(best);
}

bool insideZone(int x, int z) { return x >= MIN_X && x <= MAX_X && z >= MIN_Z && z <= MAX_Z; }

float radiationAt(int x, int z, u32 seed) {
  const Region* r = regionAt(x, z);
  float rad = 0;
  if (r) {
    const float w = regionWeight(*r, x, z);
    switch (r->kind) {
      case K_SCRAPYARD: rad = 0.6f * w; break;
      case K_GRAVEYARD: rad = 0.9f * w; break;
      case K_RED_FOREST: rad = 1.6f * w; break;
      case K_NPP: rad = 2.5f * w; break;
      case K_GENERATORS: rad = 1.4f * w; break;
      case K_CITY: rad = 0.5f * w; break;
      default: break;
    }
  }
  // hot spots: one per 192-block cell at most, more of them further north
  const int cx = (int)std::floor(x / 192.0), cz = (int)std::floor(z / 192.0);
  for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
    const int gx = cx + dx, gz = cz + dz;
    const double chance = 0.12 + std::clamp((8000.0 - gz * 192.0) / 22000.0, 0.0, 0.5);
    if (hashFloat((i32)seed, gx, gz, 41) > chance) continue;
    const float hx = gx * 192 + 32 + float(hashFloat((i32)seed, gx, gz, 42)) * 128, hz = gz * 192 + 32 + float(hashFloat((i32)seed, gx, gz, 43)) * 128;
    const float hr = 10 + float(hashFloat((i32)seed, gx, gz, 44)) * 22;
    const float d = std::sqrt((x - hx) * (x - hx) + (z - hz) * (z - hz));
    if (d < hr) rad += (1 - d / hr) * (0.8f + float(hashFloat((i32)seed, gx, gz, 45)) * 3.f);
  }
  return rad;
}

float psyAt(int x, int z) {
  float p = 0;
  if (auto* r = regionById("array")) p = std::max(p, std::clamp(1.3f - std::sqrt(float(x - r->cx) * (x - r->cx) + float(z - r->cz) * (z - r->cz)) / 900.f, 0.f, 1.f));
  if (auto* r = regionById("npp")) p = std::max(p, std::clamp(1.2f - std::sqrt(float(x - r->cx) * (x - r->cx) + float(z - r->cz) * (z - r->cz)) / 700.f, 0.f, 1.f) * 0.8f);
  return p;
}

} // namespace dq::zone
