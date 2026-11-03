// gen/zone_plan.h — the Zone's site plan for one seed: every building, landmark, camp, anomaly field and A-Life
// "smart terrain" (a place squads go to and hold), plus the terrain height function. Built once per seed (a few
// milliseconds), shared by the generator and gameplay (sim/zone_alife.cpp, sim/zone_anomaly.cpp, the trader, the
// bunker hatch). Pure and thread-safe after construction.
#pragma once
#include "gen/zone_build.h"
#include "gen/zone_map.h"
#include <unordered_map>

namespace dq::zone {

enum Faction : u8 { F_DRIFTER, F_BANDIT, F_ARMY, F_BASTION, F_OPENSKY, F_MERC, F_CHOIR, F_INSTITUTE, F_MUTANT, F_COUNT, F_NONE = 255 };
const char* factionName(int f);
const char* factionKey(int f);
// standing between two factions: -1 hostile, 0 neutral, 1 friendly
int factionRelation(int a, int b);

enum SmartKind : u8 { SM_HUB, SM_BASE, SM_CAMP, SM_RUIN, SM_LAIR, SM_FIELD, SM_POST };
struct Smart {
  int x, z, y;          // world x/z, band-local ground y
  SmartKind kind;
  u8 faction;           // who holds it at the start (F_NONE = anyone may take it)
  u8 slots;             // how many it can hold
  u8 danger;
  int region;           // index into regions(), -1 wild
  std::string name;
};

enum AnoFamily : u8 { A_THERMAL, A_ELECTRIC, A_GRAVITY, A_CHEMICAL, A_PSY, A_COUNT };
struct AnomalyField {
  int x, z, y;          // centre (y = ground, band-local)
  int r;                // blocks
  u8 family, tier;      // tier 1..5 (danger)
  u8 count;             // anomalies in the field
};

// a place to sit by a camp fire (a log or a crate round it): world x/z, band-local y of the seat block, facing the fire
struct Seat { int x, y, z; float yaw; };

struct Plan {
  std::vector<Placed> s;
  std::vector<Seat> seats;                               // every camp's seats (gen/zone_build.cpp bCamp marks the blocks {"seat":true} too)
  std::vector<Smart> smarts;
  std::vector<AnomalyField> fields;
  std::unordered_map<i64, std::vector<int>> grid;        // 64-block cell -> structures touching it
  std::unordered_map<i64, std::vector<int>> fieldGrid;   // 64-block cell -> fields touching it
  int traderX = 0, traderY = 0, traderZ = 0;             // the rookie village's trader in Cordon (world x/z, band-local y of the cellar floor)
  int hatchX = 0, hatchY = 0, hatchZ = 0;                // the player's bunker hatch
  std::vector<int> near(int x, int z, int r) const;      // structures within r of (x,z)
};

const Plan& zonePlan(u32 seed);

// the ground (band-local y of the top solid block, before buildings) and what it is made of
// cut: a steep slope down from / up to a building's pad (faced with stone)
struct Ground { float h; bool water; int waterY; u8 road; float roadD; float riverD; const Region* region; float rw; u8 cut; };
Ground groundAt(u32 seed, int x, int z);
// the same from the 8 m coarse layers (no tile loads): for whole-map work (the planner, the PDA's overview)
Ground groundCoarse(u32 seed, int x, int z);
// along a road line (roads()[li]): its length, and the point `along` blocks from its start with the direction there
float roadLength(int li);
void roadPointAt(int li, float along, float& x, float& z, float& dx, float& dz);
inline i64 cellKey(int cx, int cz) { return ((i64)cx << 32) ^ (u32)cz; }

// the anomalies of a field, one by one (deterministic): position offset, radius, family
struct Anomaly { int x, y, z; float r; u8 family; u8 tier; };
void fieldAnomalies(u32 seed, const AnomalyField& f, std::vector<Anomaly>& out);

} // namespace dq::zone
