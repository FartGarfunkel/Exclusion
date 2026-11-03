// gen/zone_world_blocks.cpp — the user's buildings rebuilt with our blocks (gen/zone_world.h): each of their blocks is mapped
// by material (white quartz and wool -> whitewash and plaster, stone bricks and double slabs -> concrete, bricks, dark
// brick, iron -> rusted metal, glass -> dirty or broken panes, planks -> weathered planks, stained clay -> painted panels...)
// with their orientations (stairs, slabs, logs, doors, trapdoors, ladders, beds, chests, furnaces) turned into ours; then
// the detail they haven't got, seeded and the same whichever chunk comes first: weathered and mossy patches low on the walls,
// missing and broken windows, rust, crumbled wall tops, moss on the roofs, rubble along the walls' feet. Chests become the
// Zone's loot containers (L::loot in gen/zone_build.cpp), furnaces stoves, beds the two-block metal beds.
#include "gen/zone_world.h"
#include "gen/zone_plan.h"
#include "core/registry.h"
#include "world/dirs.h"
#include "common/noise.h"
#include <algorithm>
#include <cmath>
#include <mutex>

namespace dq::zone {

void zoneWorldBuild(Ctx& c, const WTile& t, const Ground* G, u8* occupied);
u32 zoneWorldTop(const WTile& t, int i, int& topY);

namespace {

// how a mapped block is treated when it is placed
enum MK : u8 { MK_SKIP, MK_BLOCK, MK_WHITE, MK_GREY, MK_DARK, MK_BRICK, MK_PLANK, MK_METAL, MK_GLASS, MK_BARS, MK_DOOR, MK_CHEST, MK_COLOR, MK_NAT };
inline u32 pack(BlockId id, u8 meta, MK k) { return (u32)id | ((u32)meta << 16) | ((u32)k << 24); }
inline BlockId idOf(u32 m) { return (BlockId)(m & 0xFFFF); }
inline u8 metaOf(u32 m) { return (u8)(m >> 16); }
inline MK kindOf(u32 m) { return (MK)(m >> 24); }

BlockId bid(const char* k, const char* alt = "stone") {
  const Registry& R = reg();
  BlockId i = R.blockId(k); if (i == AIR && alt) i = R.blockId(alt); if (i == AIR && alt && std::string(alt) != "air") i = R.blockId("stone");
  return i;
}
const char* COL[16] = {"white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray", "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black"};
const char* WOOD[6] = {"oak", "spruce", "birch", "jungle", "acacia", "dark_oak"};
std::string woodKey(int w, const char* what) { return std::string(w == 0 ? "" : WOOD[w]) + (w == 0 ? "" : "_") + what; }   // oak planks are "planks"

// their facings -> ours. Ours: N E S W = 0 1 2 3 (world/dirs.h); chests/furnaces' front: 0 +z 1 +x 2 -z 3 -x (render/mesher.cpp)
u8 stairMeta(int d) { static const u8 F[4] = {1, 3, 2, 0}; return (u8)(F[d & 3] | ((d & 4) ? 4 : 0)); }   // their 0 E 1 W 2 S 3 N, 4 upside down
u8 frontMeta(int d) { switch (d) { case 2: return 2; case 3: return 0; case 4: return 3; case 5: return 1; default: return 0; } }   // 2 N 3 S 4 W 5 E
u8 ladderMeta(int d) { switch (d) { case 2: return 2; case 3: return 0; case 4: return 1; case 5: return 3; default: return 0; } }   // the wall behind it
u8 trapMeta(int d) { static const u8 F[4] = {2, 0, 1, 3}; return (u8)(F[d & 3] | ((d & 4) ? 4 : 0) | ((d & 8) ? 8 : 0)); }
u8 bedMeta(int d) { return (u8)(((d & 3) + 2) & 3 | ((d & 8) ? 4 : 0)); }   // their 0 S 1 W 2 N 3 E toward the head, 8 head half
u8 logMeta(int d) { const int a = (d >> 2) & 3; return (u8)(a == 1 ? 1 : a == 2 ? 2 : 0); }

u32 mapBlock(const std::string& full) {
  const size_t c = full.find(':');
  const std::string n = full.substr(0, c);
  const int d = c == std::string::npos ? 0 : std::atoi(full.c_str() + c + 1);
  auto B = [](const char* k, const char* alt = "stone") { return bid(k, alt); };
  // natural blocks left standing above the ground (overhangs, planters, roofs of earth)
  if (n == "stone") {
    switch (d) { case 1: return pack(B("granite"), 0, MK_BLOCK); case 2: return pack(B("polished_granite", "granite"), 0, MK_BLOCK);
      case 3: return pack(B("light_gray_concrete"), 0, MK_GREY); case 4: return pack(B("polished_diorite", "diorite"), 0, MK_GREY);
      case 5: return pack(B("gray_concrete"), 0, MK_DARK); case 6: return pack(B("polished_andesite", "andesite"), 0, MK_DARK); default: return pack(B("stone"), 0, MK_NAT); }
  }
  if (n == "grass") return pack(B("grass"), 0, MK_NAT);
  if (n == "dirt") return pack(B(d == 1 ? "coarse_dirt" : d == 2 ? "podzol" : "dirt", "dirt"), 0, MK_NAT);
  if (n == "sand") return pack(B("sand"), 0, MK_NAT);
  if (n == "gravel") return pack(B("gravel"), 0, MK_NAT);
  if (n == "clay") return pack(B("mud", "dirt"), 0, MK_NAT);
  if (n == "sandstone") return pack(B(d == 1 ? "chiseled_sandstone" : d == 2 ? "smooth_sandstone" : "sandstone", "sandstone"), 0, MK_BLOCK);
  if (n == "red_sandstone") return pack(B("red_sandstone", "sandstone"), 0, MK_BLOCK);
  if (n.size() > 4 && n.compare(n.size() - 4, 4, "_ore") == 0) return pack(B("stone"), 0, MK_NAT);
  if (n == "monster_egg" || n == "bedrock") return pack(B("stone"), 0, MK_NAT);
  if (n == "snow" || n == "ice" || n == "packed_ice") return pack(B("glass", "glass"), 0, MK_GLASS);
  if (n == "hay_block") return pack(B("hay_bale"), 0, MK_BLOCK);
  // walls
  if (n == "quartz_block") {
    if (d == 1) return pack(B("zone_panel_tiles", "chiseled_quartz_block"), 0, MK_BLOCK);
    if (d >= 2) return pack(B("quartz_pillar"), (u8)(d == 3 ? 2 : d == 4 ? 1 : 0), MK_BLOCK);
    return pack(B("zone_whitewash", "white_concrete"), 0, MK_WHITE);
  }
  if (n == "wool") return d == 0 ? pack(B("zone_plaster", "white_concrete"), 0, MK_WHITE) : pack(B((std::string(COL[d & 15]) + "_wool").c_str(), "wool"), 0, MK_BLOCK);
  if (n == "double_stone_slab" || n == "double_stone_slab2") {
    switch (d & 7) { case 1: return pack(B("smooth_sandstone", "sandstone"), 0, MK_BLOCK); case 2: return pack(B("spruce_planks", "planks"), 0, MK_PLANK);
      case 3: return pack(B("cobblestone"), 0, MK_DARK); case 4: return pack(B("bricks"), 0, MK_BRICK); case 5: return pack(B("stone_bricks"), 0, MK_GREY);
      case 6: return pack(B("nether_bricks", "bricks"), 0, MK_BLOCK); case 7: return pack(B("smooth_quartz", "quartz_block"), 0, MK_WHITE);
      default: return pack(B("light_gray_concrete"), 0, MK_GREY); }
  }
  if (n == "stonebrick") {
    switch (d) { case 1: return pack(B("zone_concrete_mossy", "mossy_stone_bricks"), 0, MK_BLOCK); case 2: return pack(B("cracked_stone_bricks", "stone_bricks"), 0, MK_BLOCK);
      case 3: return pack(B("chiseled_stone_bricks", "stone_bricks"), 0, MK_BLOCK); default: return pack(B("light_gray_concrete"), 0, MK_GREY); }
  }
  if (n == "cobblestone") return pack(B("cobblestone"), 0, MK_DARK);
  if (n == "mossy_cobblestone") return pack(B("mossy_cobblestone"), 0, MK_BLOCK);
  if (n == "brick_block") return pack(B("bricks"), 0, MK_BRICK);
  if (n == "nether_brick") return pack(B("nether_bricks", "bricks"), 0, MK_BLOCK);
  if (n == "red_nether_brick") return pack(B("red_nether_bricks", "nether_bricks"), 0, MK_BLOCK);
  if (n == "iron_block") return pack(B("zone_roof_rust", "brown_terracotta"), 0, MK_METAL);
  if (n == "gold_block") return pack(B("yellow_terracotta", "terracotta"), 0, MK_BLOCK);
  if (n == "diamond_block" || n == "emerald_block") return pack(B("zone_steel_green", "iron_block"), 0, MK_METAL);
  if (n == "coal_block" || n == "obsidian") return pack(B(n == "obsidian" ? "obsidian" : "black_concrete"), 0, MK_BLOCK);
  if (n == "lapis_block") return pack(B("blue_terracotta", "terracotta"), 0, MK_BLOCK);
  if (n == "prismarine") return pack(B(d == 1 ? "zone_paint_green" : d == 2 ? "zone_corrugated_green" : "zone_steel_green", "green_terracotta"), 0, d == 0 ? MK_METAL : MK_BLOCK);
  if (n == "sea_lantern") return pack(B("sea_lantern", "glowstone"), 0, MK_BLOCK);
  if (n == "glowstone") return pack(B("glowstone"), 0, MK_BLOCK);
  if (n == "redstone_lamp" || n == "lit_redstone_lamp") return pack(B("redstone_lamp", "glowstone"), 0, MK_BLOCK);
  if (n == "end_stone" || n == "end_bricks") return pack(B("smooth_sandstone", "sandstone"), 0, MK_BLOCK);
  if (n == "purpur_block" || n == "purpur_pillar") return pack(B("zone_plaster", "white_concrete"), 0, MK_WHITE);
  if (n == "bone_block") return pack(B("smooth_quartz", "quartz_block"), 0, MK_WHITE);
  if (n == "netherrack" || n == "nether_wart_block") return pack(B("red_terracotta", "terracotta"), 0, MK_BLOCK);
  if (n == "magma" || n == "lava" || n == "flowing_lava") return d == 0 ? pack(B("magma_block", "netherrack"), 0, MK_BLOCK) : 0;
  if (n == "soul_sand") return pack(B("zone_asphalt", "black_concrete"), 0, MK_BLOCK);
  if (n == "hardened_clay") return pack(B("terracotta"), 0, MK_BLOCK);
  if (n == "stained_hardened_clay") {
    const int k = d & 15;
    if (k == 0) return pack(B("zone_whitewash", "white_terracotta"), 0, MK_WHITE);
    if (k == 13) return pack(B("zone_paint_green", "green_terracotta"), 0, MK_COLOR);
    if (k == 7 || k == 8) return pack(B("zone_panel", "light_gray_terracotta"), 0, MK_GREY);
    return pack(B((std::string(COL[k]) + "_terracotta").c_str(), "terracotta"), 0, MK_COLOR);
  }
  if (n == "concrete") return pack(B((std::string(COL[d & 15]) + "_concrete").c_str(), "light_gray_concrete"), 0, MK_COLOR);
  if (n == "planks") return pack(B(woodKey(d % 6, "planks").c_str(), "planks"), 0, MK_PLANK);
  if (n == "double_wooden_slab") return pack(B(woodKey(d % 6, "planks").c_str(), "planks"), 0, MK_PLANK);
  if (n == "log" || n == "log2") return pack(B(n == "log2" ? (d & 1 ? "dark_oak_log" : "acacia_log") : (d & 3) == 1 ? "spruce_log" : (d & 3) == 2 ? "birch_log" : "log", "log"), logMeta(d), MK_BLOCK);
  // shapes
  if (n == "stone_slab" || n == "stone_slab2") {
    static const char* S[8] = {"smooth_stone_slab", "sandstone_slab", "spruce_slab", "cobblestone_slab", "brick_slab", "stone_brick_slab", "nether_brick_slab", "quartz_slab"};
    return pack(B(n == "stone_slab2" ? "red_sandstone_slab" : S[d & 7], "smooth_stone_slab"), (u8)((d & 8) ? 1 : 0), MK_BLOCK);
  }
  if (n == "wooden_slab") return pack(B((std::string(WOOD[(d & 7) % 6]) + "_slab").c_str(), "spruce_slab"), (u8)((d & 8) ? 1 : 0), MK_BLOCK);
  if (n.size() > 7 && n.compare(n.size() - 7, 7, "_stairs") == 0) {
    std::string k = n == "stone_stairs" ? "cobblestone_stairs" : n == "purpur_stairs" ? "quartz_stairs" : n;
    return pack(B(k.c_str(), "cobblestone_stairs"), stairMeta(d), MK_BLOCK);
  }
  if (n == "wooden_door" || n == "iron_door" || (n.size() > 5 && n.compare(n.size() - 5, 5, "_door") == 0)) {
    const char* k = n == "wooden_door" ? "spruce_door" : n == "iron_door" ? "steel_door" : n.c_str();
    return pack(B(k, "spruce_door"), (u8)d, MK_DOOR);   // the meta is worked out with the other half (zoneWorldBuild)
  }
  if (n == "trapdoor") return pack(B("spruce_trapdoor", "oak_trapdoor"), trapMeta(d), MK_BLOCK);
  if (n == "iron_trapdoor") return pack(B("steel_trapdoor", "iron_trapdoor"), trapMeta(d), MK_BLOCK);
  if (n == "ladder") return pack(B("ladder"), ladderMeta(d), MK_BLOCK);
  if (n == "fence" || n == "spruce_fence" || n == "birch_fence" || n == "jungle_fence" || n == "dark_oak_fence" || n == "acacia_fence") return pack(B("spruce_fence", "oak_fence"), 0, MK_BLOCK);
  if (n == "nether_brick_fence") return pack(B("steel_fence", "iron_bars"), 0, MK_BLOCK);
  if (n == "cobblestone_wall") return pack(B(d == 1 ? "mossy_cobblestone_wall" : "cobblestone_wall", "cobblestone_wall"), 0, MK_BLOCK);
  if (n == "fence_gate" || (n.size() > 11 && n.compare(n.size() - 11, 11, "_fence_gate") == 0)) return pack(B("spruce_fence_gate", "oak_fence_gate"), (u8)((((d & 3) + 2) & 3) | ((d & 4) ? 4 : 0)), MK_BLOCK);
  if (n == "iron_bars") return pack(B("iron_bars"), 0, MK_BARS);
  if (n == "glass" || n == "glass_pane") return pack(B("zone_window_broken", "glass_pane"), 0, MK_GLASS);
  if (n == "stained_glass") return pack(B((std::string(COL[d & 15]) + "_stained_glass").c_str(), "glass"), 0, MK_BLOCK);
  if (n == "stained_glass_pane") return pack(B((std::string(COL[d & 15]) + "_stained_glass_pane").c_str(), "glass_pane"), 0, MK_GLASS);
  if (n == "carpet") return pack(B((std::string(COL[d & 15]) + "_carpet").c_str(), "white_carpet"), 0, MK_BLOCK);
  if (n == "web") return pack(B("cobweb", "air"), 0, MK_BLOCK);
  // furniture and containers
  if (n == "chest" || n == "trapped_chest") return pack(B("chest"), frontMeta(d), MK_CHEST);
  if (n == "furnace" || n == "lit_furnace") return pack(B("furnace"), frontMeta(d), MK_BLOCK);
  if (n == "bed") return pack(B("zone_bed_frame", "bed"), bedMeta(d), MK_BLOCK);
  if (n == "jukebox") return pack(B("jukebox"), 0, MK_BLOCK);
  if (n == "crafting_table") return pack(B("crafting_table"), 0, MK_BLOCK);
  if (n == "bookshelf") return pack(B("bookshelf"), 0, MK_BLOCK);
  if (n == "cauldron") return pack(B("cauldron"), 0, MK_BLOCK);
  if (n == "flower_pot") return pack(B("flower_pot", "air"), 0, MK_BLOCK);
  if (n == "anvil") return pack(B("damaged_anvil", "anvil"), (u8)(d & 1), MK_BLOCK);
  if (n == "pumpkin" || n == "lit_pumpkin") return pack(B("pumpkin"), 0, MK_BLOCK);
  if (n == "sponge") return pack(B("hay_bale"), 0, MK_BLOCK);
  return 0;   // signs, torches, banners, heads, the odd decoration: left out
}

std::mutex g_mapM;
const std::vector<u32>& mappedOf(const WTile& t) {
  // filled once per tile (the tile is shared and otherwise read-only: guard the one write)
  std::lock_guard<std::mutex> g(g_mapM);
  auto& m = const_cast<std::vector<u32>&>(t.mapped);
  if (m.size() != t.names.size()) { m.resize(t.names.size()); for (size_t i = 0; i < t.names.size(); i++) m[i] = mapBlock(t.names[i]); }
  return m;
}

inline double H(u32 s, int a, int b, int c, int k) { return hashFloat((i32)s, a, b, c * 131 + k); }

} // namespace

// the topmost of the user's blocks in column i (for the distant view): our block, its y; 0 if none
u32 zoneWorldTop(const WTile& t, int i, int& topY) {
  const u32 a = t.runOff[i], b = t.runOff[i + 1];
  if (a == b) return 0;
  const auto& M = mappedOf(t);
  for (u32 k = b; k-- > a;) {
    const WRun& r = t.runs[k];
    const u32 m = r.pal < M.size() ? M[r.pal] : 0;
    if (!m || kindOf(m) == MK_GLASS || kindOf(m) == MK_BARS) continue;
    topY = r.y0 + r.len - 1; return idOf(m);
  }
  return 0;
}

void zoneWorldBuild(Ctx& c, const WTile& t, const Ground* G, u8* occupied) {
  const Pal& B = c.B;
  const auto& M = mappedOf(t);
  const u32 S = c.seed;
  const BlockId moss = B.zMoss, debris = B.zDebris;
  for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    const int wx = c.ox + x, wz = c.oz + z, i = WTile::at(wx, wz);
    const u32 a = t.runOff[i], b = t.runOff[i + 1];
    const int g = t.ground[i];
    if (!g) continue;
    const int Hh = (int)std::round(G[z * 16 + x].h);
    if (a == b) {
      // rubble along the walls' feet: an open column beside one of their walls standing on the ground
      if (Hh != g || G[z * 16 + x].water || G[z * 16 + x].road || occupied[z * 16 + x]) continue;
      bool wall = false;
      for (int k = 0; k < 4 && !wall; k++) {
        const int nx = wx + DIRX[k], nz = wz + DIRZ[k];
        WCol n; if (!worldColumn(nx, nz, n) || n.nruns == 0) continue;
        wall = n.runs[0].y0 == n.g + 1 && n.runs[0].len >= 3;
      }
      if (!wall) continue;
      const double r = H(S, wx, g, wz, 1);
      if (r < 0.07) { c.put(wx, g + 1, wz, debris, (u8)(H(S, wx, g, wz, 2) * 4)); occupied[z * 16 + x] = 1; }
      else if (r < 0.12) { c.put(wx, g + 1, wz, B.looseStones); occupied[z * 16 + x] = 1; }
      continue;
    }
    if (Hh != g) continue;   // a pad levelled this column: their blocks would float or sink
    occupied[z * 16 + x] = 1;
    const Region* reg0 = G[z * 16 + x].region;
    const int dg = reg0 ? reg0->danger : wildDanger(wx, wz);
    const float wear = 0.5f + dg * 0.12f;   // the deeper the Zone, the more ruined
    auto palAt = [&](int y) -> int { for (u32 k = a; k < b; k++) { const WRun& r = t.runs[k]; if (y >= r.y0 && y < r.y0 + r.len) return r.pal; } return -1; };
    int topY = 0; for (u32 k = a; k < b; k++) topY = std::max(topY, t.runs[k].y0 + t.runs[k].len - 1);
    {   // a house standing on posts over a dip in their ground: fill the short gap under its walls / floor with a foundation.
        // Only where the column carries more built above (a wall, a floor under a roof): not under eaves, porches, bridges.
      int y0 = 255; MK k0 = MK_SKIP;
      for (u32 k = a; k < b; k++) {
        const WRun& r = t.runs[k]; const u32 m = r.pal < M.size() ? M[r.pal] : 0; const MK km = m ? kindOf(m) : MK_SKIP;
        if (km != MK_SKIP && km != MK_GLASS && km != MK_NAT && km != MK_BARS && r.y0 < y0) { y0 = r.y0; k0 = km; }
      }
      const int gap = y0 - (g + 1);
      if (gap >= 1 && gap <= 6 && topY - y0 >= 3 && !G[z * 16 + x].water)
        for (int y = g + 1; y < y0; y++) {
          const double n = H(S, wx, y, wz, 5);
          const bool conc = k0 == MK_GREY || k0 == MK_WHITE || k0 == MK_COLOR;
          c.put(wx, y, wz, conc ? (n < 0.2 * wear ? B.zConcMoss : B.concrete2) : (n < 0.3 * wear ? B.mossy : B.cobble));
        }
    }
    for (u32 k = a; k < b; k++) {
      const WRun& r = t.runs[k];
      const u32 m = r.pal < M.size() ? M[r.pal] : 0;
      if (!m) continue;
      const MK kind = kindOf(m);
      for (int y = r.y0; y < r.y0 + r.len && y < 255; y++) {
        BlockId id = idOf(m); u8 meta = metaOf(m);
        const double n = H(S, wx, y, wz, 3), patch = H(S, wx >> 1, y >> 1, wz >> 1, 4);
        const bool low = y <= g + 2;
        switch (kind) {
          case MK_WHITE: id = patch < 0.45 ? B.zWhitewash : patch < 0.85 ? B.zPlaster : B.zPanel; if (low && n < 0.18 * wear) id = B.zConcMoss; break;
          case MK_GREY: id = patch < 0.5 ? id : patch < 0.8 ? B.zPanel : B.concrete2; if (low && n < 0.2 * wear) id = B.zConcMoss; else if (n < 0.05 * wear) id = B.crackedb; break;
          case MK_DARK: if (low && n < 0.25 * wear) id = id == B.cobble ? B.mossy : B.zConcMoss; break;
          case MK_BRICK: if (patch < 0.3 * wear) id = B.zBrick; break;
          case MK_PLANK: if (patch < 0.25 * wear) id = B.zPlanksRot; break;
          case MK_METAL: if (id == B.zRoofRust) id = patch < 0.55 ? B.zRoofRust : patch < 0.8 ? B.steel : B.zSteelGreen; break;
          case MK_COLOR: if (low && n < 0.1 * wear) id = B.zConcMoss; break;
          case MK_GLASS: { static const BlockId pane = bid("glass_pane"); if (n < 0.18 * wear) id = AIR; else if (n < 0.55) id = B.zWindow; else if (id == B.zWindow) id = pane; break; }
          case MK_BARS: if (n < 0.08 * wear) id = AIR; break;
          case MK_DOOR: {
            // ours: 0-1 facing, 2 open, 3 hinge on the right, 4 upper half; theirs: the lower holds facing and open, the upper the hinge
            int lo = meta, up = 0;
            const bool upper = meta & 8;
            if (upper) { up = meta; const int p = palAt(y - 1); lo = p >= 0 ? std::atoi(t.names[p].c_str() + t.names[p].find(':') + 1) : 0; }
            else { const int p = palAt(y + 1); up = p >= 0 ? std::atoi(t.names[p].c_str() + t.names[p].find(':') + 1) : 8; }
            meta = (u8)((((lo & 3) + 1) & 3) | (lo & 4) | ((up & 1) ? 8 : 0) | (upper ? 16 : 0));
            break;
          }
          case MK_CHEST: {
            // a loot container where they kept a chest (the Zone's own loot, one function for every spot)
            Placed p; p.type = S_HUT; p.x0 = wx; p.z0 = wz; p.W = p.D = p.w = p.d = 1; p.h = 1; p.rot = meta & 3; p.y = y;
            p.seed = hash32((i32)S, wx, y, wz); p.danger = (u8)dg; p.ruin = 0; p.pad = false;
            L q{c, p}; q.lx0 = q.lx1 = q.lz0 = q.lz1 = 0;
            const Kind rk = reg0 ? reg0->kind : K_WILD;
            const double lr = H(S, wx, y, wz, 9);
            const char* lk = rk == K_MILITARY || rk == K_RADAR ? "military" : rk == K_HOSPITAL ? "medical" : dg >= 4 && lr < 0.12 ? "safe" : lr < 0.5 ? "crate" : "locker";
            const char* room = rk == K_MILITARY || rk == K_RADAR ? "military" : rk == K_HOSPITAL ? "medical" : rk == K_INSTITUTE || rk == K_LAKE || rk == K_NPP ? "office" : nullptr;
            q.loot(0, 0, 0, lk, 0, room);
            continue;
          }
          default: break;
        }
        if (id == AIR) { c.put(wx, y, wz, AIR); continue; }
        c.put(wx, y, wz, id, meta);
        // a crumbled wall top now and then
        if (y == topY && (kind == MK_WHITE || kind == MK_GREY || kind == MK_BRICK || kind == MK_DARK) && n > 1 - 0.06 * wear) {
          static const u8 CUT[6] = {0x0C, 0xC0, 0x44, 0x88, 0x04, 0x80};
          c.carve(wx, y, wz, CUT[(int)(H(S, wx, y, wz, 5) * 6) % 6]);
        }
      }
    }
    // moss on the flat roofs
    if (topY > g + 2 && topY < 253) {
      const int p = palAt(topY);
      const u32 m = p >= 0 && p < (int)M.size() ? M[p] : 0;
      const MK k = kindOf(m);
      if (m && (k == MK_WHITE || k == MK_GREY || k == MK_DARK || k == MK_BRICK || k == MK_METAL || k == MK_COLOR) && H(S, wx >> 2, topY, wz >> 2, 6) < 0.35 && H(S, wx, topY, wz, 7) < 0.4)
        c.put(wx, topY + 1, wz, moss);
    }
  }
}

} // namespace dq::zone
