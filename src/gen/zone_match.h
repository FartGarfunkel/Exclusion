// gen/zone_match.h — Anomaly squads, anomaly fields, radiation, stashes and furniture fitted onto the
// regions (data/zone_match/layout.bin). Missing file: the hand-placed fields and hot spots stay.
#pragma once
#include "common/common.h"
#include <string>
#include <vector>

namespace dq::zone {

struct Plan;
struct Ctx;

struct MatchStash { int x, z, y, danger; };
struct MatchFurn { int x, z, y; u8 kind; };

bool zoneMatchLoaded();
void zoneMatchApply(Plan& P, u32 seed);          // smart terrains and anomaly fields
const std::vector<MatchStash>& zoneMatchStashes();
const std::vector<MatchFurn>& zoneMatchFurn();
float zoneMatchHot(int x, int z);                // 0 when no matched volumes are loaded
void dressMatchedFurniture(Ctx& c);

} // namespace dq::zone
