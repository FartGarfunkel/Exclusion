// gen/zone_layers.cpp — the layout layers (zone_layers.h), now read from the user's world's coarse layers
// (gen/zone_world.h, data/zone_world/coarse.*): water, distance to water, forest, open ground, built-up ground, the roads.
#include "gen/zone_layers.h"
#include "gen/zone_world.h"

namespace dq::zone {

float layerAt(MapLayer k, float x, float z) {
  switch (k) {
    case ML_WATER: return coarseAt(CL_WATER, x, z);
    case ML_WDIST: return coarseAt(CL_WDIST, x, z);
    case ML_FOREST: return coarseAt(CL_FOREST, x, z);
    case ML_OPEN: return coarseAt(CL_OPEN, x, z);
    case ML_BUILT: return coarseAt(CL_BUILT, x, z);
    default: return 0.f;
  }
}

const std::vector<MapSite>& mapSites() { static const std::vector<MapSite> none; return none; }   // (the plan uses worldSites())
std::vector<Line> mapRoads() { return worldRoads(); }
std::vector<Line> mapRivers() { return {}; }   // the rivers are in the world's water
std::vector<Line> mapRails() { return {}; }

} // namespace dq::zone
