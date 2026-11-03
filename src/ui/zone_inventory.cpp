// ui/zone_inventory.cpp — Exclusion's inventory, in the manner of S.T.A.L.K.E.R. / Misery / STALCRAFT: the character on the
// left (outfit, helmet, primary / secondary / sidearm = hotbar 0-2, knife = 3, detector, the quick bar = hotbar 4-8, the
// artifact belt = bag slots 27-31 as many as the suit hooks, the body's bars, the suit's protection), the backpack as a
// grid where every item takes w x h cells (packed from the bag slots when drawn: nothing about the layout is stored), the
// selected item's card (icon, weight, price, condition, its stats) with its actions, and the weight bar along the bottom.
// Mouse: click selects, drag moves (between the grid and every slot; onto a gun it fits an attachment; out of the panel it
// drops), double / right click uses or equips, wheel scrolls the grid. Keys: arrows move, Enter picks up / places, F uses or
// equips, Q drops (Ctrl: the stack), 1-9 puts the item in that hotbar slot, Tab / the weapon key open the PDA / bench, E closes.
// Controller: d-pad / stick move over every slot, cell and button, A picks up / places, X drops, Y uses / equips (right after
// opening it closes again), B backs out (cancels a pick-up first), LB / RB switch panel. It works for the session's selected
// local player, so split screen is covered. The rules (what goes where) are in the first half of this file.
// The loot screen (crates, lockers, safes, stashes, barrels: your backpack | the card | the container) is at the end (ZoneLoot).
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "client/play.h"
#include "render/chest_layer.h"
#include "sim/zone.h"
#include "sim/controller.h"
#include "audio/audio.h"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <map>

namespace dq {
namespace zone { bool useItemAt(PlayerController& pc, int slot); }   // sim/zone_core.cpp
using namespace ui;

namespace {
using namespace zone;

constexpr int COLS = 10;
constexpr int BELT0 = 27, BELT_N = 5, QUICK0 = 4, QUICK1 = 8;
bool isBag(int i) { return (i >= 9 && i <= 26) || (i >= 32 && i <= 35); }
bool isBelt(int i) { return i >= BELT0 && i < BELT0 + BELT_N; }

std::string sf(const char* f, ...) { char b[256]; va_list ap; va_start(ap, f); std::vsnprintf(b, sizeof b, f, ap); va_end(ap); return b; }
std::string grp(long long v) { std::string s = std::to_string(v); for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert(i, " "); return s; }
const json& J(const ZItem* z, const char* k) {
  static const json none = json::object();
  if (z && z->raw.is_object()) { auto it = z->raw.find(k); if (it != z->raw.end()) return *it; }
  return none;
}
float num(const json& j, const char* k, float d = 0) { auto it = j.find(k); return it != j.end() && it->is_number() ? it->get<float>() : d; }

// ---------------------------------------------------------------- where things are
struct Loc {
  enum K : u8 { None, Inv, Armor, Det, Bag } k = None;   // Det: the detector slot (whichever hotbar slot holds one); Bag: anywhere in the backpack
  int i = 0;
  bool operator==(const Loc& o) const { return k == o.k && i == o.i; }
  bool operator!=(const Loc& o) const { return !(*this == o); }
};
Loc inv(int i) { return Loc{Loc::Inv, i}; }
int beltCount(const Player& p) { return std::min(resist(p).belt, BELT_N); }
int detIndex(const Player& p) {   // the best detector in the hotbar (what the HUD uses)
  int best = -1, bt = 0;
  for (int i = 0; i < 9; i++) if (const ZItem* z = p.inv[i].empty() ? nullptr : zi(p.inv[i].id); z && z->cat == "detector") {
    const int t = J(z, "detector").value("tier", 1);
    if (t > bt) { bt = t; best = i; }
  }
  return best;
}
Loc firstFreeBag(const Player& p) { for (int i = 9; i < 36; i++) if (isBag(i) && p.inv[i].empty()) return inv(i); return Loc{}; }
Loc firstFreeQuick(const Player& p) { for (int i = QUICK0; i <= QUICK1; i++) if (p.inv[i].empty()) return inv(i); return Loc{}; }
Loc resolveSrc(const Player& p, Loc l) {
  if (l.k == Loc::Det) { const int d = detIndex(p); return d >= 0 ? inv(d) : Loc{}; }
  return l;
}
Loc resolveDst(const Player& p, Loc l) {
  if (l.k == Loc::Det) { const int d = detIndex(p); if (d >= 0) return inv(d); const Loc q = firstFreeQuick(p); return q.k ? q : inv(QUICK1); }
  if (l.k == Loc::Bag) return firstFreeBag(p);
  return l;
}
ItemStack* ref(Player& p, Loc l) {
  if (l.k == Loc::Inv && l.i >= 0 && l.i < (int)p.inv.size()) return &p.inv[l.i];
  if (l.k == Loc::Armor && l.i >= 0 && l.i < (int)p.armor.size()) return &p.armor[l.i];
  return nullptr;
}
const ItemStack* cref(const Player& p, Loc l) { return ref(const_cast<Player&>(p), resolveSrc(p, l)); }
bool carriedLoc(const Loc& l, const ItemStack& s) {   // in the bag (or spilled into a belt slot as a plain item): not equipped
  if (l.k != Loc::Inv) return false;
  if (isBag(l.i)) return true;
  const ZItem* z = zi(s.id);
  return isBelt(l.i) && !(z && z->cat == "artifact");
}

bool isQuickCat(const ZItem* z) {
  if (!z) return false;
  static const char* C[] = {"med", "food", "grenade", "throw", "detector", "repair", "toolkit", "gear"};
  for (const char* c : C) if (z->cat == c) return true;
  return false;
}
// may this stack go there?
bool accepts(const Player& p, Loc d, const ItemStack& s, std::string* why = nullptr) {
  auto no = [&](const char* w) { if (why) *why = w; return false; };
  const ZItem* z = zi(s.id);
  if (d.k == Loc::Armor) {
    if (d.i == 0) return z && z->cat == "helmet" ? true : no("That isn't a helmet");
    if (d.i == 1) return z && z->cat == "suit" ? true : no("That isn't an outfit");
    if (d.i == 2) return s.id == "headlamp" ? true : no("The lamp slot takes a head torch");
    return no("No such slot");
  }
  if (d.k != Loc::Inv) return true;
  if (d.i == 0 || d.i == 1) return z && z->isGun ? true : no("Only a gun goes there");
  if (d.i == 2) return z && z->isGun && z->gun.cls == "pistol" ? true : no("The sidearm slot takes a pistol");
  if (d.i == 3) return z && (z->cat == "melee" || z->cat == "grenade" || z->cat == "throw") ? true : no("The knife slot takes a blade or a grenade");
  if (d.i >= QUICK0 && d.i <= QUICK1) return isQuickCat(z) ? true : no("The quick bar takes meds, food, grenades, tools and the detector");
  if (isBelt(d.i)) {
    if (!(z && z->cat == "artifact")) return no("Only artifacts hang on the belt");
    return d.i - BELT0 < beltCount(p) ? true : no("Your outfit has no hook there");
  }
  return true;
}

// move or swap what is at src onto dst (merging stacks, fitting attachments onto guns); false + why on failure
bool moveTo(Player& p, Loc src, Loc dst, std::string& why, Loc* landed = nullptr) {
  Loc dummy; if (!landed) landed = &dummy;
  src = resolveSrc(p, src);
  ItemStack* S = ref(p, src);
  if (!S || S->empty()) return false;
  if (dst.k == Loc::Bag && src.k == Loc::Inv && isBag(src.i)) return false;
  dst = resolveDst(p, dst);
  if (dst.k == Loc::None) { why = "Your backpack is full"; return false; }
  if (src == dst) return false;
  *landed = dst;
  ItemStack* D = ref(p, dst);
  if (!D) return false;
  if (!D->empty()) {
    const ZItem* sz = zi(S->id); const ZItem* dz = zi(D->id);
    if (sz && dz && sz->cat == "attach" && dz->isGun && src.k == Loc::Inv && dst.k == Loc::Inv) return attachTo(p, dst.i, src.i, why);
  }
  if (!accepts(p, dst, *S, &why)) return false;
  if (D->empty()) { *D = std::move(*S); S->clear(); return true; }
  if (D->canMerge(*S)) {
    const int mv = std::min(S->n, D->maxStack() - D->n);
    if (mv > 0) { D->n += mv; S->n -= mv; if (S->n <= 0) S->clear(); return true; }
  }
  if (accepts(p, src, *D)) { std::swap(*S, *D); return true; }
  // D can't go back where S came from: S goes to the bag (dropped on a bag slot) or D is shoved out to the bag
  const Loc f = firstFreeBag(p);
  if (f.k == Loc::None) { why = "No room in the backpack"; return false; }
  if (dst.k == Loc::Inv && isBag(dst.i)) { p.inv[f.i] = std::move(*S); S->clear(); *landed = inv(f.i); }
  else { p.inv[f.i] = std::move(*D); *D = std::move(*S); S->clear(); }
  return true;
}

// would dropping what is at src onto dst work (for the green / red frame while dragging)
bool canDrop(const Player& p, Loc src, Loc dst) {
  const ItemStack* S = cref(p, src);
  if (!S || S->empty()) return false;
  dst = resolveDst(p, dst);
  if (dst.k == Loc::None) return false;
  if (const ItemStack* D = cref(p, dst); D && !D->empty()) {
    const ZItem* sz = zi(S->id); const ZItem* dz = zi(D->id);
    if (sz && dz && sz->cat == "attach" && dz->isGun) return true;
  }
  return accepts(p, dst, *S);
}

// ---------------------------------------------------------------- sizes and packing (Tarkov / Stalker grid)
void itemSize(const ItemStack& s, int& w, int& h) {
  w = h = 1;
  const ZItem* z = zi(s.id);
  if (!z) return;
  const std::string& c = z->cat;
  if (z->isGun) {
    const std::string& k = z->gun.cls;
    if (k == "pistol") { w = 2; h = 1; }
    else if (k == "smg" || k == "carbine") { w = 3; h = 2; }
    else if (k == "shotgun") { w = 4; h = 1; }
    else if (k == "sniper") { w = 5; h = 1; }
    else { w = 4; h = 2; }
  }
  else if (c == "suit") { w = 2; h = 3; }
  else if (c == "helmet") { w = 2; h = 2; }
  else if (c == "melee" || c == "toolkit") { w = 2; h = 1; }
  else if (c == "detector") { w = 1; h = 2; }
  else if (c == "attach" && J(z, "att").value("scope", false)) { w = 2; h = 1; }
}
struct Cell { int slot = 0, x = 0, y = 0, w = 1, h = 1; };
struct Packed { std::vector<Cell> cells; int rows = 0, freeX = 0, freeY = 0, used = 0; };
using SlotList = std::vector<std::pair<int, const ItemStack*>>;   // (slot key, stack): the bag's slots, or a container's
Packed pack(const SlotList& list) {
  Packed r;
  for (auto& [k, s] : list) if (s && !s->empty()) { Cell c; c.slot = k; itemSize(*s, c.w, c.h); r.cells.push_back(c); r.used++; }
  std::stable_sort(r.cells.begin(), r.cells.end(), [](const Cell& a, const Cell& b) { return a.w * a.h != b.w * b.h ? a.w * a.h > b.w * b.h : a.w > b.w; });
  std::vector<std::array<bool, COLS>> occ;
  auto fits = [&](int x, int y, int w, int h) {
    for (int yy = y; yy < y + h; yy++) for (int xx = x; xx < x + w; xx++) if (yy < (int)occ.size() && occ[yy][xx]) return false;
    return true;
  };
  for (Cell& c : r.cells) {
    bool done = false;
    for (int y = 0; !done; y++) for (int x = 0; x + c.w <= COLS && !done; x++) if (fits(x, y, c.w, c.h)) {
      c.x = x; c.y = y; done = true;
      while ((int)occ.size() < y + c.h) occ.push_back(std::array<bool, COLS>{});
      for (int yy = y; yy < y + c.h; yy++) for (int xx = x; xx < x + c.w; xx++) occ[yy][xx] = true;
    }
    r.rows = std::max(r.rows, c.y + c.h);
  }
  r.freeX = 0; r.freeY = (int)occ.size();
  for (int y = 0; y < (int)occ.size(); y++) { bool f = false; for (int x = 0; x < COLS; x++) if (!occ[y][x]) { r.freeX = x; r.freeY = y; f = true; break; } if (f) break; }
  return r;
}
SlotList bagSlots(const Player& p) { SlotList l; for (int i = 9; i < 36; i++) if (isBag(i)) l.push_back({i, &p.inv[i]}); return l; }
Packed pack(const Player& p) { return pack(bagSlots(p)); }

// ---------------------------------------------------------------- what an item looks like
ImU32 catColor(const ZItem* z) {
  if (!z) return col::rgb(0x6a6a5a);
  const std::string& c = z->cat;
  if (c == "weapon" || c == "melee") return col::rgb(0xc8742f);
  if (c == "ammo") return col::rgb(0xb59a3a);
  if (c == "med") return col::rgb(0xc2403f);
  if (c == "food") return col::rgb(0x7f9a42);
  if (c == "artifact") return col::rgb(0x9d86b0);
  if (c == "suit" || c == "helmet") return col::rgb(0x6f8aa0);
  if (c == "attach") return col::rgb(0x59a5a0);
  if (c == "detector" || c == "gear") return col::rgb(0x7aa05a);
  if (c == "throw" || c == "grenade") return col::rgb(0xc8a030);
  return col::rgb(0x8a8672);
}
std::string catName(const ZItem* z) {
  if (!z) return "Item";
  if (z->isGun) {
    static const std::map<std::string, const char*> N = {{"ar", "Assault rifle"}, {"smg", "Submachine gun"}, {"pistol", "Pistol"}, {"shotgun", "Shotgun"}, {"sniper", "Sniper rifle"},
      {"dmr", "Marksman rifle"}, {"carbine", "Carbine"}, {"lmg", "Light machine gun"}, {"launcher", "Grenade launcher"}};
    auto it = N.find(z->gun.cls); return it != N.end() ? it->second : "Weapon";
  }
  static const std::map<std::string, const char*> N = {{"melee", "Melee weapon"}, {"ammo", "Ammunition"}, {"attach", "Weapon attachment"}, {"suit", "Outfit"}, {"helmet", "Helmet"}, {"med", "Medicine"},
    {"food", "Food and drink"}, {"detector", "Artifact detector"}, {"throw", "Throwing item"}, {"grenade", "Grenade"}, {"toolkit", "Toolkit"}, {"repair", "Repair kit"}, {"gear", "Gear"},
    {"money", "Money"}, {"part", "Part"}, {"trophy", "Mutant trophy"}, {"artifact", "Artifact"}};
  auto it = N.find(z->cat); return it != N.end() ? it->second : "Item";
}
float condOf(const ItemStack& s) {   // 0..1, or -1 when it doesn't wear
  const ItemDef* d = s.def(); if (!d) return -1;
  const int dur = d->hasTool ? d->tool.durability : d->props.contains("armor") ? d->props["armor"].value("durability", 0) : 0;
  return dur > 0 ? std::clamp(1.f - (float)s.d / dur, 0.f, 1.f) : -1.f;
}
ImU32 condColor(float c) { return c > 0.66f ? col::rgb(0x8fa856) : c > 0.33f ? col::rgb(0xe2b84c) : col::rgb(0xcc5848); }
std::string slotName(Loc l) {
  if (l.k == Loc::Armor) return l.i == 0 ? "Helmet" : l.i == 1 ? "Outfit" : "Head torch";
  if (l.k == Loc::Det) return "Detector";
  if (l.k == Loc::Inv) {
    static const char* N[] = {"Primary weapon", "Secondary weapon", "Sidearm", "Knife"};
    if (l.i < 4) return N[l.i];
    if (l.i <= QUICK1) return "Quick slot " + std::to_string(l.i - 3);
    if (isBelt(l.i)) return "Artifact belt " + std::to_string(l.i - BELT0 + 1);
    return "Backpack";
  }
  return "";
}

// ---------------------------------------------------------------- the actions on an item
struct Prim { enum K { None, Use, Equip, Unequip, Fit } k = None; Loc to; std::string label; };
Prim primary(const Player& p, Loc l, const ItemStack& s) {
  Prim r;
  const ZItem* z = zi(s.id); if (!z) return r;
  l = resolveSrc(p, l);
  const std::string& c = z->cat;
  if (c == "med" || c == "food" || c == "repair") {
    r.k = Prim::Use;
    r.label = c == "food" ? (J(z, "use").contains("water") && !J(z, "use").contains("food") ? "Drink" : "Eat") : c == "repair" ? "Use kit" : "Use";
    return r;
  }
  if (!carriedLoc(l, s)) { r.k = Prim::Unequip; r.to = Loc{Loc::Bag}; r.label = "To backpack"; return r; }
  auto emptyInv = [&](int i) { return p.inv[i].empty(); };
  if (z->isGun) {
    int t;
    if (z->gun.cls == "pistol") t = emptyInv(2) ? 2 : emptyInv(0) ? 0 : emptyInv(1) ? 1 : 2;
    else t = emptyInv(0) ? 0 : emptyInv(1) ? 1 : 0;
    r.k = Prim::Equip; r.to = inv(t); r.label = "Equip"; return r;
  }
  if (c == "melee") { r.k = Prim::Equip; r.to = inv(3); r.label = "Equip"; return r; }
  if (c == "suit") { r.k = Prim::Equip; r.to = Loc{Loc::Armor, 1}; r.label = "Wear"; return r; }
  if (s.id == "headlamp") { r.k = Prim::Equip; r.to = Loc{Loc::Armor, 2}; r.label = "Wear"; return r; }
  if (c == "helmet") { r.k = Prim::Equip; r.to = Loc{Loc::Armor, 0}; r.label = "Wear"; return r; }
  if (c == "detector") { r.k = Prim::Equip; r.to = Loc{Loc::Det, 0}; r.label = "Equip"; return r; }
  if (c == "grenade" || c == "throw") { const Loc q = firstFreeQuick(p); if (q.k) { r.k = Prim::Equip; r.to = q; r.label = "To quick slot"; } return r; }
  if (c == "artifact") {
    const int n = beltCount(p);
    if (n <= 0) { r.label = "No belt hooks"; return r; }
    int t = BELT0; for (int i = 0; i < n; i++) if (p.inv[BELT0 + i].empty()) { t = BELT0 + i; break; }
    r.k = Prim::Equip; r.to = inv(t); r.label = p.inv[t].empty() ? "To belt" : "Swap onto belt"; return r;
  }
  if (c == "attach") {
    const std::string slot = J(z, "att").value("slot", std::string());
    for (int i = 0; i < 36; i++) {
      const ZItem* g = p.inv[i].empty() ? nullptr : zi(p.inv[i].id);
      if (!g || !g->isGun || !g->gun.fits.contains(slot)) continue;
      const json& arr = g->gun.fits[slot];
      if (std::find(arr.begin(), arr.end(), json(s.id)) == arr.end()) continue;
      r.k = Prim::Fit; r.to = inv(i); r.label = "Fit: " + g->name; return r;
    }
  }
  return r;
}

void tidy(Player& p, bool fill) {
  for (int r = 0; r < 4; r++) {   // what doesn't belong in a gear slot goes elsewhere
    ItemStack& s = p.inv[r];
    if (s.empty() || accepts(p, inv(r), s)) continue;
    bool done = false;
    for (int t = 0; t < 4 && !done; t++) if (t != r && p.inv[t].empty() && accepts(p, inv(t), s)) { p.inv[t] = std::move(s); s.clear(); done = true; }
    if (!done) { const Loc f = firstFreeBag(p); if (f.k) { p.inv[f.i] = std::move(s); s.clear(); done = true; } }
    if (!done) for (int q = QUICK0; q <= QUICK1 && !done; q++) if (p.inv[q].empty() && accepts(p, inv(q), s)) { p.inv[q] = std::move(s); s.clear(); done = true; }
  }
  if (!fill) return;
  for (int r : {2, 0, 1, 3}) {   // empty gear slots take a fitting gun / blade from the quick bar and the bag
    if (!p.inv[r].empty()) continue;
    for (int pass = 0; pass < 2 && p.inv[r].empty(); pass++)
      for (int i = QUICK0; i < 36 && p.inv[r].empty(); i++) {
        if (isBelt(i) || p.inv[i].empty() || !accepts(p, inv(r), p.inv[i])) continue;
        const ZItem* z = zi(p.inv[i].id);
        if (r < 2 && pass == 0 && z && z->isGun && z->gun.cls == "pistol") continue;   // rifles first
        p.inv[r] = std::move(p.inv[i]); p.inv[i].clear();
      }
  }
}

// ---------------------------------------------------------------- the card's text
struct Line { std::string l, v; ImU32 c = 0; bool para = false; };
std::string fxName(const std::string& k) {
  static const std::map<std::string, const char*> N = {{"bullet", "Bullet protection"}, {"burn", "Burn protection"}, {"shock", "Shock protection"}, {"chem", "Chemical protection"},
    {"psy", "Psy protection"}, {"carry", "Carry weight"}, {"stamina", "Stamina"}, {"regen", "Health regeneration"}, {"bleedHeal", "Bleeding healing"}};
  auto it = N.find(k); return it != N.end() ? it->second : k;
}
void cardLines(Player& p, Loc l, const ItemStack& s, std::vector<Line>& o) {
  const ImU32 good = col::rgb(0x9bbd5c), bad = col::rgb(0xd8664f);
  const ZItem* z = zi(s.id);
  l = resolveSrc(p, l);
  o.push_back({"Weight", s.n > 1 ? sf("%.2f kg  (%.2f each)", itemWeight(s), z ? z->w : 0.f) : sf("%.2f kg", itemWeight(s))});
  if (!z) return;
  if (z->price > 0) o.push_back({"Price", grp((long long)z->price) + " RU"});
  const float cd = condOf(s);
  if (cd >= 0) o.push_back({"Condition", sf("%d%%", (int)std::round(cd * 100)), condColor(cd)});
  if (z->isGun) {
    std::string cal = z->gun.cal;
    for (const ZItem* a : allZoneItems()) if (a->cat == "ammo" && J(a, "ammo").value("cal", std::string()) == z->gun.cal) { cal = a->name; break; }
    o.push_back({"Calibre", cal});
    o.push_back({"Damage", z->gun.pellets > 1 ? sf("%.0f x %d", z->gun.dmg, z->gun.pellets) : sf("%.0f", z->gun.dmg)});
    o.push_back({"Rate of fire", sf("%.0f rpm", z->gun.rpm)});
    GunHud H;
    if (l.k == Loc::Inv) { const int saved = p.selected; p.selected = l.i; H = gunHud(p); p.selected = saved; }
    else { H.mag = s.tag.is_object() ? s.tag.value("am", 0) : 0; H.clip = z->gun.mag; }   // (lying in a container)
    o.push_back({"Magazine", sf("%d / %d  %s", H.mag, H.clip, H.ammo.c_str())});
    const json att = gunAttachments(s);
    for (const std::string& slot : gunSlots(s.id)) {
      static const std::map<std::string, const char*> NAMES = {{"optic", "Optic"}, {"muzzle", "Muzzle"}, {"under", "Underbarrel"}, {"mag", "Magazine"}};
      std::string name;
      if (att.contains(slot) && att[slot].is_string()) { const ZItem* a = zi(att[slot].get<std::string>()); name = a ? a->name : att[slot].get<std::string>(); }
      else if (slot == "optic" && !s.tag.is_object() && !z->gun.defaultOptic.empty()) { const ZItem* a = zi(z->gun.defaultOptic); name = a ? a->name : z->gun.defaultOptic; }
      o.push_back({NAMES.at(slot), name.empty() ? "-" : name, name.empty() ? col::dim : 0});
    }
  }
  if (z->cat == "suit" || z->cat == "helmet") {
    const json& e = J(z, z->cat == "suit" ? "suit" : "helmet");
    static const std::pair<const char*, const char*> P[] = {{"bullet", "Bullet"}, {"burn", "Burn"}, {"shock", "Shock"}, {"chem", "Chemical"}, {"rad", "Radiation"}, {"psy", "Psy"}};
    for (auto& [k, n] : P) { const float v = num(e, k); if (v != 0) o.push_back({n, sf("%.0f%%", v * 100), v > 0 ? good : bad}); }
    if (z->cat == "suit") {
      o.push_back({"Belt hooks", std::to_string(e.value("belt", 0))});
      if (num(e, "carry") != 0) o.push_back({"Carry weight", sf("%+.0f kg", num(e, "carry")), good});
      if (e.value("noSprint", false)) o.push_back({"Too heavy to sprint", "", bad});
    } else if (e.value("mask", false)) o.push_back({"Gas mask", "filters the air", good});
  }
  if (z->cat == "artifact") {
    const json& a = J(z, "artifact");
    if (a.contains("fx") && a["fx"].is_object()) for (auto& [k, v] : a["fx"].items()) {
      if (!v.is_number()) continue;
      const float f = v.get<float>();
      const bool pct = k != "carry";
      o.push_back({fxName(k), pct ? sf("%+.0f%%", f * 100) : sf("%+.1f kg", f), f >= 0 ? good : bad});
    }
    o.push_back({"Radiation", sf("%.2f", num(a, "rad")), num(a, "rad") > 0.05f ? bad : 0});
  }
  if (z->cat == "med" || z->cat == "food") {
    const json& u = J(z, "use");
    struct E { const char* k; const char* n; const char* fmt; float sign; bool goodUp; };
    static const E EF[] = {{"heal", "Health", "%+.0f", 1, true}, {"bleed", "Bleeding", "%+.1f", 1, false}, {"rad", "Radiation", "%+.0f", 1, false}, {"psy", "Psy health", "%+.0f", -1, true},
      {"food", "Hunger", "%+.0f", 1, true}, {"water", "Thirst", "%+.0f", 1, true}, {"sleep", "Sleep", "%+.0f", 1, true}, {"stamina", "Stamina", "%+.0f", 1, true}};
    for (const E& e : EF) if (u.contains(e.k)) { const float v = num(u, e.k) * e.sign; o.push_back({e.n, sf(e.fmt, v), (v > 0) == e.goodUp ? good : bad}); }
    if (u.contains("psyGuard")) o.push_back({"Psy protection", sf("%.0f s", num(u, "psyGuard")), good});
    if (u.contains("carryBoost")) o.push_back({"Carry weight", sf("+%.0f kg for %.0f s", num(u, "carryBoost"), num(u, "carryTime", 300)), good});
    if (u.contains("drunk")) o.push_back({"Drunk", sf("%.0f s", num(u, "drunk")), bad});
    if (u.contains("sick")) o.push_back({"Risk of sickness", sf("%.0f%%", num(u, "sick") * 100), bad});
  }
  if (z->cat == "ammo") {
    const json& a = J(z, "ammo");
    o.push_back({"Damage", sf("x%.2f", num(a, "dmg", 1))});
    o.push_back({"Penetration", sf("x%.2f", num(a, "pen", 1))});
    o.push_back({"Gun wear", sf("x%.2f", num(a, "wear", 1))});
  }
  if (z->cat == "attach") {
    const json& a = J(z, "att");
    o.push_back({"Fits", a.value("slot", std::string("?"))});
    const json& m = a.contains("mods") ? a["mods"] : J(nullptr, "");
    static const std::pair<const char*, const char*> M[] = {{"adsZoom", "Zoom"}, {"rangeMul", "Range"}, {"clipMul", "Magazine"}, {"recoilV", "Recoil up"}, {"recoilH", "Recoil side"},
      {"adsSpreadMul", "Aimed spread"}, {"hipSpreadMul", "Hip spread"}, {"reloadMul", "Reload time"}, {"minDmgMul", "Long-range damage"}};
    for (auto& [k, n] : M) if (m.contains(k)) o.push_back({n, sf("x%.2f", num(m, k))});
    if (m.contains("loud")) o.push_back({"Noise", sf("%.0f%%", num(m, "loud") * 100), good});
  }
  if (z->cat == "detector") {
    const json& d = J(z, "detector");
    o.push_back({"Range", sf("%.0f m", num(d, "range"))});
    o.push_back({"Class", std::to_string(d.value("tier", 1))});
  }
  if (z->cat == "grenade") { const json& g = J(z, "grenade"); o.push_back({"Blast radius", sf("%.1f m", num(g, "blast"))}); o.push_back({"Fuse", sf("%.1f s", num(g, "fuse"))}); }
  if (z->cat == "repair") { const json& r = J(z, "repair"); o.push_back({"Restores", sf("%.0f%% (up to %.0f%%)", num(r, "amount"), num(r, "cap"))}); }
  if (z->cat == "toolkit") { const json& t = J(z, "toolkit"); o.push_back({"Bench tier", std::to_string(t.value("tier", 1))}); }
  const std::string desc = z->raw.value("desc", std::string());
  if (!desc.empty()) o.push_back({"", desc, col::muted, true});
}

enum Ac { AC_PRIMARY = 1, AC_QUICK, AC_UNLOAD, AC_BENCH, AC_DROP, AC_PDA, AC_TIDY };
struct Btn { std::string label; int id; bool on = true; };

// ---------------------------------------------------------------- the screen
struct Node {
  enum K : u8 { Slot, Bag, Button } k = Slot;
  Loc loc; int id = 0;
  ImVec2 a{0, 0}, b{0, 0};
  bool inBag = false;
  bool contains(ImVec2 p) const { return p.x >= a.x && p.y >= a.y && p.x < b.x && p.y < b.y; }
  ImVec2 c() const { return ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f); }
};
bool same(const Node& x, const Node& y) { return x.k == y.k && x.loc == y.loc && x.id == y.id; }
int groupOf(const Node& n) { return n.k == Node::Button ? 2 : n.k == Node::Bag ? 1 : (n.loc.k == Loc::Inv && isBag(n.loc.i)) ? 1 : 0; }
struct Rc { ImVec2 a, b; ImVec2 size() const { return b - a; } };

float sc(FontId f, float px) { return px / (fontPx(f) * Z()); }
void T(ImDrawList* dl, FontId f, ImVec2 p, ImU32 c, const std::string& s, float px) { text(dl, f, p, c, s.c_str(), 0, sc(f, px)); }
ImVec2 TS(FontId f, const std::string& s, float px) { return textSize(f, s.c_str(), 0, sc(f, px)); }
float para(ImDrawList* dl, FontId f, ImVec2 p, ImU32 c, const std::string& s, float px, float maxW, float lineH, float maxH) {
  std::vector<std::string> lines; std::string cur, w;
  auto flush = [&] {
    if (w.empty()) return;
    const std::string t = cur.empty() ? w : cur + " " + w;
    if (!cur.empty() && TS(f, t, px).x > maxW) { lines.push_back(cur); cur = w; } else cur = t;
    w.clear();
  };
  for (char ch : s) { if (ch == ' ') flush(); else w += ch; }
  flush();
  if (!cur.empty()) lines.push_back(cur);
  float y = 0;
  for (auto& ln : lines) { if (y + lineH > maxH) break; if (dl) T(dl, f, p + ImVec2(0, y), c, ln, px); y += lineH; }
  return y;
}
void rivet(ImDrawList* dl, ImVec2 c, float r) {
  dl->AddCircleFilled(c, r, col::rgb(0x15160f)); dl->AddCircleFilled(c - ImVec2(r * 0.15f, r * 0.15f), r * 0.7f, col::rgb(0x6a6a56));
}
void grime(ImDrawList* dl, Rc r, u32 seed) {   // rust streaks and scuffs on the plates (deterministic)
  auto h = [&](u32 k) { u32 x = seed * 2654435761u + k * 40503u; x ^= x >> 13; x *= 0x5bd1e995u; x ^= x >> 15; return (x & 0xffff) / 65535.f; };
  const ImVec2 s = r.size();
  for (u32 k = 0; k < 26; k++) {
    const float x = r.a.x + h(k * 4) * s.x, y = r.a.y + h(k * 4 + 1) * s.y, w = (0.02f + h(k * 4 + 2) * 0.12f) * s.x, hh = (0.004f + h(k * 4 + 3) * 0.05f) * s.y;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + w, r.b.x), std::min(y + hh, r.b.y)), k % 3 == 0 ? IM_COL32(120, 64, 24, 20) : IM_COL32(0, 0, 0, 26));
  }
}

// the drawing both screens share (u = one grid cell in pixels this frame)
struct Kit {
  float u = 40;
  // the outer frame: worn plate, rivets, the title strip
  void plate(ImDrawList* dl, Rc F, float padX, const char* title, const std::string& right) {
    dl->AddRectFilledMultiColor(F.a, F.b, col::rgb(0x2b2d23), col::rgb(0x2b2d23), col::rgb(0x191a14), col::rgb(0x191a14));
    grime(dl, F, 1);
    bevel(dl, F.a, F.b, col::pnHi, col::pnLo, std::max(2.f, u * 0.07f));
    dl->AddRect(F.a + ImVec2(1, 1) * (u * 0.1f), F.b - ImVec2(1, 1) * (u * 0.1f), withAlpha(col::emberDeep, 0.8f), 0, 0, std::max(1.f, u * 0.03f));
    for (ImVec2 c : {F.a, ImVec2(F.b.x, F.a.y), ImVec2(F.a.x, F.b.y), F.b}) rivet(dl, c + ImVec2(c.x == F.a.x ? 1 : -1, c.y == F.a.y ? 1 : -1) * (u * 0.28f), u * 0.1f);
    T(dl, F_H3, F.a + ImVec2(padX * u, u * 0.14f), col::amber, title, u * 0.62f);
    const ImVec2 ts = TS(F_BODY, right, u * 0.34f);
    T(dl, F_BODY, ImVec2(F.b.x - padX * u - ts.x, F.a.y + u * 0.3f), col::dim, right, u * 0.34f);
  }
  void panelHead(ImDrawList* dl, Rc r, const char* title, const std::string& right, u32 seed) {
    dl->AddRectFilledMultiColor(r.a, r.b, col::rgb(0x23251d), col::rgb(0x23251d), col::rgb(0x171811), col::rgb(0x171811));
    grime(dl, r, seed);
    bevel(dl, r.a, r.b, col::pnLo, col::pnHi, std::max(1.f, u * 0.045f));
    const Rc h{r.a + ImVec2(u * 0.06f, u * 0.06f), ImVec2(r.b.x - u * 0.06f, r.a.y + u * 0.6f)};
    dl->AddRectFilledMultiColor(h.a, h.b, col::rgb(0x4a3d22), col::rgb(0x3a3220), col::rgb(0x2b2618), col::rgb(0x2b2618));
    dl->AddRectFilled(ImVec2(h.a.x, h.b.y - 2), h.b, withAlpha(col::ember, 0.6f));
    T(dl, F_BIG, h.a + ImVec2(u * 0.22f, u * 0.08f), col::amber, title, u * 0.4f);
    if (!right.empty()) { const ImVec2 ts = TS(F_BODY, right, u * 0.3f); T(dl, F_BODY, ImVec2(h.b.x - u * 0.2f - ts.x, h.a.y + u * 0.13f), col::ink2, right, u * 0.3f); }
  }
  void bar(ImDrawList* dl, Rc r, float f, ImU32 c, const std::string& label, const std::string& value, ImU32 valueCol) {
    dl->AddRectFilled(r.a, r.b, col::rgb(0x0e0f0b));
    if (f > 0) dl->AddRectFilledMultiColor(r.a + ImVec2(1, 1), ImVec2(r.a.x + 1 + (r.size().x - 2) * clampv(f, 0.f, 1.f), r.b.y - 1), withAlpha(c, 0.95f), withAlpha(c, 0.8f), withAlpha(c, 0.55f), withAlpha(c, 0.7f));
    dl->AddRect(r.a, r.b, col::rgb(0x050604));
    const float px = std::min(u * 0.3f, r.size().y * 0.85f);
    T(dl, F_BODY, r.a + ImVec2(u * 0.08f, (r.size().y - px) * 0.5f - 1), col::rgb(0xf0ecdc), label, px);
    const ImVec2 ts = TS(F_BODY, value, px);
    T(dl, F_BODY, ImVec2(r.b.x - ts.x - u * 0.08f, r.a.y + (r.size().y - px) * 0.5f - 1), valueCol ? valueCol : col::rgb(0xf0ecdc), value, px);
  }
  // an item lying in a grid: tinted by its class, icon, count, condition strip, selection / grab / hover / focus frames
  void tile(ImDrawList* cd, Rc r, const ItemStack& s, float alpha, bool hov, bool foc, bool selected, bool grabbed, ImU32 hovCol, float tnow) {
    const float bw = std::max(1.f, u * 0.05f);
    const ImU32 cc = catColor(zi(s.id));
    cd->AddRectFilled(r.a, r.b, col::rgb(0x1d1f17));
    cd->AddRectFilledMultiColor(r.a, r.b, withAlpha(cc, hov ? 0.36f : 0.26f), withAlpha(cc, hov ? 0.30f : 0.20f), withAlpha(cc, 0.10f), withAlpha(cc, 0.14f));
    cd->AddRect(r.a, r.b, withAlpha(cc, 0.75f), 0, 0, bw);
    const float sz = std::min(r.size().x, r.size().y) - u * 0.1f;
    ItemStack t(s.id, 1);
    drawItem(cd, t, ImVec2((r.a.x + r.b.x) * 0.5f - sz * 0.5f, (r.a.y + r.b.y) * 0.5f - sz * 0.5f), sz, alpha, false);
    if (s.n > 1) { const std::string cs = std::to_string(s.n); const ImVec2 ts = TS(F_BODY, cs, u * 0.34f); T(cd, F_BODY, r.b - ts - ImVec2(u * 0.05f, u * 0.03f), col::rgb(0xfff6e0), cs, u * 0.34f); }
    const float cn = condOf(s);
    if (cn >= 0) { const float h = std::max(2.f, u * 0.07f); cd->AddRectFilled(ImVec2(r.a.x + bw, r.b.y - bw - h), ImVec2(r.b.x - bw, r.b.y - bw), IM_COL32(0, 0, 0, 200)); cd->AddRectFilled(ImVec2(r.a.x + bw, r.b.y - bw - h), ImVec2(r.a.x + bw + (r.size().x - 2 * bw) * cn, r.b.y - bw), condColor(cn)); }
    if (selected && !hov) cd->AddRect(r.a, r.b, col::amber, 0, 0, bw * 1.5f);
    if (grabbed) cd->AddRect(r.a, r.b, withAlpha(col::amber, 0.6f + 0.4f * std::sin(tnow * 8)), 0, 0, bw * 2);
    if (hov) cd->AddRect(r.a, r.b, hovCol, 0, 0, bw * 1.5f);
    if (foc) { cd->AddRect(r.a - ImVec2(bw * 2, bw * 2), r.b + ImVec2(bw * 2, bw * 2), col::black, 0, 0, bw * 1.5f); cd->AddRect(r.a - ImVec2(bw, bw), r.b + ImVec2(bw, bw), col::gold, 0, 0, bw * 2); }
  }
  // the card's body: icon, name, class, where it is, condition, the stat lines down to `limit` (s null: the empty card)
  void cardInfo(ImDrawList* dl, Player& p, Rc body, const ItemStack* s, Loc at, const std::string& where,
                const std::string& emptyTitle, const std::string& emptySub, const std::string& hint, float limit) {
    const float W = body.size().x, is = std::min(u * 2.4f, W * 0.36f);
    const Rc ib{body.a, body.a + ImVec2(is, is)};
    dl->AddRectFilled(ib.a, ib.b, col::sl); bevel(dl, ib.a, ib.b, col::slLo, col::slHi, std::max(1.f, u * 0.05f));
    const float tx = body.a.x + is + u * 0.25f, tw = body.b.x - tx;
    if (!s || s->empty()) {
      T(dl, F_BTN, ImVec2(tx, body.a.y), col::ink2, emptyTitle, u * 0.4f);
      T(dl, F_BODY, ImVec2(tx, body.a.y + u * 0.55f), col::dim, emptySub, u * 0.32f);
      para(dl, F_BODY, body.a + ImVec2(0, is + u * 0.3f), col::muted, hint, u * 0.3f, W, u * 0.4f, std::max(0.f, limit - body.a.y - is - u * 0.3f));
      return;
    }
    const ZItem* z = zi(s->id);
    ItemStack t(s->id, 1);
    drawItem(dl, t, ib.a + ImVec2(u * 0.1f, u * 0.1f), is - u * 0.2f, 1, false);
    const float ny = body.a.y;
    const float nh = para(dl, F_BTN, ImVec2(tx, ny), col::ink2, z ? z->name : s->id, u * 0.4f, tw, u * 0.44f, u * 1.0f);
    T(dl, F_BODY, ImVec2(tx, ny + nh + u * 0.05f), catColor(z), catName(z), u * 0.3f);
    T(dl, F_BODY, ImVec2(tx, ny + nh + u * 0.4f), col::dim, where, u * 0.28f);
    const float cd = condOf(*s);
    if (cd >= 0) bar(dl, Rc{ImVec2(tx, ib.b.y - u * 0.3f), ImVec2(body.b.x, ib.b.y)}, cd, condColor(cd), "CONDITION", sf("%d%%", (int)std::round(cd * 100)), 0);
    std::vector<Line> lines; cardLines(p, at, *s, lines);
    float y = ib.b.y + u * 0.3f;
    const float lh = u * 0.4f;
    dl->PushClipRect(ImVec2(body.a.x, y), ImVec2(body.b.x, limit), true);
    for (const Line& ln : lines) {
      if (ln.l == "Condition") continue;   // (the bar above)
      if (ln.para) { y += para(dl, F_BODY, ImVec2(body.a.x, y), ln.c, ln.v, u * 0.29f, W, lh * 0.95f, limit - y) + u * 0.05f; continue; }
      T(dl, F_BODY, ImVec2(body.a.x, y), col::dim, ln.l, u * 0.3f);
      const ImVec2 ts = TS(F_BODY, ln.v, u * 0.3f);
      T(dl, F_BODY, ImVec2(body.b.x - ts.x, y), ln.c ? ln.c : col::rgb(0xe6e2d0), ln.v, u * 0.3f);
      dl->AddLine(ImVec2(body.a.x, y + lh - 2), ImVec2(body.b.x, y + lh - 2), IM_COL32(255, 255, 255, 12));
      y += lh;
    }
    dl->PopClipRect();
  }
};

class ZoneInventory : public Screen, protected Kit {
public:
  const char* id() const override { return "inventory"; }
  bool pausesGame() const override { return false; }
  Player* player(App& a) { return a.session ? a.session->player : nullptr; }

  void onOpen(App& a) override {
    Player* p = player(a);
    if (!p) { uiDefer([](App& x) { x.popScreen(); }); return; }
    tidy(*p, false);
    sel = inv(std::clamp(p->selected, 0, 8));
    focus = Node{}; focus.loc = sel;
    audio = a.audio.get();
    navUsed = a.input.usingPad() || a.input.padCount() > 0;
  }
  bool onBack(App&) override { if (grab.k != Loc::None) { grab = Loc{}; return true; } return false; }

  // ---- per frame: controller and keys --------------------------------------------------------------------
  void update(App& a, double) override {
    Player* pp = player(a); if (!pp) return;
    Player& p = *pp;
    Input& in = a.input;
    if (!pendingTab.empty()) { const std::string t = pendingTab; pendingTab.clear(); openTab(a, t); return; }
    if (p.dead) return;
    frames++;
    if (in.pressed(Act::Inventory) && !in.padButtonPressed(3) && !in.textActive) { a.popScreen(); return; }
    if (in.pressed(Act::Pda)) { openTab(a, "pda"); return; }
    if (in.pressed(Act::Weapon)) { openTab(a, "weapon"); return; }
    const bool pad = in.usingPad();
    const ivec2 nav = in.navStep();
    if (nav != ivec2(0)) { navUsed = true; if (pad) padActed = true; moveFocus(nav.x, nav.y); }
    if (in.padButtonPressed(4)) switchPanel(-1);
    if (in.padButtonPressed(5)) switchPanel(1);
    const bool A = in.padButtonPressed(0) || in.keyPressed(SDL_SCANCODE_RETURN) || in.keyPressed(SDL_SCANCODE_SPACE);
    const bool X = in.padButtonPressed(2), Y = in.padButtonPressed(3);
    const Node* f = findNode(focus);
    if (!f && navUsed && !nodes.empty()) { const Node* best = nullptr; float bd = 1e30f; for (auto& n : nodes) { const float d = std::hypot(n.c().x - focusPos.x, n.c().y - focusPos.y); if (d < bd) { bd = d; best = &n; } } if (best) { focus = *best; f = findNode(focus); } }
    if (f) focusPos = f->c();
    if (f && f->k == Node::Slot && navUsed) sel = f->loc;
    if (f) lastFocus[groupOf(*f)] = focus;
    // the slot keys act on: the one under the mouse, else the focused one
    Loc tgt = f && f->k == Node::Slot ? f->loc : sel;
    if (!pad && hoverLoc.k != Loc::None) tgt = hoverLoc;
    if (A || X || Y) navUsed = true;
    if (A) {
      padActed = true;
      if (f && f->k == Node::Button) { grab = Loc{}; doAction(a, p, f->id); }
      else if (f && f->k == Node::Slot) {
        if (grab.k != Loc::None) { place(a, p, grab, f->loc); grab = Loc{}; }
        else if (const ItemStack* s = cref(p, f->loc); s && !s->empty()) { grab = f->loc; click(); }
      } else if (f && f->k == Node::Bag && grab.k != Loc::None) { place(a, p, grab, Loc{Loc::Bag}); grab = Loc{}; }
    }
    if (X) { padActed = true; dropStack(a, p, grab.k != Loc::None ? grab : tgt, true); grab = Loc{}; }
    if (Y) {
      if (pad && !padActed) { a.popScreen(); return; }   // (the button that opened it shuts it again until something moved)
      padActed = true; doAction(a, p, AC_PRIMARY, grab.k != Loc::None ? grab : tgt); grab = Loc{};
    }
    if (!in.textActive) {
      if (in.keyPressed(SDL_SCANCODE_F)) doAction(a, p, AC_PRIMARY, tgt);
      if (in.keyPressed(SDL_SCANCODE_DELETE) || in.keyPressed(in.keyFor(Act::Drop))) dropStack(a, p, tgt, in.ctrlHeld());
      for (int k = 0; k < 9; k++) if (in.keyPressed((SDL_Scancode)(SDL_SCANCODE_1 + k))) place(a, p, tgt, inv(k));
    }
  }

  // ---- drawing --------------------------------------------------------------------------------------------
  void draw(App& a) override {
    Player* pp = player(a);
    if (!pp || a.topScreen() != this) return;
    Player& p = *pp;
    const float z = Z(), W = (float)a.width, H = (float)a.height;
    constexpr float LW = 11.4f, BW = 10.7f, CW = 7.8f, GAP = 0.45f, PADX = 0.5f, TOPU = 1.05f, BOTU = 1.55f;
    u = std::floor(clampv(std::min((W - 12 * z) / (LW + BW + CW + GAP * 2 + PADX * 2), (H - 12 * z) / (TOPU + BOTU + 10.8f)), 14.f, 54.f * z));
    const float frameH = std::min(H - 12 * z, (TOPU + BOTU + 13.5f) * u), frameW = (LW + BW + CW + GAP * 2 + PADX * 2) * u;
    Rc F{ImVec2(std::floor((W - frameW) * 0.5f), std::floor((H - frameH) * 0.5f)), ImVec2(0, 0)};
    F.b = F.a + ImVec2(frameW, frameH);
    frame = F;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(4, 5, 3, 170));
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##zinv", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    cur.clear();
    const bool pad = a.input.usingPad();
    mouseP = ImGui::GetIO().MousePos; tnow = (float)a.time;
    plate(dl, F, PADX, "INVENTORY", regionName(ifloor(p.pos.x), ifloor(p.pos.z)));
    const float x0 = F.a.x + PADX * u, y0 = F.a.y + TOPU * u, bodyH = frameH - (TOPU + BOTU) * u + 0.2f * u;
    const Rc L{ImVec2(x0, y0), ImVec2(x0 + LW * u, y0 + bodyH)};
    const Rc B{ImVec2(L.b.x + GAP * u, y0), ImVec2(L.b.x + GAP * u + BW * u, y0 + bodyH)};
    const Rc C{ImVec2(B.b.x + GAP * u, y0), ImVec2(B.b.x + GAP * u + CW * u, y0 + bodyH)};
    const Rc Ft{ImVec2(x0, y0 + bodyH + 0.15f * u), ImVec2(F.b.x - PADX * u, F.b.y - 0.2f * u)};
    // what the mouse is over this frame (from last frame's nodes: the layout hardly moves)
    hoverNode = nullptr; bagHover = false;
    if (!pad) {
      for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) if (it->contains(mouseP) && (!it->inBag || inRect(bagVis, mouseP))) { hoverNode = &*it; break; }
      bagHover = inRect(bagVis, mouseP) && (!hoverNode || hoverNode->k == Node::Bag);
    }
    hoverLoc = hoverNode && hoverNode->k == Node::Slot ? hoverNode->loc : Loc{};
    const Loc shown = hoverLoc.k != Loc::None ? hoverLoc : sel;
    drawCharacter(dl, p, L);
    drawBag(dl, p, B);
    drawCard(dl, p, C, shown);
    drawFooter(dl, p, Ft, a);
    // ---- the mouse
    mouse(a, p);
    if (dragging) {
      const ItemStack* s = cref(p, dragSrc);
      if (s && !s->empty()) {
        ItemStack t(s->id, 1);
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        fg->AddRectFilled(mouseP - ImVec2(u, u) * 0.6f, mouseP + ImVec2(u, u) * 0.6f, IM_COL32(10, 10, 6, 150));
        drawItem(fg, t, mouseP - ImVec2(u, u) * 0.6f, u * 1.2f, 0.95f, false);
        if (s->n > 1) T(fg, F_BODY, mouseP + ImVec2(u * 0.1f, u * 0.2f), col::white, std::to_string(s->n), u * 0.34f);
      }
    } else if (grab.k != Loc::None && pad) {
      const ItemStack* s = cref(p, grab);
      if (const Node* f = findNode(focus); f && s && !s->empty()) {
        ItemStack t(s->id, 1);
        const ImVec2 at = f->b - ImVec2(u * 0.55f, u * 0.55f);
        ImGui::GetForegroundDrawList()->AddRectFilled(at, at + ImVec2(u, u) * 0.9f, IM_COL32(10, 10, 6, 190));
        drawItem(ImGui::GetForegroundDrawList(), t, at, u * 0.9f, 1, false);
      }
    }
    nodes = cur;
    ImGui::End();
    ImGui::PopStyleVar();
  }

private:
  Rc frame{};
  std::vector<Node> cur, nodes;
  Node focus;
  Loc sel, grab, hoverLoc, dragSrc;
  const Node* hoverNode = nullptr;
  bool bagHover = false, navUsed = false, padActed = false, scrollToFocus = false, dragArmed = false, dragging = false;
  Node lastFocus[3];
  Rc bagVis{ImVec2(0, 0), ImVec2(0, 0)};
  ImVec2 mouseP{0, 0}, dragFrom{0, 0}, focusPos{0, 0};
  std::string pendingTab;
  int frames = 0;
  AudioEngine* audio = nullptr;
  float tnow = 0;
  Packed packed;

  static bool inRect(const Rc& r, ImVec2 p) { return p.x >= r.a.x && p.y >= r.a.y && p.x < r.b.x && p.y < r.b.y; }
  void click() { playClick(); }
  void tick() { if (audio) audio->play("tick"); }
  const Node* findNode(const Node& k) const { for (auto& n : nodes) if (same(n, k)) return &n; return nullptr; }

  void openTab(App& a, const std::string& key) {
    for (auto& t : menuTabs()) if (t.key == key && (!t.can || t.can(a))) { a.popScreen(); t.open(a); return; }
  }
  // focus moves to the nearest node in a direction (edges and overlap, so a 5-wide rifle and a 1-wide pill still line up)
  void moveFocus(int dx, int dy) {
    const Node* c = findNode(focus);
    if (!c) {   // lost (the item moved away): the nearest node to where it was
      const Node* best = nullptr; float bd = 1e30f;
      for (auto& n : nodes) { const float d = std::hypot(n.c().x - focusPos.x, n.c().y - focusPos.y); if (d < bd) { bd = d; best = &n; } }
      if (best) { focus = *best; tick(); }
      return;
    }
    const Node* best = nullptr; float bd = 1e30f;
    auto gap = [](float a0, float a1, float b0, float b1) { return std::max(0.f, std::max(a0, b0) - std::min(a1, b1)); };
    for (auto& n : nodes) {
      if (same(n, *c)) continue;
      float along, across;
      if (dx) { along = (n.c().x - c->c().x) * dx; across = gap(c->a.y, c->b.y, n.a.y, n.b.y); }
      else    { along = (n.c().y - c->c().y) * dy; across = gap(c->a.x, c->b.x, n.a.x, n.b.x); }
      if (along <= 2) continue;
      const float edge = dx ? (dx > 0 ? n.a.x - c->b.x : c->a.x - n.b.x) : (dy > 0 ? n.a.y - c->b.y : c->a.y - n.b.y);
      const float d = std::max(0.f, edge) + along * 0.15f + across * 3.f;
      if (d < bd) { bd = d; best = &n; }
    }
    if (best) { focus = *best; focusPos = best->c(); tick(); if (best->inBag) scrollToFocus = true; }
  }
  void switchPanel(int dir) {
    const Node* c = findNode(focus);
    const int g = c ? groupOf(*c) : 0, t = (g + dir + 3) % 3;
    const Node* n = findNode(lastFocus[t]);
    if (!n) for (auto& x : nodes) if (groupOf(x) == t) { n = &x; break; }
    if (n) { focus = *n; focusPos = n->c(); navUsed = true; tick(); if (n->inBag) scrollToFocus = true; }
  }

  // ---- doing things ----
  void dropStack(App& a, Player& p, Loc l, bool all) {
    ItemStack* s = ref(p, resolveSrc(p, l));
    if (!s || s->empty()) return;
    ItemStack t = *s;
    if (!all && s->n > 1) { t.n = 1; s->n--; } else s->clear();
    throwStackFromPlayer(a, t);
    click();
  }
  void place(App&, Player& p, Loc src, Loc dst) {
    if (src.k == Loc::None || dst.k == Loc::None) return;
    std::string why;
    Loc at;
    if (moveTo(p, src, dst, why, &at)) { click(); sel = at; }
    else if (!why.empty()) hudToast(why, "warn");
  }
  void doAction(App& a, Player& p, int ac, Loc at = Loc{}) {
    Loc l = at.k != Loc::None ? at : sel;
    ItemStack* s = ref(p, resolveSrc(p, l));
    if (ac == AC_PDA) { pendingTab = "pda"; return; }
    if (ac == AC_BENCH) { pendingTab = "weapon"; return; }
    if (ac == AC_TIDY) { tidy(p, true); click(); return; }
    if (!s || s->empty()) return;
    l = resolveSrc(p, l);
    const ZItem* z = zi(s->id);
    if (ac == AC_DROP) { dropStack(a, p, l, true); return; }
    if (ac == AC_UNLOAD) { if (l.k == Loc::Inv && unloadGun(p, l.i)) click(); else hudToast("Nothing to unload", "warn"); return; }
    if (ac == AC_QUICK) {
      if (l.k == Loc::Inv && l.i >= QUICK0 && l.i <= QUICK1) place(a, p, l, Loc{Loc::Bag});
      else { const Loc q = firstFreeQuick(p); if (q.k) place(a, p, l, q); else hudToast("The quick bar is full", "warn"); }
      return;
    }
    if (ac != AC_PRIMARY) return;
    const Prim pr = primary(p, l, *s);
    if (pr.k == Prim::None) { if (!pr.label.empty()) hudToast(pr.label, "warn"); return; }
    if (pr.k == Prim::Use) {
      PlayerController* pc = a.session ? localController(*a.session) : nullptr;
      if (pc && l.k == Loc::Inv && zone::useItemAt(*pc, l.i)) { click(); sel = l; }
      else hudToast(std::string("Can't use ") + (z ? z->name : "that") + " now", "warn");
      return;
    }
    if (pr.k == Prim::Fit) {
      std::string why;
      if (l.k == Loc::Inv && attachTo(p, pr.to.i, l.i, why)) click(); else if (!why.empty()) hudToast(why, "warn");
      return;
    }
    place(a, p, l, pr.to);
  }

  // ---- widgets ----
  void addNode(Node::K k, Loc l, int id, ImVec2 a, ImVec2 b, bool inBag = false) { Node n; n.k = k; n.loc = l; n.id = id; n.a = a; n.b = b; n.inBag = inBag; cur.push_back(n); }
  bool focused(const Node& n) const { return navUsed && same(n, focus); }
  // one slot: sunk plate, the item (icon, count, condition), a faint label, selection / focus / drag frames
  void slotBox(ImDrawList* dl, Player& p, Loc l, Rc r, const char* label, bool locked = false, bool shadowOfDet = false) {
    const Loc rl = resolveSrc(p, l);
    const ItemStack* s = rl.k != Loc::None ? cref(p, rl) : nullptr;
    const bool has = s && !s->empty();
    const float bw = std::max(1.f, u * 0.05f);
    Node n; n.k = Node::Slot; n.loc = l; n.a = r.a; n.b = r.b;
    const bool hov = hoverNode && same(*hoverNode, n), foc = focused(n);
    dl->AddRectFilled(r.a, r.b, hov ? col::rgb(0x2a2c22) : col::sl);
    if (has) dl->AddRectFilled(r.a + ImVec2(bw, bw), r.b - ImVec2(bw, bw), withAlpha(catColor(zi(s->id)), 0.17f));
    bevel(dl, r.a, r.b, col::slLo, col::slHi, bw);
    if (label && *label) T(dl, F_SMALL, r.a + ImVec2(u * 0.07f, u * 0.03f), withAlpha(col::muted, has ? 0.45f : 0.7f), label, u * 0.24f);
    if (has) {
      const float alpha = locked || shadowOfDet || (dragging && dragSrc == l) ? 0.35f : (grab == l ? 0.55f : 1.f);
      const float sz = std::min(r.size().x, r.size().y) - u * 0.12f;
      ItemStack t(s->id, 1);
      drawItem(dl, t, ImVec2((r.a.x + r.b.x) * 0.5f - sz * 0.5f, (r.a.y + r.b.y) * 0.5f - sz * 0.5f), sz, alpha, false);
      if (s->n > 1) { const std::string c = std::to_string(s->n); const ImVec2 ts = TS(F_BODY, c, u * 0.34f); T(dl, F_BODY, r.b - ts - ImVec2(u * 0.06f, u * 0.04f), col::rgb(0xfff6e0), c, u * 0.34f); }
      const float cd = condOf(*s);
      if (cd >= 0) { const float h = std::max(2.f, u * 0.07f); dl->AddRectFilled(ImVec2(r.a.x + bw, r.b.y - bw - h), ImVec2(r.b.x - bw, r.b.y - bw), IM_COL32(0, 0, 0, 200)); dl->AddRectFilled(ImVec2(r.a.x + bw, r.b.y - bw - h), ImVec2(r.a.x + bw + (r.size().x - 2 * bw) * cd, r.b.y - bw), condColor(cd)); }
      if (shadowOfDet) T(dl, F_SMALL, r.a + ImVec2(u * 0.08f, r.size().y * 0.5f - u * 0.1f), col::dim, "DET", u * 0.26f);
    }
    if (locked) { dl->AddRectFilled(r.a, r.b, IM_COL32(0, 0, 0, 110)); dl->AddLine(r.a, r.b, IM_COL32(150, 60, 40, 140), std::max(1.f, u * 0.05f)); dl->AddLine(ImVec2(r.a.x, r.b.y), ImVec2(r.b.x, r.a.y), IM_COL32(150, 60, 40, 140), std::max(1.f, u * 0.05f)); }
    if (l.k == Loc::Inv && l.i == p.selected) { const ImVec2 q(r.b.x - u * 0.2f, r.a.y); dl->AddTriangleFilled(ImVec2(q.x, q.y), ImVec2(r.b.x, q.y), ImVec2(r.b.x, q.y + u * 0.2f), col::amber); }
    if (sel == l && !hov) dl->AddRect(r.a + ImVec2(bw, bw), r.b - ImVec2(bw, bw), withAlpha(col::amber, 0.8f), 0, 0, bw);
    if (grab == l) dl->AddRect(r.a, r.b, withAlpha(col::amber, 0.6f + 0.4f * std::sin(tnow * 8)), 0, 0, bw * 2);
    if (hov) {
      ImU32 c = col::lineHi;
      if (dragging) c = canDrop(p, dragSrc, l) ? col::rgb(0x8fbf4a) : col::rgb(0xcc5848);
      dl->AddRect(r.a, r.b, c, 0, 0, bw * 1.5f);
    }
    if (foc) { dl->AddRect(r.a - ImVec2(bw * 2, bw * 2), r.b + ImVec2(bw * 2, bw * 2), col::black, 0, 0, bw * 1.5f); dl->AddRect(r.a - ImVec2(bw, bw), r.b + ImVec2(bw, bw), col::gold, 0, 0, bw * 2); }
    cur.push_back(n);
  }
  void drawCharacter(ImDrawList* dl, Player& p, Rc L) {
    panelHead(dl, L, "CHARACTER", p.name, 2);
    const float extra = std::max(0.f, (L.size().y / u - 0.6f) - 9.5f), gap = std::min(extra / 5, 0.5f);
    const ImVec2 O = L.a + ImVec2(0.3f, 0.6f + 0.25f) * u;
    auto R = [&](float x, float y, float w, float h) { return Rc{O + ImVec2(x, y) * u, O + ImVec2(x + w, y + h) * u}; };
    // outfit, helmet, detector and the body's bars
    slotBox(dl, p, Loc{Loc::Armor, 1}, R(0, 0, 2, 3), "OUTFIT");
    slotBox(dl, p, Loc{Loc::Armor, 0}, R(2.15f, 0, 2, 2), "HELMET");
    slotBox(dl, p, Loc{Loc::Armor, 2}, R(2.15f, 2.1f, 0.9f, 1), "LAMP");   // the head torch (L / d-pad up switches it)
    slotBox(dl, p, Loc{Loc::Det, 0}, R(3.1f, 2.1f, 1.05f, 1), "DETECTOR");
    struct Bar { const char* n; float f; ImU32 c; std::string v; bool badHigh; };
    const float hp = p.hp / std::max(1.f, p.maxHp), st = zget(p, "stamina", 100) / 100, rad = zget(p, "rad") / 100, bl = zget(p, "bleed") / 10;
    const float fd = zget(p, "food", 100) / 100, wt = zget(p, "water", 100) / 100, sl = zget(p, "sleep", 100) / 100, ps = zget(p, "psy", 100) / 100;
    const Bar B[] = {{"HEALTH", hp, col::rgb(0xc2403f), sf("%d", (int)std::round(hp * 100)), false}, {"STAMINA", st, col::rgb(0xc8b24a), sf("%d", (int)std::round(st * 100)), false},
      {"RADIATION", rad, col::rgb(0x8bc34a), sf("%d", (int)std::round(rad * 100)), true}, {"BLEEDING", bl, col::rgb(0xa02020), bl > 0.005f ? sf("%.1f", bl * 10) : "-", true},
      {"FOOD", fd, col::rgb(0xb8863a), sf("%d", (int)std::round(fd * 100)), false}, {"WATER", wt, col::rgb(0x4a86b0), sf("%d", (int)std::round(wt * 100)), false},
      {"SLEEP", sl, col::rgb(0x6a74a8), sf("%d", (int)std::round(sl * 100)), false}, {"PSY", ps, col::rgb(0x9a62b8), sf("%d", (int)std::round(ps * 100)), false}};
    const float bx = 4.4f, bw = LW_INNER - bx, rowH = 3.1f / 8;
    for (int i = 0; i < 8; i++) {
      const Bar& b = B[i];
      const bool crit = b.badHigh ? b.f > 0.5f : b.f < 0.2f;
      bar(dl, R(bx, i * rowH + 0.02f, bw, rowH - 0.05f), b.f, b.c, b.n, b.v, crit ? col::rgb(0xff8a70) : 0);
    }
    // weapons
    float y = 3.1f + 0.25f + gap;
    slotBox(dl, p, inv(0), R(0, y, 5.3f, 2), "1  PRIMARY");
    slotBox(dl, p, inv(1), R(5.5f, y, 5.3f, 2), "2  SECONDARY");
    y += 2.0f + 0.2f + gap;
    slotBox(dl, p, inv(2), R(0, y, 3.1f, 1.1f), "3  SIDEARM");
    slotBox(dl, p, inv(3), R(3.2f, y, 2.1f, 1.1f), "4  KNIFE");
    const int di = detIndex(p);
    for (int k = 0; k < 5; k++) slotBox(dl, p, inv(QUICK0 + k), R(5.5f + k * 1.075f, y, 1.0f, 1.1f), sf("%d", 5 + k).c_str(), false, QUICK0 + k == di);
    y += 1.1f + 0.15f + gap;
    // the artifact belt and the suit's protection
    T(dl, F_BODY, O + ImVec2(0.02f, y) * u, col::amber, "ARTIFACT BELT", u * 0.3f);
    T(dl, F_BODY, O + ImVec2(6.1f, y) * u, col::amber, "PROTECTION", u * 0.3f);
    const int bc = beltCount(p);
    for (int k = 0; k < BELT_N; k++) slotBox(dl, p, inv(BELT0 + k), R(k * 1.15f, y + 0.42f, 1.1f, 1.1f), sf("%d", k + 1).c_str(), k >= bc);
    const Resist rs = resist(p);
    const std::pair<const char*, float> PR[] = {{"BUL", rs.bullet}, {"BRN", rs.burn}, {"SHK", rs.shock}, {"CHM", rs.chem}, {"RAD", rs.rad}, {"PSY", rs.psy}};
    for (int k = 0; k < 6; k++) {
      const ImVec2 q = O + ImVec2(6.1f + (k % 3) * 1.55f, y + 0.45f + (k / 3) * 0.58f) * u;
      dl->AddRectFilled(q, q + ImVec2(1.5f, 0.52f) * u, col::rgb(0x12130e));
      dl->AddRect(q, q + ImVec2(1.5f, 0.52f) * u, col::rgb(0x34372b));
      T(dl, F_BODY, q + ImVec2(0.08f, 0.1f) * u, col::dim, PR[k].first, u * 0.27f);
      const std::string v = sf("%d", (int)std::round(PR[k].second * 100));
      const ImVec2 ts = TS(F_BODY, v, u * 0.3f);
      T(dl, F_BODY, q + ImVec2(1.44f * u - ts.x, 0.09f * u), PR[k].second > 0.005f ? col::rgb(0xd9d4bc) : PR[k].second < -0.005f ? col::rgb(0xd8664f) : col::dim, v, u * 0.3f);
    }
    y += 0.42f + 1.1f + 0.2f + gap;
    const Rc mr = R(0, y, LW_INNER, 0.62f);
    dl->AddRectFilled(mr.a, mr.b, col::rgb(0x12130e)); dl->AddRect(mr.a, mr.b, col::rgb(0x34372b));
    T(dl, F_BODY, mr.a + ImVec2(0.12f, 0.12f) * u, col::dim, "MONEY", u * 0.3f);
    const std::string m = grp((long long)std::llround(money(p))) + " RU";
    const ImVec2 ms = TS(F_BTN, m, u * 0.42f);
    T(dl, F_BTN, ImVec2(mr.b.x - ms.x - u * 0.15f, mr.a.y + (mr.size().y - ms.y) * 0.5f), col::amber, m, u * 0.42f);
  }
  static constexpr float LW_INNER = 10.8f;

  void drawBag(ImDrawList* dl, Player& p, Rc Bp) {
    packed = pack(p);
    panelHead(dl, Bp, "BACKPACK", sf("%d / 22", packed.used), 3);
    const Rc body{Bp.a + ImVec2(0.22f, 0.6f + 0.22f) * u, Bp.b - ImVec2(0.22f, 0.22f) * u};
    ImGui::SetCursorScreenPos(body.a);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, std::max(6.f, u * 0.2f));
    ImGui::BeginChild("##zbag", body.size(), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings);
    ImDrawList* cd = ImGui::GetWindowDrawList();
    const ImVec2 org = ImGui::GetCursorScreenPos();
    const int visRows = (int)std::ceil(body.size().y / u);
    const int rows = std::max(visRows, packed.rows + 1);
    ImGui::Dummy(ImVec2(COLS * u, rows * u));
    bagVis = Rc{ImGui::GetWindowPos(), ImGui::GetWindowPos() + ImVec2(body.size().x - std::max(6.f, u * 0.2f), body.size().y)};
    const float bw = std::max(1.f, u * 0.05f);
    for (int y = 0; y < rows; y++) for (int x = 0; x < COLS; x++) {
      const ImVec2 a = org + ImVec2(x * u, y * u);
      if (a.y + u < bagVis.a.y || a.y > bagVis.b.y) continue;
      cd->AddRectFilled(a, a + ImVec2(u, u), ((x + y) & 1) ? col::rgb(0x15170f) : col::rgb(0x181a12));
      cd->AddRect(a, a + ImVec2(u, u), col::rgb(0x0c0d09));
    }
    for (const Cell& c : packed.cells) {
      const ItemStack& s = p.inv[c.slot];
      const Rc r{org + ImVec2(c.x * u, c.y * u) + ImVec2(1, 1), org + ImVec2((c.x + c.w) * u, (c.y + c.h) * u) - ImVec2(1, 1)};
      Node n; n.k = Node::Slot; n.loc = inv(c.slot); n.a = r.a; n.b = r.b; n.inBag = true;
      const bool vis = r.b.y > bagVis.a.y && r.a.y < bagVis.b.y;
      cur.push_back(n);
      if (!vis) continue;
      const bool hov = hoverNode && same(*hoverNode, n);
      const ImU32 hc = dragging ? (canDrop(p, dragSrc, n.loc) ? col::rgb(0x8fbf4a) : col::rgb(0xcc5848)) : col::lineHi;
      tile(cd, r, s, (dragging && dragSrc == n.loc) ? 0.3f : (grab == n.loc ? 0.5f : 1.f), hov, focused(n), sel == n.loc, grab == n.loc, hc, tnow);
    }
    // the first free cell is a place to put things down (controller)
    {
      Node n; n.k = Node::Bag; n.a = org + ImVec2(packed.freeX * u, packed.freeY * u) + ImVec2(1, 1); n.b = n.a + ImVec2(u, u) - ImVec2(2, 2); n.inBag = true;
      const bool foc = focused(n);
      if (foc || (bagHover && dragging)) cd->AddRect(n.a, n.b, foc ? col::gold : col::rgb(0x8fbf4a), 0, 0, bw * 2);
      cur.push_back(n);
    }
    if (scrollToFocus) {
      scrollToFocus = false;
      if (const Node* f = findNode(focus); f && f->inBag) {
        const float top = bagVis.a.y, bot = bagVis.b.y, sy = ImGui::GetScrollY();
        if (f->a.y < top) ImGui::SetScrollY(sy - (top - f->a.y) - 2); else if (f->b.y > bot) ImGui::SetScrollY(sy + (f->b.y - bot) + 2);
      }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    if (dragging && bagHover) dl->AddRect(body.a, body.b, withAlpha(col::rgb(0x8fbf4a), 0.8f), 0, 0, 2.f);
  }

  void drawCard(ImDrawList* dl, Player& p, Rc Cr, Loc shown) {
    panelHead(dl, Cr, "ITEM", "", 4);
    const Rc body{Cr.a + ImVec2(0.3f, 0.6f + 0.3f) * u, Cr.b - ImVec2(0.3f, 0.3f) * u};
    const float W = body.size().x;
    const Loc rl = resolveSrc(p, shown);
    const ItemStack* s = rl.k != Loc::None ? cref(p, rl) : nullptr;
    const bool has = s && !s->empty();
    const ZItem* z = has ? zi(s->id) : nullptr;
    // the buttons first (they sit at the bottom)
    std::vector<Btn> btns;
    if (has) {
      const Prim pr = primary(p, rl, *s);
      if (pr.k != Prim::None || !pr.label.empty()) btns.push_back({pr.label.empty() ? "Use" : pr.label, AC_PRIMARY, pr.k != Prim::None});
      if (rl.k == Loc::Inv && rl.i >= QUICK0 && rl.i <= QUICK1 && pr.k == Prim::Use) btns.push_back({"To backpack", AC_QUICK});
      else if (carriedLoc(rl, *s) && isQuickCat(z) && pr.k == Prim::Use) btns.push_back({"To quick slot", AC_QUICK, firstFreeQuick(p).k != Loc::None});
      if (z && z->isGun && s->tag.is_object() && s->tag.value("am", 0) > 0) btns.push_back({"Unload", AC_UNLOAD});
      if (z && (z->isGun || z->cat == "attach")) btns.push_back({"Weapon bench", AC_BENCH});
      btns.push_back({"Drop", AC_DROP});
    }
    const float bh = u * 0.78f, bgap = u * 0.1f, bw = (W - bgap) * 0.5f;
    const int brows = ((int)btns.size() + 1) / 2;
    const float btop = body.b.y - brows * (bh + bgap) + bgap;
    for (size_t i = 0; i < btns.size(); i++) {
      const Rc r{ImVec2(body.a.x + (i % 2) * (bw + bgap), btop + (i / 2) * (bh + bgap)), ImVec2(body.a.x + (i % 2) * (bw + bgap) + bw, btop + (i / 2) * (bh + bgap) + bh)};
      Node n; n.k = Node::Button; n.id = btns[i].id; n.a = r.a; n.b = r.b;
      const bool hov = hoverNode && same(*hoverNode, n), foc = focused(n);
      btnFace(dl, r.a, r.b, hov || foc, hov && ImGui::IsMouseDown(0), btns[i].on ? 1.f : 0.45f);
      const ImVec2 ts = TS(F_MID, btns[i].label, u * 0.34f);
      T(dl, F_MID, ImVec2((r.a.x + r.b.x - ts.x) * 0.5f, (r.a.y + r.b.y - ts.y) * 0.5f), btns[i].on ? (hov || foc ? col::amber : col::ink2) : col::dim, btns[i].label, u * 0.34f);
      if (foc) dl->AddRect(r.a - ImVec2(2, 2), r.b + ImVec2(2, 2), col::gold, 0, 0, 2.f);
      cur.push_back(n);
    }
    // the header (icon, name, class, condition) and the lines
    cardInfo(dl, p, body, has ? s : nullptr, shown, has ? (carriedLoc(rl, *s) ? std::string("In the backpack") : slotName(shown)) : std::string(),
             shown.k == Loc::None ? "Nothing selected" : slotName(shown), shown.k == Loc::None ? "" : "Empty",
             "Pick an item with the mouse or the d-pad. Drag it onto a slot, or press the action keys shown below.", btop - u * 0.1f);
  }

  void drawFooter(ImDrawList* dl, Player& p, Rc Ft, App& a) {
    dl->AddRectFilledMultiColor(Ft.a, Ft.b, col::rgb(0x23251d), col::rgb(0x23251d), col::rgb(0x171811), col::rgb(0x171811));
    bevel(dl, Ft.a, Ft.b, col::pnLo, col::pnHi, std::max(1.f, u * 0.045f));
    // weight
    const float w = carryWeight(p), cap = std::max(1.f, carryCap(p)), r = w / cap;
    const ImU32 wc = r < 0.8f ? col::rgb(0x6f8247) : r < 1.f ? col::rgb(0xd09a2c) : col::rgb(0xc2403f);
    const float bw = u * 9.0f, ph = Ft.size().y;
    T(dl, F_BODY, ImVec2(Ft.a.x + u * 0.25f, Ft.a.y + (ph - u * 0.3f) * 0.5f - 1), col::amber, "WEIGHT", u * 0.3f);
    const Rc br{ImVec2(Ft.a.x + u * 1.4f, Ft.a.y + ph * 0.5f - u * 0.26f), ImVec2(Ft.a.x + u * 1.4f + bw, Ft.a.y + ph * 0.5f + u * 0.26f)};
    dl->AddRectFilled(br.a, br.b, col::rgb(0x0e0f0b));
    dl->AddRectFilled(br.a + ImVec2(1, 1), ImVec2(br.a.x + 1 + (bw - 2) * clampv(r / 1.5f, 0.f, 1.f), br.b.y - 1), wc);
    dl->AddRect(br.a, br.b, col::rgb(0x050604));
    dl->AddLine(ImVec2(br.a.x + bw / 1.5f, br.a.y), ImVec2(br.a.x + bw / 1.5f, br.b.y), IM_COL32(255, 255, 255, 120));   // the limit
    const std::string ws = sf("%.1f / %.1f kg", w, cap);
    const ImVec2 wsz = TS(F_BODY, ws, u * 0.32f);
    T(dl, F_BODY, ImVec2((br.a.x + br.b.x - wsz.x) * 0.5f, (br.a.y + br.b.y - wsz.y) * 0.5f), col::rgb(0xf6f2e2), ws, u * 0.32f);
    if (moveSlow(p) < 0.999f) T(dl, F_BODY, ImVec2(br.b.x + u * 0.2f, Ft.a.y + (ph - u * 0.3f) * 0.5f - 1), wc, r > 1.25f ? "OVERLOADED" : r > 1.f ? "Slowed" : "Heavy", u * 0.3f);
    // the buttons, right-aligned
    const std::pair<const char*, int> BT[] = {{"Weapon bench", AC_BENCH}, {"PDA", AC_PDA}, {"Tidy gear", AC_TIDY}};
    float x = Ft.b.x - u * 0.15f;
    for (int i = 2; i >= 0; i--) {
      const float bwid = TS(F_MID, BT[i].first, u * 0.34f).x + u * 0.7f;
      const Rc b{ImVec2(x - bwid, Ft.a.y + u * 0.18f), ImVec2(x, Ft.b.y - u * 0.18f)};
      Node n; n.k = Node::Button; n.id = BT[i].second; n.a = b.a; n.b = b.b;
      const bool hov = hoverNode && same(*hoverNode, n), foc = focused(n);
      btnFace(dl, b.a, b.b, hov || foc, hov && ImGui::IsMouseDown(0));
      const ImVec2 ts = TS(F_MID, BT[i].first, u * 0.34f);
      T(dl, F_MID, ImVec2((b.a.x + b.b.x - ts.x) * 0.5f, (b.a.y + b.b.y - ts.y) * 0.5f), hov || foc ? col::amber : col::ink2, BT[i].first, u * 0.34f);
      if (foc) dl->AddRect(b.a - ImVec2(2, 2), b.b + ImVec2(2, 2), col::gold, 0, 0, 2.f);
      cur.push_back(n);
      x -= bwid + u * 0.12f;
    }
    const std::string hint = a.input.usingPad() ? "A pick up / place    X drop    Y use / equip    B back    LB RB panel"
                                                 : "Drag to move    RMB / dbl-click use or equip    F use    Q drop    1-9 to slot    E close";
    const float hx = br.b.x + u * 2.2f;
    if (x - hx > TS(F_BODY, hint, u * 0.27f).x) T(dl, F_BODY, ImVec2(hx, Ft.a.y + (ph - u * 0.27f) * 0.5f - 1), col::dim, hint, u * 0.27f);
  }

  // ---- the mouse: click selects, drag moves, double / right click acts ----
  void mouse(App& a, Player& p) {
    ImGuiIO& io = ImGui::GetIO();
    if (a.input.usingPad()) { dragArmed = dragging = false; return; }
    const bool moved = io.MouseDelta.x != 0 || io.MouseDelta.y != 0;
    if (moved) navUsed = false;
    if (hoverNode && (moved || ImGui::IsMouseClicked(0))) { focus = *hoverNode; focusPos = hoverNode->c(); }
    if (ImGui::IsMouseClicked(0)) {
      if (hoverNode && hoverNode->k == Node::Button) { doAction(a, p, hoverNode->id); grab = Loc{}; }
      else if (hoverNode && hoverNode->k == Node::Slot) {
        if (grab.k != Loc::None) { place(a, p, grab, hoverNode->loc); grab = Loc{}; }
        else {
          sel = hoverNode->loc;
          const ItemStack* s = cref(p, sel);
          if (s && !s->empty()) { dragArmed = true; dragSrc = sel; dragFrom = mouseP; if (ImGui::IsMouseDoubleClicked(0)) { doAction(a, p, AC_PRIMARY, sel); dragArmed = false; } }
        }
      } else if (bagHover && grab.k != Loc::None) { place(a, p, grab, Loc{Loc::Bag}); grab = Loc{}; }
    }
    if (dragArmed && !dragging && ImGui::IsMouseDown(0) && std::hypot(mouseP.x - dragFrom.x, mouseP.y - dragFrom.y) > std::max(5.f, u * 0.15f)) dragging = true;
    if (!ImGui::IsMouseDown(0) && (dragArmed || dragging)) {
      if (dragging) {
        if (hoverNode && hoverNode->k == Node::Slot) place(a, p, dragSrc, hoverNode->loc);
        else if (bagHover) place(a, p, dragSrc, Loc{Loc::Bag});
        else if (!inRect(frame, mouseP)) dropStack(a, p, dragSrc, true);
      }
      dragArmed = dragging = false;
    }
    if (ImGui::IsMouseClicked(1) && hoverNode && hoverNode->k == Node::Slot) { sel = hoverNode->loc; doAction(a, p, AC_PRIMARY, sel); }
  }
};
// ---------------------------------------------------------------- the loot screen
// A crate, locker, safe, stash, barrel... opened in a Zone world (ui_main.cpp onScreenOpen and openInventory send block containers
// here, see zoneLootFits): the backpack on the left with the weight bar and money under it, the selected item's card in the middle,
// the container's contents on the right as the same packed w x h grid, with Take (Store for a backpack item) / Take all / Close under
// it. The contents are the block entity's `items` array, read again every frame (someone else may be at the same crate) and written
// back through World::blockEntityChanged, which saves the chunk and sends the change on (guest -> host -> everyone near). Like
// STALKER, you may take more than you can carry: the bar goes red and a warning shows, nothing is refused.
// Mouse: click selects, double / right click or a drag across moves an item to the other side. Keys: arrows, Enter / Space move it
// across, F takes all, Tab switches side, E / Esc close. Controller: d-pad / stick, A moves across, Y takes all, LB / RB switch side,
// B closes.
std::string containerTitle(const json* e, const BlockDef& b) {
  static const std::map<std::string, const char*> N = {{"crate", "Crate"}, {"locker", "Locker"}, {"military", "Military crate"}, {"medical", "Medical cabinet"},
    {"safe", "Safe"}, {"stash", "Stash"}, {"body", "Dead stalker's pack"}};
  const std::string k = e && e->is_object() ? e->value("zone", std::string()) : std::string();
  if (k == "stash" || k == "body") return N.at(k);
  if (b.key.rfind("zone_", 0) == 0 && b.key != "zone_body_pack") return b.name;   // the Zone's furniture: Wardrobe, Fridge, Metal Locker... (gen/zone_loot.cpp)
  if (auto it = N.find(k); it != N.end()) return it->second;
  return b.key == "barrel" ? "Barrel" : "Container";
}
std::string lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
std::string upper(std::string s) { for (char& c : s) c = (char)std::toupper((unsigned char)c); return s; }

enum LootBtn { LB_MOVE = 1, LB_ALL, LB_CLOSE };
struct LNode {
  int side = -1, i = -1, btn = 0;   // side 0: the backpack (p.inv slot), 1: the container (items index); btn: a button
  ImVec2 a{0, 0}, b{0, 0};
  bool inGrid = false;
  bool contains(ImVec2 p) const { return p.x >= a.x && p.y >= a.y && p.x < b.x && p.y < b.y; }
  ImVec2 c() const { return ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f); }
  int group() const { return btn ? 2 : side; }
};
bool same(const LNode& x, const LNode& y) { return x.side == y.side && x.i == y.i && x.btn == y.btn; }

class ZoneLoot : public Screen, protected Kit {
public:
  explicit ZoneLoot(ivec3 at) : pos(at) {}
  const char* id() const override { return "inventory"; }
  bool pausesGame() const override { return false; }
  Player* player(App& a) { return a.session ? a.session->player : nullptr; }

  void onOpen(App& a) override {
    if (!player(a) || !load(a)) { uiDefer([](App& x) { x.popScreen(); }); return; }
    opened = true;
    chestScreenOpened(pos, true);
    audio = a.audio.get();
    navUsed = a.input.usingPad() || a.input.padCount() > 0;
  }
  void onClose(App& a) override {
    if (!opened) return;
    chestScreenOpened(pos, false);
    // a dead stalker's pack, emptied: gone (sim/zone_jobs.cpp drops them)
    if (a.session && a.session->level && !a.session->level->isRemote()) {
      World& w = a.session->world(); const json* e = w.blockEntity(pos.x, pos.y, pos.z);
      bool empty = e && e->is_object() && e->value("zone", std::string()) == "body";
      if (empty) for (auto& it : e->value("items", json::array())) if (!ItemStack::fromJson(it).empty()) { empty = false; break; }
      if (empty) { w.setBlockEntity(pos.x, pos.y, pos.z, nullptr); w.setBlock(pos.x, pos.y, pos.z, AIR); }
    }
    if (a.audio && a.session && !reg().block(a.session->world().getBlock(pos.x, pos.y, pos.z)).has("openSound")) a.audio->playAt("chest_close", vec3(pos) + vec3(0.5f));
  }

  void update(App& a, double) override {
    Player* pp = player(a); if (!pp) return;
    Player& p = *pp;
    // gone (broken, blown up, another realm) or walked away: shut
    if (closeReq || p.dead || !load(a) || glm::distance(p.pos, vec3(pos) + vec3(0.5f)) > 7.f) { a.popScreen(); return; }
    Input& in = a.input;
    if (++frames < 3) return;   // (the press that opened it)
    if (!in.textActive && in.keyPressed(in.keyFor(Act::Inventory))) { a.popScreen(); return; }   // E (the pad's inventory button is Y: take all here)
    const bool pad = in.usingPad();
    const ivec2 nav = in.navStep();
    if (nav != ivec2(0)) { navUsed = true; moveFocus(nav.x, nav.y); }
    if (in.padButtonPressed(4)) switchSide(-1);
    if (in.padButtonPressed(5) || (!in.textActive && in.keyPressed(SDL_SCANCODE_TAB))) switchSide(1);
    const LNode* f = findNode(focus);
    if (!f && navUsed && !nodes.empty()) { if ((f = nearest(focusPos, focus.group()))) focus = *f; }   // its item moved away: the nearest one
    if (f) { focusPos = f->c(); lastFocus[f->group()] = focus; if (navUsed && !f->btn) sel = *f; }
    const bool A = in.padButtonPressed(0) || (!in.textActive && (in.keyPressed(SDL_SCANCODE_RETURN) || in.keyPressed(SDL_SCANCODE_SPACE)));
    const bool Y = in.padButtonPressed(3) || (!in.textActive && in.keyPressed(SDL_SCANCODE_F));
    if (A) {
      LNode t = f ? *f : sel;
      if (!pad && hoverItem.i >= 0) t = hoverItem;
      if (t.btn) button(a, p, t.btn); else moveAcross(a, p, t);
    }
    if (Y) takeAll(a, p);
  }

  void draw(App& a) override {
    Player* pp = player(a);
    if (!pp || a.topScreen() != this) return;
    Player& p = *pp;
    const float z = Z(), W = (float)a.width, H = (float)a.height;
    constexpr float BW = 10.7f, CW = 7.8f, GAP = 0.45f, PADX = 0.5f, TOPU = 1.05f, BOTU = 1.55f, UNITS = BW * 2 + CW + GAP * 2 + PADX * 2;
    u = std::floor(clampv(std::min((W - 12 * z) / UNITS, (H - 12 * z) / (TOPU + BOTU + 10.8f)), 14.f, 54.f * z));
    const float frameH = std::min(H - 12 * z, (TOPU + BOTU + 13.5f) * u), frameW = UNITS * u;
    Rc F{ImVec2(std::floor((W - frameW) * 0.5f), std::floor((H - frameH) * 0.5f)), ImVec2(0, 0)};
    F.b = F.a + ImVec2(frameW, frameH);
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(4, 5, 3, 170));
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##zloot", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    cur.clear();
    const bool pad = a.input.usingPad();
    mouseP = ImGui::GetIO().MousePos; tnow = (float)a.time;
    plate(dl, F, PADX, "LOOT", regionName(ifloor(p.pos.x), ifloor(p.pos.z)));
    const float x0 = F.a.x + PADX * u, y0 = F.a.y + TOPU * u, bodyH = frameH - (TOPU + BOTU) * u + 0.2f * u;
    const Rc Bk{ImVec2(x0, y0), ImVec2(x0 + BW * u, y0 + bodyH)};
    const Rc Cd{ImVec2(Bk.b.x + GAP * u, y0), ImVec2(Bk.b.x + GAP * u + CW * u, y0 + bodyH)};
    const Rc Ct{ImVec2(Cd.b.x + GAP * u, y0), ImVec2(Cd.b.x + GAP * u + BW * u, y0 + bodyH)};
    const Rc Ft{ImVec2(x0, y0 + bodyH + 0.15f * u), ImVec2(F.b.x - PADX * u, F.b.y - 0.2f * u)};
    panel[0] = Bk; panel[1] = Ct;
    // what the mouse is over (last frame's nodes: the layout hardly moves)
    hoverNode = nullptr;
    if (!pad) for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) if (it->contains(mouseP) && (!it->inGrid || inRect(vis[it->side], mouseP))) { hoverNode = &*it; break; }
    hoverItem = hoverNode && !hoverNode->btn ? *hoverNode : LNode{};
    const LNode shown = hoverItem.i >= 0 ? hoverItem : sel;
    int nBag = 0, nIt = 0;
    for (int i = 9; i < 36; i++) if (isBag(i) && !p.inv[i].empty()) nBag++;
    for (auto& s : items) if (!s.empty()) nIt++;
    grid(dl, p, Bk, 0, "BACKPACK", sf("%d / 22", nBag), 3, "##lbag");
    drawCard(dl, p, Cd, shown, pad);
    grid(dl, p, Ct, 1, upper(title), sf("%d / %d", nIt, (int)items.size()), 5, "##lcont");
    drawFooter(dl, p, Ft, Bk, Cd, shown, nIt > 0);
    if (firstFocus) {   // start on the container's first item (or Take all when it's empty)
      firstFocus = false;
      const LNode* f = nullptr;
      for (auto& n : cur) if (n.side == 1 && n.inGrid) { f = &n; break; }
      if (!f) for (auto& n : cur) if (n.btn == LB_ALL) { f = &n; break; }
      if (f) { focus = *f; focusPos = f->c(); if (!f->btn) sel = *f; }
    }
    mouse(a, p);
    if (dragging) if (const ItemStack* s = stackOf(p, dragSrc)) {
      ImDrawList* fg = ImGui::GetForegroundDrawList();
      ItemStack t(s->id, 1);
      fg->AddRectFilled(mouseP - ImVec2(u, u) * 0.6f, mouseP + ImVec2(u, u) * 0.6f, IM_COL32(10, 10, 6, 150));
      drawItem(fg, t, mouseP - ImVec2(u, u) * 0.6f, u * 1.2f, 0.95f, false);
      if (s->n > 1) T(fg, F_BODY, mouseP + ImVec2(u * 0.1f, u * 0.2f), col::white, std::to_string(s->n), u * 0.34f);
    }
    nodes = cur;
    ImGui::End();
    ImGui::PopStyleVar();
  }

private:
  ivec3 pos;
  std::string title = "Container";
  std::vector<ItemStack> items;   // the container, as of this frame
  std::vector<LNode> cur, nodes;
  LNode focus, sel, dragSrc, hoverItem, lastFocus[3];
  const LNode* hoverNode = nullptr;
  Rc panel[2]{}, vis[2]{};
  ImVec2 mouseP{0, 0}, dragFrom{0, 0}, focusPos{0, 0};
  bool opened = false, closeReq = false, navUsed = false, firstFocus = true, dragArmed = false, dragging = false;
  int frames = 0, scrollTo = -1;
  float tnow = 0;
  AudioEngine* audio = nullptr;

  static bool inRect(const Rc& r, ImVec2 p) { return p.x >= r.a.x && p.y >= r.a.y && p.x < r.b.x && p.y < r.b.y; }
  void click() { playClick(); }
  void tick() { if (audio) audio->play("tick"); }

  // ---- the container: read from / written to its block entity ----
  bool load(App& a) {
    World& w = a.session->world();
    const BlockDef& b = reg().block(w.getBlock(pos.x, pos.y, pos.z));
    if (!b.has("container")) return false;
    const json* e = w.blockEntity(pos.x, pos.y, pos.z);
    items.assign(std::max(1, (int)b.num("container", 27)), ItemStack());
    if (e && e->is_object() && e->contains("items") && (*e)["items"].is_array()) {
      const json& j = (*e)["items"];
      if (j.size() > items.size()) items.resize(j.size());
      for (size_t i = 0; i < j.size(); i++) items[i] = ItemStack::fromJson(j[i]);
    }
    title = containerTitle(e, b);
    return true;
  }
  void save(App& a) {
    World& w = a.session->world();
    json* e = w.blockEntity(pos.x, pos.y, pos.z, true);
    if (!e) return;
    if (!e->is_object()) *e = json::object();
    if (!e->contains("type")) (*e)["type"] = "chest";
    json arr = json::array();
    for (auto& s : items) arr.push_back(s.toJson());
    (*e)["items"] = std::move(arr);
    w.blockEntityChanged(pos.x, pos.y, pos.z);   // saved, and sent to the host / the guests
  }
  const ItemStack* stackOf(Player& p, const LNode& n) const {
    if (n.btn || n.i < 0) return nullptr;
    const ItemStack* s = n.side == 0 ? (n.i < (int)p.inv.size() ? &p.inv[n.i] : nullptr) : n.side == 1 && n.i < (int)items.size() ? &items[n.i] : nullptr;
    return s && !s->empty() ? s : nullptr;
  }

  // ---- moving things ----
  bool take(Player& p, int i, std::string& why) {   // container -> backpack (merging first); rubles go to the wallet
    ItemStack& s = items[i];
    if (s.empty()) return false;
    if (s.id == "rubles") { addMoney(p, s.n); hudToast("+" + grp(s.n) + " RU", "rare"); s.clear(); return true; }
    const int n0 = s.n;
    for (int k = 9; k < 36 && !s.empty(); k++) if (isBag(k) && p.inv[k].canMerge(s)) {
      const int mv = std::min(s.n, p.inv[k].maxStack() - p.inv[k].n);
      if (mv > 0) { p.inv[k].n += mv; s.n -= mv; if (s.n <= 0) s.clear(); }
    }
    if (!s.empty()) { const Loc f = firstFreeBag(p); if (f.k != Loc::None) { p.inv[f.i] = std::move(s); s.clear(); } else why = "Your backpack is full"; }
    return s.empty() || s.n != n0;
  }
  bool store(Player& p, int k, std::string& why) {   // backpack -> container
    ItemStack& s = p.inv[k];
    if (s.empty()) return false;
    const int n0 = s.n;
    for (auto& d : items) if (!s.empty() && d.canMerge(s)) {
      const int mv = std::min(s.n, d.maxStack() - d.n);
      if (mv > 0) { d.n += mv; s.n -= mv; if (s.n <= 0) s.clear(); }
    }
    if (!s.empty()) {
      bool put = false;
      for (auto& d : items) if (d.empty()) { d = std::move(s); s.clear(); put = true; break; }
      if (!put) why = "No room in the " + lower(title);
    }
    return s.empty() || s.n != n0;
  }
  void overloadWarn(Player& p, float before) {
    const float cap = carryCap(p);
    if (before <= cap && carryWeight(p) > cap) hudToast("You are overloaded", "warn");
  }
  void moveAcross(App& a, Player& p, const LNode& n) {
    if (n.btn || n.i < 0 || !stackOf(p, n)) return;
    std::string why;
    const float w0 = carryWeight(p);
    const bool ok = n.side == 1 ? take(p, n.i, why) : store(p, n.i, why);
    if (ok) { save(a); click(); if (n.side == 1) overloadWarn(p, w0); }
    if (!why.empty()) hudToast(why, "warn");
  }
  void takeAll(App& a, Player& p) {
    const float w0 = carryWeight(p);
    bool any = false; std::string why;
    for (int i = 0; i < (int)items.size(); i++) if (!items[i].empty()) any |= take(p, i, why);
    if (any) { save(a); click(); overloadWarn(p, w0); }
    if (!why.empty()) hudToast(why, "warn");
    else if (!any) hudToast("The " + lower(title) + " is empty");
  }
  void button(App& a, Player& p, int id) {
    if (id == LB_MOVE) moveAcross(a, p, sel);
    else if (id == LB_ALL) takeAll(a, p);
    else if (id == LB_CLOSE) closeReq = true;
  }

  // ---- focus (controller / arrows) ----
  const LNode* findNode(const LNode& k) const { for (auto& n : nodes) if (same(n, k)) return &n; return nullptr; }
  const LNode* nearest(ImVec2 at, int group) const {
    const LNode* best = nullptr; float bd = 1e30f;
    for (auto& n : nodes) { const float d = std::hypot(n.c().x - at.x, n.c().y - at.y) + (n.group() == group ? 0.f : 1e5f); if (d < bd) { bd = d; best = &n; } }
    return best;
  }
  void setFocus(const LNode& n) { focus = n; focusPos = n.c(); tick(); if (n.inGrid) scrollTo = n.side; }
  void moveFocus(int dx, int dy) {   // the nearest node that way (edges and overlap, as in the inventory)
    const LNode* c = findNode(focus);
    if (!c) { if (const LNode* n = nearest(focusPos, focus.group())) setFocus(*n); return; }
    const LNode* best = nullptr; float bd = 1e30f;
    auto gap = [](float a0, float a1, float b0, float b1) { return std::max(0.f, std::max(a0, b0) - std::min(a1, b1)); };
    for (auto& n : nodes) {
      if (same(n, *c)) continue;
      const float along = dx ? (n.c().x - c->c().x) * dx : (n.c().y - c->c().y) * dy;
      const float across = dx ? gap(c->a.y, c->b.y, n.a.y, n.b.y) : gap(c->a.x, c->b.x, n.a.x, n.b.x);
      if (along <= 2) continue;
      const float edge = dx ? (dx > 0 ? n.a.x - c->b.x : c->a.x - n.b.x) : (dy > 0 ? n.a.y - c->b.y : c->a.y - n.b.y);
      const float d = std::max(0.f, edge) + along * 0.15f + across * 3.f;
      if (d < bd) { bd = d; best = &n; }
    }
    if (best) setFocus(*best);
  }
  void switchSide(int dir) {   // backpack <-> container (from the buttons: LB the backpack, RB the container)
    const LNode* c = findNode(focus);
    const int g = c ? c->group() : 1, t = g == 2 ? (dir < 0 ? 0 : 1) : 1 - g;
    const LNode* n = findNode(lastFocus[t]);
    if (!n) for (auto& x : nodes) if (x.group() == t) { n = &x; break; }
    if (!n) for (auto& x : nodes) if (x.btn == (t == 1 ? LB_ALL : LB_CLOSE)) { n = &x; break; }   // an empty side
    if (n) { navUsed = true; setFocus(*n); }
  }

  // ---- drawing ----
  void grid(ImDrawList* dl, Player& p, Rc P, int side, const std::string& head, const std::string& right, u32 seed, const char* id) {
    SlotList list;
    if (side == 0) list = bagSlots(p); else for (int i = 0; i < (int)items.size(); i++) list.push_back({i, &items[i]});
    const Packed pk = pack(list);
    panelHead(dl, P, head.c_str(), right, seed);
    const Rc body{P.a + ImVec2(0.22f, 0.6f + 0.22f) * u, P.b - ImVec2(0.22f, 0.22f) * u};
    const float sbw = std::max(6.f, u * 0.2f);
    ImGui::SetCursorScreenPos(body.a);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, sbw);
    ImGui::BeginChild(id, body.size(), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings);
    ImDrawList* cd = ImGui::GetWindowDrawList();
    const ImVec2 org = ImGui::GetCursorScreenPos();
    const int rows = std::max((int)std::ceil(body.size().y / u), pk.rows);
    ImGui::Dummy(ImVec2(COLS * u, rows * u));
    const Rc V{ImGui::GetWindowPos(), ImGui::GetWindowPos() + ImVec2(body.size().x - sbw, body.size().y)};
    vis[side] = V;
    for (int y = 0; y < rows; y++) for (int x = 0; x < COLS; x++) {
      const ImVec2 c = org + ImVec2(x * u, y * u);
      if (c.y + u < V.a.y || c.y > V.b.y) continue;
      cd->AddRectFilled(c, c + ImVec2(u, u), ((x + y) & 1) ? col::rgb(0x15170f) : col::rgb(0x181a12));
      cd->AddRect(c, c + ImVec2(u, u), col::rgb(0x0c0d09));
    }
    for (const Cell& c : pk.cells) {
      const ItemStack& s = side == 0 ? p.inv[c.slot] : items[c.slot];
      const Rc r{org + ImVec2(c.x * u, c.y * u) + ImVec2(1, 1), org + ImVec2((c.x + c.w) * u, (c.y + c.h) * u) - ImVec2(1, 1)};
      LNode n; n.side = side; n.i = c.slot; n.a = r.a; n.b = r.b; n.inGrid = true;
      cur.push_back(n);
      if (r.b.y <= V.a.y || r.a.y >= V.b.y) continue;
      const bool hov = hoverNode && same(*hoverNode, n);
      tile(cd, r, s, dragging && same(dragSrc, n) ? 0.3f : 1.f, hov, navUsed && same(focus, n), same(sel, n), false, col::lineHi, tnow);
    }
    if (pk.used == 0) {
      const char* e = side == 1 ? "Empty" : "Your backpack is empty";
      const ImVec2 ts = TS(F_MID, e, u * 0.4f);
      T(cd, F_MID, ImVec2((V.a.x + V.b.x - ts.x) * 0.5f, V.a.y + u * 1.2f), withAlpha(col::muted, 0.7f), e, u * 0.4f);
    }
    if (scrollTo == side) {
      scrollTo = -1;
      if (const LNode* f = findNode(focus); f && f->inGrid && f->side == side) {
        const float sy = ImGui::GetScrollY();
        if (f->a.y < V.a.y) ImGui::SetScrollY(sy - (V.a.y - f->a.y) - 2); else if (f->b.y > V.b.y) ImGui::SetScrollY(sy + (f->b.y - V.b.y) + 2);
      }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    if (dragging && dragSrc.side != side && inRect(P, mouseP)) dl->AddRect(body.a, body.b, withAlpha(col::rgb(0x8fbf4a), 0.8f), 0, 0, 2.f);
  }

  void drawCard(ImDrawList* dl, Player& p, Rc Cr, const LNode& shown, bool pad) {
    panelHead(dl, Cr, "ITEM", "", 4);
    const Rc body{Cr.a + ImVec2(0.3f, 0.6f + 0.3f) * u, Cr.b - ImVec2(0.3f, 0.3f) * u};
    const std::string hint = pad ? "A: move across.  Y: take all.  LB / RB: switch side.  B: close."
                                 : "Double-click, right-click or drag an item to move it across.  Enter: move.  F: take all.  Tab: switch side.  E: close.";
    const float hpx = u * 0.27f, hl = u * 0.36f, hh = para(nullptr, F_BODY, ImVec2(0, 0), 0, hint, hpx, body.size().x, hl, 1e9f);
    dl->AddLine(ImVec2(body.a.x, body.b.y - hh - u * 0.12f), ImVec2(body.b.x, body.b.y - hh - u * 0.12f), IM_COL32(255, 255, 255, 18));
    para(dl, F_BODY, ImVec2(body.a.x, body.b.y - hh), col::dim, hint, hpx, body.size().x, hl, hh + 1);
    cardInfo(dl, p, body, stackOf(p, shown), shown.side == 0 ? inv(shown.i) : Loc{}, shown.side == 0 ? std::string("In the backpack") : "In the " + lower(title),
             "Nothing selected", "", "Pick an item in your backpack or in the " + lower(title) + " to see it here.", body.b.y - hh - u * 0.25f);
  }

  void drawFooter(ImDrawList* dl, Player& p, Rc Ft, Rc Bk, Rc Cd, const LNode& shown, bool anyItems) {
    dl->AddRectFilledMultiColor(Ft.a, Ft.b, col::rgb(0x23251d), col::rgb(0x23251d), col::rgb(0x171811), col::rgb(0x171811));
    bevel(dl, Ft.a, Ft.b, col::pnLo, col::pnHi, std::max(1.f, u * 0.045f));
    const float ph = Ft.size().y;
    // under the backpack: the weight (with what the item looked at in the container would add) and the money
    const float w = carryWeight(p), cap = std::max(1.f, carryCap(p)), r = w / cap;
    const ItemStack* look = shown.side == 1 ? stackOf(p, shown) : nullptr;
    const float add = look && look->id != "rubles" ? itemWeight(*look) : 0.f;
    const ImU32 wc = r < 0.8f ? col::rgb(0x6f8247) : r < 1.f ? col::rgb(0xd09a2c) : col::rgb(0xc2403f);
    T(dl, F_BODY, ImVec2(Ft.a.x + u * 0.25f, Ft.a.y + (ph - u * 0.3f) * 0.5f - 1), col::amber, "WEIGHT", u * 0.3f);
    const std::string m = grp((long long)std::llround(money(p))) + " RU";
    const ImVec2 ms = TS(F_BTN, m, u * 0.4f);
    T(dl, F_BTN, ImVec2(Bk.b.x - ms.x - u * 0.15f, Ft.a.y + (ph - ms.y) * 0.5f), col::amber, m, u * 0.4f);
    const float bx = Ft.a.x + u * 1.4f, bwid = std::max(u * 3.f, Bk.b.x - ms.x - u * 0.55f - bx);
    const Rc br{ImVec2(bx, Ft.a.y + ph * 0.5f - u * 0.26f), ImVec2(bx + bwid, Ft.a.y + ph * 0.5f + u * 0.26f)};
    auto fx = [&](float v) { return br.a.x + 1 + (bwid - 2) * clampv(v / 1.5f, 0.f, 1.f); };
    dl->AddRectFilled(br.a, br.b, col::rgb(0x0e0f0b));
    dl->AddRectFilled(br.a + ImVec2(1, 1), ImVec2(fx(r), br.b.y - 1), wc);
    if (add > 0) dl->AddRectFilled(ImVec2(fx(r), br.a.y + 1), ImVec2(fx((w + add) / cap), br.b.y - 1), w + add > cap ? IM_COL32(194, 64, 63, 120) : IM_COL32(240, 220, 160, 70));
    dl->AddRect(br.a, br.b, col::rgb(0x050604));
    dl->AddLine(ImVec2(br.a.x + bwid / 1.5f, br.a.y), ImVec2(br.a.x + bwid / 1.5f, br.b.y), IM_COL32(255, 255, 255, 120));   // the limit
    const std::string ws = add > 0 ? sf("%.1f (+%.1f) / %.1f kg", w, add, cap) : sf("%.1f / %.1f kg", w, cap);
    const ImVec2 wsz = TS(F_BODY, ws, u * 0.32f);
    T(dl, F_BODY, ImVec2((br.a.x + br.b.x - wsz.x) * 0.5f, (br.a.y + br.b.y - wsz.y) * 0.5f), col::rgb(0xf6f2e2), ws, u * 0.32f);
    // under the card: how the load feels
    if (moveSlow(p) < 0.999f || r > 1.f) T(dl, F_BODY, ImVec2(Cd.a.x + u * 0.1f, Ft.a.y + (ph - u * 0.32f) * 0.5f - 1), wc, r > 1.25f ? "OVERLOADED" : r > 1.f ? "Overweight: slowed" : "Heavy", u * 0.32f);
    // under the container: Take (Store for a backpack item) / Take all / Close
    const ItemStack* selS = stackOf(p, sel);
    const struct { const char* l; int id; bool on; } BT[] = {{sel.side == 0 && selS ? "Store" : "Take", LB_MOVE, selS != nullptr}, {"Take all", LB_ALL, anyItems}, {"Close", LB_CLOSE, true}};
    float x = Ft.b.x - u * 0.15f;
    for (int i = 2; i >= 0; i--) {
      const float bwd = std::max(u * 2.6f, TS(F_MID, BT[i].l, u * 0.34f).x + u * 0.9f);
      const Rc b{ImVec2(x - bwd, Ft.a.y + u * 0.18f), ImVec2(x, Ft.b.y - u * 0.18f)};
      LNode n; n.btn = BT[i].id; n.a = b.a; n.b = b.b;
      const bool hov = hoverNode && same(*hoverNode, n), foc = navUsed && same(focus, n);
      btnFace(dl, b.a, b.b, hov || foc, hov && ImGui::IsMouseDown(0), BT[i].on ? 1.f : 0.45f);
      const ImVec2 ts = TS(F_MID, BT[i].l, u * 0.34f);
      T(dl, F_MID, ImVec2((b.a.x + b.b.x - ts.x) * 0.5f, (b.a.y + b.b.y - ts.y) * 0.5f), BT[i].on ? (hov || foc ? col::amber : col::ink2) : col::dim, BT[i].l, u * 0.34f);
      if (foc) dl->AddRect(b.a - ImVec2(2, 2), b.b + ImVec2(2, 2), col::gold, 0, 0, 2.f);
      cur.push_back(n);
      x -= bwd + u * 0.12f;
    }
  }

  // ---- the mouse: click selects, double / right click or a drag across moves ----
  void mouse(App& a, Player& p) {
    ImGuiIO& io = ImGui::GetIO();
    if (a.input.usingPad()) { dragArmed = dragging = false; return; }
    const bool moved = io.MouseDelta.x != 0 || io.MouseDelta.y != 0;
    if (moved) navUsed = false;
    if (hoverNode && (moved || ImGui::IsMouseClicked(0))) { focus = *hoverNode; focusPos = hoverNode->c(); }
    if (ImGui::IsMouseClicked(0) && hoverNode) {
      if (hoverNode->btn) button(a, p, hoverNode->btn);
      else {
        sel = *hoverNode;
        if (ImGui::IsMouseDoubleClicked(0)) { moveAcross(a, p, sel); dragArmed = false; }
        else { dragArmed = true; dragSrc = sel; dragFrom = mouseP; }
      }
    }
    if (dragArmed && !dragging && ImGui::IsMouseDown(0) && std::hypot(mouseP.x - dragFrom.x, mouseP.y - dragFrom.y) > std::max(5.f, u * 0.15f)) dragging = true;
    if (!ImGui::IsMouseDown(0) && (dragArmed || dragging)) {
      if (dragging) { const int to = inRect(panel[0], mouseP) ? 0 : inRect(panel[1], mouseP) ? 1 : -1; if (to >= 0 && to != dragSrc.side) moveAcross(a, p, dragSrc); }
      dragArmed = dragging = false;
    }
    if (ImGui::IsMouseClicked(1) && hoverNode && !hoverNode->btn) { sel = *hoverNode; moveAcross(a, p, sel); }
  }
};
} // namespace

std::unique_ptr<Screen> makeZoneInventoryScreen() { return std::make_unique<ZoneInventory>(); }
std::unique_ptr<Screen> makeZoneLootScreen(ivec3 pos) { return std::make_unique<ZoneLoot>(pos); }
bool zoneLootFits(App& a, const InvOpen& o) {
  if (!a.session || !a.session->level || o.mode != "container" || !o.hasPos || o.ender || o.heldPack || o.entItems || !zone::zoneWorld(*a.session->level)) return false;
  return reg().block(a.session->world().getBlock(o.pos.x, o.pos.y, o.pos.z)).has("container");
}

} // namespace dq
