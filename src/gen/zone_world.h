// gen/zone_world.h — the Zone's layout is the user's own hand-built world (data/zone_world, exported by
// tools/zone/mc_world.py, ~12.4 x 9.1 km at 1 block = 1 m): 512 x 512 tiles of ground height, water, surface block, ground
// cover, trees and the built blocks per column, loaded on demand into a small thread-safe cache (workers only read);
// plus the coarse layers derived from them (tools/zone/mc_layers.py: heights, water, forest, open, built, road at 8 m,
// the roads as polylines, the building sites, the chest spots) for whole-map work (the planner, the PDA, A-Life).
// Heights here are band-local (their sea 62 -> our 64).
#pragma once
#include "gen/zone_map.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace dq::zone {

constexpr int MC_DY = 2;   // their y + MC_DY = our band-local y

struct WRun { u8 y0, len; u16 pal; };              // built blocks y0 .. y0+len-1 (band-local), palette entry
struct WTree { u16 x, z; u8 trunk, kind, leaves; };  // tile-local; kind 0 oak 1 spruce 2 birch 3 jungle 4 acacia 5 dark oak
struct WTile {
  int rx = 0, rz = 0;
  std::vector<std::string> names;                  // 'mc_name:data'
  std::vector<u8> ground, water, cover;            // per column (z*512+x): ground top y (0 = no data), water surface y (0 dry)
  std::vector<u16> surf;                           // palette entry of the ground's top block
  std::vector<u32> runOff;                         // runs of column i: runs[runOff[i] .. runOff[i+1])
  std::vector<WRun> runs;
  std::vector<WTree> trees;                        // sorted by 16x16 chunk
  std::vector<u32> treeOff;                        // per chunk (cz*32+cx), 1025 entries
  std::vector<u32> mapped;                         // per palette entry: our block (filled once, zone_world_blocks.cpp)
  static int at(int x, int z) { return ((z & 511) << 9) | (x & 511); }
};
// the tile holding world block column (x, z) (nullptr: no data there). Thread-safe; keep the pointer while you use it
std::shared_ptr<const WTile> worldTile(int rx, int rz);
inline std::shared_ptr<const WTile> worldTileAt(int x, int z) { return worldTile(x >> 9, z >> 9); }
bool worldHasData();                               // false: data/zone_world is missing (the Zone falls back to flat land)

// one column of the world: ground y (band-local, -1 none), water surface y (0 dry), surface block name, cover, built runs
struct WCol { int g = -1, w = 0; const std::string* surf = nullptr; u8 cover = 0; const WRun* runs = nullptr; int nruns = 0; };
bool worldColumn(const WTile& t, int x, int z, WCol& c);
bool worldColumn(int x, int z, WCol& c);           // (looks the tile up: keep a tile pointer in hot loops instead)

// coarse layers (0..1 bilinear over 8 m cells; CL_HEIGHT is band-local y)
enum CoarseLayer { CL_HEIGHT, CL_WATER, CL_WDIST, CL_FOREST, CL_OPEN, CL_BUILT, CL_ROAD, CL_COUNT };
float coarseAt(CoarseLayer l, float x, float z);
inline float coarseHeight(float x, float z) { return coarseAt(CL_HEIGHT, x, z) * 255.f; }

struct WSite { int x, z, w, d, y, h, n; std::string cls; };   // footprint box, floor y (band-local first air), height, m²
const std::vector<WSite>& worldSites();
const std::vector<Line>& worldRoads();                        // kind 1 paved, 2 gravel
const std::vector<std::array<int, 3>>& worldChests();         // x, band-local y, z

// what the user's surface / built blocks are (by their name without the data value)
bool mcNatural(const std::string& name);

} // namespace dq::zone
