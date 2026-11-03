// gen/zone_build.cpp — the Zone's buildings, wrecks and landmarks (see zone_build.h). Every builder draws in the
// structure's local frame and only the part inside the current chunk, from hashes of its own seed, so each chunk
// gets the same blocks whichever order the chunks come in.
#include "gen/zone_build.h"
#include "core/registry.h"
#include <cmath>
#include <algorithm>

namespace dq::zone {

void Pal::init() {
  const Registry& R = reg();
  auto b = [&](const char* k, const char* alt = "stone") { BlockId i = R.blockId(k); if (i == AIR) i = R.blockId(alt); if (i == AIR && std::string(alt) != "air") i = R.blockId("stone"); return i; };
  stone = b("stone"); dirt = b("dirt"); grass = b("grass"); coarse = b("coarse_dirt", "dirt"); podzol = b("podzol", "dirt");
  litter = b("dirt_with_coniferous_litter", "podzol"); dryDirt = b("dirt_with_dry_grass", "grass"); mud = b("mud", "dirt");
  peat = b("peat", "mud"); moss = b("moss_block", "grass"); sand = b("sand"); gravel = b("gravel"); water = b("water");
  bedrock = b("bedrock"); andesite = b("andesite"); asphalt = b("black_concrete"); crack = b("gray_concrete_powder", "gravel");
  curb = b("light_gray_concrete"); path = b("dirt_path", "coarse_dirt"); concrete = b("light_gray_concrete"); concrete2 = b("gray_concrete");
  wconcrete = b("white_concrete"); smooth = b("smooth_stone"); slab = b("smooth_stone_slab", "smooth_stone"); brick = b("bricks");
  stoneb = b("stone_bricks"); crackedb = b("cracked_stone_bricks", "stone_bricks"); cobble = b("cobblestone"); mossy = b("mossy_cobblestone");
  plank = b("spruce_planks", "planks"); plank2 = b("dark_oak_planks", "planks"); log = b("spruce_log", "log"); birchLog = b("birch_log", "log");
  pineLog = b("pine_log", "spruce_log"); charred = b("charred_log", "dark_oak_log"); pineLeaves = b("pine_needles", "spruce_leaves");
  birchLeaves = b("birch_leaves", "leaves"); deadLeaves = b("acacia_leaves", "leaves"); glass = b("zone_window_broken", "glass_pane"); bars = b("iron_bars");
  chain = b("chain", "iron_bars"); ladder = b("ladder"); rail = b("rail"); steel = b("steel_block", "iron_block"); iron = b("iron_block");
  rust = b("brown_terracotta", "terracotta"); rust2 = b("orange_terracotta", "terracotta"); rust3 = b("light_gray_terracotta", "terracotta");
  roofTiles = b("dark_roof_tiles", "bricks"); table = b("spruce_table", "crafting_table"); chair = b("spruce_chair", "air");
  cabinet = b("spruce_cabinet", "bookshelf"); shelf = b("spruce_shelf", "bookshelf"); bookcase = b("spruce_bookcase", "bookshelf");
  bed = b("bed", "air"); sleeping = b("sleeping_bag", "white_wool"); campfire = b("campfire", "fire"); barrel = b("barrel");
  chest = b("chest"); anvil = b("damaged_anvil", "anvil"); furnace = b("furnace"); cauldron = b("cauldron"); cobweb = b("cobweb", "air");
  puddle = b("puddle", "air"); tallGrass = b("tall_grass", "air"); dryGrass = b("dry_grass", "tall_grass"); fern = b("fern", "tall_grass");
  deadBush = b("dead_bush", "air"); sandbag = b("packed_mud", "dirt"); greenWool = b("green_wool", "wool"); brownWool = b("brown_wool", "wool");
  lantern = b("lantern", "torch"); weaponRack = b("weapon_rack", "air"); itemFrame = b("item_frame", "air"); hay = b("hay_bale");
  terracotta = b("terracotta"); redConcrete = b("red_concrete"); whiteConcrete = b("white_concrete"); green = b("green_terracotta", "green_concrete");
  glowstone = b("glowstone"); looseStones = b("loose_stones", "gravel"); magma = b("magma_block", "netherrack");
  // the Zone kit
  zAsphalt = b("zone_asphalt", "black_concrete"); zAsphaltCr = b("zone_asphalt_cracked", "gray_concrete_powder"); zPaving = b("zone_paving", "smooth_stone");
  zPanel = b("zone_panel", "light_gray_concrete"); zPanelTiles = b("zone_panel_tiles", "light_gray_concrete"); zPlaster = b("zone_plaster", "white_concrete");
  zPaintGreen = b("zone_paint_green", "green_terracotta"); zWhitewash = b("zone_whitewash", "white_concrete"); zBrick = b("zone_brick_ruined", "bricks");
  zTiles = b("zone_tiles_dirty", "white_concrete"); zConcMoss = b("zone_concrete_mossy", "gray_concrete"); zFence = b("zone_fence_panel", "light_gray_concrete");
  zFloorLino = b("zone_floor_lino", "smooth_stone"); zFloorTiles = b("zone_floor_tiles", "smooth_stone"); zPlanksRot = b("zone_planks_rotten", "spruce_planks");
  zCorrGreen = b("zone_corrugated_green", "green_terracotta"); zRoofRust = b("zone_roof_rust", "brown_terracotta"); zSteelGreen = b("zone_steel_green", "iron_block");
  zLitter = b("zone_leaf_litter", "podzol");
  zChain = b("zone_chainlink", "iron_bars"); zChainPost = b("zone_chainlink_post", "iron_bars"); zWindow = b("zone_window_broken", "glass_pane");
  zMoss = b("zone_moss_carpet", "air"); zCableX = b("zone_cable_x", "air"); zCableZ = b("zone_cable_z", "air");
  zBirchTrunk = b("zone_birch_trunk", "birch_log"); zPineTrunk = b("zone_pine_trunk", "pine_log"); zDeadTrunk = b("zone_dead_trunk", "charred_log");
  zBurntTrunk = b("zone_burnt_trunk", "charred_log"); zBirchLeaves = b("zone_birch_leaves", "birch_leaves"); zPineNeedles = b("zone_pine_needles", "pine_needles");
  zRedNeedles = b("zone_red_needles", "acacia_leaves"); zTwigs = b("zone_dead_twigs", "air"); zTallGrass = b("zone_tall_dry_grass", "dry_grass");
  zShrub = b("zone_shrub", "dead_bush"); zReeds = b("zone_reeds", "tall_grass"); zRebar = b("zone_rebar", "air"); zBarbed = b("zone_barbed_wire", "cobweb");
  zDebris = b("zone_debris", "air"); zBarrel = b("zone_barrel", "barrel"); zBarrelBlue = b("zone_barrel_blue", "barrel"); zCrate = b("zone_crate", "barrel");
  zTyres = b("zone_tyres", "air"); zSandbags = b("zone_sandbags", "packed_mud"); zBarrier = b("zone_barrier", "smooth_stone"); zPipe = b("zone_pipe", "iron_bars");
  zHeatPipe = b("zone_heat_pipe", "iron_block"); zHeatPipeSup = b("zone_heat_pipe_support", "iron_block"); zRadiator = b("zone_radiator", "air");
  zBedFrame = b("zone_bed_frame", "air"); zPole = b("zone_pole", "spruce_fence"); zPoleTop = b("zone_pole_top", "spruce_fence");
  zLampPost = b("zone_lamp_post", "iron_bars"); zLampHead = b("zone_lamp_head", "iron_bars"); zFallen = b("zone_fallen_trunk", "air"); zSignRad = b("zone_sign_rad", "air");
}

// ---------------------------------------------------------------- local helpers
void L::box(int x0, int y0, int z0, int x1, int y1, int z1, BlockId id, u8 meta) {
  for (int z = std::max(z0, lz0); z <= std::min(z1, lz1); z++) for (int x = std::max(x0, lx0); x <= std::min(x1, lx1); x++)
    for (int y = y0; y <= y1; y++) put(x, y, z, id, meta);
}
void L::shell(int x0, int y0, int z0, int x1, int y1, int z1, BlockId id) {
  for (int z = std::max(z0, lz0); z <= std::min(z1, lz1); z++) for (int x = std::max(x0, lx0); x <= std::min(x1, lx1); x++)
    if (x == x0 || x == x1 || z == z0 || z == z1) for (int y = y0; y <= y1; y++) put(x, y, z, id);
}
void L::clear(int x0, int y0, int z0, int x1, int y1, int z1) { box(x0, y0, z0, x1, y1, z1, AIR); }

void L::loot(int lx, int y, int lz, const char* kind, BlockId block) {
  if (!in(lx, lz)) return;
  int wx, wz; world(lx, lz, wx, wz);
  put(lx, y, lz, block ? block : c.B.chest, (u8)(p.rot & 3));
  Mulberry32 r(hash32(wx, p.y + y, wz, (i32)(p.seed ^ 0x2545F491)));
  c.entity(wx, p.y + y, wz, json{{"type", "chest"}, {"items", zoneLoot(kind, p.danger, r)}, {"zone", kind}});
}

static double h3(u32 s, int a, int b, int c) { return hashFloat((i32)s, a, b, c); }

// ---------------------------------------------------------------- chiseled detail (octant masks, zone_build.h oct::)
static u8 hbits(const L& q, int a, int b, int c) { return (u8)hash32((i32)(q.p.seed ^ 0x7F4A7C15u), a, b, c); }
// a wall block beside a hole knocked in it (broken() at a neighbour in the wall's plane) loses some of its eighths toward
// the hole, so holes and window gaps get ragged edges instead of square ones. `alongX`: the wall runs along local x;
// up / down: whether the rows above / below can break at all
static u8 ragged(const L& q, int lx, int y, int lz, bool alongX, bool up, bool down, float extra = 0) {
  if (q.p.ruin < 0.08f) return 0;
  const int ax = alongX ? 1 : 0, az = alongX ? 0 : 1;
  u8 m = 0;
  if (q.broken(lx - ax, y, lz - az, extra)) m |= (alongX ? oct::X0 : oct::Z0) & hbits(q, lx, y, lz * 4 + 1);
  if (q.broken(lx + ax, y, lz + az, extra)) m |= (alongX ? oct::X1 : oct::Z1) & hbits(q, lx, y, lz * 4 + 2);
  if (up && q.broken(lx, y + 1, lz, extra)) m |= oct::TOP & hbits(q, lx, y, lz * 4 + 3);
  if (down && q.broken(lx, y - 1, lz, extra)) m |= oct::BOT & hbits(q, lx, y, lz * 4 + 4);
  return m == 0xFF ? 0x7F : m;
}
// a lump of rubble: a block of `mat` with all but one to three of its lower eighths gone
static void rubble(L& q, int lx, int y, int lz, BlockId mat, int key) {
  const u8 keep = (u8)(oct::BOT & (hbits(q, lx, y + key, lz) | 0x01));
  q.put(lx, y, lz, mat, 0, (u8)~keep);
}

// furniture for one room (local box x0..x1, z0..z1 at floor y)
static void furnish(L& q, int x0, int z0, int x1, int z1, int y, int key) {
  const Pal& B = q.c.B;
  const double r = q.rnd(key, y, 91);
  if (x1 - x0 < 2 || z1 - z0 < 2) return;
  if (r < 0.25) { q.put(x0, y, z0, B.zBedFrame, q.face(q.rnd(key, y, 95) < 0.5 ? 0 : 1)); q.put(x1, y, z1, B.cabinet); }
  else if (r < 0.5) { q.put((x0 + x1) / 2, y, (z0 + z1) / 2, B.table); q.put((x0 + x1) / 2 + 1, y, (z0 + z1) / 2, B.chair); }
  else if (r < 0.62) { q.put(x0, y, z1, B.bookcase); q.put(x0, y + 1, z1, B.bookcase); }
  else if (r < 0.75) { q.put(x1, y, z0, B.shelf); }
  // a shelf board on the back wall at head height (a plank chiseled to its top back quarter)
  if (q.rnd(key, y, 97) < 0.4) for (int x = x0 + 1; x <= std::min(x1 - 1, x0 + 3); x++) if (q.get(x, y + 2, z1) == AIR && q.get(x, y + 2, z1 + 1) != AIR) q.put(x, y + 2, z1, B.plank, 0, (u8)~(oct::TOP & oct::Z1));
  // junk and loot
  const double l = q.rnd(key, y, 92);
  if (l < 0.22) q.loot(x1, y, z0 + (z1 - z0) / 2, l < 0.05 ? "stash" : l < 0.12 ? "locker" : "crate");
  if (q.rnd(key, y, 93) < 0.3) q.put(x0 + 1, y + 2, z0, B.cobweb);
  if (q.rnd(key, y, 94) < 0.2) q.put(x1 - 1, y, z1 - 1, q.rnd(key, y, 96) < 0.5 ? B.zCrate : B.zBarrel);
  // what's left lying about: plaster, glass, paper
  for (int i = 0; i < 3; i++) {
    const int dx = x0 + int(q.rnd(key, y, 100 + i) * (x1 - x0 + 1)), dz = z0 + int(q.rnd(key, y, 110 + i) * (z1 - z0 + 1));
    if (q.rnd(key, y, 120 + i) < 0.55 && q.get(dx, y, dz) == AIR) q.put(dx, y, dz, B.zDebris, u8(i + key) & 3);
  }
}
// a radiator under a window, its back to the wall on the local +lz side (wallSide 0) or -lz side (2)
static void radiator(L& q, int lx, int y, int lz, int wallSide) { if (q.get(lx, y, lz) == AIR && q.rnd(lx, y, lz) < 0.7) q.put(lx, y, lz, q.c.B.zRadiator, q.face(wallSide)); }

// ---------------------------------------------------------------- multi-storey blocks
// Soviet panel blocks (concrete), offices (brick), the hospital and the closed town's houses all come from this:
// storeys 4 blocks apart, stairwells (a ladder shaft) every 16 blocks, flats either side of a corridor.
static void storeys(L& q, int floors, BlockId wall, BlockId seam, BlockId floorB, bool bigWindows) {
  const Pal& B = q.c.B;
  const Placed& p = q.p;
  const int W = p.w, D = p.d, H = floors * 4;
  // collapsed end in ruined buildings: the top floors gone over part of the length
  const int fallX = p.ruin > 0.45f ? int(q.rnd(1, 2, 3) * W) : -1, fallW = int(W * 0.25), fallTop = int(H * (0.3 + q.rnd(1, 2, 4) * 0.4));
  for (int lz = q.lz0; lz <= q.lz1; lz++) for (int lx = q.lx0; lx <= q.lx1; lx++) {
    const bool edgeX = lx == 0 || lx == W - 1, edgeZ = lz == 0 || lz == D - 1, outer = edgeX || edgeZ;
    const int stairBase = (lx / 16) * 16 + 6;            // a stairwell in each 16-block section
    const bool inStair = lx >= stairBase && lx <= stairBase + 3 && lz >= 1 && lz <= 4 && stairBase + 3 < W - 1;
    const bool fallen = fallX >= 0 && lx >= fallX && lx < fallX + fallW;
    if (lz == -1 && p.type != S_STATION) {
      // the street face: a string course at each floor, a cornice under the roof, sills under the windows, a step at
      // each entrance, rubble where the end came down (all chiseled: thin bands, not whole blocks)
      for (int y = 0; y <= H; y++) {
        const int fy = y % 4;
        if (fallen && y > fallTop) { q.put(lx, y, lz, AIR); continue; }
        BlockId id = AIR; u8 cut = 0;
        if (y == H - 1) { id = seam; cut = oct::BOT; }
        else if (fy == 3 && y < H) { id = seam; cut = (u8)(oct::TOP | (oct::BOT & oct::Z0)); }
        else if (fy == 1 && (lx % 4 == 1 || lx % 4 == 2) && lx > 0 && lx < W - 1) { if (!q.broken(lx, y, 0, 0.25f)) { id = seam; cut = (u8)~(oct::BOT & oct::Z1); } }   // (only under a glazed window)
        else if (y == 0 && lx >= stairBase && lx <= stairBase + 3 && stairBase + 3 < W - 1) { id = B.concrete2; cut = oct::TOP; }
        if (id != AIR && fy != 1 && q.broken(lx, y, 0, 0.1f)) { id = AIR; cut = 0; }
        q.put(lx, y, lz, id, 0, cut);
      }
      if ((fallen || (p.ruin > 0.3f && q.broken(lx, 1, 0))) && h3(p.seed, lx, 7, 77) < 0.7) {
        rubble(q, lx, 0, -1, wall, 1);
        if (h3(p.seed, lx, 8, 77) < 0.4) rubble(q, lx, 1, -1, seam, 2);
      }
      continue;
    }
    BlockId prev = AIR;
    for (int y = 0; y <= H + 1; y++) {
      if (fallen && y > fallTop) {
        if (y == fallTop + 1 && prev != AIR && h3(p.seed, lx, lz, 79) < 0.75) {
          if (h3(p.seed, lx, lz, 78) < 0.4) q.put(lx, y, lz, B.zDebris, u8(lx + lz) & 3); else rubble(q, lx, y, lz, outer ? wall : floorB, 3);
        } else q.put(lx, y, lz, AIR);
        prev = AIR; continue;
      }
      const int f = y / 4, fy = y % 4;
      BlockId id = AIR; u8 cut = 0;
      if (y >= H) {                                                                       // parapet; moss and grit on the roof
        if (y == H && outer) { id = seam; if (h3(p.seed, lx, lz, 82) < 0.15 + p.ruin * 0.6) cut = oct::TOP & hbits(q, lx, lz, 82); }
        else if (y == H && !outer) { const double m = h3(p.seed, lx, lz, 81); id = m < 0.25 ? B.zMoss : m < 0.28 ? B.zDebris : AIR; }
      }
      else if (fy == 3) id = y == H - 1 ? B.zConcMoss : floorB;                           // ceiling/floor slab (the roof weathered)
      else if (outer) {
        id = (fy == 0 && !edgeX) ? (f == 0 ? B.zConcMoss : seam) : wall;
        const int along = edgeZ ? lx : lz;
        const bool win = edgeZ ? (along % 4 == 1 || along % 4 == 2) : (along % 6 == 3);
        if (win && fy >= 1 && fy <= (bigWindows ? 2 : 2) && !(edgeX && edgeZ)) id = q.broken(lx, y, lz, 0.25f) ? AIR : B.glass;
        // entrances: each stairwell opens to the street at lz = 0
        if (lz == 0 && f == 0 && fy <= 1 && lx >= stairBase + 1 && lx <= stairBase + 2 && stairBase + 3 < W - 1) id = AIR;
        if (id != AIR && id != B.glass && q.broken(lx, y, lz)) id = (prev != AIR && prev != B.glass && h3(p.seed, lx, y, lz) < 0.3) ? B.zRebar : AIR;
        if ((id == wall || id == seam) && !(edgeX && edgeZ)) cut = ragged(q, lx, y, lz, edgeZ, fy < 2, fy > 0);
      } else {
        // inside: the corridor wall at lz = 5, flat walls every 8, stairwell walls
        const bool corridor = lz == 5 && D > 9, flatWall = (lx % 8 == 0) && lz > 5, stairWall = (lx == stairBase || lx == stairBase + 3) && lz >= 1 && lz <= 4;
        if ((corridor || flatWall || stairWall) && fy <= 2) {
          const bool door = (corridor && (lx % 8 == 4)) || (flatWall && lz == D - 3) || (stairWall && lz == 3);
          id = door && fy <= 1 ? AIR : (fy == 0 ? B.zPaintGreen : B.zWhitewash);
          if (q.broken(lx, y, lz, 0.05f)) id = AIR;
        }
        if (id == AIR && fy == 0 && h3(p.seed, lx, y, lz + 7) < 0.04 + p.ruin * 0.12) id = B.zDebris;
      }
      // the stair shaft: a ladder up the inner face, holes through the slabs
      if (inStair && lx == stairBase + 1 && lz == 1 && y < H) id = fy == 3 ? AIR : B.ladder;
      if (inStair && lx == stairBase + 1 && lz == 1 && y < H && id == B.ladder) { q.put(lx, y, lz, id, q.face(0)); prev = id; continue; }
      q.put(lx, y, lz, id, id == B.zDebris ? u8(h3(p.seed, lx, y, lz) * 4) & 3 : 0, cut);
      prev = id;
    }
  }
  // rooms: furniture and loot per flat per floor
  for (int f = 0; f < floors; f++) for (int fx = 0; fx + 8 <= W; fx += 8) {
    if (fallX >= 0 && f * 4 - 1 > fallTop && fx + 8 > fallX && fx < fallX + fallW) continue;   // that floor came down here
    furnish(q, fx + 1, 6, fx + 7, D - 2, f * 4, fx * 31 + f);
    if (D > 9) furnish(q, fx + 1, 1, fx + 5, 4, f * 4, fx * 37 + f + 500);
    radiator(q, fx + 5, f * 4, D - 2, 0);
    if (D > 9 && !(fx % 16 >= 6 && fx % 16 <= 9)) radiator(q, fx + 1, f * 4, 1, 2);
  }
  // a canopy over each entrance
  for (int s = 6; s + 3 < W - 1; s += 16) for (int x = s; x <= s + 3; x++) { q.put(x, 2, -1, B.slab); }
}

static void bPanel(L& q) { storeys(q, std::max<int>(2, q.p.variant), q.c.B.zPanel, q.c.B.zPanelTiles, q.c.B.zFloorLino, false); }
static void bOffice(L& q) { storeys(q, std::max<int>(2, q.p.variant), q.p.seed % 3 == 0 ? q.c.B.zPlaster : q.c.B.brick, q.c.B.concrete2, q.c.B.zFloorLino, true); }

static void bPublic(L& q) {
  const Pal& B = q.c.B;
  const int W = q.p.w, D = q.p.d, F = std::max<int>(2, q.p.variant), H = F * 5;
  // a tall ground hall, columns along the front (lz 0..2), flats of offices above
  for (int lz = q.lz0; lz <= q.lz1; lz++) for (int lx = q.lx0; lx <= q.lx1; lx++) {
    if (lz < 3) {   // portico
      if (lz == 0 && lx % 4 == 1) for (int y = 0; y < 5; y++) q.put(lx, y, lz, B.zPlaster);
      q.put(lx, 5, lz, B.wconcrete);
      if (q.rnd(lx, lz, 9) < 0.1) q.put(lx, 0, lz, B.zDebris, u8(lx) & 3);
      continue;
    }
    const bool outer = lx == 0 || lx == W - 1 || lz == 3 || lz == D - 1;
    for (int y = 0; y <= H; y++) {
      BlockId id = AIR;
      if (y == H) id = outer ? B.wconcrete : B.zConcMoss;
      else if (y % 5 == 4 && y > 4) id = B.zFloorTiles;
      else if (y == 4) id = B.zFloorTiles;
      else if (outer) {
        id = B.zPlaster;
        const bool win = (lz == 3 || lz == D - 1) ? (lx % 3 != 0) : (lz % 4 == 2);
        if (win && y % 5 >= 1 && y % 5 <= 3) id = q.broken(lx, y, lz, 0.3f) ? AIR : B.glass;
        if (lz == 3 && y < 3 && std::abs(lx - W / 2) <= 1) id = AIR;     // main doors
      }
      q.put(lx, y, lz, id);
    }
    if (lx == W - 3 && lz == D - 2) for (int y = 0; y < H; y++) q.put(lx, y, lz, B.ladder, q.face(2));
    if (lx == W - 3 && lz == D - 2) for (int f = 1; f <= F; f++) q.put(lx, f * 5 - 1, lz, B.ladder, q.face(2));
  }
  for (int f = 0; f < F; f++) for (int fx = 2; fx + 8 < W; fx += 9) furnish(q, fx, 5, fx + 6, D - 3, f == 0 ? 0 : f * 5, fx * 13 + f);
  q.loot(W / 2, 0, D - 2, "locker");
}

static void bHospital(L& q) {
  // a U: the long front wing and two side wings, four storeys, white
  const Pal& B = q.c.B;
  const int W = q.p.w, D = q.p.d;
  struct Wing { int x0, z0, w, d; } wings[3] = {{0, 0, W, 14}, {0, 14, 14, D - 14}, {W - 14, 14, 14, D - 14}};
  for (auto& wg : wings) {
    for (int lz = std::max(q.lz0, wg.z0); lz <= std::min(q.lz1, wg.z0 + wg.d - 1); lz++)
      for (int lx = std::max(q.lx0, wg.x0); lx <= std::min(q.lx1, wg.x0 + wg.w - 1); lx++) {
        const int ux = lx - wg.x0, uz = lz - wg.z0;
        const bool outer = ux == 0 || uz == 0 || ux == wg.w - 1 || uz == wg.d - 1;
        for (int y = 0; y <= 16; y++) {
          BlockId id = AIR;
          if (y == 16) id = outer ? B.wconcrete : (h3(q.p.seed, lx, lz, 81) < 0.2 ? B.zMoss : AIR);
          else if (y % 4 == 3) id = y == 15 ? B.zConcMoss : B.zFloorTiles;
          else if (outer) {
            id = y % 4 == 0 ? B.zPanel : B.zPlaster;
            if ((ux + uz) % 3 == 1 && y % 4 >= 1) id = q.broken(lx, y, lz, 0.35f) ? AIR : B.glass;
            if (uz == 0 && y < 2 && ux % 20 == 10) id = AIR;
          } else if ((ux % 7 == 0 || uz == wg.d / 2) && y % 4 <= 2 && !((ux + uz) % 7 == 3)) id = y % 4 == 0 ? B.zPaintGreen : B.zTiles;
          else if (y % 4 == 0 && h3(q.p.seed, lx, lz, y) < 0.05 && (y == 0 || !q.broken(lx, y - 1, lz))) id = B.zDebris;   // (not over a hole in the slab)
          if (id != AIR && (y % 4 != 3 || y == 15) && q.broken(lx, y, lz)) id = AIR;   // (the floors hold: there's furniture on them)
          q.put(lx, y, lz, id);
        }
        if (ux == 2 && uz == 2) for (int y = 0; y < 16; y++) q.put(lx, y, lz, y % 4 == 3 ? AIR : B.ladder, q.face(0));
      }
  }
  for (int f = 0; f < 4; f++) for (int x = 3; x + 6 < W; x += 7) { furnish(q, x, 8, x + 5, 12, f * 4, x * 7 + f); if (q.rnd(x, f, 5) < 0.3) q.loot(x + 2, f * 4, 9, "medical"); }
}

// ---------------------------------------------------------------- small houses
static void roofGable(L& q, int x0, int x1, int z0, int z1, int y0, BlockId mat) {
  const int span = z1 - z0;
  for (int lz = std::max(z0, q.lz0); lz <= std::min(z1, q.lz1); lz++) {
    const int up = std::min(lz - z0, z1 - lz);
    // each course chiseled to a step (its top eighths on the downhill side gone), so the slope reads as a pitched roof
    const u8 step = (lz - z0 < z1 - lz) ? (oct::TOP & oct::Z0) : (lz - z0 > z1 - lz) ? (oct::TOP & oct::Z1) : 0;
    for (int lx = std::max(x0, q.lx0); lx <= std::min(x1, q.lx1); lx++) {
      u8 cut = step;
      if (q.p.ruin > 0.35f && h3(q.p.seed, lx, lz, 401) < (q.p.ruin - 0.3f) * 0.5f) cut |= hbits(q, lx, up, lz) & (lx == x0 || lx == x1 || up == 0 ? 0xFF : oct::TOP);
      q.put(lx, y0 + up, lz, mat, 0, cut == 0xFF ? 0x7F : cut);
      if ((lx == x0 || lx == x1) && up > 0) for (int y = y0; y < y0 + up; y++) q.put(lx, y, lz, q.c.B.plank);
    }
  }
  (void)span;
}

static void bHut(L& q) {
  const Pal& B = q.c.B;
  const int W = q.p.w, D = q.p.d;
  q.box(0, -1, 0, W - 1, -1, D - 1, B.zPlanksRot);
  for (int lz = q.lz0; lz <= q.lz1; lz++) for (int lx = q.lx0; lx <= q.lx1; lx++) {
    const bool cx = lx == 0 || lx == W - 1, cz = lz == 0 || lz == D - 1;
    if (!(cx || cz)) {
      if (q.rnd(lx, lz, 8) < 0.12) q.put(lx, 0, lz, B.zDebris, u8(lx + lz) & 3);
      else if (q.p.ruin > 0.5f && q.rnd(lx, lz, 9) < 0.1) rubble(q, lx, 0, lz, q.rnd(lx, lz, 10) < 0.5 ? B.plank : B.zPlanksRot, 4);
      continue;
    }
    for (int y = 0; y < 4; y++) {
      BlockId id = (cx && cz) ? B.log : (q.rnd(lx, y, lz + 50) < 0.3 ? B.zPlanksRot : B.plank);
      if (y >= 1 && y <= 2 && ((cz && lx % 3 == 1) || (cx && lz % 3 == 1))) id = q.broken(lx, y, lz, 0.3f) ? AIR : B.glass;
      if (lz == 0 && lx == W / 2 && y <= 1) id = AIR;
      if (id != AIR && q.broken(lx, y, lz)) id = AIR;
      q.put(lx, y, lz, id, 0, (id == B.plank || id == B.zPlanksRot) ? ragged(q, lx, y, lz, cz, y < 3, y > 0) : 0);
    }
  }
  roofGable(q, -1, W, -1, D, 4, q.p.variant & 1 ? B.roofTiles : B.zRoofRust);
  if (q.p.ruin > 0.6f) q.clear(W / 3, 4, 0, W - 2, 8, D - 1);
  // inside: a stove, a bed or bag, a table, maybe a crate
  q.put(1, 0, D - 2, B.furnace, q.face(0)); q.put(W - 2, 0, D - 2, q.rnd(1) < 0.5 ? B.zBedFrame : B.sleeping, q.face(1));
  q.put(W - 2, 0, 1, B.table);
  if (q.rnd(2) < 0.55) q.loot(1, 0, 1, q.rnd(3) < 0.15 ? "stash" : "crate");
  if (q.rnd(4) < 0.4) q.put(1, 2, 1, B.cobweb);
}

static void bHouse2(L& q) {
  const Pal& B = q.c.B;
  const int W = q.p.w, D = q.p.d;
  for (int lz = q.lz0; lz <= q.lz1; lz++) for (int lx = q.lx0; lx <= q.lx1; lx++) {
    const bool outer = lx == 0 || lx == W - 1 || lz == 0 || lz == D - 1;
    for (int y = -1; y < 8; y++) {
      BlockId id = AIR;
      if (y == -1 || y == 3) id = B.zPlanksRot;
      else if (outer) {
        id = q.rnd(lx, y, lz + 60) < 0.35 ? B.zBrick : B.brick;
        if (y % 4 >= 1 && y % 4 <= 2 && (lx + lz) % 3 == 1 && !((lx == 0 || lx == W - 1) && (lz == 0 || lz == D - 1))) id = q.broken(lx, y, lz, 0.3f) ? AIR : B.glass;
        if (lz == 0 && lx == 2 && y <= 1) id = AIR;
        if (id != AIR && q.broken(lx, y, lz)) id = AIR;
      } else if (lx == W / 2 && y % 4 <= 2 && lz != D / 2) id = B.plank;
      const bool cnr = (lx == 0 || lx == W - 1) && (lz == 0 || lz == D - 1);
      q.put(lx, y, lz, id, 0, outer && !cnr && (id == B.brick || id == B.zBrick) ? ragged(q, lx, y, lz, lz == 0 || lz == D - 1, y % 4 < 2, y % 4 > 0) : 0);
    }
  }
  q.box(1, 0, D - 2, 1, 3, D - 2, B.ladder, q.face(2));
  if (q.in(2, -1)) q.put(2, 0, -1, B.cobble, 0, oct::TOP);   // the doorstep
  roofGable(q, -1, W, -1, D, 8, q.p.seed & 1 ? B.zRoofRust : B.roofTiles);
  furnish(q, 1, 1, W / 2 - 1, D - 2, 0, 1); furnish(q, W / 2 + 1, 1, W - 2, D - 2, 0, 2);
  furnish(q, 2, 1, W / 2 - 1, D - 2, 4, 3); furnish(q, W / 2 + 1, 1, W - 2, D - 2, 4, 4);
}

static void bBarn(L& q) {
  const Pal& B = q.c.B;
  const int W = q.p.w, D = q.p.d;
  for (int lz = q.lz0; lz <= q.lz1; lz++) for (int lx = q.lx0; lx <= q.lx1; lx++) {
    const bool outer = lx == 0 || lx == W - 1 || lz == 0 || lz == D - 1;
    if (!outer) { if (q.rnd(lx, lz, 1) < 0.08) q.put(lx, 0, lz, B.hay); continue; }
    for (int y = 0; y < 5; y++) { BlockId id = (lx % 4 == 0) ? B.log : B.plank2; if ((lx == 0 || lx == W - 1) && std::abs(lz - D / 2) <= 1 && y < 4) id = AIR; if (q.broken(lx, y, lz, 0.05f)) id = AIR; q.put(lx, y, lz, id); }
  }
  roofGable(q, -1, W, -1, D, 5, q.p.ruin > 0.5f ? B.zRoofRust : B.plank2);
  if (q.rnd(9) < 0.5) q.loot(W - 2, 0, 1, "crate");
}

static void bChurch(L& q) {
  const Pal& B = q.c.B;
  const int W = q.p.w, D = q.p.d;
  for (int lz = q.lz0; lz <= q.lz1; lz++) for (int lx = q.lx0; lx <= q.lx1; lx++) {
    const bool outer = lx == 0 || lx == W - 1 || lz == 0 || lz == D - 1;
    const bool tower = lx < 5 && lz >= D / 2 - 2 && lz <= D / 2 + 2;
    for (int y = 0; y < (tower ? 22 : 8); y++) {
      BlockId id = AIR;
      const bool tw = tower && (lx == 0 || lx == 4 || lz == D / 2 - 2 || lz == D / 2 + 2);
      if (outer || tw) { id = B.wconcrete; if (y >= 2 && y <= 5 && (lx % 3 == 1 || lz % 3 == 1) && !tw) id = q.broken(lx, y, lz, 0.4f) ? AIR : B.glass; if (lx == W - 1 && std::abs(lz - D / 2) <= 1 && y < 3) id = AIR; }
      if (id != AIR && q.broken(lx, y, lz)) id = AIR;
      q.put(lx, y, lz, id);
    }
    if (tower) { const int dx = lx - 2, dz = lz - D / 2; for (int y = 22; y < 27; y++) { const int r = y < 25 ? 2 : (y == 25 ? 1 : 0); if (std::abs(dx) <= r && std::abs(dz) <= r) q.put(lx, y, lz, B.green); } }
  }
  roofGable(q, 5, W, -1, D, 8, B.green);
  q.loot(W - 3, 0, 1, "stash");
}

// ---------------------------------------------------------------- industrial
static void bHangar(L& q) {
  const Pal& B = q.c.B;
  const int W = q.p.w, D = q.p.d;
  for (int lz = q.lz0; lz <= q.lz1; lz++) for (int lx = q.lx0; lx <= q.lx1; lx++) {
    const int roof = 7 + int(std::round(4.0 * std::sin(3.14159 * lz / std::max(1, D - 1))));
    const bool sideZ = lz == 0 || lz == D - 1, endX = lx == 0 || lx == W - 1;
    q.put(lx, -1, lz, B.concrete2);
    for (int y = 0; y <= roof; y++) {
      BlockId id = AIR;
      if (y == roof) id = (lx % 5 == 0) ? B.rust : B.zRoofRust;
      else if (sideZ && y <= 7) id = q.rnd(lx / 2, y / 3, lz) < 0.6 ? B.zCorrGreen : B.rust;
      else if (endX) { id = (lz % 2) ? B.zCorrGreen : B.zSteelGreen; if (lz >= 2 && lz <= D - 3 && y < 6) id = (lx == 0) ? AIR : id; }
      if (id != AIR && q.broken(lx, y, lz)) id = AIR;
      q.put(lx, y, lz, id);
    }
  }
  const BlockId junk[6] = {B.zBarrel, B.zBarrelBlue, B.zTyres, B.zCrate, B.zDebris, B.zBarrel};
  for (int i = 0; i < 8; i++) { const int x = 2 + int(q.rnd(i, 1) * (W - 4)), z = 1 + int(q.rnd(i, 2) * (D - 2)); q.put(x, 0, z, junk[int(q.rnd(i, 3) * 6)], i & 3); }
  q.loot(W - 2, 0, 1, q.p.danger >= 3 ? "military" : "crate"); q.loot(W - 2, 0, D - 2, "locker");
}

static void bFactory(L& q) {
  const Pal& B = q.c.B;
  const int W = q.p.w, D = q.p.d, H = q.p.h ? q.p.h : 14;
  const BlockId wall = q.p.variant == 1 ? B.zPanel : B.brick;
  for (int lz = q.lz0; lz <= q.lz1; lz++) for (int lx = q.lx0; lx <= q.lx1; lx++) {
    const bool outer = lx == 0 || lx == W - 1 || lz == 0 || lz == D - 1;
    q.put(lx, -1, lz, B.concrete2);
    for (int y = 0; y <= H; y++) {
      BlockId id = AIR;
      if (y == H) id = (lx % 6 == 3 && lz % 4 != 0 && !outer) ? (q.broken(lx, y, lz, 0.3f) ? AIR : B.glass) : B.steel;
      else if (outer) {
        id = (wall == B.brick && q.rnd(lx, y, lz + 70) < 0.3) ? B.zBrick : (y == 0 && q.rnd(lx, lz, 71) < 0.5) ? B.zConcMoss : wall;
        if (y >= H - 5 && y <= H - 2 && (lx + lz) % 4 != 0) id = q.broken(lx, y, lz, 0.4f) ? AIR : B.glass;
        if ((lz == 0 || lz == D - 1) && lx % 24 == 12 && y < 6) id = AIR;     // loading doors
        if ((lx == 0 || lx == W - 1) && std::abs(lz - D / 2) <= 2 && y < 6) id = AIR;
      } else if (lx % 8 == 0 && lz % 8 == 0) id = B.zSteelGreen;                    // columns
      else if (y == 4 && (lz == 1 || lz == D - 2) && lx % 24 != 12) id = B.zPipe;      // pipes along the walls
      else if (y == 0 && lz == D / 2 + 2 && lx > 1 && lx < W - 2) id = lx % 5 == 0 ? B.zHeatPipeSup : B.zHeatPipe;
      else if (y == 0 && q.rnd(lx, lz, 72) < 0.04) id = B.zDebris;
      else if (y == H - 6 && lz == 2) id = B.slab;                                  // catwalk
      if (id != AIR && id != B.glass && q.broken(lx, y, lz)) id = AIR;
      const u8 cut = outer && y < H && (id == wall || id == B.zBrick || id == B.zConcMoss) && !((lx == 0 || lx == W - 1) && (lz == 0 || lz == D - 1)) ? ragged(q, lx, y, lz, lz == 0 || lz == D - 1, y < H - 1, y > 0) : 0;
      q.put(lx, y, lz, id, (id == B.zPipe || id == B.zHeatPipe || id == B.zHeatPipeSup) ? q.face(0) : id == B.zDebris ? u8(lx + lz) & 3 : 0, cut);
    }
    if (!outer && lx % 8 == 4 && lz % 6 == 3 && q.rnd(lx, lz, 3) < 0.45) { q.put(lx, 0, lz, q.rnd(lx, lz, 4) < 0.5 ? B.anvil : B.furnace); q.put(lx, 1, lz, B.iron); }
  }
  if (q.col(1, 1)) for (int y = 0; y < H - 6; y++) q.put(1, y, 1, B.ladder, q.face(0));
  for (int i = 0; i < 3; i++) q.loot(4 + int(q.rnd(i, 7) * (W - 8)), 0, D - 2, i == 0 ? "locker" : "crate");
}

static void bChimney(L& q) {
  const Pal& B = q.c.B;
  const int R = q.p.w / 2, H = q.p.h;
  for (int lz = q.lz0; lz <= q.lz1; lz++) for (int lx = q.lx0; lx <= q.lx1; lx++) {
    const float d = std::sqrt(float(lx - R) * (lx - R) + float(lz - R) * (lz - R));
    for (int y = -1; y < H; y++) {
      const float r = R - y * 0.012f * R;
      if (d <= r && d > r - 1.6f) q.put(lx, y, lz, q.p.variant == 1 ? (((y / 8) & 1) ? B.redConcrete : B.whiteConcrete) : B.brick);
      else if (d <= r - 1.6f) q.put(lx, y, lz, AIR);
    }
  }
}

static void bCooling(L& q) {
  const Pal& B = q.c.B;
  const float R0 = q.p.w / 2.f, H = (float)q.p.h;
  for (int lz = q.lz0; lz <= q.lz1; lz++) for (int lx = q.lx0; lx <= q.lx1; lx++) {
    const float d = std::sqrt((lx - R0 + 0.5f) * (lx - R0 + 0.5f) + (lz - R0 + 0.5f) * (lz - R0 + 0.5f));
    for (int y = 0; y < (int)H; y++) {
      const float t = (y - H * 0.75f) / (H * 0.55f);
      const float r = R0 * 0.62f * std::sqrt(1 + t * t * 1.6f);
      const bool leg = y < 4 && ((int)std::floor(std::atan2(lz - R0, lx - R0) * 12) & 1);
      if (d <= r && d > r - 2.2f && !(y < 4 && !leg)) { BlockId id = (y > H - 6) ? B.concrete2 : B.concrete; if (q.broken(lx, y, lz, 0.02f)) id = AIR; q.put(lx, y, lz, id); }
      else if (d < r - 2.2f) q.put(lx, y, lz, AIR);
    }
  }
  // the unfinished one in the turbine fields has scaffolding hanging off it
  if (q.p.variant == 1) for (int y = 0; y < (int)H; y += 3) q.put(int(R0), y, 1, B.bars);
}

static void bReactor(L& q) {
  const Pal& B = q.c.B;
  const int W = q.p.w, D = q.p.d, H = q.p.h;
  for (int lz = q.lz0; lz <= q.lz1; lz++) for (int lx = q.lx0; lx <= q.lx1; lx++) {
    const bool outer = lx == 0 || lx == W - 1 || lz == 0 || lz == D - 1;
    // the sarcophagus: stepped steel walls over the ruined unit at the high end
    const bool sarc = lx > W * 0.55;
    const int top = sarc ? H + 6 - int(std::abs(lz - D / 2) * 0.35f) : H;
    q.put(lx, -1, lz, B.concrete2);
    for (int y = 0; y <= top; y++) {
      BlockId id = AIR;
      if (y == top) id = sarc ? B.steel : B.concrete2;
      else if (outer) { id = sarc ? ((y / 3) & 1 ? B.steel : B.iron) : B.concrete; if (!sarc && y % 6 >= 2 && y % 6 <= 4 && lx % 4 == 2) id = q.broken(lx, y, lz, 0.4f) ? AIR : B.glass; }
      else if (y % 8 == 7 && !sarc) id = B.smooth;
      else if (sarc && y < 6 && h3(q.p.seed, lx, lz, y) < 0.08) id = B.cobble;     // debris inside
      if (!sarc && lz == 0 && std::abs(lx - W / 4) <= 2 && y < 5) id = AIR;
      q.put(lx, y, lz, id);
    }
  }
  q.loot(4, 0, 4, "safe"); q.loot(W / 3, 8, D / 2, "military");
}

static void bRadar(L& q) {
  // the over-horizon array: masts every 12 blocks, a lattice of bars between them, beams every 10
  const Pal& B = q.c.B;
  const int W = q.p.w, D = q.p.d, H = q.p.h;
  for (int lz = q.lz0; lz <= q.lz1; lz++) for (int lx = q.lx0; lx <= q.lx1; lx++) {
    const bool mast = lx % 12 == 0 && (lz == 0 || lz == D - 1);
    const bool face = lz == 0 || lz == D - 1, mid = lz == D / 2;
    if (mast) q.put(lx, -1, lz, B.concrete);
    for (int y = 0; y < H - (int)(std::abs(lx - W / 2) * 0.05f); y++) {
      BlockId id = AIR;
      if (mast) id = B.steel;
      else if (face && (y % 10 == 0 || ((lx + y) % 12 == 0) || ((lx - y + 1200) % 12 == 0))) id = B.bars;
      else if (mid && y % 10 == 0) id = B.chain;
      else if (y % 20 == 0 && (lz % 3 == 0)) id = B.bars;
      if (id != AIR) q.put(lx, y, lz, id);
    }
  }
}

static void bTurbineHall(L& q) { bFactory(q); }

// ---------------------------------------------------------------- military, camps, small things
static void bCheckpoint(L& q) {
  const Pal& B = q.c.B;
  const int W = q.p.w, D = q.p.d;
  // the booth
  q.box(0, -1, 0, 4, -1, 4, B.concrete2);
  q.shell(0, 0, 0, 4, 3, 4, B.zPanel);
  q.box(1, 1, 0, 3, 2, 0, B.glass); q.box(0, 1, 1, 0, 2, 3, B.glass);
  q.clear(2, 0, 4, 2, 1, 4);
  q.box(0, 4, 0, 4, 4, 4, B.slab);
  q.loot(1, 0, 3, "military");
  // sandbag walls and concrete blocks
  for (int x = 6; x < W; x++) if (x % 5 != 0) { q.put(x, 0, 0, B.zSandbags, q.face(0)); if (x % 3) q.put(x, 1, 0, B.zSandbags, q.face(0)); }
  for (int z = 6; z < D; z += 2) q.put(W - 2, 0, z, B.zBarrier, q.face(1));
  q.box(6, 0, D / 2, W - 4, 0, D / 2, B.bars);     // the barrier
  for (int x = 0; x < W; x++) if (q.rnd(x, 3) < 0.85) q.put(x, 0, D - 1, B.zBarbed);   // wire along the back
  q.put(5, 0, 1, B.zSignRad, q.face(2)); q.put(6, 0, 2, B.zBarrel); q.put(6, 0, 3, B.zTyres);
}

static void bTower(L& q) {
  const Pal& B = q.c.B;
  const int H = q.p.h;
  for (int x : {0, 3}) for (int z : {0, 3}) q.box(x, -1, z, x, H, z, q.p.variant ? B.steel : B.log);
  q.box(0, H - 3, 0, 3, H - 3, 3, B.plank2);
  for (int x = 0; x <= 3; x++) for (int z = 0; z <= 3; z++) if (x == 0 || x == 3 || z == 0 || z == 3) q.put(x, H - 2, z, B.zSandbags, q.face(x == 0 || x == 3 ? 1 : 0));
  q.box(0, H + 1, 0, 3, H + 1, 3, B.zRoofRust);
  q.clear(1, H - 3, 1, 1, H - 3, 1);
  q.box(1, 0, 1, 1, H - 3, 1, B.ladder, q.face(0));
  if (q.rnd(5) < 0.5) q.loot(2, H - 2, 2, "military");
}

static void bWaterTower(L& q) {
  const Pal& B = q.c.B;
  for (int x : {1, 5}) for (int z : {1, 5}) q.box(x, -1, z, x, 14, z, B.steel);
  for (int lz = q.lz0; lz <= q.lz1; lz++) for (int lx = q.lx0; lx <= q.lx1; lx++) {
    const float d = std::sqrt(float(lx - 3) * (lx - 3) + float(lz - 3) * (lz - 3));
    if (d <= 3.4f) for (int y = 15; y <= 21; y++) q.put(lx, y, lz, (d > 2.4f || y == 15 || y == 21) ? (y % 2 ? B.rust : B.rust2) : AIR);
  }
  q.box(3, 0, 0, 3, 14, 0, B.ladder, q.face(2));
}

static void bCamp(L& q) {
  const Pal& B = q.c.B;
  q.put(6, 0, 6, B.campfire);
  for (int t = 0; t < 3; t++) {
    const double a = t * 2.1 + q.rnd(t) * 0.6;
    const int tx = 6 + int(std::cos(a) * 4), tz = 6 + int(std::sin(a) * 4);
    for (int dx = -1; dx <= 1; dx++) { q.put(tx + dx, 0, tz, B.greenWool); q.put(tx + dx, 1, tz, (dx == 0) ? B.greenWool : AIR); }
    q.put(tx, 0, tz + 1, B.sleeping);
  }
  q.loot(9, 0, 9, "crate");
  q.put(3, 0, 9, B.zBarrel); q.put(2, 0, 2, B.lantern); q.put(10, 0, 9, B.zCrate); q.put(3, 0, 3, B.zTyres);
  for (int i = 0; i < 2; i++) q.put(5 + i, 0, 4, B.zFallen, q.face(0));   // a log to sit on
  for (int i = 0; i < 2; i++) q.put(8, 0, 5 + i, B.zFallen, q.face(1));
}

static void bWreck(L& q) {
  // 0 car, 1 truck, 2 bus, 3 APC, 4 helicopter, 5 tractor
  const Pal& B = q.c.B;
  const int v = q.p.variant, W = q.p.w, D = q.p.d;
  const BlockId paint = (q.p.seed % 3 == 0) ? B.rust : (q.p.seed % 3 == 1) ? B.rust2 : B.rust3;
  auto wheels = [&](int y) { for (int x : {1, W - 2}) { q.put(x, y, 0, B.asphalt); q.put(x, y, D - 1, B.asphalt); } };
  switch (v) {
    case 0: wheels(0); q.box(0, 0, 0, W - 1, 0, D - 1, paint); q.box(1, 1, 0, W - 3, 1, D - 1, paint); q.box(2, 1, 1, W - 4, 1, D - 2, AIR); q.box(2, 1, 0, 3, 1, 0, B.glass); break;
    case 1: wheels(0); q.box(0, 1, 0, 2, 3, D - 1, paint); q.box(0, 2, 1, 0, 2, D - 2, B.glass); q.box(3, 1, 0, W - 1, 1, D - 1, B.rust); q.box(3, 2, 0, W - 1, 3, 0, paint); q.box(3, 2, D - 1, W - 1, 3, D - 1, paint); if (q.rnd(1) < 0.5) q.loot(W - 2, 2, 1, "crate"); break;
    case 2: q.box(0, 0, 0, W - 1, 3, D - 1, B.rust2); q.box(1, 1, 1, W - 2, 2, D - 2, AIR); for (int x = 1; x < W - 1; x += 2) { q.put(x, 2, 0, B.glass); q.put(x, 2, D - 1, AIR); } wheels(0); break;
    case 3: q.box(0, 0, 0, W - 1, 2, D - 1, B.green); q.box(W / 2 - 1, 3, D / 2 - 1, W / 2 + 1, 3, D / 2 + 1, B.green); q.box(W / 2 + 2, 3, D / 2, W - 1 + 3, 3, D / 2, B.steel); if (q.rnd(2) < 0.6) q.loot(2, 1, 1, "military"); break;
    case 4: q.box(2, 0, 1, W - 6, 3, D - 2, B.green); q.box(W - 6, 2, D / 2, W - 1, 2, D / 2, B.green); q.box(0, 1, 1, 1, 2, D - 2, B.glass); for (int x = -4; x <= 12; x++) q.put(5 + x, 4, D / 2, B.steel); break;
    default: q.box(0, 0, 0, 3, 2, D - 1, B.rust2); q.put(1, 0, 0, B.asphalt); q.put(1, 0, D - 1, B.asphalt); q.box(4, 0, 0, W - 1, 0, D - 1, B.rust); break;
  }
}

static void bFenceRing(L& q) {
  const Pal& B = q.c.B;
  const int W = q.p.w, D = q.p.d;
  for (int lz = q.lz0; lz <= q.lz1; lz++) for (int lx = q.lx0; lx <= q.lx1; lx++) {
    const bool edge = lx == 0 || lx == W - 1 || lz == 0 || lz == D - 1;
    if (!edge) continue;
    const bool gate = (lz == 0 || lz == D - 1) ? std::abs(lx - W / 2) <= 3 : std::abs(lz - D / 2) <= 3;
    if (gate) continue;
    if (q.broken(lx, 0, lz, 0.04f)) continue;
    if (q.p.variant) {   // chain-link on a concrete footing, posts every four, barbed wire on top
      for (int y = -2; y < 0; y++) q.put(lx, y, lz, B.concrete);
      const bool post = ((lz == 0 || lz == D - 1) ? lx : lz) % 4 == 0;
      for (int y = 0; y < 3; y++) q.put(lx, y, lz, post ? B.zChainPost : B.zChain);
      q.put(lx, 3, lz, B.zBarbed);
    } else for (int y = -2; y < 3; y++) q.put(lx, y, lz, y < 0 ? B.concrete : B.zFence);
  }
}

static void bPylon(L& q) {
  const Pal& B = q.c.B;
  const int H = q.p.h;
  for (int y = -1; y < H; y++) {
    const int s = std::max(0, 3 - y / 7);
    for (int x : {3 - s, 3 + s}) for (int z : {3 - s, 3 + s}) q.put(x, y, z, B.bars);
    if (y % 6 == 0) for (int i = 3 - s; i <= 3 + s; i++) { q.put(i, y, 3 - s, B.bars); q.put(i, y, 3 + s, B.bars); q.put(3 - s, y, i, B.bars); q.put(3 + s, y, i, B.bars); }
  }
  for (int x = 0; x <= 6; x++) q.put(x, H - 3, 3, B.steel);
}

static void bHeap(L& q) {
  const Pal& B = q.c.B;
  const float R = q.p.w / 2.f, Hh = (float)q.p.h;
  for (int lz = q.lz0; lz <= q.lz1; lz++) for (int lx = q.lx0; lx <= q.lx1; lx++) {
    const float d = std::sqrt((lx - R) * (lx - R) + (lz - R) * (lz - R)) / R;
    if (d >= 1) continue;
    const int top = int(Hh * (1 - d * d) + h3(q.p.seed, lx, lz, 1) * 1.5);
    for (int y = -1; y <= top; y++) {
      const double r = h3(q.p.seed, lx, lz, y + 9);
      q.put(lx, y, lz, r < 0.4 ? B.gravel : r < 0.6 ? B.coarse : r < 0.75 ? B.cobble : r < 0.85 ? B.rust : r < 0.93 ? B.rust3 : B.iron);
    }
    const double j = h3(q.p.seed, lx, lz, 50);
    if (j < 0.08) q.put(lx, top + 1, lz, j < 0.02 ? B.zBarrel : j < 0.04 ? B.zTyres : j < 0.05 ? B.zCrate : j < 0.06 ? B.zBarrelBlue : B.zDebris, u8(lx) & 3);
  }
  if (q.rnd(3) < 0.35) q.loot(int(R), 0, 1, "stash");
}

static void bBunker(L& q) {
  // a concrete hut at the middle of the footprint; a shaft down 12 to a lab of rooms
  const Pal& B = q.c.B;
  const int W = q.p.w, D = q.p.d, mx = W / 2 - 3, mz = D / 2 - 3, deep = -14;
  q.box(mx, -1, mz, mx + 6, 4, mz + 6, B.concrete2);
  q.clear(mx + 1, 0, mz + 1, mx + 5, 3, mz + 5);
  q.clear(mx + 3, 0, mz, mx + 3, 1, mz);
  // the lab
  q.box(0, deep - 1, 0, W - 1, deep + 5, D - 1, B.zTiles);
  q.box(1, deep - 1, 1, W - 2, deep - 1, D - 2, B.zFloorTiles);
  q.clear(1, deep, 1, W - 2, deep + 3, D - 2);
  for (int x = 7; x < W - 1; x += 7) q.box(x, deep, 1, x, deep + 3, D - 2, B.zPaintGreen);
  for (int x = 7; x < W - 1; x += 7) q.clear(x, deep, D / 2, x, deep + 1, D / 2);
  q.box(1, deep + 4, 1, W - 2, deep + 4, D - 2, B.smooth);
  for (int x = 3; x < W - 1; x += 6) q.put(x, deep + 3, D / 2, B.glowstone);
  q.clear(mx + 1, deep, mz + 1, mx + 1, 0, mz + 1);
  for (int y = deep; y < 0; y++) q.put(mx + 1, y, mz + 1, B.ladder, q.face(0));
  q.put(mx + 1, -1, mz + 1, B.ladder, q.face(0));
  q.loot(2, deep, 2, "safe"); q.loot(W - 3, deep, D - 3, "military"); q.loot(9, deep, D - 3, "medical"); q.loot(16, deep, 2, "locker");
  for (int i = 0; i < 6; i++) q.put(2 + int(q.rnd(i, 3) * (W - 4)), deep, 1 + int(q.rnd(i, 4) * (D - 2)), i % 2 ? B.table : B.cabinet);
}

static void bBarge(L& q) {
  const Pal& B = q.c.B;
  const int W = q.p.w, D = q.p.d;
  for (int lz = q.lz0; lz <= q.lz1; lz++) for (int lx = q.lx0; lx <= q.lx1; lx++) {
    const float bow = lx > W - 8 ? (lx - (W - 8)) / 8.f : 0;
    const float half = (D - 1) / 2.f * (1 - bow * 0.8f);
    const float off = std::abs(lz - (D - 1) / 2.f);
    if (off > half + 0.5f) continue;
    const bool hull = off > half - 0.6f || lx == 0;
    for (int y = -4; y <= 2; y++) {
      BlockId id = AIR;
      if (y == -4) id = B.rust;
      else if (hull) id = (y + lx) % 5 == 0 ? B.rust2 : B.rust;
      else if (y == 0) id = B.iron;
      q.put(lx, y, lz, id);
    }
  }
  // the deckhouse (a bar in the Zaton barge)
  q.box(3, 1, 2, 12, 5, D - 3, B.rust3); q.clear(4, 1, 3, 11, 4, D - 4);
  q.clear(7, 1, 2, 8, 2, 2);
  for (int x = 4; x < 12; x += 2) q.put(x, 3, 2, B.glass);
  q.put(5, 1, D - 4, B.table); q.loot(10, 1, D - 4, "locker");
  if (q.p.variant) q.loot(W - 6, 1, D / 2, "crate");
}

static void bStation(L& q) {
  const Pal& B = q.c.B;
  const int W = q.p.w, D = q.p.d;
  Placed s = q.p; s.variant = 2;
  storeys(q, 2, B.wconcrete, B.concrete2, B.smooth, true);
  q.box(0, -1, -6, W - 1, -1, -2, B.concrete2);    // the platform (outside the box on the track side)
  (void)D; (void)s;
}

static void bGreenhouse(L& q) {
  const Pal& B = q.c.B;
  const int W = q.p.w, D = q.p.d;
  for (int lz = q.lz0; lz <= q.lz1; lz++) for (int lx = q.lx0; lx <= q.lx1; lx++) {
    const bool outer = lx == 0 || lx == W - 1 || lz == 0 || lz == D - 1;
    const int roof = 4 + std::min(lz, D - 1 - lz) / 2;
    for (int y = 0; y <= roof; y++) {
      BlockId id = AIR;
      if (y == roof || outer) id = (lx % 4 == 0) ? B.steel : (q.broken(lx, y, lz, 0.45f) ? AIR : B.glass);
      if (lz == 0 && lx == W / 2 && y < 2) id = AIR;
      q.put(lx, y, lz, id);
    }
    if (!outer && lz % 3 == 1) q.put(lx, 0, lz, q.rnd(lx, lz) < 0.6 ? B.deadBush : B.dryGrass);
  }
}

static void bFerris(L& q) {
  const Pal& B = q.c.B;
  const int R = 12, cx = q.p.w / 2, cy = 14;
  for (int lx = q.lx0; lx <= q.lx1; lx++) {
    for (int y = 0; y < 28; y++) {
      const float d = std::sqrt(float(lx - cx) * (lx - cx) + float(y - cy) * (y - cy));
      const bool ring = std::abs(d - R) < 0.6f, spoke = d < R && (std::abs(lx - cx) == std::abs(y - cy) || lx == cx || y == cy);
      if (ring || spoke) { q.put(lx, y, 2, B.rust2); q.put(lx, y, 4, B.rust2); }
      if (ring && ((lx * 7 + y * 3) % 9 == 0)) { q.put(lx, y - 1, 3, B.redConcrete); q.put(lx, y - 2, 3, B.redConcrete); }
    }
    if (std::abs(lx - cx) <= 6) { const int legY = 14 - std::abs(lx - cx) * 2; for (int y = -1; y <= legY; y++) if (std::abs(lx - cx) * 2 == 14 - y || y < 0) { q.put(lx, y, 1, B.steel); q.put(lx, y, 5, B.steel); } }
  }
  q.box(cx, cy, 1, cx, cy, 5, B.steel);
}

static void bHelipad(L& q) {
  const Pal& B = q.c.B;
  const int R = q.p.w / 2;
  for (int lz = q.lz0; lz <= q.lz1; lz++) for (int lx = q.lx0; lx <= q.lx1; lx++) {
    const float d = std::sqrt(float(lx - R) * (lx - R) + float(lz - R) * (lz - R));
    if (d > R) continue;
    const bool H = (std::abs(lx - R) == 3 && std::abs(lz - R) <= 4) || (lz == R && std::abs(lx - R) <= 3);
    q.put(lx, -1, lz, H || std::abs(d - (R - 1)) < 0.5f ? B.wconcrete : B.concrete2);
  }
}

static void bStash(L& q) {
  q.loot(0, -1, 0, "stash");
  q.put(0, 0, 0, q.c.B.looseStones);
}

static void bHomeHatch(L& q) {
  // the player's bunker: a concrete collar with an iron hatch; sim/zone_bunker.cpp sends you down from here
  const Pal& B = q.c.B;
  q.box(0, -1, 0, 4, -1, 4, B.concrete2);
  q.shell(0, 0, 0, 4, 0, 4, B.concrete);
  q.put(2, 0, 2, B.iron);
  q.put(2, 1, 2, AIR);
  q.entity(2, 0, 2, json{{"type", "zone_hatch"}});
}

static void bTrader(L& q) {
  // a hut over a cellar: the trader sits at the far end of the cellar (sim spawns them at the marked spot)
  bHut(q);
  const Pal& B = q.c.B;
  const int W = q.p.w, D = q.p.d;
  q.box(0, -7, 0, W - 1, -1, D - 1, B.stoneb);
  q.clear(1, -6, 1, W - 2, -3, D - 2);
  q.clear(1, -6, 1, 1, -1, 1);
  for (int y = -6; y <= 0; y++) q.put(1, y, 1, B.ladder, q.face(0));
  for (int x = 2; x <= W - 2; x++) if (q.col(x, D - 3)) q.put(x, -6, D - 3, B.plank2, 0, oct::BOT & oct::Z0);   // the counter (its top overhangs your side)
  q.put(3, -4, D - 2, B.lantern);
  q.put(W - 2, -6, 2, B.zBarrel); q.put(W - 2, -6, 3, B.zCrate); q.put(2, -6, 2, B.zBarrelBlue);
}

static void bRuin(L& q) { bHut(q); }

void buildStructure(Ctx& c, const Placed& p) {
  L q{c, p};
  // the part of the local footprint inside this chunk
  int ax = 1 << 30, az = 1 << 30, bx = -(1 << 30), bz = -(1 << 30);
  auto inv = [&](int wx, int wz) {
    int lx, lz; const int ux = wx - p.x0, uz = wz - p.z0;
    switch (p.rot & 3) { case 0: lx = ux; lz = uz; break; case 1: lz = p.d - 1 - ux; lx = uz; break; case 2: lx = p.w - 1 - ux; lz = p.d - 1 - uz; break; default: lz = ux; lx = p.w - 1 - uz; break; }
    ax = std::min(ax, lx); bx = std::max(bx, lx); az = std::min(az, lz); bz = std::max(bz, lz);
  };
  inv(c.ox, c.oz); inv(c.ox + 15, c.oz); inv(c.ox, c.oz + 15); inv(c.ox + 15, c.oz + 15);
  // (+-8 so things drawn just outside the footprint, canopies and platforms, still get their columns)
  q.lx0 = std::max(-8, ax); q.lx1 = std::min(p.w + 7, bx); q.lz0 = std::max(-8, az); q.lz1 = std::min(p.d + 7, bz);
  if (q.lx0 > q.lx1 || q.lz0 > q.lz1) return;
  // most builders only draw within the footprint
  const int fx0 = q.lx0, fx1 = q.lx1, fz0 = q.lz0, fz1 = q.lz1;
  q.lx0 = std::max(0, fx0); q.lx1 = std::min(p.w - 1, fx1); q.lz0 = std::max(0, fz0); q.lz1 = std::min(p.d - 1, fz1);
  const bool any = q.lx0 <= q.lx1 && q.lz0 <= q.lz1;
  switch (p.type) {
    case S_HUT: if (any) bHut(q); break;
    case S_RUIN: if (any) bRuin(q); break;
    case S_HOUSE2: if (any) bHouse2(q); break;
    case S_PANEL: { L w = q; w.lz0 = std::max(-1, fz0); if (w.lx0 <= w.lx1 && w.lz0 <= w.lz1) bPanel(w); break; }
    case S_OFFICE: { L w = q; w.lz0 = std::max(-1, fz0); if (w.lx0 <= w.lx1 && w.lz0 <= w.lz1) bOffice(w); break; }
    case S_PUBLIC: if (any) bPublic(q); break;
    case S_HOSPITAL: if (any) bHospital(q); break;
    case S_HANGAR: if (any) bHangar(q); break;
    case S_FACTORY: case S_TURBINE_HALL: if (any) bFactory(q); break;
    case S_CHIMNEY: if (any) bChimney(q); break;
    case S_CHECKPOINT: if (any) bCheckpoint(q); break;
    case S_TOWER: if (any) bTower(q); break;
    case S_WATERTOWER: if (any) bWaterTower(q); break;
    case S_CHURCH: if (any) bChurch(q); break;
    case S_BUNKER: if (any) bBunker(q); break;
    case S_COOLING: if (any) bCooling(q); break;
    case S_REACTOR: if (any) bReactor(q); break;
    case S_RADAR: if (any) bRadar(q); break;
    case S_BARGE: if (any) bBarge(q); break;
    case S_CAMP: if (any) bCamp(q); break;
    case S_WRECK: if (any) bWreck(q); break;
    case S_FENCE_RING: if (any) bFenceRing(q); break;
    case S_PYLON: if (any) bPylon(q); break;
    case S_BARN: if (any) bBarn(q); break;
    case S_HEAP: if (any) bHeap(q); break;
    case S_TRADER: if (any) bTrader(q); break;
    case S_STATION: { L w = q; w.lz0 = std::max(-6, fz0); if (w.lx0 <= w.lx1 && w.lz0 <= w.lz1) bStation(w); break; }
    case S_GREENHOUSE: if (any) bGreenhouse(q); break;
    case S_FERRIS: if (any) bFerris(q); break;
    case S_HELIPAD: if (any) bHelipad(q); break;
    case S_STASH: if (any) bStash(q); break;
    case S_HOME_HATCH: if (any) bHomeHatch(q); break;
    default: break;
  }
}

// ---------------------------------------------------------------- loot
// weighted entries: item, min, max, weight; the danger level shifts the table toward better gear
namespace {
struct LE { const char* id; int a, b; float w; int minDanger; };
const std::vector<LE>& table(const std::string& k) {
  static const std::vector<LE> crate = {
    {"bread", 1, 2, 6, 1}, {"sausage", 1, 1, 4, 1}, {"canned_stew", 1, 1, 3, 1}, {"canned_fish", 1, 1, 3, 1}, {"crackers", 1, 3, 4, 1}, {"water", 1, 2, 6, 1},
    {"vodka", 1, 1, 3, 1}, {"bandage", 1, 2, 5, 1}, {"scrap_metal", 1, 4, 6, 1}, {"cloth", 1, 3, 4, 1}, {"wires", 1, 2, 3, 1}, {"bolt", 3, 10, 3, 1},
    {"ammo_9x18", 8, 24, 5, 1}, {"ammo_12g_buck", 4, 10, 4, 1}, {"ammo_545", 10, 30, 3, 2}, {"ammo_762x39", 10, 20, 2, 2}, {"rubles", 50, 400, 5, 1},
    {"electronics", 1, 2, 2, 1}, {"gun_oil", 1, 1, 1, 1}, {"medkit", 1, 1, 1, 2}, {"antirad", 1, 1, 2, 1}};
  static const std::vector<LE> locker = {
    {"bandage", 1, 3, 4, 1}, {"medkit", 1, 1, 3, 1}, {"antirad", 1, 2, 3, 1}, {"energy", 1, 1, 2, 1}, {"springs", 1, 3, 4, 1}, {"electronics", 1, 2, 3, 1},
    {"repair_weapon", 1, 1, 2, 1}, {"repair_armor", 1, 1, 2, 1}, {"toolkit_basic", 1, 1, 1, 2}, {"ammo_9x19", 10, 30, 3, 1}, {"ammo_545", 15, 30, 3, 2},
    {"ammo_556", 15, 30, 2, 3}, {"ammo_12g_slug", 4, 8, 2, 2}, {"headlamp", 1, 1, 1, 1}, {"gasmask", 1, 1, 1, 2}, {"rubles", 100, 800, 4, 1},
    {"flash_drive", 1, 1, 1, 2}, {"supp_pistol", 1, 1, 1, 2}, {"kobra", 1, 1, 1, 2}, {"pm", 1, 1, 1, 1}, {"fort12", 1, 1, 1, 2}, {"jacket_leather", 1, 1, 1, 1}};
  static const std::vector<LE> military = {
    {"ammo_545", 30, 90, 6, 1}, {"ammo_545_ap", 20, 60, 3, 3}, {"ammo_556", 30, 60, 4, 2}, {"ammo_762x54", 10, 30, 3, 3}, {"ammo_9x39", 20, 40, 3, 3},
    {"ammo_762x39", 30, 60, 3, 2}, {"grenade_rgd5", 1, 2, 4, 1}, {"grenade_f1", 1, 2, 3, 2}, {"vog25", 1, 3, 2, 3}, {"medkit_army", 1, 2, 3, 1},
    {"aks74u", 1, 1, 2, 1}, {"ak74m", 1, 1, 2, 2}, {"akm", 1, 1, 2, 2}, {"pkm", 1, 1, 1, 4}, {"svd", 1, 1, 1, 4}, {"rpk", 1, 1, 1, 3},
    {"helmet_tactical", 1, 1, 2, 2}, {"suit_army", 1, 1, 1, 2}, {"pso1", 1, 1, 1, 3}, {"gp25", 1, 1, 1, 3}, {"mag_ext", 1, 1, 1, 2}, {"supp_545", 1, 1, 1, 3},
    {"crackers", 2, 4, 3, 1}, {"rubles", 200, 1500, 3, 1}};
  static const std::vector<LE> medical = {
    {"bandage", 2, 5, 6, 1}, {"medkit", 1, 2, 5, 1}, {"medkit_army", 1, 1, 2, 2}, {"medkit_sci", 1, 1, 2, 3}, {"antirad", 1, 3, 5, 1},
    {"psyblock", 1, 2, 2, 3}, {"atlas", 1, 1, 1, 2}, {"energy", 1, 1, 2, 1}};
  static const std::vector<LE> safe = {
    {"rubles", 1000, 6000, 8, 1}, {"documents", 1, 1, 4, 1}, {"flash_drive", 1, 2, 4, 1}, {"toolkit_adv", 1, 1, 2, 3}, {"toolkit_expert", 1, 1, 1, 4},
    {"medkit_sci", 1, 2, 2, 1}, {"detector_bear", 1, 1, 2, 2}, {"detector_veles", 1, 1, 1, 4}, {"acog", 1, 1, 1, 3}, {"scope8", 1, 1, 1, 4},
    {"moth_wing", 1, 1, 1, 2}, {"night_candle", 1, 1, 1, 3}, {"glass_eye", 1, 1, 1, 4}};
  static const std::vector<LE> stash = {
    {"rubles", 300, 3000, 6, 1}, {"medkit", 1, 2, 4, 1}, {"antirad", 1, 2, 4, 1}, {"ammo_545", 20, 60, 4, 1}, {"ammo_9x18", 16, 40, 3, 1},
    {"ammo_12g_buck", 8, 20, 3, 1}, {"ammo_556", 20, 60, 2, 2}, {"ammo_9x39", 20, 40, 2, 3}, {"grenade_f1", 1, 2, 2, 1},
    {"mp5", 1, 1, 1, 1}, {"toz34", 1, 1, 1, 1}, {"saiga", 1, 1, 1, 2}, {"m416", 1, 1, 1, 3}, {"groza", 1, 1, 1, 4}, {"vss", 1, 1, 1, 4}, {"sv98", 1, 1, 1, 5},
    {"suit_drifter", 1, 1, 1, 1}, {"suit_opensky", 1, 1, 1, 3}, {"suit_hazmat", 1, 1, 1, 4}, {"helmet_sphere", 1, 1, 1, 3},
    {"kobra", 1, 1, 2, 1}, {"holo", 1, 1, 1, 2}, {"supp_556", 1, 1, 1, 3}, {"vgrip", 1, 1, 1, 1}, {"laser", 1, 1, 1, 2}, {"detector_echo", 1, 1, 2, 1},
    {"repair_weapon", 1, 2, 2, 1}, {"moth_wing", 1, 1, 1, 2}, {"static_pearl", 1, 1, 1, 2}, {"rust_shell", 1, 1, 1, 2}};
  if (k == "locker") return locker;
  if (k == "military") return military;
  if (k == "medical") return medical;
  if (k == "safe") return safe;
  if (k == "stash") return stash;
  return crate;
}
} // namespace

json zoneLoot(const std::string& kind, int danger, Mulberry32& r) {
  json items = json::array(); for (int i = 0; i < 27; i++) items.push_back(nullptr);
  const auto& T = table(kind);
  float total = 0; for (auto& e : T) if (e.minDanger <= danger + 1) total += e.w * (e.minDanger > danger ? 0.35f : 1.f);
  const int rolls = (kind == "stash" ? 3 : kind == "safe" ? 3 : 2) + int(r() * (kind == "crate" ? 2 : 3)) + (danger >= 4 ? 1 : 0);
  for (int k = 0; k < rolls && total > 0; k++) {
    float x = float(r()) * total;
    for (auto& e : T) {
      if (e.minDanger > danger + 1) continue;
      x -= e.w * (e.minDanger > danger ? 0.35f : 1.f);
      if (x > 0) continue;
      if (!reg().item(e.id)) break;
      const int n = e.a + int(r() * (e.b - e.a + 1));
      int s = int(r() * 27); for (int t = 0; t < 27 && !items[s].is_null(); t++) s = (s + 1) % 27;
      json it = json{{"id", e.id}, {"n", n}};
      // found gear is worn: condition 15..70% (stashes and military crates 50..95%), stored as durability used
      if (const ItemDef* d = reg().item(e.id)) {
        const int dur = d->hasTool ? d->tool.durability : d->props.contains("armor") ? d->props["armor"].value("durability", 0) : 0;
        if (dur > 0) {
          const bool good = kind == "stash" || kind == "military" || kind == "safe";
          const double cond = good ? 0.5 + r() * 0.45 : 0.15 + r() * 0.55;
          it["d"] = int(dur * (1 - cond));
        }
      }
      items[s] = it;
      break;
    }
  }
  return items;
}

} // namespace dq::zone
