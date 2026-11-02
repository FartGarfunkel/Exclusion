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
static std::vector<Line> makeRivers() { return {}; }
static std::vector<Line> makeRoads() { return mapRoads(); }

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
