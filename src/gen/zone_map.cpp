// gen/zone_map.cpp — the Zone's layout (see zone_map.h). The ground, water, roads and buildings are the user's own world
// (gen/zone_world.h); our 24 regions sit on its places (the STALKER names: the user's choice) (anchors from its teleport room: Cordon, Garbage, Agroprom, Dark
// Valley, Rostok, Yantar, the Military Warehouses, Dead City, Limansk, the Duga, Zaton, Yanov, Pripyat, the plant), and
// the ones it hasn't got are put where there is room for our procedural builders (gen/zone_plan.cpp).
#include "gen/zone_map.h"
#include "gen/zone_layers.h"
#include "common/noise.h"
#include <cmath>
#include <algorithm>
#include <functional>

namespace dq::zone {

static const std::vector<Region> REGIONS = {
  // ---- the south: where everyone starts ----
  {"perimeter", "Cordon", -4118, 3486, 620, K_VILLAGE, 1, "The fence, the Military checkpoint and the Rookie Village by the railway bridge, where the trader lives in a bunker."},
  {"marsh", "Great Swamps", -5583, 4417, 700, K_MARSH, 1, "Reeds, peat and hidden paths through the backwaters. Clear Sky keep a base here; mutants keep the rest."},
  {"gorge", "Darkscape", -3640, 2980, 380, K_GORGE, 2, "A long abandoned asphalt road through deep woods between Cordon and Dark Valley. Bandits watch it."},
  {"lesnoe", "Meadow", -2950, 3560, 380, K_FARMSTEAD, 2, "A small factory sector east of Cordon, grown over with grass."},
  // ---- the centre ----
  {"institute", "Agroprom", -4896, 2390, 420, K_INSTITUTE, 2, "The agricultural research institute, held by the Military. Tunnels run under it."},
  {"scrapyard", "Garbage", -4176, 2224, 450, K_SCRAPYARD, 2, "Heaps of junk from the liquidation, hot with radiation. Bandits and Loners fight over it."},
  {"hollow", "Dark Valley", -3236, 2438, 520, K_VALLEY, 2, "A factory in a damp valley, the Bandits' headquarters. Lab X-18 lies under it."},
  {"molot", "Rostok", -4379, 1277, 450, K_HUB, 1, "Duty's fortified factory and the 100 Rads Bar. No shooting inside."},
  {"wild", "Wild Territory", -3600, 1270, 360, K_YARD, 3, "An abandoned railway station and industrial yard outside Rostok. Mercenaries, anomalies, sniper nests."},
  {"lake", "Yantar", -5200, 1705, 450, K_LAKE, 3, "A dry lake bed and a swamp round the Ecologists' bunker. Lab X-16 lies under it."},
  // ---- the north ----
  {"depot", "Army Warehouses", -3960, 490, 500, K_MILITARY, 3, "Military warehouses, barracks and a helipad: Freedom's headquarters."},
  {"sulfur", "Dead City", -5532, 772, 480, K_GHOST_TOWN, 3, "An empty district town. The Mercenaries run their business from it."},
  {"graveyard", "Truck Cemetery", -1050, -770, 380, K_GRAVEYARD, 3, "Rows of trucks, buses and helicopters that cleaned up after the disaster. Most still glow; mutants nest among them."},
  {"redforest", "Red Forest", -1950, -1250, 520, K_RED_FOREST, 4, "Pines killed in one night, still rust-red. Dense, hot, hard to see in, thick with anomalies; the old mine goes deep."},
  {"zarechny", "Limansk", -5533, -396, 550, K_CLOSED_TOWN, 4, "A long closed town across the river, at war with itself. The Monolith hold the streets."},
  {"array", "Radar", -1504, 3428, 650, K_RADAR, 4, "The over-horizon radar, the Brain Scorcher: a steel wall 150 m high. Its psy field burns unprotected minds; the Monolith guard it."},
  // ---- the deep north ----
  {"jupiter", "Jupiter", -4249, -3083, 550, K_JUPITER, 3, "The giant Jupiter factory and Yanov Station beside it: Loners, Freedom and Duty on their way to the city."},
  {"zaton", "Zaton", -3426, -3999, 650, K_BACKWATER, 3, "River backwaters full of rusted ships and a waste processing station. The Skadovsk is the stalkers' hub."},
  {"outskirts", "Pripyat Outskirts", -3040, -3180, 380, K_CITY, 5, "The suburbs: high-rises, a school, the Monolith's lookouts."},
  {"pripyat", "Pripyat", -2800, -2150, 700, K_CITY, 5, "The city of the plant workers, empty since the evacuation. The Monolith hold the rooftops and the squares."},
  {"sanatorium", "Deserted Hospital", -1650, -300, 320, K_HOSPITAL, 5, "The hospital on the way from Pripyat to the plant: floor after floor of wards and stairwells, the Monolith inside."},
  {"npp", "CNPP North", -420, 800, 480, K_NPP, 5, "The reactor halls and the sarcophagus. Where the Zone began."},
  {"npp_south", "CNPP South", -450, 1650, 460, K_NPP, 5, "The plant's south side: the approach road, the switchyards and the cooling channels. The Monolith hold it."},
  {"turbines", "Generators", 4600, -3700, 650, K_GENERATORS, 5, "The far end of the Zone beyond the river: generator halls, the power-line corridors and the energy spheres."},
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

// the water is the world's own (no river lines); the roads are the world's paved and gravel surfaces traced into lines
// (tools/zone/mc_layers.py), each loose end run on to the nearest road within reach so the places are on one network
static std::vector<Line> makeRivers() { return {}; }

// the tracks laid where the world's traced roads leave two neighbouring places unlinked (kept for the generator to draw)
static std::vector<Line> g_tracks;

// every pair of neighbouring places (the level transitions) linked: the traced network's pieces are found (lines whose end
// comes within reach of another line join), and where a pair's nearest roads are in different pieces a dirt track runs
// from the one piece's road nearest the other place to the other piece's nearest road, stepping round water where a dry
// way is near (else it crosses on a plank bridge)
static void addTracks(std::vector<Line>& L) {
  const int n = (int)L.size();
  std::vector<int> par(n); for (int i = 0; i < n; i++) par[i] = i;
  std::function<int(int)> find = [&](int a) { while (par[a] != a) { par[a] = par[par[a]]; a = par[a]; } return a; };
  auto segD = [](Pt p, Pt a, Pt b) { const float ux = b.x - a.x, uz = b.z - a.z, L2 = ux * ux + uz * uz; const float t = std::clamp(L2 > 0 ? ((p.x - a.x) * ux + (p.z - a.z) * uz) / L2 : 0.f, 0.f, 1.f); return std::hypot(a.x + ux * t - p.x, a.z + uz * t - p.z); };
  for (int i = 0; i < n; i++) for (const Pt& p : {L[i].pts.front(), L[i].pts.back()})
    for (int j = 0; j < n; j++) {
      if (j == i || find(i) == find(j)) continue;
      for (size_t k = 0; k + 1 < L[j].pts.size(); k++) if (segD(p, L[j].pts[k], L[j].pts[k + 1]) < 45) { par[find(i)] = find(j); break; }
    }
  // the piece whose road comes nearest (x, z) within reach, and that point
  auto nearestRoad = [&](float x, float z, float reach, int only, Pt& at) {
    int best = -1; float bd = reach;
    for (int i = 0; i < (int)L.size(); i++) {
      if (only >= 0 && find(i) != only) continue;
      for (const Pt& q : L[i].pts) { const float d = std::hypot(q.x - x, q.z - z); if (d < bd) { bd = d; best = i; at = q; } }
    }
    return best;
  };
  static const char* PAIRS[][2] = {{"marsh", "perimeter"}, {"perimeter", "scrapyard"}, {"perimeter", "gorge"}, {"perimeter", "hollow"}, {"scrapyard", "institute"},
    {"scrapyard", "hollow"}, {"scrapyard", "molot"}, {"institute", "lake"}, {"molot", "lake"}, {"molot", "depot"}, {"depot", "sulfur"}, {"molot", "wild"}, {"wild", "depot"}, {"perimeter", "lesnoe"}, {"gorge", "hollow"},
    {"lesnoe", "gorge"}, {"sulfur", "zarechny"}, {"depot", "redforest"}, {"redforest", "graveyard"}, {"redforest", "pripyat"},
    {"outskirts", "jupiter"}, {"outskirts", "pripyat"}, {"pripyat", "jupiter"}, {"jupiter", "zaton"}, {"graveyard", "npp"}, {"pripyat", "sanatorium"}, {"sanatorium", "npp"}, {"npp", "npp_south"}, {"npp", "turbines"},
    {"npp_south", "array"}, {"array", "gorge"}};
  for (auto& pr : PAIRS) {
    const Region* A = regionById(pr[0]); const Region* B = regionById(pr[1]);
    if (!A || !B) continue;
    Pt pa{(float)A->cx, (float)A->cz}, pb{(float)B->cx, (float)B->cz};
    const int ia = nearestRoad((float)A->cx, (float)A->cz, A->r * 1.2f, -1, pa), ib = nearestRoad((float)B->cx, (float)B->cz, B->r * 1.2f, -1, pb);
    const int ca = ia >= 0 ? find(ia) : -1, cb = ib >= 0 ? find(ib) : -1;
    if (ca >= 0 && ca == cb) continue;
    // from A's piece, the road nearest B; then B's piece's road nearest that
    Pt p = pa, q = pb;
    if (ca >= 0) nearestRoad(pb.x, pb.z, 1e9f, ca, p);
    if (cb >= 0) nearestRoad(p.x, p.z, 1e9f, cb, q);
    const float len = std::hypot(q.x - p.x, q.z - p.z);
    if (len < 20) { if (ca >= 0 && cb >= 0) par[ca] = cb; continue; }
    const float dx = (q.x - p.x) / len, dz = (q.z - p.z) / len;
    Line t; t.kind = 2; t.width = 4.f; t.pts.push_back(p);
    const int k = std::max(2, (int)(len / 120));
    for (int i = 1; i < k; i++) {
      const float u = (float)i / k, off = std::min(60.f, len * 0.04f) * std::sin(u * 3.14159f) * std::sin(u * 9.f + p.x * 0.01f);
      float px = p.x + dx * len * u - dz * off, pz = p.z + dz * len * u + dx * off;
      if (layerAt(ML_WATER, px, pz) > 0.3f)
        for (int s = 1; s <= 8; s++) { const float o = (s & 1 ? 1 : -1) * 30.f * ((s + 1) / 2); if (layerAt(ML_WATER, px - dz * o, pz + dx * o) < 0.3f) { px -= dz * o; pz += dx * o; break; } }
      t.pts.push_back({std::clamp(px, (float)MIN_X + 40, (float)MAX_X - 40), std::clamp(pz, (float)MIN_Z + 40, (float)MAX_Z - 40)});
    }
    t.pts.push_back(q);
    g_tracks.push_back(t);
    L.push_back(std::move(t)); par.push_back((int)par.size());
    const int ni = (int)L.size() - 1;
    if (ca >= 0) par[find(ca)] = ni;
    if (cb >= 0) par[find(cb)] = ni;
  }
  DQ_LOG("zone map: %zu roads, %zu tracks laid between places", L.size(), g_tracks.size());
}

static std::vector<Line> makeRoads() {
  std::vector<Line> L = mapRoads();
  const float REACH = 45;
  std::vector<float> bb(L.size() * 4);
  for (size_t j = 0; j < L.size(); j++) {
    float a = 1e30f, b = -1e30f, c = 1e30f, d = -1e30f;
    for (const Pt& q : L[j].pts) { a = std::min(a, q.x); b = std::max(b, q.x); c = std::min(c, q.z); d = std::max(d, q.z); }
    bb[j * 4] = a - REACH; bb[j * 4 + 1] = b + REACH; bb[j * 4 + 2] = c - REACH; bb[j * 4 + 3] = d + REACH;
  }
  for (size_t i = 0; i < L.size(); i++) for (int end = 0; end < 2; end++) {
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
  addTracks(L);
  return L;
}

const std::vector<Line>& rivers() { static const std::vector<Line> v = makeRivers(); return v; }
const std::vector<Line>& roads() { static const std::vector<Line> v = makeRoads(); return v; }
const std::vector<Line>& tracks() { roads(); return g_tracks; }

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
  // outside the fence: the dead land round the Zone
  const int edge = std::min({x - MIN_X, MAX_X - x, z - MIN_Z, MAX_Z - z});
  if (edge < 20) rad += std::clamp((20 - edge) / 30.f, 0.f, 1.f) * 6.f;
  // hot spots: one per 192-block cell at most, more of them toward the plant
  const int cx = (int)std::floor(x / 192.0), cz = (int)std::floor(z / 192.0);
  for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
    const int gx = cx + dx, gz = cz + dz;
    const double chance = 0.12 + std::clamp((4500.0 - std::hypot(gx * 192.0 + 96 + 622, gz * 192.0 + 96 - 912)) / 9000.0, 0.0, 0.45);
    if (hashFloat((i32)seed, gx, gz, 41) > chance) continue;
    const float hx = gx * 192 + 32 + float(hashFloat((i32)seed, gx, gz, 42)) * 128, hz = gz * 192 + 32 + float(hashFloat((i32)seed, gx, gz, 43)) * 128;
    const float hr = 10 + float(hashFloat((i32)seed, gx, gz, 44)) * 22;
    const float d = std::sqrt((x - hx) * (x - hx) + (z - hz) * (z - hz));
    if (d < hr) rad += (1 - d / hr) * (0.8f + float(hashFloat((i32)seed, gx, gz, 45)) * 3.f);
  }
  return rad;
}

int wildDanger(int x, int z) {
  // by the distance to the plant, and the north is deeper than the south-west where everyone starts
  const float d = std::hypot(float(x + 622), float(z - 912)), north = std::clamp((1500.f - z) / 6000.f, 0.f, 1.f);
  return std::clamp(5 - (int)(d / 1300.f) + (int)(north * 1.5f), 1, 5);
}

float psyAt(int x, int z) {
  float p = 0;
  if (auto* r = regionById("array")) p = std::max(p, std::clamp(1.3f - std::sqrt(float(x - r->cx) * (x - r->cx) + float(z - r->cz) * (z - r->cz)) / 900.f, 0.f, 1.f));
  if (auto* r = regionById("npp")) p = std::max(p, std::clamp(1.2f - std::sqrt(float(x - r->cx) * (x - r->cx) + float(z - r->cz) * (z - r->cz)) / 700.f, 0.f, 1.f) * 0.8f);
  if (auto* r = regionById("turbines")) p = std::max(p, std::clamp(1.15f - std::sqrt(float(x - r->cx) * (x - r->cx) + float(z - r->cz) * (z - r->cz)) / 650.f, 0.f, 1.f) * 0.9f);   // the Generators' spheres
  return p;
}

} // namespace dq::zone
