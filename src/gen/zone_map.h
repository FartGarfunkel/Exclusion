// gen/zone_map.h — the Zone's layout: regions, rivers, roads, the railway and the radiation map.
// Shared by the generator (gen/zone_gen.cpp), gameplay (sim/zone_*.cpp: radiation, region names, emissions) and
// the PDA map. World x grows east, z grows SOUTH (north is -z). 1 block = 1 metre.
// The map is the user's own hand-built world (gen/zone_world.h), ~12.4 x 9.1 km at STALKER Anomaly's scale: Cordon in the
// south-west (z ~ +3500), Pripyat in the north (z ~ -2300), the plant and its cooling pond in the middle-east (-622, 912).
#pragma once
#include "common/common.h"
#include <string>
#include <vector>

namespace dq::zone {

constexpr int MIN_X = -6416, MAX_X = 6015, MIN_Z = -4480, MAX_Z = 4607;   // the user's world's bounds (data/zone_world/world.json)
constexpr int WATER = 64;          // band-local water surface
constexpr int BASE = 67;           // band-local ground level of the plain

enum Kind : u8 {
  K_WILD, K_VILLAGE, K_MARSH, K_GORGE, K_INSTITUTE, K_SCRAPYARD, K_VALLEY, K_LAKE, K_HUB, K_GRAVEYARD, K_GHOST_TOWN,
  K_MILITARY, K_FARMSTEAD, K_CLOSED_TOWN, K_RED_FOREST, K_RADAR, K_HOSPITAL, K_BACKWATER, K_JUPITER, K_CITY,
  K_GENERATORS, K_NPP, K_YARD
};

struct Region {
  const char* id;
  const char* name;
  int cx, cz, r;      // centre and radius (blocks)
  Kind kind;
  int danger;         // 1 (outer, rookies) .. 5 (the centre)
  const char* blurb;  // one line for the PDA
};

struct Pt { float x, z; };
struct Line { std::vector<Pt> pts; float width; u8 kind; };   // kind: 0 river, 1 asphalt road, 2 dirt road, 3 rail

const std::vector<Region>& regions();
const Region* regionAt(int x, int z);                 // nearest region whose radius covers (x,z), else nullptr
const Region* regionById(const std::string& id);
float regionWeight(const Region& r, int x, int z);    // 1 at the centre .. 0 at the edge
const std::vector<Line>& rivers();
const std::vector<Line>& roads();                     // the world's roads (kind 1 paved, 2 gravel) and the tracks
const std::vector<Line>& tracks();                    // the dirt tracks laid between neighbouring places the roads don't link (also in roads())
// distance from (x,z) to a line, and the parameter along it (for bridges)
float distToLine(const Line& l, float x, float z);
bool insideZone(int x, int z);
// background radiation (Sv-ish units: 0 clean .. 10+ lethal) from the region, hot spots and the plant
float radiationAt(int x, int z, u32 seed);
// psy field strength 0..1 (the Array, the closed town, the plant)
float psyAt(int x, int z);
// the start: the rookie village in Cordon, by the railway bridge
constexpr int SPAWN_X = -4210, SPAWN_Z = 3497;
// how dangerous the land is away from the regions (1 the outskirts .. 5 by the plant)
int wildDanger(int x, int z);

} // namespace dq::zone
