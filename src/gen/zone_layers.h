// gen/zone_layers.h — the layout's coarse layers (water, distance to water, forest, open ground, built-up ground) sampled
// bilinearly, and the roads: now all from the user's world (gen/zone_world.h, data/zone_world/coarse.*). Read-only after the
// first load: safe on every worker thread. (The old traced-satellite data in data/zone_map/ is no longer read.)
#pragma once
#include "gen/zone_map.h"

namespace dq::zone {

enum MapLayer { ML_WATER, ML_WDIST, ML_FOREST, ML_OPEN, ML_BUILT, ML_COUNT };
// 0..1 at world (x, z); 0 where the map has no data (ML_WDIST: metres to water / 510)
float layerAt(MapLayer l, float x, float z);
inline float waterDistAt(float x, float z) { return layerAt(ML_WDIST, x, z) * 510.f; }

struct MapSite { float x, z, r; int n; float built, forest; std::vector<Pt> dots; };
const std::vector<MapSite>& mapSites();
// the traced roads (kind 1 asphalt, 2 dirt), world polylines
std::vector<Line> mapRoads();
// hand-traced lines (data/zone_map/lines.json): the rivers the tracer misses (kind 0, width in metres) and the railway (kind 3)
std::vector<Line> mapRivers();
std::vector<Line> mapRails();

} // namespace dq::zone
