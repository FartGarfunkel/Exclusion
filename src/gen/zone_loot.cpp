// gen/zone_loot.cpp — L::loot: where the Zone keeps its loot, STALKER / Misery style. No chests: by the room around the spot
// it is furniture to search (wardrobe, fridge, locker, desk, cupboard, bedside cabinet, filing cabinet, toolbox, weapon /
// ammo case, a first-aid box on the wall: Use opens ui/zone_inventory.cpp ZoneLoot), wooden crates to smash (zone_crate,
// sometimes stacked: sim/zone_crates.cpp), or things just lying there (item piles on the floor, a table, a cupboard, a wall
// shelf: Use takes them). Everything is seeded from the spot, so each chunk gets the same whichever order chunks come in.
// Misery is stingy: most containers hold 0-3 things and plenty are empty.
#include "gen/zone_build.h"
#include "core/registry.h"
#include <cstring>
#include <cmath>

namespace dq::zone {
namespace {

enum Room { R_KITCHEN, R_BEDROOM, R_OFFICE, R_WORKSHOP, R_MILITARY, R_MEDICAL, R_OUTDOOR, R_STASH };

struct LB {
  BlockId wardrobe, wardrobeTop, fridge, fridgeTop, locker, lockerTop, desk, cupboard, bedside, filing, toolbox, wcase, ammo, aid,
      crate, pack, pile, shelf, table;
};
const LB& lb() {
  static const LB b = [] {
    const Registry& R = reg(); LB x{};
    auto g = [&](const char* k) { return R.blockId(k); };
    x.wardrobe = g("zone_wardrobe"); x.wardrobeTop = g("zone_wardrobe_top"); x.fridge = g("zone_fridge"); x.fridgeTop = g("zone_fridge_top");
    x.locker = g("zone_locker"); x.lockerTop = g("zone_locker_top"); x.desk = g("zone_desk"); x.cupboard = g("zone_cupboard"); x.bedside = g("zone_bedside");
    x.filing = g("zone_filing"); x.toolbox = g("zone_toolbox"); x.wcase = g("zone_weapon_case"); x.ammo = g("zone_ammo_box"); x.aid = g("zone_first_aid");
    x.crate = g("zone_crate"); x.pack = g("zone_body_pack"); x.pile = g("item_pile"); x.shelf = g("spruce_shelf"); x.table = g("spruce_table");
    return x;
  }();
  return b;
}

// what lies about in each kind of room (and fills the furniture that holds everyday things): id, min, max, weight
struct WI { const char* id; int a, b; float w; };
const std::vector<WI>& lying(Room rm) {
  static const std::vector<WI> kitchen = {{"bread", 1, 1, 5}, {"sausage", 1, 1, 4}, {"canned_stew", 1, 1, 4}, {"canned_fish", 1, 1, 4}, {"crackers", 1, 2, 4},
                                          {"water", 1, 1, 5}, {"vodka", 1, 1, 5}, {"energy", 1, 1, 2}};
  static const std::vector<WI> bedroom = {{"vodka", 1, 1, 4}, {"cloth", 1, 2, 4}, {"bandage", 1, 1, 3}, {"rubles", 20, 150, 3}, {"water", 1, 1, 3},
                                          {"canned_fish", 1, 1, 2}, {"headlamp", 1, 1, 1}, {"documents", 1, 1, 1}, {"antirad", 1, 1, 1}};
  static const std::vector<WI> office = {{"documents", 1, 1, 5}, {"flash_drive", 1, 1, 3}, {"rubles", 50, 300, 3}, {"electronics", 1, 1, 2}, {"water", 1, 1, 2},
                                         {"pda_stalker", 1, 1, 1}, {"vodka", 1, 1, 1}, {"wires", 1, 1, 1}};
  static const std::vector<WI> workshop = {{"scrap_metal", 1, 3, 5}, {"wires", 1, 2, 4}, {"electronics", 1, 1, 3}, {"springs", 1, 2, 3}, {"gun_oil", 1, 1, 2},
                                           {"bolt", 3, 8, 3}, {"lead_sheet", 1, 1, 1}, {"repair_weapon", 1, 1, 1}, {"repair_armor", 1, 1, 1},
                                           {"toolkit_basic", 1, 1, 0.5f}, {"cloth", 1, 2, 2}};
  static const std::vector<WI> military = {{"ammo_545", 10, 30, 5}, {"ammo_9x18", 8, 16, 4}, {"ammo_762x39", 10, 20, 3}, {"ammo_12g_buck", 4, 8, 2},
                                           {"crackers", 1, 2, 3}, {"canned_stew", 1, 1, 3}, {"bandage", 1, 1, 3}, {"medkit_army", 1, 1, 1},
                                           {"grenade_rgd5", 1, 1, 1}, {"water", 1, 1, 2}, {"vodka", 1, 1, 1}};
  static const std::vector<WI> medical = {{"bandage", 1, 2, 5}, {"antirad", 1, 1, 4}, {"medkit", 1, 1, 2}, {"psyblock", 1, 1, 1}, {"water", 1, 1, 2},
                                          {"atlas", 1, 1, 0.3f}, {"energy", 1, 1, 1}};
  static const std::vector<WI> outdoor = {{"bolt", 2, 6, 4}, {"scrap_metal", 1, 2, 3}, {"water", 1, 1, 3}, {"bandage", 1, 1, 3}, {"canned_stew", 1, 1, 2},
                                          {"vodka", 1, 1, 3}, {"ammo_9x18", 6, 12, 3}, {"ammo_12g_buck", 3, 6, 2}, {"cloth", 1, 2, 2}, {"bread", 1, 1, 2}};
  switch (rm) {
    case R_KITCHEN: return kitchen; case R_BEDROOM: return bedroom; case R_OFFICE: return office; case R_WORKSHOP: return workshop;
    case R_MILITARY: return military; case R_MEDICAL: return medical; default: return outdoor;
  }
}
json oneItem(Room rm, Mulberry32& r) {
  const auto& T = lying(rm);
  float tot = 0; for (auto& e : T) tot += e.w;
  float x = float(r()) * tot;
  for (auto& e : T) { if ((x -= e.w) > 0) continue; if (!reg().item(e.id)) return nullptr; return json{{"id", e.id}, {"n", e.a + int(r() * (e.b - e.a + 1))}}; }
  return nullptr;
}
json slots(int cap) { json a = json::array(); for (int i = 0; i < cap; i++) a.push_back(nullptr); return a; }
// how many things a container holds: none at `empty`, else 1..most
int howMany(Mulberry32& r, double empty, int most) { return r() < empty ? 0 : 1 + int(r() * most); }
// a container's contents from a loot table (gen/zone_build.cpp zoneLoot), thinned out, `keep` filtering what may be in it
json roll(const char* kind, int danger, Mulberry32& r, int cap, double empty, int most, bool (*keep)(const std::string&) = nullptr) {
  const json all = zoneLoot(kind, danger, r);
  json out = slots(cap);
  int n = howMany(r, empty, most), s = 0;
  for (auto& it : all) if (s < n && s < cap && it.is_object() && (!keep || keep(it.value("id", std::string())))) out[s++] = it;
  return out;
}
// ...or from what lies about in that kind of room
json rollRoom(Room rm, Mulberry32& r, int cap, double empty, int most) {
  json out = slots(cap);
  const int n = std::min(cap, howMany(r, empty, most));
  for (int i = 0; i < n; i++) out[i] = oneItem(rm, r);
  return out;
}
bool ammoLike(const std::string& id) { return id.rfind("ammo_", 0) == 0 || id.rfind("grenade", 0) == 0 || id == "vog25" || id == "m203_he"; }

Room roomFor(const L& q, const std::string& kind, const char* room, double r) {
  if (room && *room) {
    static const std::pair<const char*, Room> M[] = {{"kitchen", R_KITCHEN}, {"bedroom", R_BEDROOM}, {"home", r < 0.5 ? R_KITCHEN : R_BEDROOM}, {"office", R_OFFICE},
                                                     {"workshop", R_WORKSHOP}, {"industrial", R_WORKSHOP}, {"military", R_MILITARY}, {"medical", R_MEDICAL},
                                                     {"outdoor", R_OUTDOOR}, {"camp", R_OUTDOOR}, {"stash", R_STASH}};
    for (auto& m : M) if (!std::strcmp(room, m.first)) return m.second;
  }
  if (kind == "stash") return R_STASH;
  if (kind == "medical") return R_MEDICAL;
  if (kind == "military") return R_MILITARY;
  if (kind == "safe") return R_OFFICE;
  switch (q.p.type) {
    case S_HUT: case S_HOUSE2: case S_PANEL: return r < 0.5 ? R_KITCHEN : R_BEDROOM;
    case S_OFFICE: case S_PUBLIC: case S_STATION: return r < 0.75 ? R_OFFICE : R_KITCHEN;
    case S_HOSPITAL: return r < 0.6 ? R_MEDICAL : R_OFFICE;
    case S_CHECKPOINT: case S_TOWER: case S_BUNKER: case S_RADAR: case S_HELIPAD: case S_FENCE_RING: return R_MILITARY;
    case S_HANGAR: case S_FACTORY: case S_CHIMNEY: case S_COOLING: case S_REACTOR: case S_TURBINE_HALL: case S_WATERTOWER: case S_PYLON:
    case S_GREENHOUSE: case S_TRADER: return R_WORKSHOP;
    case S_BARN: return r < 0.5 ? R_WORKSHOP : R_OUTDOOR;
    case S_CHURCH: return r < 0.5 ? R_OUTDOOR : R_BEDROOM;
    default: return R_OUTDOOR;
  }
}

// the meta that turns a furniture model's back (+z at facing 0) to a wall beside world (wx, gy, wz); -1: none in this chunk
int wallMeta(const Ctx& c, int wx, int gy, int wz) {
  static const int D[4][3] = {{0, 1, 0}, {-1, 0, 1}, {0, -1, 2}, {1, 0, 3}};   // dx, dz -> meta
  for (auto& d : D) { const BlockId b = c.get(wx + d[0], gy, wz + d[1]); if (b && reg().tSolid[b] && !reg().block(b).has("container")) return d[2]; }
  return -1;
}

struct Spot {
  L& q; int lx, y, lz, gy, meta, wall; bool head; Mulberry32& r; const char* kind;
  bool free(int dy) const { const int g = gy + dy; if (g < 1 || g > 254) return false; const BlockId b = q.get(lx, y + dy, lz); return b == AIR || reg().tReplaceable[b]; }
  void container(BlockId b, BlockId top, json items, int dy = 0, int m = -1) {
    q.put(lx, y + dy, lz, b, (u8)(m >= 0 ? m : meta));
    if (top) q.put(lx, y + dy + 1, lz, top, (u8)(m >= 0 ? m : meta));
    q.entity(lx, y + dy, lz, json{{"type", "chest"}, {"items", std::move(items)}, {"zone", kind}});
  }
  // furniture to search; a two-tall one needs the block above free
  bool furn(BlockId b, BlockId top, json items) { if (!b || (top && !head)) return false; container(b, top, std::move(items)); return true; }
  // things lying on whatever is under (lx, y + dy, lz): an item pile, 1..n of them
  bool pile(int dy, Room rm, int n) {
    const LB& B = lb();
    if (!B.pile || !free(dy)) return false;
    json items = slots(4), rot = json::array({0, 0, 0, 0});
    int k = 0;
    for (int i = 0; i < n && k < 4; i++) { json it = oneItem(rm, r); if (it.is_null()) continue; items[k] = it; rot[k] = std::round(r() * 16) * (3.14159265 / 8); k++; }
    if (!k) return false;
    q.put(lx, y + dy, lz, B.pile);
    q.entity(lx, y + dy, lz, json{{"type", "pile"}, {"items", items}, {"rot", rot}});
    return true;
  }
  bool table(Room rm) {
    const LB& B = lb();
    if (!B.table || !head) return false;
    q.put(lx, y, lz, B.table, (u8)meta);
    pile(1, rm, 1 + (r() < 0.4 ? 1 : 0));
    return true;
  }
  // a wall shelf at head height with a thing or two on it (its model's back is at -z: turned half round from ours)
  bool shelf(Room rm) {
    const LB& B = lb();
    if (!B.shelf || wall < 0 || !head) return false;
    json items = slots(3);
    const int n = 1 + (r() < 0.35 ? 1 : 0);
    for (int i = 0; i < n; i++) items[int(r() * 3)] = oneItem(rm, r);
    q.put(lx, y + 1, lz, B.shelf, (u8)((wall + 2) & 3));
    q.entity(lx, y + 1, lz, json{{"type", "shelf"}, {"items", items}});
    return true;
  }
  // wooden crates to smash: one, sometimes two or three stacked, sometimes another beside
  bool crates(int danger) {
    const LB& B = lb();
    if (!B.crate) return false;
    const int n = 1 + (r() < 0.35 ? 1 : 0) + (r() < 0.12 ? 1 : 0);
    auto one = [&](int x, int dy, int z) {
      q.put(x, y + dy, z, B.crate);
      q.entity(x, y + dy, z, json{{"type", "crate"}, {"items", roll(kind, danger, r, 4, 0.45, 2)}, {"zone", "crate"}});
    };
    for (int i = 0; i < n; i++) { if (i && !free(i)) break; one(lx, i, lz); }
    if (r() < 0.3 && q.in(lx + 1, lz)) {
      const BlockId at = q.get(lx + 1, y, lz), under = q.get(lx + 1, y - 1, lz);
      if ((at == AIR || reg().tReplaceable[at]) && under && reg().tSolid[under]) one(lx + 1, 0, lz);
    }
    return true;
  }
  // a hidden stash: a backpack tucked away, or an ammo box (the PDA's stashes: sim/zone_jobs.cpp)
  bool stash(int danger) {
    const LB& B = lb();
    json items = roll("stash", danger, r, 27, 0.05, 4);
    if (B.pack && r() < 0.6) {
      q.put(lx, y, lz, B.pack);
      q.entity(lx, y, lz, json{{"type", "backpack"}, {"stack", json{{"id", "leather_backpack"}, {"n", 1}}}, {"yaw", r() * 6.2832}, {"items", items}, {"zone", "stash"}});
      return true;
    }
    if (!B.ammo) return false;
    q.put(lx, y, lz, B.ammo, (u8)meta);
    q.entity(lx, y, lz, json{{"type", "chest"}, {"items", items}, {"zone", "stash"}});
    return true;
  }
};

} // namespace

void L::loot(int lx, int y, int lz, const char* kind, BlockId block, const char* room) {
  if (!in(lx, lz)) return;
  const int gy = p.y + y;
  if (gy < 1 || gy > 254) return;
  int wx, wz; world(lx, lz, wx, wz);
  Mulberry32 r(hash32(wx, gy, wz, (i32)(p.seed ^ 0x2545F491)));
  const LB& B = lb();
  const std::string k = kind && *kind ? kind : "crate";
  const int d = p.danger;
  const int wall = wallMeta(c, wx, gy, wz);
  const BlockId above = get(lx, y + 1, lz);
  Spot s{*this, lx, y, lz, gy, wall >= 0 ? wall : int(r() * 4), wall, gy + 1 < 255 && (above == AIR || reg().tReplaceable[above]), r, k.c_str()};
  if (block) { s.container(block, 0, roll(k.c_str(), d, r, 27, 0.25, 3)); return; }
  const Room rm = roomFor(*this, k, room, r());
  const double x = r();
  bool done = false;
  switch (rm) {
    case R_KITCHEN:
      if (x < 0.35) done = s.furn(B.fridge, B.fridgeTop, rollRoom(R_KITCHEN, r, 12, 0.35, 3));
      else if (x < 0.65) { done = s.furn(B.cupboard, 0, rollRoom(R_KITCHEN, r, 9, 0.4, 2)); if (done && r() < 0.3) s.pile(1, R_KITCHEN, 1); }
      else if (x < 0.9) done = s.table(R_KITCHEN);
      break;
    case R_BEDROOM:
      if (x < 0.4) done = s.furn(B.wardrobe, B.wardrobeTop, roll(k.c_str(), d, r, 18, 0.35, 2));
      else if (x < 0.7) done = s.furn(B.bedside, 0, rollRoom(R_BEDROOM, r, 6, 0.4, 2));
      else if (x < 0.85) done = s.shelf(R_BEDROOM);
      break;
    case R_OFFICE:
      if (x < 0.4) done = s.furn(B.desk, 0, k == "safe" ? roll("safe", d, r, 9, 0.15, 3) : roll(k.c_str(), d, r, 9, 0.35, 2));
      else if (x < 0.7) { done = s.furn(B.filing, 0, rollRoom(R_OFFICE, r, 9, 0.35, 2)); if (done && r() < 0.35) s.pile(1, R_OFFICE, 1); }
      else if (x < 0.85) done = s.table(R_OFFICE);
      else done = s.shelf(R_OFFICE);
      break;
    case R_WORKSHOP:
      if (x < 0.35) done = s.furn(B.locker, B.lockerTop, roll(k.c_str(), d, r, 12, 0.3, 3));
      else if (x < 0.6) done = s.furn(B.toolbox, 0, rollRoom(R_WORKSHOP, r, 6, 0.3, 2));
      else if (x < 0.75) done = s.crates(d);
      else if (x < 0.9) done = s.table(R_WORKSHOP);
      break;
    case R_MILITARY:
      if (x < 0.25) done = s.furn(B.locker, B.lockerTop, roll(k.c_str(), d, r, 12, 0.25, 3));
      else if (x < 0.45) done = s.furn(B.wcase, 0, roll("military", d, r, 9, 0.3, 2));
      else if (x < 0.65) done = s.furn(B.ammo, 0, roll("military", d, r, 6, 0.25, 2, ammoLike));
      else if (x < 0.9) done = s.crates(d);
      break;
    case R_MEDICAL:
      if (x < 0.4 && s.wall >= 0 && s.free(1) && B.aid) { s.container(B.aid, 0, roll("medical", d, r, 6, 0.3, 2), 1, s.wall); done = true; }
      else if (x < 0.65) done = s.furn(B.locker, B.lockerTop, roll("medical", d, r, 12, 0.3, 3));
      else if (x < 0.85) done = s.table(R_MEDICAL);
      break;
    case R_OUTDOOR:
      if (x < 0.55) done = s.crates(d);
      else if (x < 0.85) done = s.pile(0, R_OUTDOOR, 1 + (r() < 0.3 ? 1 : 0));
      else done = s.furn(B.ammo, 0, roll(k.c_str(), d, r, 6, 0.35, 2));
      break;
    case R_STASH: done = s.stash(d); break;
  }
  if (!done) done = s.pile(0, rm == R_STASH ? R_OUTDOOR : rm, 1);   // the furniture didn't fit: something left on the floor
  if (!done) s.crates(d);
}

} // namespace dq::zone
