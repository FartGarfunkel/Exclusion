// gen/zone_layers.h — the Zone's layout traced from a reference map (tools/zone/trace_map.py -> data/zone_map/):
// low-res layers (water, distance to water, forest, open ground, built-up ground) sampled bilinearly, the traced
// roads, and the sites (clusters of building marks). Loaded once, read-only after: safe on every worker thread.
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

} // namespace dq::zone
