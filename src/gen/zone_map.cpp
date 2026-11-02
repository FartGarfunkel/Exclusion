// gen/zone_map.cpp — the Zone's layout (see zone_map.h). The arrangement follows the reference maps of the
// whole Zone (outer marsh/perimeter/gorge in the south, the institute, scrapyard and valley, the factory hub, the
// depot, the red forest and the array, the backwater and the city, the plant at the top), stretched out to
// ~13 x 28 km so every area is many times bigger than in the games. Real places keep real names; the rest are ours.
#include "gen/zone_map.h"
#include "common/noise.h"
#include <cmath>
#include <algorithm>

namespace dq::zone {

static const std::vector<Region> REGIONS = {
  // ---- the south: where everyone starts ----
  {"perimeter", "Southern Perimeter", 0, 12000, 1700, K_VILLAGE, 1, "The fence, the army checkpoint and Old Hamlet, where rookies sleep and the trader lives in a cellar."},
  {"marsh", "Reedwater Marsh", -4300, 11300, 1800, K_MARSH, 1, "Reeds, peat and sunken huts round a drowned church. Fog sits on the water all day."},
  {"gorge", "Blackwater Gorge", 4300, 11000, 1600, K_GORGE, 2, "A quarry cut down to black water. Bandits watch the bridge."},
  // ---- the outer ring ----
  {"institute", "Agrarian Institute", -3600, 7600, 1400, K_INSTITUTE, 2, "Research halls and greenhouses over a flooded sewer. The army lost a squad here."},
  {"scrapyard", "The Scrapyard", 0, 8000, 1500, K_SCRAPYARD, 2, "Hills of junk from the liquidation, hot with radiation. Packs of dogs den in the hangars."},
  {"hollow", "Hollow Valley", 4200, 7400, 1600, K_VALLEY, 2, "A brick works in a damp valley, held by the Wolves gang."},
  // ---- the middle ----
  {"lake", "Lake Kalina", -4100, 3900, 1300, K_LAKE, 3, "A still lake by an old institute bunker. Something walks the factory floors at night."},
  {"molot", "Molot Works", -900, 4100, 1400, K_HUB, 1, "A fortified steel works: the Furnace bar, a technician and the Bastion faction's guns. No shooting inside."},
  {"graveyard", "Rassokha Vehicle Graveyard", 3700, 3600, 1500, K_GRAVEYARD, 3, "Rows of trucks, buses and helicopters that cleaned up the first disaster. Most still glow."},
  {"sulfur", "Sulfur Town", -4500, 300, 1400, K_GHOST_TOWN, 3, "An empty district town. Contractors run their business from the town hall."},
  {"depot", "Depot 17", 600, 0, 1500, K_MILITARY, 3, "Army warehouses, barracks and a helipad. Open Sky keeps the gate."},
  {"lesnoe", "Lesnoe Farmstead", 4500, 400, 1400, K_FARMSTEAD, 3, "A collective farm on the road north. The last stop before the deep Zone."},
  // ---- the deep Zone ----
  {"zarechny", "Zarechny", -4500, -4100, 1600, K_CLOSED_TOWN, 4, "A closed science town across the river, never on the maps. The streets are full of anomalies."},
  {"redforest", "Red Forest", -1400, -4300, 1600, K_RED_FOREST, 4, "Pines killed in one night, still rust-red. The ground is hot and the old mine goes deep."},
  {"array", "Duga Array", 2600, -4400, 1600, K_RADAR, 4, "The over-horizon radar: a steel wall 150 m high. Its hum turns minds; the Choir guard it."},
  {"sanatorium", "Abandoned Sanatorium", -4700, -8100, 1200, K_HOSPITAL, 4, "A children's sanatorium in the pines, roofs fallen in."},
  {"zaton", "Zaton Backwater", -1600, -8000, 1500, K_BACKWATER, 3, "River backwaters full of rusting barges. A trader runs a bar in a beached ship."},
  {"jupiter", "Jupiter Plant & Yanov Station", 300, -7300, 1400, K_JUPITER, 3, "A secret factory and the railway station beside it, a camp for stalkers going to the city."},
  {"pripyat", "Pripyat", 3300, -8600, 2000, K_CITY, 5, "The city of the plant workers, empty since the evacuation. The Choir hold the rooftops."},
  // ---- the centre ----
  {"turbines", "Turbine Fields", -3800, -12300, 1500, K_GENERATORS, 5, "Unfinished power blocks and generator yards wrapped in the strongest anomalies."},
  {"npp", "Chernobyl NPP", 1200, -12500, 1800, K_NPP, 5, "The plant, the sarcophagus and the cooling pond. Where the Zone began."},
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

static Pt C(const char* id) { auto* r = regionById(id); return {float(r->cx), float(r->cz)}; }

// a gently winding line through the given points: each leg gets a hashed sideways bend at its middle
static Line wind(std::vector<Pt> pts, float width, u8 kind, u32 salt) {
  Line l; l.width = width; l.kind = kind;
  for (size_t i = 0; i < pts.size(); i++) {
    l.pts.push_back(pts[i]);
    if (i + 1 < pts.size()) {
      const Pt a = pts[i], b = pts[i + 1];
      const float dx = b.x - a.x, dz = b.z - a.z, len = std::sqrt(dx * dx + dz * dz);
      if (len < 300) continue;
      const int n = std::max(1, int(len / 420));
      for (int k = 1; k <= n; k++) {
        const float t = float(k) / (n + 1);
        const float bend = (float(hashFloat((i32)salt, (i32)i, k, 7)) - 0.5f) * std::min(260.f, len * 0.16f);
        l.pts.push_back({a.x + dx * t - dz / len * bend, a.z + dz * t + dx / len * bend});
      }
    }
  }
  return l;
}

static std::vector<Line> makeRivers() {
  std::vector<Line> v;
  // the Pripyat: in from the north-west, past the plant's north side, then down the east edge
  v.push_back(wind({{-6700, -13900}, {-2600, -13700}, {600, -14150}, {4200, -13300}, {5900, -11000}, {5700, -8200}, {6150, -5200}, {6750, -2200}}, 70, 0, 11));
  // the Veresnya: from the west, past Zarechny, through the backwaters into the Pripyat
  v.push_back(wind({{-6700, -2500}, {-5000, -3300}, {-3700, -5100}, {-2700, -6700}, {-1700, -7900}, {-900, -10400}, {-1300, -13700}}, 34, 0, 12));
  // the gorge's black stream and the marsh's channel south
  v.push_back(wind({{6700, 9200}, {5200, 10400}, {4300, 11000}, {3600, 12300}, {3900, 13900}}, 22, 0, 13));
  v.push_back(wind({{-6700, 9800}, {-5100, 10900}, {-4200, 11500}, {-3000, 12600}, {-2600, 13900}}, 18, 0, 14));
  // the stream from Lake Kalina south to the marsh
  v.push_back(wind({{-4100, 4300}, {-4500, 6200}, {-4900, 8800}, {-5100, 10900}}, 10, 0, 15));
  return v;
}

static std::vector<Line> makeRoads() {
  std::vector<Line> v;
  auto asphalt = [&](std::vector<Pt> p, u32 s) { v.push_back(wind(std::move(p), 7, 1, s)); };
  auto dirt = [&](std::vector<Pt> p, u32 s) { v.push_back(wind(std::move(p), 4, 2, s)); };
  // the main highway, the checkpoint to the plant
  asphalt({{0, 13820}, C("perimeter"), C("scrapyard"), C("molot"), C("depot"), {2200, -1800}, C("array"), {3100, -6400}, C("pripyat"), {2400, -10400}, C("npp")}, 100);
  // the north road from the farmstead to the city
  asphalt({C("lesnoe"), {4600, -3000}, {4200, -6200}, C("pripyat")}, 101);
  asphalt({C("jupiter"), C("pripyat")}, 102);
  asphalt({C("zaton"), C("jupiter")}, 103);
  // dirt roads
  dirt({C("perimeter"), C("marsh")}, 200);
  dirt({C("perimeter"), C("gorge")}, 201);
  dirt({C("marsh"), C("institute")}, 202);
  dirt({C("institute"), C("scrapyard")}, 203);
  dirt({C("scrapyard"), C("hollow")}, 204);
  dirt({C("gorge"), C("hollow")}, 205);
  dirt({C("institute"), C("lake")}, 206);
  dirt({C("molot"), C("lake")}, 207);
  dirt({C("hollow"), C("graveyard")}, 208);
  dirt({C("graveyard"), C("lesnoe")}, 209);
  dirt({C("lake"), C("sulfur")}, 210);
  dirt({C("depot"), C("sulfur")}, 211);
  dirt({C("depot"), C("lesnoe")}, 212);
  dirt({C("depot"), C("redforest")}, 213);
  dirt({C("sulfur"), C("zarechny")}, 214);
  dirt({C("zarechny"), C("redforest")}, 215);
  dirt({C("redforest"), C("jupiter")}, 216);
  dirt({C("zarechny"), C("sanatorium")}, 217);
  dirt({C("sanatorium"), C("turbines")}, 218);
  dirt({C("sanatorium"), C("zaton")}, 219);
  dirt({C("turbines"), C("npp")}, 220);
  dirt({C("molot"), C("graveyard")}, 221);
  // the railway: in from the south, through Yanov, to the plant
  v.push_back(wind({{2300, 13820}, {1700, 10500}, {900, 6500}, {1800, 2600}, {1300, -1500}, {900, -5000}, {500, -7300}, {2000, -10300}, {1500, -12200}}, 4, 3, 300));
  return v;
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
