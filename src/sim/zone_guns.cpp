// sim/zone_guns.cpp — guns, Battle of Spades' rules (game/weapons.js weaponStats, WeaponState) on the Zone's
// catalogue: hit-scan bullets with spread, recoil and damage falloff; aiming down the sights (the right trigger
// zooms by the optic's power); attachments you fit and strip (optic, muzzle, underbarrel, magazine: the gun's
// stack keeps them in tag.att); the rounds in the magazine (tag.am) and their type (tag.at: FMJ/AP/slug...);
// condition (the stack's durability) that makes a gun jam; underbarrel launchers (tag.gl = the launcher is up).
// The player's own machine fires (client-authoritative like the body); a creature owned by the host takes its hit
// through LevelNet::hitEntity, as melee does.
#include "sim/zone.h"
#include "sim/controller.h"
#include "sim/combat.h"
#include "sim/creature.h"
#include "sim/creature_entities.h"
#include "sim/physics.h"
#include "core/registry.h"
#include "gen/zone_plan.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <random>

namespace dq::zone {

namespace {
struct Cat { float spread, adsSpread, rv, rh, range, fs, fe, fm, adsZoom, head; };
Cat catOf(const std::string& c) {
  if (c == "carbine") return {0.0085f, 0, 0.018f, 0.00007f, 210, 40, 140, 0.62f, 1.25f, 2};
  if (c == "lmg") return {0.013f, 0, 0.02f, 0.0001f, 250, 55, 180, 0.7f, 1.2f, 2};
  if (c == "sniper") return {0.0025f, 0.0003f, 0.12f, 0.0003f, 450, 0, 0, 1, 1.4f, 1.9f};
  if (c == "dmr") return {0.0035f, 0.001f, 0.055f, 0.00025f, 360, 0, 0, 1, 1.4f, 2};
  if (c == "smg") return {0.011f, 0, 0.012f, 0.00006f, 150, 25, 80, 0.6f, 1.25f, 2};
  if (c == "shotgun") return {0.03f, 0, 0.07f, 0.0002f, 90, 10, 40, 0.4f, 1.15f, 1.6f};
  if (c == "pistol") return {0.012f, 0, 0.03f, 0.0001f, 120, 20, 60, 0.6f, 1.15f, 2};
  if (c == "launcher") return {0.006f, 0, 0.1f, 0.0005f, 220, 0, 0, 1, 1.3f, 1};
  return {0.0075f, 0, 0.02f, 0.00008f, 260, 60, 180, 0.7f, 1.25f, 2};   // assault rifle
}

struct Stats {
  float delay, dmg, adsSpread, hipSpread, rv, rh, range, fs, fe, fm, reload, adsZoom = 1.2f, speedMul = 1, loud = 1, head = 2;
  int clip, pellets; bool slowReload, autoFire, suppressed = false, laser = false, light = false, bipod = false, scope = false; std::string reticle = "iron", launcher;
  float pen = 1, wear = 1;
};

std::string optOf(const json& att, const char* slot) { return att.is_object() && att.contains(slot) && att[slot].is_string() ? att[slot].get<std::string>() : ""; }

Stats statsFor(const ZItem& z, const ItemStack& s) {
  const GunDef& G = z.gun; const Cat C = catOf(G.cls);
  Stats S;
  S.delay = 60.f / std::max(1.f, G.rpm); S.dmg = G.dmg; S.clip = G.mag; S.pellets = G.pellets; S.reload = G.reload;
  S.slowReload = G.slowReload; S.autoFire = G.autoFire; S.head = C.head;
  S.hipSpread = C.spread * G.spreadMul * 2; S.adsSpread = (C.adsSpread > 0 ? C.adsSpread : C.spread) * G.spreadMul;
  S.rv = C.rv * G.kick * std::sqrt(G.dmg / 25.f); S.rh = std::max(C.rh * 40, C.rv * 0.25f) * G.kickH;
  S.range = C.range; S.fs = C.fs; S.fe = C.fe; S.fm = C.fm; S.adsZoom = C.adsZoom; S.speedMul = G.speedMul;
  json att = gunAttachments(s);
  if (!att.contains("optic") && !G.defaultOptic.empty() && !s.tag.is_object()) att["optic"] = G.defaultOptic;
  float rangeMul = 1, minMul = 1;
  for (auto& [slot, v] : att.items()) {
    const ZItem* a = v.is_string() ? zi(v.get<std::string>()) : nullptr;
    if (!a || !a->raw.contains("att")) continue;
    const json& A = a->raw["att"]; const json& m = A.value("mods", json::object());
    if (m.contains("adsZoom")) S.adsZoom = m["adsZoom"];
    if (m.contains("adsSpreadMul")) S.adsSpread *= m["adsSpreadMul"].get<float>();
    if (m.contains("hipSpreadMul")) S.hipSpread *= m["hipSpreadMul"].get<float>();
    if (m.contains("rangeMul")) rangeMul *= m["rangeMul"].get<float>();
    if (m.contains("minDmgMul")) minMul *= m["minDmgMul"].get<float>();
    if (m.contains("recoilV")) S.rv *= m["recoilV"].get<float>();
    if (m.contains("recoilH")) S.rh *= m["recoilH"].get<float>();
    if (m.contains("clipMul")) S.clip = (int)std::round(S.clip * m["clipMul"].get<float>());
    if (m.contains("reloadMul")) S.reload *= m["reloadMul"].get<float>();
    if (m.contains("loud")) S.loud = m["loud"];
    if (A.value("hidden", false)) S.suppressed = true;
    if (A.value("laser", false)) S.laser = true;
    if (A.value("light", false)) S.light = true;
    if (A.value("bipod", false)) S.bipod = true;
    if (A.value("scope", false)) S.scope = true;
    if (A.contains("reticle")) S.reticle = A["reticle"];
    if (A.contains("launcher")) S.launcher = A["launcher"];
  }
  if (G.suppressedBuiltIn) { S.suppressed = true; S.loud = 0.3f; }
  if (G.cls == "lmg") S.bipod = true;
  S.range *= rangeMul; S.fs *= rangeMul; S.fe *= rangeMul; S.fm = std::clamp(S.fm * minMul, 0.05f, 1.f);
  // the rounds loaded
  if (const ZItem* am = zi(s.tag.is_object() ? s.tag.value("at", std::string()) : std::string())) {
    const json& A = am->raw["ammo"];
    S.dmg *= A.value("dmg", 1.f); S.pen = A.value("pen", 1.f); S.wear = A.value("wear", 1.f);
    if (A.contains("pellets")) S.pellets = A["pellets"];
    if (A.contains("spread")) { S.hipSpread *= A["spread"].get<float>(); S.adsSpread *= A["spread"].get<float>(); }
  }
  return S;
}

struct GunState { double next = 0; float reloadLeft = 0; bool reloading = false, jammed = false, ads = false; float jamClear = 0, bloom = 0; std::string heldKey; int heldSlot = -1; float equip = 0; int slowLeft = 0; float sprintRec = 0; };
std::unordered_map<u32, GunState>& states() { static std::unordered_map<u32, GunState> s; return s; }
double now() { static double t = 0; return t; }
std::mt19937& rng() { static thread_local std::mt19937 g{std::random_device{}()}; return g; }
float frand() { return std::uniform_real_distribution<float>(0, 1)(rng()); }

int durOf(const ItemStack& s) { const ItemDef* d = s.def(); return d && d->hasTool ? d->tool.durability : 1000; }
float condOf(const ItemStack& s) { return std::clamp(1.f - (float)s.d / std::max(1, durOf(s)), 0.f, 1.f); }

// the rounds in your bags that fit this calibre (the loaded type first)
int reserveOf(const Player& p, const ZItem& z, const std::string& type) {
  int n = 0;
  for (auto& s : p.inv) if (!s.empty()) if (const ZItem* a = zi(s.id); a && a->cat == "ammo" && a->raw["ammo"].value("cal", "") == z.gun.cal && (type.empty() || s.id == type)) n += s.n;
  return n;
}
std::string firstAmmo(const Player& p, const ZItem& z) {
  for (auto& s : p.inv) if (!s.empty()) if (const ZItem* a = zi(s.id); a && a->cat == "ammo" && a->raw["ammo"].value("cal", "") == z.gun.cal) return s.id;
  return "";
}
int takeAmmo(Player& p, const std::string& id, int want) {
  int got = 0;
  for (auto& s : p.inv) if (!s.empty() && s.id == id && got < want) { const int k = std::min(want - got, s.n); s.n -= k; got += k; if (s.n <= 0) s.clear(); }
  return got;
}
std::string launcherAmmo(const std::string& l) { return l == "vog" ? "vog25" : l == "m203" ? "m203_he" : ""; }
} // namespace

json gunAttachments(const ItemStack& gun) {
  if (gun.tag.is_object() && gun.tag.contains("att") && gun.tag["att"].is_object()) return gun.tag["att"];
  return json::object();
}
std::vector<std::string> gunSlots(const std::string& id) {
  std::vector<std::string> out;
  if (const ZItem* z = zi(id); z && z->isGun) for (const char* s : {"optic", "muzzle", "under", "mag"}) if (z->gun.fits.contains(s)) out.push_back(s);
  return out;
}

bool attachTo(Player& p, int gi, int ai, std::string& why) {
  if (gi < 0 || ai < 0 || gi >= (int)p.inv.size() || ai >= (int)p.inv.size()) { why = "Nothing there"; return false; }
  ItemStack& g = p.inv[gi]; ItemStack& a = p.inv[ai];
  const ZItem* gz = zi(g.id); const ZItem* az = zi(a.id);
  if (!gz || !gz->isGun) { why = "That's not a gun"; return false; }
  if (!az || !az->raw.contains("att")) { why = "That's not an attachment"; return false; }
  const std::string slot = az->raw["att"].value("slot", "");
  const json& fits = gz->gun.fits.value(slot, json::array());
  if (std::find(fits.begin(), fits.end(), json(a.id)) == fits.end()) { why = az->name + " doesn't fit the " + gz->name; return false; }
  if (!g.tag.is_object()) { g.tag = json::object(); if (!gz->gun.defaultOptic.empty()) g.tag["att"] = json{{"optic", gz->gun.defaultOptic}}; }
  json& att = g.tag["att"]; if (!att.is_object()) att = json::object();
  // what was there comes off into the bag
  if (att.contains(slot) && att[slot].is_string()) { ItemStack old(att[slot].get<std::string>(), 1); att.erase(slot); if (p.give(old) > 0) { why = "No room for the old " + slot; return false; } }
  att[slot] = a.id;
  if (--a.n <= 0) a.clear();
  return true;
}
bool detachFrom(Player& p, int gi, const std::string& slot, std::string& why) {
  if (gi < 0 || gi >= (int)p.inv.size()) { why = "Nothing there"; return false; }
  ItemStack& g = p.inv[gi];
  const ZItem* gz = zi(g.id);
  if (!gz || !gz->isGun) { why = "That's not a gun"; return false; }
  if (!g.tag.is_object()) { g.tag = json::object(); if (!gz->gun.defaultOptic.empty()) g.tag["att"] = json{{"optic", gz->gun.defaultOptic}}; }
  json& att = g.tag["att"];
  if (!att.is_object() || !att.contains(slot)) { why = "Nothing fitted there"; return false; }
  ItemStack old(att[slot].get<std::string>(), 1);
  att.erase(slot);
  if (slot == "under") g.tag.erase("gl");
  if (p.give(old) > 0) { att[slot] = old.id; why = "No room in your bags"; return false; }
  // a smaller magazine spills the extra rounds back
  if (slot == "mag") { const Stats S = statsFor(*gz, g); const int am = g.tag.value("am", 0); if (am > S.clip) { p.give(ItemStack(g.tag.value("at", std::string()), am - S.clip)); g.tag["am"] = S.clip; } }
  return true;
}
bool unloadGun(Player& p, int gi) {
  if (gi < 0 || gi >= (int)p.inv.size()) return false;
  ItemStack& g = p.inv[gi];
  if (!g.tag.is_object() || g.tag.value("am", 0) <= 0) return false;
  const std::string at = g.tag.value("at", std::string()); const int am = g.tag.value("am", 0);
  g.tag["am"] = 0;
  if (!at.empty()) p.give(ItemStack(at, am));
  return true;
}
void cycleAmmo(Player& p, int gi) {
  if (gi < 0 || gi >= (int)p.inv.size()) return;
  ItemStack& g = p.inv[gi]; const ZItem* z = zi(g.id); if (!z || !z->isGun) return;
  std::vector<std::string> types;
  for (auto& s : p.inv) if (!s.empty()) if (const ZItem* a = zi(s.id); a && a->cat == "ammo" && a->raw["ammo"].value("cal", "") == z->gun.cal && std::find(types.begin(), types.end(), s.id) == types.end()) types.push_back(s.id);
  if (types.empty()) return;
  const std::string cur = g.tag.is_object() ? g.tag.value("at", std::string()) : std::string();
  auto it = std::find(types.begin(), types.end(), cur);
  const std::string next = (it == types.end() || ++it == types.end()) ? types[0] : *it;
  if (next == cur) return;
  unloadGun(p, gi);
  if (!g.tag.is_object()) g.tag = json::object();
  g.tag["at"] = next; g.tag["am"] = 0;
}

float gunFovScale(const Player& p) {
  auto it = states().find(p.id); if (it == states().end() || !it->second.ads) return 1;
  const ItemStack& h = p.inv[p.selected];
  const ZItem* z = h.empty() ? nullptr : zi(h.id);
  if (!z || !z->isGun) return 1;
  return 1.f / statsFor(*z, h).adsZoom;
}

GunHud gunHud(const Player& p) {
  GunHud H;
  const ItemStack& h = p.inv[p.selected];
  const ZItem* z = h.empty() ? nullptr : zi(h.id);
  if (!z || !z->isGun) return H;
  const Stats S = statsFor(*z, h);
  H.has = true; H.name = z->name; H.clip = S.clip; H.cond = (int)std::round(condOf(h) * 100);
  const bool gl = h.tag.is_object() && h.tag.value("gl", false) && !S.launcher.empty();
  const std::string at = gl ? launcherAmmo(S.launcher) : h.tag.is_object() ? h.tag.value("at", std::string()) : std::string();
  H.mag = gl ? (h.tag.value("gla", 0)) : h.tag.is_object() ? h.tag.value("am", 0) : 0;
  if (gl) H.clip = 1;
  H.ammo = zi(at) ? zi(at)->name : at.empty() ? "(no rounds)" : at;
  H.reserve = gl ? reserveOf(p, *zi(h.id), "") * 0 + [&] { int n = 0; for (auto& s : p.inv) if (!s.empty() && s.id == at) n += s.n; return n; }() : reserveOf(p, *z, at);
  auto it = states().find(p.id);
  if (it != states().end()) { H.reloading = it->second.reloading; H.reloadT = it->second.reloadLeft; H.ads = it->second.ads; H.jammed = it->second.jammed; H.spread = it->second.bloom; }
  H.scope = S.scope; H.reticle = S.reticle;
  return H;
}

// ---------------------------------------------------------------- firing
static void shoot(PlayerController& pc, ItemStack& h, const ZItem& z, const Stats& S, GunState& gs, bool moving) {
  Level& lv = pc.lv; Player& p = pc.p;
  World& w = lv.world(p.realm);
  const glm::dvec3 o(p.pos.x, p.pos.y + p.eye, p.pos.z);
  const vec3 L = lookVector(p.yaw, p.pitch);
  float spread = gs.ads ? S.adsSpread : S.hipSpread;
  if (moving) spread *= gs.ads ? 2.f : 1.6f;
  if (!p.onGround) spread *= 3;
  if (p.sneaking) spread *= 0.75f;
  spread += gs.bloom;
  const float drunk = zget(p, "drunk") > 0 ? 1.5f : 1.f;
  spread *= drunk;
  const vec3 right = glm::normalize(glm::cross(L, vec3(0, 1, 0)) + vec3(1e-6f));
  const vec3 up = glm::cross(right, L);
  for (int k = 0; k < S.pellets; k++) {
    const float a = frand() * 6.2832f, r = std::sqrt(frand()) * spread;
    const vec3 d = glm::normalize(L + right * (std::cos(a) * r) + up * (std::sin(a) * r));
    const RayHit wall = raycastBlocks(w, o, d, S.range);
    double maxD = wall.hit ? wall.dist : S.range;
    Entity* best = nullptr; double bd = maxD; bool headHit = false;
    for (auto& e : lv.entities()) {
      Entity* E = e.get();
      if (E->removed || E == &p || E->realm != p.realm || E->type == "item" || E->type == "projectile") continue;
      if (E->isPlayer()) { if (!lv.info.rules.value("pvp", false)) continue; }
      else if (!dynamic_cast<Creature*>(E)) continue;
      if (auto* c = dynamic_cast<Creature*>(E); c && !c->alive()) continue;
      const glm::dvec3 lo(E->pos.x - E->w, E->pos.y, E->pos.z - E->w), hi(E->pos.x + E->w, E->pos.y + E->h, E->pos.z + E->w);
      const double t = rayAABB(o, glm::dvec3(d), lo, hi);
      if (t >= 0 && t < bd) { bd = t; best = E; headHit = (o.y + d.y * t) > E->pos.y + E->h * 0.8; }
    }
    if (best) {
      float dmg = S.dmg;
      if (S.fe > S.fs) dmg *= 1.f + (S.fm - 1.f) * std::clamp(float((bd - S.fs) / (S.fe - S.fs)), 0.f, 1.f);
      if (headHit) dmg *= S.head;
      dmg *= DMG_SCALE;
      DamageSource src; src.type = "bullet"; src.attacker = p.id; src.weapon = h.id; src.tool = "gun"; src.knock = S.pellets > 1 ? 0.8f : 1.5f;
      src.hasRay = true; src.rayO = vec3(o); src.rayD = d;
      if (best->ghost && lv.net && lv.net->isClient()) lv.net->hitEntity(p, best->id, dmg, {{"tool", "gun"}, {"weapon", h.id}, {"knock", src.knock}, {"crit", headHit}});
      else damageEntity(lv, *best, dmg, src);
      lv.fx->particles(headHit ? "crit" : "burst", vec3(o + glm::dvec3(d) * bd), headHit ? 6 : 3);
    } else if (wall.hit) {
      lv.fx->particles("smoke", wall.point, 2);
      const BlockDef& B = reg().block(wall.id);
      if (!lv.isRemote() && (B.key.find("glass") != std::string::npos)) { w.setBlock(wall.block.x, wall.block.y, wall.block.z, AIR); lv.fx->sound("glass_break", wall.point, 16); }
      else if (!lv.isRemote() && B.has("zoneBreak")) breakCrate(lv, w, wall.block.x, wall.block.y, wall.block.z);   // a round through a wooden crate (sim/zone_crates.cpp)
    }
  }
  // the shot heard: suppressed rounds are a cough
  const vec3 muzzle = vec3(o) + L * 0.8f;
  if (S.suppressed) lv.fx->sound("bow", muzzle, 14, 0.7f, 0.55f);
  else lv.fx->sound("zone_gun_" + z.gun.cls, muzzle, 90 * S.loud, 0.9f, 0.97f + 0.06f * frand());   // audio/zone_sfx.cpp (client_fx.cpp: indoor / far takes)
  if (!S.suppressed) lv.fx->particles("smoke", muzzle, 1);
  // recoil: the kick up and a swing to the side, less on a bipod or crouched, less aimed
  float kick = S.rv * 1.7f * (gs.ads ? 0.8f : 1.f) * (p.sneaking ? 0.8f : 1.f) * (S.bipod && p.sneaking ? 0.4f : 1.f);
  p.pitch = std::clamp(p.pitch + kick, -1.55f, 1.55f);
  p.yaw += (frand() - 0.5f) * 2 * S.rh * 1.7f;
  gs.bloom = std::min(gs.bloom + S.hipSpread * 0.12f, S.hipSpread * 1.5f);
  // wear: a point of condition every few shots (bad rounds faster)
  if (!p.creative() && frand() < 0.35f * S.wear) h.d = std::min(durOf(h), h.d + 1);
}

bool gunTick(PlayerController& pc, float dt, const PlayerInput& in) {
  Player& p = pc.p; Level& lv = pc.lv;
  if (!zoneWorld(lv)) return false;
  ItemStack& h = p.inv[p.selected];
  const ZItem* z = h.empty() ? nullptr : zi(h.id);
  GunState& gs = states()[p.id];
  static double clock = 0; clock += dt / std::max<size_t>(1, states().size());
  const double tnow = pc.t;
  if (!z || !z->isGun) { gs.ads = false; gs.reloading = false; gs.heldKey.clear(); return false; }
  // a new gun in the hands: it takes a moment to bring up
  if (gs.heldKey != h.id || gs.heldSlot != p.selected) { gs.heldKey = h.id; gs.heldSlot = p.selected; gs.reloading = false; gs.jammed = false; gs.next = tnow + 0.35; }
  if (!h.tag.is_object()) { h.tag = json::object(); if (!z->gun.defaultOptic.empty()) h.tag["att"] = json{{"optic", z->gun.defaultOptic}}; h.tag["am"] = 0; h.tag["at"] = firstAmmo(p, *z); }
  const Stats S = statsFor(*z, h);
  const bool gl = h.tag.value("gl", false) && !S.launcher.empty();
  // Use on loot (furniture, a crate's pile, a shelf, a pack) searches / takes it with a gun up too (STALKER's F), instead of aiming
  if (in.usePressed && !gs.ads && pc.target.hit) {
    const BlockDef& tb = reg().block(pc.target.id);
    if (tb.has("container") || tb.has("pile") || tb.has("shelf") || tb.has("furnTop") || tb.has("placedBackpack")) return false;
  }
  gs.ads = in.use && !p.sprinting;
  // out of a sprint the gun takes a moment to come back up (render/zone_fpgun.cpp blends the pose out in the same time)
  if (p.sprinting) gs.sprintRec = 0.3f; else gs.sprintRec = std::max(0.f, gs.sprintRec - dt);
  gs.bloom = std::max(0.f, gs.bloom - dt * S.hipSpread * 2.5f);
  const bool moving = glm::length(vec3(p.vel.x, 0, p.vel.z)) > 0.6f;
  // the fire-mode key raises the underbarrel launcher (or lowers it)
  if (in.fireMode && !S.launcher.empty()) { h.tag["gl"] = !h.tag.value("gl", false); pc.toastMsg(h.tag["gl"].get<bool>() ? "Grenade launcher" : "Rifle", ""); lv.fx->sound("click", p.pos, 6); }
  // clearing a jam
  if (gs.jammed) {
    if ((in.reload || in.attackPressed) && gs.jamClear <= 0) { gs.jamClear = condOf(h) < 0.2f ? 2.5f : 1.2f; lv.fx->sound("door_iron_open", p.pos, 8, 0.5f, 1.8f); }
    if (gs.jamClear > 0 && (gs.jamClear -= dt) <= 0) { gs.jammed = false; pc.toastMsg("Jam cleared", ""); }
    return true;
  }
  // reloading
  if (gs.reloading) {
    gs.reloadLeft -= dt;
    if (in.attackPressed && S.slowReload && h.tag.value("am", 0) > 0) gs.reloading = false;   // a pump gun: shoot what's in
    if (gs.reloadLeft <= 0) {
      if (gl) {
        const std::string ga = launcherAmmo(S.launcher);
        if (takeAmmo(p, ga, 1) == 1) h.tag["gla"] = 1;
        gs.reloading = false;
      } else {
        std::string at = h.tag.value("at", std::string());
        if (at.empty() || reserveOf(p, *z, at) == 0) { at = firstAmmo(p, *z); h.tag["at"] = at; }
        const int am = h.tag.value("am", 0);
        const int want = S.slowReload ? 1 : S.clip - am;
        const int got = at.empty() ? 0 : takeAmmo(p, at, want);
        h.tag["am"] = am + got;
        lv.fx->sound("click_off", p.pos, 6, 0.8f, 0.8f);
        if (S.slowReload && h.tag.value("am", 0) < S.clip && reserveOf(p, *z, at) > 0) gs.reloadLeft = S.reload;
        else gs.reloading = false;
      }
    }
    return true;
  }
  const int am = gl ? h.tag.value("gla", 0) : h.tag.value("am", 0);
  if (in.reload || (in.attackPressed && am == 0)) {
    const std::string at = gl ? launcherAmmo(S.launcher) : h.tag.value("at", std::string());
    const int have = gl ? [&] { int n = 0; for (auto& s : p.inv) if (!s.empty() && s.id == at) n += s.n; return n; }() : (reserveOf(p, *z, at) + reserveOf(p, *z, ""));
    if (am < (gl ? 1 : S.clip) && have > 0) {
      gs.reloading = true; gs.reloadLeft = gl ? 2.4f : S.reload * (am == 0 && !S.slowReload ? 1.15f : 1.f);
      lv.fx->sound("door_iron_close", p.pos, 8, 0.6f, 2.f);
      return true;
    }
    if (in.attackPressed) { lv.fx->sound("click", p.pos, 6); if (have == 0) pc.toastMsg("No " + z->gun.cal + " rounds", "warn"); }
    return true;
  }
  // firing
  const bool trigger = (S.autoFire && !gl) ? in.attack : in.attackPressed;
  if (trigger && tnow >= gs.next && am > 0 && !p.sprinting && gs.sprintRec <= 0) {
    if (gl) {
      const vec3 L = lookVector(p.yaw, p.pitch);
      spawnProjectile(lv, p.realm, p.pos + vec3(0, p.eye - 0.1f, 0) + L * 0.8f, L * 30.f + vec3(0, 2.f, 0), "cannonball", &p);
      h.tag["gla"] = 0; gs.next = tnow + 0.6;
      lv.fx->sound("firework_launch", p.pos, 40, 0.9f, 0.7f);
      p.pitch = std::clamp(p.pitch + 0.06f, -1.55f, 1.55f);
      return true;
    }
    // a worn gun jams (Misery-like: never above 80%, often below 40%)
    const float c = condOf(h);
    const float pj = c >= 0.8f ? 0 : c >= 0.6f ? 0.005f : c >= 0.4f ? 0.02f : c >= 0.2f ? 0.05f : 0.12f;
    if (frand() < pj * S.wear) { gs.jammed = true; gs.jamClear = 0; pc.toastMsg("Jammed! Press reload to clear it", "warn"); lv.fx->sound("click", p.pos, 8, 1, 0.7f); return true; }
    h.tag["am"] = am - 1;
    gs.next = std::max(gs.next, tnow - dt) + S.delay;
    if (z->gun.launcher) {
      const vec3 L = lookVector(p.yaw, p.pitch);
      spawnProjectile(lv, p.realm, p.pos + vec3(0, p.eye - 0.1f, 0) + L * 0.9f, L * 26.f, "rocket", &p);
      lv.fx->sound("firework_launch", p.pos, 60, 1, 0.6f);
      p.pitch = std::clamp(p.pitch + 0.08f, -1.55f, 1.55f);
    } else shoot(pc, h, *z, S, gs, moving);
    pc.swingArm();
    return true;
  }
  if (in.attackPressed && am == 0) lv.fx->sound("click", p.pos, 6);
  (void)clock;
  return true;   // a gun in the hands never digs or punches
}

} // namespace dq::zone
