// sim/zone_crates.cpp — STALKER's wooden boxes: no lid, you smash them (a knife, the butt, fists, a bullet) and what's in
// them, if anything, spills out. A crate gen/zone_loot.cpp placed carries its few things in its block entity; one without
// (the builders' props) rolls on the spot. Crates stacked on a smashed one drop down a block. Also the two-tall loot
// furniture's upper halves (`furnTop`), which hand Use and breaking on to the base under them.
#include "sim/zone.h"
#include "sim/gameplay.h"
#include "gen/zone_build.h"
#include "gen/zone_map.h"
#include "core/registry.h"
#include "common/noise.h"

namespace dq::zone {

bool furnBase(World& w, int x, int& y, int z) {
  if (!reg().block(w.getBlock(x, y, z)).has("furnTop")) return false;
  if (!reg().block(w.getBlock(x, y - 1, z)).has("tall")) return false;
  y -= 1;
  return true;
}

void breakCrate(Level& lv, World& w, int x, int y, int z) {
  const BlockId id = w.getBlock(x, y, z);
  if (!id || !reg().block(id).has("zoneBreak")) return;
  const vec3 c = vec3(x, y, z) + vec3(0.5f);
  json ent = w.blockEntity(x, y, z) ? *w.blockEntity(x, y, z) : json();
  if (!ent.is_object() || !ent.contains("items")) {   // a prop crate: a roll, most of them nothing
    Mulberry32 r(hash32(x, y, z, (i32)(lv.info.seed ^ 0x7C3A9E11)));
    json items = zoneLoot("crate", wildDanger(x, z), r), keep = json::array();
    const int n = r() < 0.55 ? 0 : 1 + int(r() * 2);
    for (auto& it : items) if (it.is_object() && (int)keep.size() < n) keep.push_back(it);
    ent = json{{"items", keep}};
  }
  w.setBlockEntity(x, y, z, nullptr);
  w.setBlock(x, y, z, AIR, 0, SetFlags::Urgent);
  spillEntity(lv, w, ent, x, y, z);
  lv.fx->sound("zone_crate_break", c, 18);   // audio/zone_sfx.cpp: the crack and the splinters
  burst(lv, c, reg().block(id).key, 18, 1.6f);
  lv.fx->particles("smoke", c, 3);
  // what was stacked on it comes down a block
  for (int k = 1; k < 4; k++) {
    const BlockId up = w.getBlock(x, y + k, z);
    if (!up || !reg().block(up).has("zoneBreak")) break;
    json e = w.blockEntity(x, y + k, z) ? *w.blockEntity(x, y + k, z) : json();
    const u8 m = w.getMeta(x, y + k, z);
    w.setBlockEntity(x, y + k, z, nullptr); w.setBlock(x, y + k, z, AIR, 0, SetFlags::Urgent);
    w.setBlock(x, y + k - 1, z, up, m, SetFlags::Urgent);
    if (!e.is_null()) w.setBlockEntity(x, y + k - 1, z, e);
  }
}

} // namespace dq::zone
