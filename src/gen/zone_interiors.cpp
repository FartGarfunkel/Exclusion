// gen/zone_interiors.cpp — the three story labs (Agroprom Underground, Lab X-18, Lab X-16), furniture in the
// named landmark interiors, and one rail line on the Cordon embankment. Existing blocks only: the stock rail,
// puddles (they do not flow), concrete, the Zone kit. The shared bunker room stays as it was for every other hatch.
// The next band fills the institute, the bandit works, the Wild Territory station, the forester's hut, Freedom's
// compound and the warehouses' farm and village, and Pripyat blocks past the school.
// The 100 Rads bar is the Rostok hall at (-4498, 1324). dressBar lays that room out. The scatter cannot:
// the ceiling is one block past what findFloor will call a room, so the first pass left a blank shed.
#include "gen/zone_plan.h"
#include "gen/zone_world.h"
#include "core/registry.h"
#include "world/dirs.h"
#include "common/common.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace dq::zone {
namespace {

bool passage(BlockId b) {
  const u8 s = reg().tShape[b];
  return s == ShapeKind::Door || s == ShapeKind::Ladder || s == ShapeKind::Trapdoor || s == ShapeKind::Stairs;
}
bool isOpen(BlockId b) {
  if (b == AIR) return true;
  const Registry& R = reg();
  return R.tReplaceable[b] && !R.tLiquid[b];
}
bool isFloor(BlockId b) {
  if (b == AIR) return false;
  const Registry& R = reg();
  if (R.tLiquid[b] || passage(b)) return false;
  return R.tSolid[b] || R.tShape[b] == ShapeKind::Slab;
}

// ---------------------------------------------------------------- the three labs
// The shell, floor and cleared room are already drawn. Shaft (sx, sz) is cleared and laddered afterwards.
// Water is a source and spreads sideways through anything that is not solid, so the Agroprom bay is a sealed
// tank one block deep. Puddles are the wet floor everywhere else. The rail is the stock block (meta 1 runs east-west).

void labAgroprom(L& q, int deep) {
  const Pal& B = q.c.B;
  // flooded bay, east of the dry hall: a solid floor and walls, so the source cannot fall or seep, bars and a two-wide door facing the hall
  q.box(17, deep - 1, 9, 27, deep - 1, 14, B.concrete);
  q.box(17, deep, 9, 17, deep + 2, 14, B.concrete);
  q.box(27, deep, 9, 27, deep + 2, 14, B.concrete);
  q.box(18, deep, 14, 26, deep + 2, 14, B.concrete);
  q.box(18, deep, 9, 26, deep, 9, B.concrete);
  q.box(18, deep, 10, 26, deep, 13, B.water);
  q.box(18, deep + 1, 9, 26, deep + 2, 9, B.bars);
  q.clear(22, deep + 1, 9, 23, deep + 2, 9);
  // a rail hint along the dry spine, off the shaft (z = 6) and two rows short of the tank
  for (int x = 2; x <= 26; x++) {
    q.put(x, deep - 1, 5, B.gravel);
    q.put(x, deep, 5, B.rail, 1);
  }
  if (B.puddle) for (int x : {4, 8, 16, 22, 26}) { q.put(x, deep, 3, B.puddle); q.put(x, deep, 7, B.puddle); }
  for (int x = 2; x <= 26; x += 2) q.put(x, deep + 3, 1, B.zPipe);
  q.loot(6, deep, 2, "stash");
  q.loot(4, deep, 10, "locker", 0, "military");
  q.loot(4, deep, 12, "safe", 0, "office");
  q.loot(12, deep, 12, "military");
  q.put(9, deep, 8, B.zSandbags ? B.zSandbags : B.sandbag);
  q.put(10, deep, 8, B.zSandbags ? B.zSandbags : B.sandbag);
  q.put(10, deep, 3, B.table);
  q.put(12, deep, 11, B.table);
  q.put(15, deep, 3, B.zBarrel ? B.zBarrel : B.barrel);
}

void labX18(L& q, int deep, int sx, int sz) {
  const Pal& B = q.c.B;
  // four white cells along the south wall, barred fronts, a gurney in the second
  for (int x : {1, 7, 13, 19, 25}) q.box(x, deep, 1, x, deep + 3, 5, B.zWhitewash);
  q.box(2, deep - 1, 1, 6, deep - 1, 4, B.wconcrete);
  q.box(8, deep - 1, 1, 12, deep - 1, 4, B.wconcrete);
  q.box(14, deep - 1, 1, 18, deep - 1, 4, B.wconcrete);
  q.box(20, deep - 1, 1, 24, deep - 1, 4, B.wconcrete);
  q.box(1, deep, 5, 25, deep + 2, 5, B.bars);
  for (int x : {3, 4, 9, 10, 15, 16, 21, 22}) q.clear(x, deep, 5, x, deep + 1, 5);
  q.clear(sx, deep, 5, sx, deep + 2, 5);   // the shaft sits just north of this grate
  q.loot(3, deep, 2, "medical", 0, "medical");
  q.loot(9, deep, 2, "medical", 0, "medical");
  q.loot(15, deep, 2, "medical", 0, "medical");
  q.loot(21, deep, 2, "medical", 0, "medical");
  q.put(5, deep, 4, B.cauldron);
  q.put(11, deep, 4, B.cauldron);
  q.put(17, deep, 4, B.cauldron);
  q.put(23, deep, 4, B.cauldron);
  const BlockId bed = B.zBedFrame ? B.zBedFrame : B.sleeping;
  if (bed) { q.put(8, deep, 3, bed, q.face(1)); q.put(9, deep, 3, bed, (u8)(q.face(1) | 4)); }
  // the hall, and a document room off its east end. Doors in the cross-wall line up with the cell doors.
  q.box(1, deep - 1, 7, 28, deep - 1, 13, B.zFloorLino);
  q.box(1, deep, 6, 28, deep + 3, 6, B.zWhitewash);
  for (int x : {3, 4, 9, 10, 15, 16, 21, 22}) q.clear(x, deep, 6, x, deep + 1, 6);
  q.clear(sx, deep, sz, sx, deep + 3, sz);
  q.box(24, deep, 7, 24, deep + 3, 13, B.concrete2);
  q.clear(24, deep, 9, 24, deep + 1, 10);
  q.loot(26, deep, 12, "safe", 0, "office");
  q.loot(18, deep, 9, "medical", 0, "medical");
  q.put(28, deep, 8, B.redConcrete);
  q.put(28, deep + 1, 8, B.redConcrete);
  q.put(28, deep + 2, 8, B.glowstone);
  q.put(6, deep, 10, B.table);
  q.put(16, deep, 11, B.table);
  q.put(10, deep, 12, B.zBarrelBlue ? B.zBarrelBlue : B.barrel);
  q.put(20, deep, 8, B.zBarrelBlue ? B.zBarrelBlue : B.barrel);
  for (int x : {6, 12, 18}) q.put(x, deep + 3, 10, B.glowstone);
}

void labX16(L& q, int deep) {
  const Pal& B = q.c.B;
  // a dark control room on the west, a barred cage for the installation, steel posts down the hall
  q.box(8, deep, 2, 8, deep + 3, 12, B.concrete2);
  q.clear(8, deep, 7, 8, deep + 1, 8);
  q.loot(3, deep, 4, "safe", 0, "office");
  q.loot(5, deep, 10, "medical", 0, "medical");
  q.loot(4, deep, 7, "locker", 0, "military");
  q.put(3, deep, 8, B.table);
  q.put(6, deep, 2, B.cabinet);
  for (int x = 16; x <= 22; x++) for (int y = deep; y <= deep + 2; y++) { q.put(x, y, 5, B.bars); q.put(x, y, 11, B.bars); }
  for (int z = 6; z <= 10; z++) for (int y = deep; y <= deep + 2; y++) { q.put(16, y, z, B.bars); q.put(22, y, z, B.bars); }
  q.clear(18, deep, 5, 19, deep + 1, 5);
  for (int y = deep; y <= deep + 3; y++) for (int x : {16, 22}) for (int z : {5, 11}) q.put(x, y, z, B.steel);
  q.put(19, deep, 8, B.glowstone);
  q.put(19, deep + 1, 8, B.glowstone);
  q.put(19, deep + 2, 8, B.steel);
  q.box(17, deep - 1, 6, 21, deep - 1, 10, B.steel);
  for (int y = deep; y <= deep + 3; y++) for (int x : {10, 24}) for (int z : {3, 12}) q.put(x, y, z, B.steel);
  const BlockId bags = B.zSandbags ? B.zSandbags : B.sandbag;
  q.put(11, deep, 4, bags);
  q.put(11, deep, 5, bags);
  q.put(14, deep, 4, B.zCrate ? B.zCrate : B.barrel);
  q.put(24, deep, 10, B.zBarrel ? B.zBarrel : B.barrel);
  q.loot(12, deep, 10, "military");
  q.loot(26, deep, 6, "military");
  q.put(6, deep + 3, 5, B.redConcrete);
  q.put(14, deep + 3, 8, B.redConcrete);
  q.put(24, deep + 3, 8, B.redConcrete);
}

// ---------------------------------------------------------------- landmark interiors
// User buildings are already in the world. This only fills air on the indoor floor nearest the site's y
// (that y sits three blocks above the ground the layers measured, so a floor at or below it wins over a
// cellar and over the next storey). Yards and roofs stay empty. One piece per stride cell.

int findFloor(const Chunk& ch, int x, int z, int py) {
  const int lo = std::max(1, py - 8), hi = std::min(252, py + 6);
  int best = -1, bestD = 999; bool below = false;
  for (int y = lo; y <= hi; y++) {
    if (!isFloor(ch.get(x, y - 1, z))) continue;
    if (!isOpen(ch.get(x, y, z)) || !isOpen(ch.get(x, y + 1, z))) continue;
    bool ceil = false;
    for (int k = 2; k <= 8 && y + k < 256; k++) {
      const BlockId b = ch.get(x, y + k, z);
      if (reg().tSolid[b] && !reg().tLiquid[b]) { ceil = true; break; }
    }
    if (!ceil) continue;
    const int d = std::abs(y - py); const bool under = y <= py;
    if (best < 0 || (under && !below) || (under == below && d < bestD)) { best = y; bestD = d; below = under; }
  }
  return best;
}

bool safeCell(const Ctx& c, int wx, int y, int wz) {
  const int x = wx - c.ox, z = wz - c.oz;
  if (x < 1 || x > 14 || z < 1 || z > 14) return false;
  auto bad = [&](int X, int Y, int Z) {
    if ((unsigned)X >= 16 || (unsigned)Z >= 16 || (unsigned)Y >= 256) return true;
    return passage(c.ch.get(X, Y, Z));
  };
  for (int yy = y - 1; yy <= y + 1; yy++) {
    if (bad(x, yy, z)) return false;
    for (int k = 0; k < 4; k++) if (bad(x + DIRX[k], yy, z + DIRZ[k])) return false;
  }
  return true;
}

bool openAt(const Ctx& c, int wx, int y, int wz) {
  if (!c.inChunk(wx, wz) || (unsigned)y >= 256) return false;
  return isOpen(c.ch.get(wx - c.ox, y, wz - c.oz));
}
bool cellOk(const Ctx& c, const Placed& p, int wx, int y, int wz, int inset) {
  if (wx < p.x0 + inset || wx > p.x0 + p.W - 1 - inset || wz < p.z0 + inset || wz > p.z0 + p.D - 1 - inset) return false;
  return openAt(c, wx, y, wz) && openAt(c, wx, y + 1, wz) && safeCell(c, wx, y, wz);
}

bool placeBed(L& q, const Ctx& c, const Placed& p, int lx, int lz, int inset) {
  const BlockId id = c.B.zBedFrame ? c.B.zBedFrame : c.B.sleeping;
  if (!id) return false;
  static const int DX[4] = {1, -1, 0, 0}, DZ[4] = {0, 0, 1, -1}, LF[4] = {1, 3, 2, 0};
  const int wx = p.x0 + lx, wz = p.z0 + lz;
  for (int k = 0; k < 4; k++) {
    if (!cellOk(c, p, wx + DX[k], p.y, wz + DZ[k], inset)) continue;
    q.put(lx, 0, lz, id, q.face(LF[k]));
    q.put(lx + DX[k], 0, lz + DZ[k], id, (u8)(q.face(LF[k]) | 4));
    return true;
  }
  return false;
}

bool placeKit(L& q, const Ctx& c, const Placed& p, int inset, int wx, int wz, double h) {
  const Pal& B = c.B;
  const int lx = wx - p.x0, lz = wz - p.z0;
  auto put = [&](BlockId id, u8 meta = 0) { if (!id) return false; q.put(lx, 0, lz, id, meta); return true; };
  auto table = [&]() {
    if (!put(B.table)) return false;
    if (!B.chair) return true;
    if (cellOk(c, p, wx + 1, p.y, wz, inset)) q.put(lx + 1, 0, lz, B.chair);
    else if (cellOk(c, p, wx, p.y, wz + 1, inset)) q.put(lx, 0, lz + 1, B.chair);
    return true;
  };
  auto bed = [&]() { return placeBed(q, c, p, lx, lz, inset) || put(B.cabinet); };
  auto bags = [&](bool tall) {
    const BlockId id = B.zSandbags ? B.zSandbags : B.sandbag;
    if (!put(id)) return false;
    if (tall && openAt(c, wx, p.y + 1, wz)) q.put(lx, 1, lz, id);
    return true;
  };
  auto pile = [&](BlockId id) { return put(id ? id : B.barrel); };
  auto lootAt = [&](const char* kind, const char* room) { q.loot(lx, 0, lz, kind, 0, room); return true; };
  switch (p.dress) {
    case LD_HOUSE:
      if (h < 0.28) return bed();
      if (h < 0.48) return table();
      if (h < 0.66) return lootAt("locker", "bedroom");
      if (h < 0.84) return lootAt("crate", "kitchen");
      return pile(B.zCrate);
    case LD_HANGAR:
      if (h < 0.30) return bags(false);
      if (h < 0.48) return pile(B.zCrate);
      if (h < 0.62) return pile(B.zBarrel);
      if (h < 0.82) return lootAt("locker", "workshop");
      return table();
    case LD_BAR: // a generated fallback. The Rostok hall is dressBar, not this scatter.
      if (h < 0.22) return put(B.slab);
      if (h < 0.48) return table();
      if (h < 0.64) return pile(B.zBarrel);
      if (h < 0.86) return lootAt("crate", "kitchen");
      return pile(B.zCrate);
    case LD_ECO:
      if (h < 0.22) return bed();
      if (h < 0.40) return lootAt("medical", "medical");
      if (h < 0.58) return lootAt("safe", "office");
      if (h < 0.74) return put(B.cabinet);
      if (h < 0.88) return pile(B.zBarrelBlue);
      return lootAt("locker", "medical");
    case LD_STATION:
      if (h < 0.25) return bed();
      if (h < 0.50) return table();
      if (h < 0.70) return pile(B.zBarrel);
      return lootAt(h < 0.85 ? "crate" : "stash", h < 0.85 ? "kitchen" : "stash");
    case LD_SHIP:
      if (h < 0.30) return pile(B.zCrate);
      if (h < 0.50) return pile(B.zBarrel);
      if (h < 0.70) return bed();
      if (h < 0.85) return put(B.slab);
      return lootAt("locker", "workshop");
    case LD_BASE:
      if (h < 0.35) return bags(true);
      if (h < 0.60) return lootAt("military", "military");
      if (h < 0.80) return pile(B.zCrate);
      return table();
    case LD_SCHOOL:
      if (h < 0.45) return table();
      if (h < 0.70) {
        if (!B.bookcase || !openAt(c, wx, p.y + 1, wz)) return table();
        q.put(lx, 0, lz, B.bookcase); q.put(lx, 1, lz, B.bookcase); return true;
      }
      return lootAt("safe", "office");
    case LD_INSTITUTE:
      if (h < 0.18) return bags(false);
      if (h < 0.36) return lootAt("locker", "military");
      if (h < 0.52) return lootAt("safe", "office");
      if (h < 0.68) return table();
      if (h < 0.82) return lootAt("medical", "medical");
      if (h < 0.92) return put(B.cabinet);
      return pile(B.zBarrelBlue);
    case LD_WORKS:
      if (h < 0.22) return pile(B.zCrate);
      if (h < 0.40) return pile(B.zBarrel);
      if (h < 0.58) return lootAt("locker", "workshop");
      if (h < 0.74) return table();
      if (h < 0.88) return bags(false);
      return lootAt("stash", "stash");
    case LD_YARD:
      if (h < 0.20) return bags(false);
      if (h < 0.38) return pile(B.zCrate);
      if (h < 0.54) return pile(B.zBarrel);
      if (h < 0.70) return lootAt("locker", "workshop");
      if (h < 0.86) return table();
      return bed();
    case LD_FARM:
      if (h < 0.30) return put(B.hay);
      if (h < 0.50) return pile(B.zCrate);
      if (h < 0.68) return pile(B.zBarrel);
      if (h < 0.84) return table();
      return lootAt("crate", "kitchen");
    case LD_BARRACKS:
      if (h < 0.30) return bed();
      if (h < 0.52) return lootAt("locker", "military");
      if (h < 0.70) return table();
      if (h < 0.86) return bags(true);
      return lootAt("military", "military");
    case LD_BLOCK:
      if (h < 0.22) return bed();
      if (h < 0.38) return table();
      if (h < 0.54) {
        if (!B.bookcase || !openAt(c, wx, p.y + 1, wz)) return lootAt("locker", "bedroom");
        q.put(lx, 0, lz, B.bookcase); q.put(lx, 1, lz, B.bookcase); return true;
      }
      if (h < 0.70) return lootAt("locker", "bedroom");
      if (h < 0.84) return lootAt("safe", "office");
      if (h < 0.93) return bags(false);
      return pile(B.zCrate);
    case LD_LOOKOUT:
      if (h < 0.40) return bags(true);
      if (h < 0.65) return lootAt("military", "military");
      if (h < 0.84) return pile(B.zCrate);
      return bed();
    default: return false;
  }
}

void hangLamp(Ctx& c, int wx, int fy, int wz) {
  const int x = wx - c.ox, z = wz - c.oz;
  const Registry& R = reg();
  for (int k = 2; k <= 8; k++) {
    const int y = fy + k;
    if (y >= 256) return;
    const BlockId b = c.ch.get(x, y, z);
    if (!R.tSolid[b] || R.tLiquid[b]) continue;
    if (R.tShape[b] != ShapeKind::None) return;
    c.ch.set(x, y, z, c.B.glowstone);
    return;
  }
}

// Walkable floor under the north shed: solid underfoot, air at head height, a ceiling within 12.
// Samples are the cells of this chunk only; every chunk that contains the shed sees the same grass
// floor, so they agree. Prefer the site's own y when it ties.
int barFloorDy(Ctx& c, const Placed& p) {
  int counts[11] = {};
  for (int lz = 2; lz <= 16; lz += 4) for (int lx = 4; lx <= 58; lx += 6) {
    const int wx = p.x0 + lx, wz = p.z0 + lz;   // the hall is unrotated
    if (!c.inChunk(wx, wz)) continue;
    const int x = wx - c.ox, z = wz - c.oz;
    auto solidish = [&](int y) {
      if ((unsigned)y >= 256) return false;
      const BlockId b = c.ch.get(x, y, z);
      if (!b || reg().tLiquid[b] || reg().tReplaceable[b]) return false;
      return reg().tSolid[b] || reg().tShape[b] == ShapeKind::Slab;
    };
    auto openish = [&](int y) {
      if ((unsigned)y >= 256) return false;
      const BlockId b = c.ch.get(x, y, z);
      return b == AIR || reg().tReplaceable[b];
    };
    for (int dy = -6; dy <= 4; dy++) {
      const int fy = p.y + dy;
      if (fy < 2 || fy > 248) continue;
      if (!solidish(fy - 1) || !openish(fy) || !openish(fy + 1)) continue;
      bool ceil = false;
      for (int k = 2; k <= 12 && fy + k < 256; k++) if (solidish(fy + k)) { ceil = true; break; }
      if (ceil) counts[dy + 6]++;
    }
  }
  int bestDy = 0, bestN = counts[6];
  for (int i = 0; i < 11; i++) if (counts[i] > bestN) { bestN = counts[i]; bestDy = i - 6; }
  return bestN >= 2 ? bestDy : 0;
}

// The 100 Rads bar: the sealed quartz shed on the north of the Rostok landmark (local x 1..61, z 1..18).
// dressLandmarks runs after the user's building is copied in, and only while the chunk is first generated.
// A world that already saved these chunks keeps the old slabs until those chunks are regenerated.
// The counter and the walls are loot furniture (zone fridge, cupboard, wardrobe, locker, desk, and the
// rest). spruce_table is the table block. Lino on the floor, panel on the storeroom wall.
void dressBar(Ctx& c, const Placed& p) {
  Placed loc = p;
  const int floorDy = barFloorDy(c, p);
  loc.y += floorDy;
  L q{c, loc};
  const Pal& B = c.B;
  const int fireX = RADS_FIRE_X - p.x0, fireZ = RADS_FIRE_Z - p.z0;
  const int barX = RADS_BARMAN_X - p.x0, barZ = RADS_BARMAN_Z - p.z0;
  const BlockId door = reg().blockId("spruce_door"), juke = reg().blockId("jukebox");
  auto air = [&](int x, int y, int z) {
    if (!q.in(x, z)) return false;
    const BlockId b = q.get(x, y, z);
    return b == AIR || reg().tReplaceable[b];
  };
  auto stand = [&](int x, int z) {
    if (!air(x, 0, z) || !air(x, 1, z)) return false;
    const BlockId u = q.get(x, -1, z);
    return u != AIR && !reg().tLiquid[u] && (reg().tSolid[u] || reg().tShape[u] == ShapeKind::Slab);
  };
  // the aisle from the east door, the guitar circle, the counter, the barman's spot, the storeroom walls and door
  auto keep = [&](int x, int z) {
    if (z >= 8 && z <= 10) return true;
    if (std::abs(x - fireX) <= 4 && std::abs(z - fireZ) <= 4) return true;
    if (z == 2 && x >= barX - 7 && x <= barX + 7) return true;
    if (z == barZ && x >= barX - 2 && x <= barX + 2) return true;
    if (x == 50 && z >= 12 && z <= 18) return true;
    if (z == 12 && x >= 50 && x <= 61) return true;
    if (x == 55 && (z == 11 || z == 12 || z == 13)) return true;
    if ((x == 54 || x == 56) && z == 12) return true;
    if (x == 61 && z == 9) return true;
    return false;
  };
  auto natural = [&](BlockId b) {
    if (!b) return false;
    const std::string& k = reg().block(b).key;
    return k == "grass" || k == "dirt" || k == "coarse_dirt" || k == "podzol" || k == "stone" || k == "andesite"
        || k == "cobblestone" || k == "mossy_cobblestone" || k == "dirt_with_dry_grass" || k == "moss_block" || k == "peat";
  };
  const BlockId floor = B.zFloorLino ? B.zFloorLino : B.smooth;
  const BlockId part = B.zPanel ? B.zPanel : B.concrete2;
  for (int z = 1; z <= 18; z++) for (int x = 1; x <= 61; x++) {
    if (!q.in(x, z) || !air(x, 0, z)) continue;
    const BlockId u = q.get(x, -1, z);
    if (natural(u)) q.put(x, -1, z, floor);
  }
  auto wall = [&](int x, int z) {
    if (!q.in(x, z)) return;
    for (int y = 0; y <= 4; y++) if (air(x, y, z)) q.put(x, y, z, part);
  };
  for (int z = 12; z <= 18; z++) wall(50, z);
  for (int x = 51; x <= 61; x++) if (x != 55) wall(x, 12);
  auto doorAt = [&](int x, int z, u8 face) {
    if (!door || !q.in(x, z)) return;
    q.put(x, 0, z, door, face);
    q.put(x, 1, z, door, (u8)(face | 16));
  };
  if (stand(55, 13)) {   // storeroom door, panel toward the bar; the wall continues above it
    doorAt(55, 12, 2);
    for (int y = 2; y <= 4; y++) if (air(55, y, 12)) q.put(55, y, 12, part);
  }
  if (q.in(62, 9) && air(61, 0, 9) && air(61, 1, 9) && (!q.in(63, 9) || air(63, 0, 9))) {
    doorAt(62, 9, 3);   // east wall, panel toward the yard
    DQ_LOG("zone interior: 100 Rads door at %d %d", p.x0 + 62, p.z0 + 9);
  }
  auto put = [&](int x, int z, BlockId id, u8 meta = 0) {
    if (!id || keep(x, z) || !stand(x, z)) return false;
    q.put(x, 0, z, id, meta); return true;
  };
  auto stack = [&](int x, int z, BlockId id) {
    if (!put(x, z, id)) return;
    if (air(x, 1, z)) q.put(x, 1, z, id);
  };
  int furnN = 0;
  // Real loot furniture. `force` is the counter: keep() reserves that line so stools and clutter stay off it.
  // Meta 2 faces +z (the room). A two-tall piece needs the block above free. The chest entity is what Use opens.
  auto furn = [&](int x, int z, const char* baseKey, const char* topKey, u8 meta, const char* zoneKind, bool force) -> bool {
    const BlockId base = reg().blockId(baseKey);
    if (!base || (!force && keep(x, z)) || !stand(x, z)) return false;
    const BlockId top = topKey && *topKey ? reg().blockId(topKey) : 0;
    if (top && !air(x, 1, z)) return false;
    q.put(x, 0, z, base, meta);
    if (top) q.put(x, 1, z, top, meta);
    json items = json::array();
    for (int i = 0; i < 9; i++) items.push_back(nullptr);
    if (reg().item("vodka")) items[0] = json{{"id", "vodka"}, {"n", 1}};
    if (reg().item("bread")) items[1] = json{{"id", "bread"}, {"n", 1}};
    q.entity(x, 0, z, json{{"type", "chest"}, {"items", std::move(items)}, {"zone", zoneKind ? zoneKind : "kitchen"}});
    furnN++;
    return true;
  };
  for (int x = barX - 7; x <= barX + 7; x++) {   // counter along the window wall, facing the room
    if (x == barX || x == barX - 4 || x == barX + 4) furn(x, 2, "zone_fridge", "zone_fridge_top", 2, "kitchen", true);
    else if ((x & 1) == 0) furn(x, 2, "zone_cupboard", nullptr, 2, "kitchen", true);
  }
  if (stand(fireX, fireZ) && B.campfire) q.put(fireX, 0, fireZ, B.campfire);
  put(fireX - 5, fireZ, B.chair, 3); put(fireX + 5, fireZ, B.chair, 1);   // facing the fire
  for (int x : {barX - 6, barX - 4, barX + 5, barX + 7}) put(x, barZ, B.zBarrel ? B.zBarrel : B.barrel);
  for (int x : {barX - 5, barX - 2, barX + 3, barX + 6}) put(x, 3, B.chair, 2);          // stools, facing the counter
  auto table = [&](int x, int z) { if (!put(x, z, B.table)) return; put(x - 1, z, B.chair, 3); put(x + 1, z, B.chair, 1); };
  for (auto [x, z] : {std::pair{8, 4}, {8, 14}, {16, 4}, {16, 14}, {22, 14}, {28, 14}, {34, 16}, {38, 5}, {38, 14}, {46, 14}}) table(x, z);
  auto food = [&](int x, int z) { if (!keep(x, z) && stand(x, z)) q.loot(x, 0, z, "crate", 0, "kitchen"); };
  for (auto [x, z] : {std::pair{46, 5}, {57, 5}, {12, 6}, {20, 14}, {53, 15}, {56, 16}, {59, 13}}) food(x, z);
  if (juke) put(18, 18, juke);
  put(18, 17, B.chair, 0);   // facing the jukebox on the south wall
  put(17, 18, B.cabinet);
  put(57, 14, B.cauldron);
  put(61, 15, B.cabinet);
  for (int x : {4, 10, 16, 22}) put(x, 1, x % 8 == 0 ? B.zCrate : (B.zBarrel ? B.zBarrel : B.barrel));
  for (int x : {4, 10, 26, 34, 42}) stack(x, 18, B.zCrate ? B.zCrate : B.barrel);
  for (int x : {8, 14, 30, 40}) put(x, 17, B.zBarrel ? B.zBarrel : B.barrel);
  for (int x : {52, 60}) put(x, 14, B.zBarrel ? B.zBarrel : B.barrel);
  stack(52, 17, B.zCrate ? B.zCrate : B.barrel);
  stack(60, 17, B.zCrate ? B.zCrate : B.barrel);
  // Inset from the shed walls so a door's clearance ring does not delete them. One of each loot piece.
  furn(8, 16, "zone_wardrobe", "zone_wardrobe_top", 0, "bedroom", false);
  furn(14, 16, "zone_locker", "zone_locker_top", 0, "military", false);
  furn(22, 16, "zone_filing", nullptr, 0, "office", false);
  furn(28, 16, "zone_wardrobe", "zone_wardrobe_top", 0, "bedroom", false);
  furn(16, 6, "zone_desk", nullptr, 0, "office", false);
  furn(10, 6, "zone_bedside", nullptr, 0, "bedroom", false);
  furn(36, 6, "zone_toolbox", nullptr, 0, "workshop", false);
  furn(40, 6, "zone_weapon_case", nullptr, 0, "military", false);
  furn(42, 15, "zone_ammo_box", nullptr, 0, "military", false);
  furn(6, 15, "zone_first_aid", nullptr, 2, "medical", false);
  furn(53, 16, "zone_fridge", "zone_fridge_top", 0, "kitchen", false);
  furn(58, 16, "zone_locker", "zone_locker_top", 0, "military", false);
  furn(4, 14, "zone_cupboard", nullptr, 0, "kitchen", false);
  if (furnN) DQ_LOG("zone interior: 100 Rads furniture %d, floor %+d, chunk %d %d", furnN, floorDy, c.ox, c.oz);
  auto lamp = [&](int x, int z) {
    if (!B.lantern || !q.in(x, z)) return;
    for (int dy = 2; dy <= 12; dy++) {
      const BlockId b = q.get(x, dy, z);
      if (b == AIR || !reg().tSolid[b] || reg().tLiquid[b]) continue;
      if (air(x, dy - 1, z)) q.put(x, dy - 1, z, B.lantern, 1);
      return;
    }
  };
  for (auto [x, z] : {std::pair{8, 9}, {12, 6}, {12, 14}, {fireX, fireZ}, {22, 14}, {barX, 4}, {40, 9}, {56, 15}}) lamp(x, z);
}

// The hut builder already put a stove, a bed and a table. This adds the hermit's stock in cells that are still air.
void dressForester(Ctx& c, const Placed& p) {
  L q{c, p};
  const Pal& B = c.B;
  auto open = [&](int lx, int y, int lz) {
    if (!q.in(lx, lz)) return false;
    const BlockId b = q.get(lx, y, lz);
    return b == AIR || reg().tReplaceable[b];
  };
  auto stand = [&](int lx, int lz) {
    if (!open(lx, 0, lz) || !open(lx, 1, lz)) return false;
    const BlockId u = q.get(lx, -1, lz);
    return u != AIR && !reg().tLiquid[u] && (reg().tSolid[u] || reg().tShape[u] == ShapeKind::Slab);
  };
  if (stand(2, 4) && B.cabinet) q.put(2, 0, 4, B.cabinet);
  if (stand(3, 5) && B.bookcase && open(3, 1, 5)) { q.put(3, 0, 5, B.bookcase); q.put(3, 1, 5, B.bookcase); }
  if (stand(4, 3)) q.loot(4, 0, 3, "stash", 0, "stash");
  if (stand(2, 2)) q.put(2, 0, 2, B.zCrate ? B.zCrate : B.barrel);
  if (q.in(3, 4)) { int wx, wz; q.world(3, 4, wx, wz); hangLamp(c, wx, p.y, wz); }
}

void dressSite(Ctx& c, const Placed& p) {
  if (p.dress == LD_FORESTER) { dressForester(c, p); return; }
  if (p.dress == LD_BAR && p.x0 == RADS_OX && p.z0 == RADS_OZ) { dressBar(c, p); return; }
  const int ox = c.ox, oz = c.oz;
  const int inset = (p.w < 14 || p.d < 12) ? 1 : 2;
  const int area = p.w * p.d;
  const int stride = area > 4000 ? 8 : area > 700 ? 6 : 4;
  const bool hall = p.dress == LD_YARD || p.dress == LD_WORKS;
  const bool lived = p.dress == LD_BAR || p.dress == LD_SCHOOL || p.dress == LD_BASE || p.dress == LD_BARRACKS
      || p.dress == LD_BLOCK || p.dress == LD_LOOKOUT || p.dress == LD_INSTITUTE;
  const double gate = hall ? 0.55 : lived ? 0.72 : 0.80;
  const bool lit = p.dress == LD_BAR || p.dress == LD_ECO || p.dress == LD_STATION || p.dress == LD_SHIP || p.dress == LD_BASE
      || p.dress == LD_INSTITUTE || p.dress == LD_BARRACKS;
  const int xLo = std::max(p.x0 + inset, ox + 1), xHi = std::min(p.x0 + p.W - 1 - inset, ox + 14);
  const int zLo = std::max(p.z0 + inset, oz + 1), zHi = std::min(p.z0 + p.D - 1 - inset, oz + 14);
  for (int wz = zLo; wz <= zHi; wz++) for (int wx = xLo; wx <= xHi; wx++) {
    if (wx % stride != 0 || wz % stride != 0) continue;
    const int fy = findFloor(c.ch, wx - ox, wz - oz, p.y);
    if (fy < 0) continue;
    if (hashFloat((i32)p.seed, wx, wz, 41) > gate) continue;
    if (!safeCell(c, wx, fy, wz)) continue;
    Placed loc = p; loc.y = fy;
    L q{c, loc};
    if (!placeKit(q, c, loc, inset, wx, wz, hashFloat((i32)p.seed, wx, wz, 43))) continue;
    if (lit && hashFloat((i32)p.seed, wx, wz, 42) < 0.20) hangLamp(c, wx, fy, wz);
  }
}

// ---------------------------------------------------------------- Cordon embankment rail
// The world's gravel road whose vertices pass (-4375, 3443): one line of stock rail on the crest, and a
// same-height platform (concrete kerb, smooth stone) on its south side where the crest is straight.
// A column the user already built on, or anything standing at head height, keeps its block.

using RailMap = std::unordered_map<i64, u8>;

void raster(RailMap& m, int x0, int z0, int x1, int z1) {
  const u8 along = std::abs(x1 - x0) >= std::abs(z1 - z0) ? 1 : 0;
  const int dx = std::abs(x1 - x0), dz = std::abs(z1 - z0);
  const int sx = x0 < x1 ? 1 : -1, sz = z0 < z1 ? 1 : -1;
  int err = dx - dz;
  for (;;) {
    m[cellKey(x0, z0)] = along;
    if (x0 == x1 && z0 == z1) break;
    const int e2 = 2 * err;
    if (e2 > -dz) { err -= dz; x0 += sx; }
    if (e2 < dx) { err += dx; z0 += sz; }
  }
}

const RailMap& embankmentLine() {
  static const RailMap line = [] {
    RailMap m;
    int best = -1; float bestLen = -1.f;
    const std::vector<Line>& lines = roads();
    for (int i = 0; i < (int)lines.size(); i++) {
      const Line& l = lines[i];
      if (l.kind != 2 || l.pts.size() < 2) continue;
      bool near = false;
      for (const Pt& p : l.pts) if (std::hypot(p.x + 4375.f, p.z - 3443.f) <= 12.f) near = true;
      if (!near) continue;
      float len = 0.f;
      for (size_t k = 1; k < l.pts.size(); k++) len += std::hypot(l.pts[k].x - l.pts[k - 1].x, l.pts[k].z - l.pts[k - 1].z);
      if (len > bestLen) { bestLen = len; best = i; }
    }
    if (best < 0) { DQ_LOG("zone rail: cordon embankment not found"); return m; }
    const Line& l = lines[best];
    for (size_t k = 1; k < l.pts.size(); k++)
      raster(m, (int)std::lround(l.pts[k - 1].x), (int)std::lround(l.pts[k - 1].z), (int)std::lround(l.pts[k].x), (int)std::lround(l.pts[k].z));
    DQ_LOG("zone rail: cordon embankment %d cells, gravel road %d", (int)m.size(), best);
    return m;
  }();
  return line;
}

bool headOpen(const Ctx& c, int wx, int y, int wz) {
  if (!c.inChunk(wx, wz) || (unsigned)y >= 256) return false;
  return isOpen(c.ch.get(wx - c.ox, y, wz - c.oz));
}

} // namespace

bool bunkerSpecial(L& q, int deep, int sx, int sz) {
  if (q.p.variant < 1 || q.p.variant > 3 || q.p.w < 30 || q.p.d < 16) return false;
  if (q.p.variant == 1) labAgroprom(q, deep);
  else if (q.p.variant == 2) labX18(q, deep, sx, sz);
  else labX16(q, deep);
  return true;
}

void dressLandmarks(Ctx& c, const Plan& plan) {
  const int ox = c.ox, oz = c.oz;
  std::vector<int> ids;
  for (int cz = oz >> 6; cz <= (oz + 15) >> 6; cz++) for (int cx = ox >> 6; cx <= (ox + 15) >> 6; cx++) {
    auto it = plan.grid.find(cellKey(cx, cz));
    if (it == plan.grid.end()) continue;
    for (int i : it->second) {
      const Placed& p = plan.s[i];
      if (!p.dress || (!p.mc && p.dress != LD_FORESTER)) continue;
      if (std::find(ids.begin(), ids.end(), i) == ids.end()) ids.push_back(i);
    }
  }
  for (int i : ids) dressSite(c, plan.s[i]);
}

void layEmbankmentRail(Ctx& c, u32 seed, const RailCols& in) {
  const RailMap& line = embankmentLine();
  if (line.empty() || !in.G) return;
  const int ox = c.ox, oz = c.oz;
  auto groundH = [&](int wx, int wz) {
    const int x = wx - ox, z = wz - oz;
    if ((unsigned)x < 16 && (unsigned)z < 16) return (int)std::lround(in.G[z * 16 + x].h);
    return (int)std::lround(groundAt(seed, wx, wz).h);
  };
  // a short cobble or gravel cap (the embankment itself) is not a building: the rail sits on top of it.
  // a run four blocks tall, or one that starts well above the ground, is a hut or a wall and keeps the column.
  auto capTop = [&](int i, int H, bool& building) {
    building = false;
    int top = H;
    if (!(in.has && in.cols && in.has[i] && in.cols[i].nruns)) return top;
    const WCol& col = in.cols[i];
    for (int r = 0; r < col.nruns; r++) {
      const int y0 = col.runs[r].y0, y1 = y0 + col.runs[r].len - 1;
      if (col.runs[r].len >= 4 || y0 > col.g + 2) building = true;
      if (y1 > top && y1 <= col.g + 5) top = y1;
    }
    return top;
  };
  for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    const int wx = ox + x, wz = oz + z, i = z * 16 + x;
    auto it = line.find(cellKey(wx, wz));
    if (it == line.end()) continue;
    const Ground& g = in.G[i];
    if (g.water || !g.road) continue;
    const int H = (int)std::lround(g.h);
    if (H < 1 || H > 250) continue;
    bool building = false;
    const int top = capTop(i, H, building);
    if (building) continue;
    const int railY = top + 1;
    if (railY > H + 5 || railY > 254 || !headOpen(c, wx, railY, wz)) continue;
    int meta = it->second ? 1 : 0;
    auto raised = [&](int dx, int dz) {
      return line.find(cellKey(wx + dx, wz + dz)) != line.end() && groundH(wx + dx, wz + dz) == H + 1 && top == H;
    };
    const bool e = raised(1, 0), w = raised(-1, 0), n = raised(0, -1), s = raised(0, 1);
    if (e && !w) meta = 2;
    else if (w && !e) meta = 3;
    else if (n && !s) meta = 4;
    else if (s && !n) meta = 5;
    c.put(wx, railY, wz, c.B.rail, (u8)meta);
  }
  const Registry& R = reg();
  for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    const int wx = ox + x, wz = oz + z;
    if (wx < -4392 || wx > -4356) continue;
    if (line.find(cellKey(wx, wz)) != line.end()) continue;
    int band = 0;
    if (line.find(cellKey(wx, wz - 1)) != line.end()) band = 1;
    else if (line.find(cellKey(wx, wz - 2)) != line.end()) band = 2;
    else continue;
    const int i = z * 16 + x;
    const Ground& g = in.G[i];
    if (g.water) continue;
    const int H = (int)std::lround(g.h);
    if (H < 1 || H > 250) continue;
    bool building = false;
    const int top = capTop(i, H, building);
    if (building || top > 254 || !headOpen(c, wx, top + 1, wz)) continue;
    const BlockId b = c.ch.get(x, top, z);
    if (b == AIR || !R.tSolid[b] || R.tLiquid[b] || R.tShape[b] != ShapeKind::None) continue;
    c.ch.set(x, top, z, band == 1 ? c.B.concrete : c.B.smooth);
  }
}

} // namespace dq::zone
