// gen/zone_build.h — the Zone's buildings: a placed structure (Placed), the chunk writer the builders draw through
// (Ctx), and the builder table. Builders work in the structure's own frame (lx 0..w-1 along its length, lz 0..d-1
// across it, the door side at lz = 0) and are clipped to the chunk being generated, so a building of any size is
// drawn one chunk at a time with the same result whichever chunk comes first.
#pragma once
#include "world/chunk.h"
#include "common/noise.h"
#include <vector>
#include <string>

namespace dq::zone {

enum SType : u8 {
  S_HUT, S_HOUSE2, S_PANEL, S_OFFICE, S_PUBLIC, S_HANGAR, S_FACTORY, S_CHIMNEY, S_CHECKPOINT, S_TOWER, S_WATERTOWER,
  S_CHURCH, S_BUNKER, S_COOLING, S_REACTOR, S_TURBINE_HALL, S_RADAR, S_BARGE, S_CAMP, S_WRECK, S_FENCE_RING, S_PYLON,
  S_BARN, S_HEAP, S_TRADER, S_STATION, S_HOSPITAL, S_GREENHOUSE, S_FERRIS, S_HELIPAD, S_STASH, S_HOME_HATCH, S_RUIN,
  S_COUNT
};

struct Placed {
  SType type;
  int x0, z0, W, D;     // world bounding box (x0..x0+W-1, z0..z0+D-1)
  int w, d, h;          // local length, depth, height above y
  u8 rot;               // 0..3: local -> world
  int y;                // band-local floor level (first air above the ground)
  u32 seed;
  u8 danger;            // 1..5 (loot tier, ruin)
  u8 variant;           // builder-specific (floors, vehicle kind...)
  float ruin;           // 0 whole .. 1 rubble
  bool pad = true;      // clear the footprint and lay a foundation
  bool mc = false;      // one of the user's own buildings (gen/zone_world.h): drawn from the world's tiles, not by a builder
  u8 dress = 0;         // LandmarkDress, when this user building is one of the dressed landmarks (gen/zone_interiors.cpp)
};

// which of the user's buildings get an interior pass (furniture, loot, cover). 0 is everyone else.
enum LandmarkDress : u8 {
  LD_NONE = 0,
  LD_HOUSE,     // Rookie Village houses
  LD_HANGAR,    // Garbage hangar
  LD_BAR,       // 100 Rads
  LD_ECO,       // Ecologists' bunker, when it is one of the user's buildings
  LD_STATION,   // Yanov, when it is one of the user's buildings
  LD_SHIP,      // Skadovsk, when it is one of the user's buildings
  LD_BASE,      // Army Warehouses: Freedom's base and the checkpoint beside it
  LD_SCHOOL,    // Pripyat school
  LD_INSTITUTE, // Agroprom research institute
  LD_WORKS,     // Dark Valley bandit works
  LD_YARD,      // Wild Territory station
  LD_FORESTER,  // Red Forest forester's hut
  LD_FARM,      // Army Warehouses farm shed
  LD_BARRACKS,  // a house inside Freedom's compound
  LD_BLOCK,     // Pripyat city blocks
  LD_LOOKOUT    // Pripyat outskirts: Monolith lookout floors
};

// the block palette (looked up once; a missing key falls back to stone)
struct Pal {
  BlockId stone, dirt, grass, coarse, podzol, litter, dryDirt, mud, peat, moss, sand, gravel, water, bedrock, andesite,
      asphalt, crack, curb, path, concrete, concrete2, wconcrete, smooth, slab, brick, stoneb, crackedb, cobble, mossy,
      plank, plank2, log, birchLog, pineLog, charred, pineLeaves, birchLeaves, deadLeaves, glass, bars, chain, ladder, rail,
      steel, iron, rust, rust2, rust3, roofTiles, table, chair, cabinet, shelf, bookcase, bed, sleeping, campfire, barrel,
      chest, anvil, furnace, cauldron, cobweb, puddle, tallGrass, dryGrass, fern, deadBush, sandbag, greenWool, brownWool,
      lantern, weaponRack, itemFrame, hay, terracotta, redConcrete, whiteConcrete, green, glowstone, looseStones, magma;
  // the Zone kit (tools/zone/zone_blocks.py): surfaces, walls, floors
  BlockId zAsphalt, zAsphaltCr, zPaving, zPanel, zPanelTiles, zPlaster, zPaintGreen, zWhitewash, zBrick, zTiles, zConcMoss, zFence,
      zFloorLino, zFloorTiles, zPlanksRot, zCorrGreen, zRoofRust, zSteelGreen, zLitter,
  // panes, overlays, power lines
      zChain, zChainPost, zWindow, zMoss, zCableX, zCableZ,
  // trees and undergrowth (trunks are slim branch-shaped blocks: meta 1 = thinner)
      zBirchTrunk, zPineTrunk, zDeadTrunk, zBurntTrunk, zBirchLeaves, zPineNeedles, zRedNeedles, zTwigs, zTallGrass, zShrub, zReeds, zRebar, zBarbed,
  // props (models; the facing ones take meta 0..3)
      zDebris, zBarrel, zBarrelBlue, zCrate, zTyres, zSandbags, zBarrier, zPipe, zHeatPipe, zHeatPipeSup, zRadiator, zBedFrame, zPole, zPoleTop,
      zLampPost, zLampHead, zFlood, zFallen, zSignRad,
  // the overgrowth (double plants: meta 1 on the upper half)
      zDandelion, zFlowers, zWeeds, tallGrass2, largeFern, cattail, daisy, bushLeaves;
  void init();
};

struct Ctx {
  Chunk& ch;
  int ox, oz;           // world x/z of the chunk's corner
  const Pal& B;
  u32 seed;
  void put(int wx, int y, int wz, BlockId id, u8 meta = 0) {
    const int x = wx - ox, z = wz - oz;
    if ((unsigned)x < 16 && (unsigned)z < 16 && (unsigned)y < 256) { ch.set(x, y, z, id); ch.setMeta(x, y, z, meta); }
  }
  BlockId get(int wx, int y, int wz) const {
    const int x = wx - ox, z = wz - oz;
    return ((unsigned)x < 16 && (unsigned)z < 16) ? ch.get(x, y, z) : AIR;
  }
  bool inChunk(int wx, int wz) const { return (unsigned)(wx - ox) < 16 && (unsigned)(wz - oz) < 16; }
  void entity(int wx, int y, int wz, json j) { if (inChunk(wx, wz) && (unsigned)y < 256) ch.blockEntities[colIdx(wx - ox, y, wz - oz)] = std::move(j); }
  // chisel the block there (world octant mask, bit (hx<<2)|(hy<<1)|hz = that eighth cut away; after its put())
  void carve(int wx, int y, int wz, u8 m) { const int x = wx - ox, z = wz - oz; if ((unsigned)x < 16 && (unsigned)z < 16 && (unsigned)y < 256 && ch.get(x, y, z) != AIR) ch.setCarve(x, y, z, m); }
};

// octant masks in a structure's local frame (L::carve turns them to the world's): halves and quarters of a block
namespace oct {
constexpr u8 TOP = 0xCC, BOT = 0x33, X0 = 0x0F, X1 = 0xF0, Z0 = 0x55, Z1 = 0xAA;
}

// local frame of one structure
struct L {
  Ctx& c; const Placed& p;
  int lx0 = 0, lx1 = -1, lz0 = 0, lz1 = -1;   // the part of the footprint inside this chunk (local, inclusive)
  bool col(int lx, int lz) const { return lx >= lx0 && lx <= lx1 && lz >= lz0 && lz <= lz1; }
  void world(int lx, int lz, int& wx, int& wz) const {
    switch (p.rot & 3) {
      case 0: wx = p.x0 + lx; wz = p.z0 + lz; break;
      case 1: wx = p.x0 + (p.d - 1 - lz); wz = p.z0 + lx; break;
      case 2: wx = p.x0 + (p.w - 1 - lx); wz = p.z0 + (p.d - 1 - lz); break;
      default: wx = p.x0 + lz; wz = p.z0 + (p.w - 1 - lx); break;
    }
  }
  void put(int lx, int y, int lz, BlockId id, u8 meta = 0) { int wx, wz; world(lx, lz, wx, wz); c.put(wx, p.y + y, wz, id, meta); }
  // chisel the block at a local spot: `m` cut away, in local octants (oct::); a put() there afterwards undoes it
  void carve(int lx, int y, int lz, u8 m) {
    if (!m || m == 0xFF) return;
    u8 w = 0;
    for (int b = 0; b < 8; b++) if (m >> b & 1) {
      const int hx = b >> 2 & 1, hy = b >> 1 & 1, hz = b & 1;
      const int r = p.rot & 3, X = r == 0 ? hx : r == 1 ? 1 - hz : r == 2 ? 1 - hx : hz, Z = r == 0 ? hz : r == 1 ? hx : r == 2 ? 1 - hz : 1 - hx;
      w |= (u8)(1u << ((X << 2) | (hy << 1) | Z));
    }
    int wx, wz; world(lx, lz, wx, wz); c.carve(wx, p.y + y, wz, w);
  }
  void put(int lx, int y, int lz, BlockId id, u8 meta, u8 cut) { put(lx, y, lz, id, meta); carve(lx, y, lz, cut); }
  BlockId get(int lx, int y, int lz) const { int wx, wz; world(lx, lz, wx, wz); return c.get(wx, p.y + y, wz); }
  void entity(int lx, int y, int lz, json j) { int wx, wz; world(lx, lz, wx, wz); c.entity(wx, p.y + y, wz, std::move(j)); }
  bool in(int lx, int lz) const { int wx, wz; world(lx, lz, wx, wz); return c.inChunk(wx, wz); }
  void box(int x0, int y0, int z0, int x1, int y1, int z1, BlockId id, u8 meta = 0);      // filled
  void shell(int x0, int y0, int z0, int x1, int y1, int z1, BlockId id);                // walls only (no floor/roof)
  void clear(int x0, int y0, int z0, int x1, int y1, int z1);
  // a loot spot at local (lx, y, lz) on the floor (gen/zone_loot.cpp, STALKER-style, never a chest): by the room it picks
  // furniture to search (wardrobe, fridge, locker, desk, cupboard, bedside, filing cabinet, toolbox, weapon / ammo case,
  // first-aid box), breakable crates (sometimes stacked) or things lying about (item piles on the floor, a table, a shelf).
  // `kind` picks the table (crate, locker, military, medical, safe, stash); `block` forces that container block; `room`
  // (optional): kitchen, bedroom, office, workshop, military, medical, outdoor, stash (else guessed from the building)
  void loot(int lx, int y, int lz, const char* kind, BlockId block = 0, const char* room = nullptr);
  double rnd(int a, int b = 0, int c = 0) const { return hashFloat((i32)p.seed, a, b, c); }
  // a hashed "this block is missing" for ruined buildings
  bool broken(int lx, int y, int lz, float extra = 0) const { return hashFloat((i32)p.seed ^ 0x5bd1e995, lx, y, lz) < p.ruin * 0.35f + extra; }
  // ladder meta that faces into the local frame direction (0 = toward +lz ... matches the engine's ladder metas)
  u8 face(int localFacing) const { return (u8)((localFacing + p.rot) & 3); }
};

// S_WRECK.variant: kind in the low 3 bits, lie in the next 4. Length runs along local +x, the nose at x = L-1
// (rot 0 nose east, 1 south, 2 west, 3 north). Side lies swap the upright height into the depth.
enum WreckKind : u8 { WK_CAR, WK_TRUCK, WK_BUS, WK_APC, WK_HELI, WK_TRACTOR, WK_LOCO };
enum WreckLie : u8 { WL_OK, WL_BURNT, WL_STRIPPED, WL_SIDE_L, WL_SIDE_R, WL_ROOF, WL_DITCH, WL_SUNK, WL_STACK, WL_JACK };
inline int wreckVar(int kind, int lie) { return (kind & 7) | ((lie & 15) << 3); }
inline int wreckKindOf(int v) { return v & 7; }
inline int wreckLieOf(int v) { return (v >> 3) & 15; }
inline void wreckDims(int kind, int& L, int& W, int& H) {
  static const int D[7][3] = {{5, 3, 2}, {8, 3, 3}, {11, 4, 4}, {8, 4, 3}, {14, 5, 5}, {5, 3, 3}, {16, 4, 5}};
  if (kind < 0 || kind > 6) kind = 0;
  L = D[kind][0]; W = D[kind][1]; H = D[kind][2];
}
inline void wreckFoot(int kind, int lie, int& w, int& d) {
  int L, W, H; wreckDims(kind, L, W, H);
  if (lie == WL_SIDE_L || lie == WL_SIDE_R) { w = L; d = H; }
  else if (lie == WL_JACK) { w = L; d = W + 1; }
  else { w = L; d = W; }
}

void buildStructure(Ctx& c, const Placed& p);
// X-16, X-18 and Agroprom Underground (Placed::variant 3, 2, 1) fill the room in gen/zone_interiors.cpp.
// The Red Forest mine, the Radar bunker and Lab X-2 (variants 4, 5, 6) fill it in gen/zone_build.cpp.
// Variant 0 keeps the shared green room. The shell, the hut and the cleared room are already drawn; sx, sz is the ladder column.
bool bunkerSpecial(L& q, int deep, int sx, int sz);
// loot rolled for a container of this kind and danger: 27 slots of item json (nulls for empty)
json zoneLoot(const std::string& kind, int danger, Mulberry32& r);

} // namespace dq::zone
