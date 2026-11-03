// gen/zone_bunker_gen.cpp — the "bunker" world type: the pocket realm of a Zone world holds every player's bunker,
// one every 256 blocks along x (sim/zone_core.cpp travelBunker gives each player a slot). Each is a concrete room
// sunk in solid ground: the bed, a stash, a workbench, a stove, a weapon rack and the ladder up to the hatch. The
// earth all round is real: dig it out to make more rooms, wall them in, light them, and furnish them with what you
// bring back from the Zone. Deep down there is only rock.
#include "gen/dimgens.h"
#include "core/registry.h"
#include "common/noise.h"

namespace dq {
namespace {
constexpr int FLOOR = 100;    // band-local floor (sim/zone_core.cpp BUNKER_FLOOR)
class BunkerGen : public DimGenerator {
public:
  void generate(Chunk& ch) override {
    const Registry& R = reg();
    auto b = [&](const char* k, const char* alt = "stone") { BlockId i = R.blockId(k); return i == AIR ? R.blockId(alt) : i; };
    const BlockId stone = b("stone"), dirt = b("dirt"), clay = b("clay", "dirt"), gravel = b("gravel"), bedrock = b("bedrock"), concrete = b("light_gray_concrete"),
                  wall = b("gray_concrete"), floorB = b("smooth_stone"), lamp = b("lantern", "torch"), bed = b("bed", "white_wool"), locker = b("zone_locker", "barrel"), lockerTop = b("zone_locker_top", "air"),
                  bench = b("crafting_table"), stove = b("furnace"), rack = b("weapon_rack", "air"), ladder = b("ladder"), iron = b("iron_block"),
                  table = b("spruce_table", "crafting_table"), chair = b("spruce_chair", "air"), shelf = b("spruce_shelf", "bookshelf"), frame = b("item_frame", "air"),
                  barrel = b("barrel"), cabinet = b("spruce_cabinet", "bookshelf"), packed = b("packed_mud", "dirt"), coal = b("coal_ore", "stone");
    std::fill(ch.climate.begin(), ch.climate.end(), (u8)128);
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
      const int wx = ch.cx * 16 + x, wz = ch.cz * 16 + z;
      ch.set(x, 0, z, bedrock);
      for (int y = 1; y < 150; y++) {
        const double n = hashFloat(wx, y, wz, 77);
        BlockId id = y < 60 ? (n < 0.02 ? coal : stone) : y < 85 ? (n < 0.3 ? gravel : stone) : y < 120 ? (n < 0.35 ? clay : n < 0.5 ? packed : dirt) : dirt;
        ch.set(x, y, z, id);
      }
    }
    // the room: 13 x 5 x 11 (x -6..6, z -3..7) at every slot's x = slot*256
    const int slot = (int)std::floor((ch.cx * 16 + 128.0) / 256.0), ox = slot * 256;
    auto put = [&](int wx, int y, int wz, BlockId id, u8 meta = 0) { const int x = wx - ch.cx * 16, z = wz - ch.cz * 16; if ((unsigned)x < 16 && (unsigned)z < 16 && (unsigned)y < 256) { ch.set(x, y, z, id); ch.setMeta(x, y, z, meta); } };
    for (int z = -4; z <= 8; z++) for (int x = -7; x <= 7; x++) for (int y = FLOOR - 1; y <= FLOOR + 5; y++) {
      const bool shell = x == -7 || x == 7 || z == -4 || z == 8 || y == FLOOR - 1 || y == FLOOR + 5;
      put(ox + x, y, z, shell ? (y == FLOOR - 1 ? floorB : wall) : AIR);
    }
    // a corridor stub east, left for you to dig on from
    for (int x = 7; x <= 9; x++) for (int y = FLOOR; y <= FLOOR + 2; y++) for (int z = 1; z <= 2; z++) put(ox + x, y, z, AIR);
    for (int x = 7; x <= 9; x++) for (int z = 1; z <= 2; z++) { put(ox + x, FLOOR - 1, z, concrete); put(ox + x, FLOOR + 3, z, concrete); }
    // the ladder up to the hatch (use the iron hatch at the top to climb out)
    for (int y = FLOOR; y <= FLOOR + 4; y++) put(ox, y, -3, ladder, 2);
    put(ox, FLOOR + 5, -3, iron);
    put(ox, FLOOR + 4, -2, lamp);
    // furniture: what Moroz left you
    put(ox - 5, FLOOR, 6, bed); put(ox - 5, FLOOR, 5, bed, 4);   // (two blocks: the foot, the head to the north) put(ox - 6, FLOOR, 4, cabinet); put(ox - 3, FLOOR, 7, shelf);
    put(ox + 5, FLOOR, 7, bench); put(ox + 6, FLOOR, 7, stove); put(ox + 6, FLOOR, 5, rack);
    put(ox - 1, FLOOR, 3, table); put(ox - 1, FLOOR, 4, chair); put(ox + 6, FLOOR + 2, 2, frame);
    // two metal lockers against the west wall (no chests in the Zone) and a wall shelf by them
    put(ox - 6, FLOOR, -2, locker, 1); put(ox - 6, FLOOR + 1, -2, lockerTop, 1); put(ox - 6, FLOOR, -1, locker, 1); put(ox - 6, FLOOR + 1, -1, lockerTop, 1);
    put(ox - 6, FLOOR + 1, 1, shelf, 3); put(ox + 6, FLOOR, -2, barrel);
    for (int x : {-4, 4}) for (int z : {0, 5}) put(ox + x, FLOOR + 4, z, lamp);
    // the hatch's marker (sim/zone_core.cpp useBlock) and the stash's first gifts
    const int hx = ox - ch.cx * 16, hz = -3 - ch.cz * 16;
    if ((unsigned)hx < 16 && (unsigned)hz < 16) ch.blockEntities[colIdx(hx, FLOOR + 5, hz)] = json{{"type", "zone_hatch"}};
    const int sx = ox - 6 - ch.cx * 16, sz = -2 - ch.cz * 16;
    if ((unsigned)sx < 16 && (unsigned)sz < 16) {
      json items = json::array(); for (int i = 0; i < 12; i++) items.push_back(nullptr);
      auto slotItem = [&](int s, const char* id, int n) { if (reg().item(id)) items[s] = json{{"id", id}, {"n", n}}; };
      slotItem(0, "iron_shovel", 1); slotItem(1, "iron_pickaxe", 1); slotItem(2, "light_gray_concrete", 12); slotItem(3, "lantern", 1);
      slotItem(4, "spruce_planks", 8); slotItem(5, "canned_stew", 1);
      ch.blockEntities[colIdx(sx, FLOOR, sz)] = json{{"type", "chest"}, {"items", items}};
    }
    if (const int lx = ox - 6 - ch.cx * 16, lz = 1 - ch.cz * 16; (unsigned)lx < 16 && (unsigned)lz < 16) {   // on the shelf: a candle's worth of comfort
      json its = json::array({nullptr, nullptr, nullptr});
      if (reg().item("vodka")) its[0] = json{{"id", "vodka"}, {"n", 1}};
      if (reg().item("canned_fish")) its[2] = json{{"id", "canned_fish"}, {"n", 1}};
      ch.blockEntities[colIdx(lx, FLOOR + 1, lz)] = json{{"type", "shelf"}, {"items", its}};
    }
  }
  void sample(int, int, ColumnSample& s) const override { s = ColumnSample(); s.height = 149; s.top = reg().blockId("dirt"); s.under = reg().blockId("dirt"); }
  int spawnHeight(int, int) const override { return FLOOR; }
};
} // namespace

void registerBunkerWorldGen() {
  WorldGen::registerType("bunker", [](int d, const WorldGenOptions&) -> std::unique_ptr<DimGenerator> {
    if (d == DIM_OVERWORLD) return std::make_unique<BunkerGen>();
    return std::make_unique<VoidGen>();
  });
}
} // namespace dq
