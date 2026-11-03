// gen/zone_map.cpp — the Zone's layout (see zone_map.h). The ground, water, roads and buildings are the user's own world
// (gen/zone_world.h); our 21 regions sit on its places (anchors from its teleport room: Cordon, Garbage, Agroprom, Dark
// Valley, Rostok, Yantar, the Military Warehouses, Dead City, Limansk, the Duga, Zaton, Yanov, Pripyat, the plant), and
// the six it hasn't got are put where there is room for our procedural builders (gen/zone_plan.cpp).
#include "gen/zone_map.h"
#include "gen/zone_layers.h"
#include "common/noise.h"
#include <cmath>
#include <algorithm>

namespace dq::zone {

static const std::vector<Region> REGIONS = {
  // ---- the south: where everyone starts ----
  {"perimeter", "Southern Perimeter", -4118, 3486, 650, K_VILLAGE, 1, "The fence, the Military checkpoint and Old Hamlet, where rookies sleep and the trader lives in a cellar."},
  {"marsh", "Reedwater Marsh", -5583, 4417, 700, K_MARSH, 1, "Reeds, peat and sunken huts round a drowned church. Fog sits on the water all day."},
  {"gorge", "Blackwater Gorge", -2600, 4100, 420, K_GORGE, 2, "A quarry cut down to black water. Bandits watch the bridge."},
  // ---- the outer ring ----
  {"institute", "Agrarian Institute", -4896, 2390, 420, K_INSTITUTE, 2, "Research halls and greenhouses over a flooded sewer. The Military lost a squad here."},
  {"scrapyard", "The Scrapyard", -4176, 2224, 450, K_SCRAPYARD, 2, "Hills of junk from the liquidation, hot with radiation. Packs of dogs den in the hangars."},
  {"hollow", "Hollow Valley", -3236, 2438, 550, K_VALLEY, 2, "A brick works in a damp valley, held by a Bandit gang."},
  // ---- the middle ----
  {"lake", "Lake Kalina", -5200, 1705, 450, K_LAKE, 3, "A still lake by an old institute bunker. Something walks the factory floors at night."},
  {"molot", "Molot Works", -4379, 1277, 450, K_HUB, 1, "A fortified steel works: the Furnace bar, a technician and Duty's guns. No shooting inside."},
  {"graveyard", "Rassokha Vehicle Graveyard", -1050, -770, 380, K_GRAVEYARD, 3, "Rows of trucks, buses and helicopters that cleaned up the first disaster. Most still glow."},
  {"sulfur", "Sulfur Town", -5532, 772, 480, K_GHOST_TOWN, 3, "An empty district town. Mercenaries run their business from the town hall."},
  {"depot", "Depot 17", -3960, 490, 500, K_MILITARY, 3, "Military warehouses, barracks and a helipad. Freedom keeps the gate."},
  {"lesnoe", "Lesnoe Farmstead", 2400, -2300, 600, K_FARMSTEAD, 3, "A collective farm on the road north. The last stop before the deep Zone."},
  // ---- the deep Zone ----
  {"zarechny", "Zarechny", -5533, -396, 550, K_CLOSED_TOWN, 4, "A closed science town across the river, never on the maps. The streets are full of anomalies."},
  {"redforest", "Red Forest", -1950, -1250, 520, K_RED_FOREST, 4, "Pines killed in one night, still rust-red. The ground is hot and the old mine goes deep."},
  {"array", "Duga Array", -1504, 3428, 650, K_RADAR, 4, "The over-horizon radar: a steel wall 150 m high. Its hum turns minds; the Monolith guard it."},
  {"sanatorium", "Abandoned Sanatorium", -5480, -1080, 300, K_HOSPITAL, 4, "A children's sanatorium in the pines, roofs fallen in."},
  {"zaton", "Zaton Backwater", -3426, -3999, 650, K_BACKWATER, 3, "River backwaters full of rusting barges. A trader runs a bar in a beached ship."},
  {"jupiter", "Jupiter Plant & Yanov Station", -4249, -3083, 550, K_JUPITER, 3, "A secret factory and the railway station beside it, a camp for stalkers going to the city."},
  {"pripyat", "Pripyat", -2862, -2400, 850, K_CITY, 5, "The city of the plant workers, empty since the evacuation. The Monolith hold the rooftops."},
  // ---- the centre ----
  {"turbines", "Turbine Fields", -450, 1750, 420, K_GENERATORS, 5, "Unfinished power blocks and generator yards wrapped in the strongest anomalies."},
  {"npp", "Chernobyl NPP", -622, 912, 600, K_NPP, 5, "The plant, the sarcophagus and the cooling pond. Where the Zone began."},
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
  return p;
}

} // namespace dq::zone
