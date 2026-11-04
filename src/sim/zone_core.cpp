// sim/zone_core.cpp — Exclusion's catalogue, per-player Zone state, weight, outfits, survival (radiation, bleeding,
// hunger, thirst, sleep, stamina, psy), consumables, anomalies, artifacts and detectors, emissions and the bunker
// hatch. See sim/zone.h. The body is simulated where the player is local (adjustInput runs from the controller each
// frame); world things (artifacts appearing, creatures caught outdoors in an emission) on the host (ZoneSystem).
#include "sim/zone.h"
#include "sim/controller.h"
#include "sim/combat.h"
#include "sim/gameplay.h"
#include "sim/creature.h"
#include "sim/magic_combat.h"
#include "sim/physics.h"
#include "sim/creature_entities.h"
#include "core/registry.h"
#include "common/fs.h"
#include "gen/zone_plan.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <map>
#include <cstring>

namespace dq::zone {

// ---------------------------------------------------------------- catalogue
namespace {
std::unordered_map<std::string, ZItem>& table() { static std::unordered_map<std::string, ZItem> t; return t; }
std::vector<const ZItem*>& list() { static std::vector<const ZItem*> l; return l; }
void loadCatalogue() {
  auto j = readJson(dataDir() / "zone_items.json");
  if (!j || !j->is_object()) { DQ_WARN("zone: data/zone_items.json missing"); return; }
  for (auto& [id, e] : j->items()) {
    ZItem z; z.id = id; z.name = e.value("name", id); z.cat = e.value("cat", "part");
    z.w = e.value("w", 0.f); z.price = e.value("price", 0.f); z.stack = e.value("stack", 1); z.raw = e;
    if (e.contains("gun")) {
      const json& g = e["gun"]; GunDef& G = z.gun; z.isGun = true;
      G.cls = g.value("cls", "ar"); G.cal = g.value("cal", "545"); G.model = g.value("model", G.cls); G.defaultOptic = g.value("defaultOptic", "");
      G.rpm = g.value("rpm", 600.f); G.dmg = g.value("dmg", 25.f); G.mag = g.value("mag", 30); G.pellets = g.value("pellets", G.cls == "shotgun" ? 9 : 1);
      G.kick = g.value("kick", 1.f); G.spreadMul = g.value("spreadMul", 1.f); G.speedMul = g.value("speedMul", 1.f); G.pierce = g.value("pierce", 0);
      G.slowReload = g.value("slowReload", G.cls == "shotgun"); G.suppressedBuiltIn = g.value("suppressedBuiltIn", false);
      G.autoFire = g.value("auto", !(G.cls == "pistol" || G.cls == "sniper" || G.cls == "dmr" || G.cls == "shotgun" || G.cls == "launcher"));
      G.launcher = G.cls == "launcher";
      static const std::map<std::string, float> R = {{"ar", 2.3f}, {"carbine", 2.1f}, {"lmg", 4.6f}, {"sniper", 3.0f}, {"dmr", 2.7f}, {"smg", 2.1f}, {"shotgun", 0.45f}, {"pistol", 1.7f}, {"launcher", 3.0f}};
      G.reload = g.value("reload", R.count(G.cls) ? R.at(G.cls) : 2.3f);
      G.fits = g.value("fits", json::object());
    }
    table()[id] = std::move(z);
  }
  for (auto& [k, v] : table()) list().push_back(&v);
  std::sort(list().begin(), list().end(), [](auto a, auto b) { return a->id < b->id; });
  DQ_LOG("zone: %d catalogue items", (int)table().size());
}
} // namespace

const ZItem* zi(const std::string& id) { auto it = table().find(id); return it == table().end() ? nullptr : &it->second; }
const std::vector<const ZItem*>& allZoneItems() { return list(); }
bool zoneWorld(const Level& lv) { return lv.info.worldType == "zone"; }

// ---------------------------------------------------------------- per-player state
static json& zs(Player& p) { json& z = p.stats["zone"]; if (!z.is_object()) z = json{{"food", 80}, {"water", 80}, {"sleep", 90}, {"stamina", 100}, {"psy", 100}, {"rad", 0}, {"bleed", 0}, {"money", 500}}; return z; }
float zget(const Player& p, const char* k, float def) {
  auto it = p.stats.find("zone"); if (it == p.stats.end() || !it->is_object()) return def;
  auto v = it->find(k); return v == it->end() || !v->is_number() ? def : v->get<float>();
}
void zset(Player& p, const char* k, float v) { zs(p)[k] = v; }
void zadd(Player& p, const char* k, float v, float lo, float hi) { json& z = zs(p); z[k] = std::clamp(z.value(k, 0.f) + v, lo, hi); }
std::string suitSkin(const Player& p) {
  static const std::unordered_map<std::string, const char*> SUIT = {
    {"suit_bandit", "z_npc_bandit"}, {"suit_drifter", "z_npc_drifter"}, {"suit_army", "z_npc_army"}, {"suit_merc", "z_npc_merc"}, {"suit_opensky", "z_npc_opensky"},
    {"suit_bastion", "z_npc_bastion"}, {"suit_choir", "z_npc_choir"}, {"suit_hazmat", "z_npc_scientist"}, {"suit_exo", "z_suit_exo"},
    {"suit_sealed", "z_suit_sealed"}, {"suit_assault", "z_suit_assault"}};
  std::string k = "z_player";
  if (p.armor.size() > 1 && !p.armor[1].empty()) if (auto it = SUIT.find(p.armor[1].id); it != SUIT.end()) k = it->second;
  // a helmet on: the look with it painted over the head (tools/zone/paint_looks.py)
  if (!p.armor.empty() && !p.armor[0].empty()) {
    const std::string& h = p.armor[0].id;
    if (h == "gasmask" || h == "helmet_tactical" || h == "helmet_sphere" || h == "helmet_psy") {
      std::string b = k.rfind("z_npc_", 0) == 0 ? k.substr(6) : k == "z_player" ? "player" : k.substr(7);   // z_suit_hazmat -> hazmat
      k = "z_look_" + b + "_" + h;
    }
  }
  return "textures/entity/zone/" + k + ".png";
}
double money(const Player& p) { auto it = p.stats.find("zone"); return it == p.stats.end() || !it->is_object() ? 500 : it->value("money", 500.0); }
void addMoney(Player& p, double v) { json& z = zs(p); z["money"] = std::max(0.0, z.value("money", 500.0) + v); }
int faction(const Player& p) { auto it = p.stats.find("zone"); return it == p.stats.end() || !it->is_object() ? F_DRIFTER : it->value("faction", (int)F_DRIFTER); }

std::string regionName(int x, int z) { const Region* r = regionAt(x, z); return r ? r->name : "The Zone"; }

// ---------------------------------------------------------------- weight and outfits
float itemWeight(const ItemStack& s) {
  if (s.empty()) return 0;
  if (const ZItem* z = zi(s.id)) {
    float w = z->w * s.n;
    // a gun carries its attachments and the rounds in it
    if (z->isGun && s.tag.is_object()) {
      if (s.tag.contains("att") && s.tag["att"].is_object()) for (auto& [slot, a] : s.tag["att"].items()) if (a.is_string()) if (const ZItem* az = zi(a.get<std::string>())) w += az->w;
      if (const ZItem* am = zi(s.tag.value("at", std::string()))) w += am->w * s.tag.value("am", 0);
    }
    if (s.id == "lead_sheet" && s.tag.is_object() && s.tag.contains("box") && s.tag["box"].is_array())
      for (auto& id : s.tag["box"]) if (id.is_string()) if (const ZItem* a = zi(id.get<std::string>())) w += a->w;
    return w;
  }
  return 0.15f * s.n;   // anything else: building stuff, odds and ends
}
int leadBoxed(const ItemStack& s) {
  if (s.id != "lead_sheet" || s.n != 1 || !s.tag.is_object() || !s.tag.contains("box") || !s.tag["box"].is_array()) return -1;
  return (int)s.tag["box"].size();
}
bool packLead(ItemStack& art, ItemStack& sheet, std::string& why) {
  const ZItem* z = zi(art.id);
  if (!z || z->cat != "artifact" || art.empty()) { why = "Only an artifact seals in lead"; return false; }
  if (&art == &sheet || sheet.id != "lead_sheet" || sheet.empty()) { why = "Fold a lead sheet around it"; return false; }
  const std::string id = art.id;
  if (sheet.n > 1) {
    sheet.n -= 1; art.clear();
    art.id = "lead_sheet"; art.n = 1; art.tag = json::object(); art.tag["box"] = json::array({id});
    return true;
  }
  json box = json::array();
  if (leadBoxed(sheet) >= 0) box = sheet.tag["box"];
  if ((int)box.size() >= 4) { why = "The lead box holds four"; return false; }
  box.push_back(id);
  if (!sheet.tag.is_object()) sheet.tag = json::object();
  sheet.tag["box"] = std::move(box);
  art.clear();
  return true;
}
bool unpackLead(Player& p, ItemStack& sheet, std::string& why) {
  if (leadBoxed(sheet) <= 0) { why = "Nothing is sealed in this lead"; return false; }
  json& box = sheet.tag["box"];
  ItemStack out(box.back().get<std::string>(), 1);
  bool placed = false;
  for (int i = 9; i < (int)p.inv.size() && !placed; i++) if ((i < 27 || i > 31) && p.inv[i].empty()) { p.inv[i] = out; placed = true; }
  for (int i = 0; i < 9 && !placed; i++) if (p.inv[i].empty()) { p.inv[i] = out; placed = true; }
  if (!placed) { why = "No room in your bags"; return false; }
  box.erase(box.end() - 1);
  if (box.empty()) sheet.tag = nullptr;
  return true;
}
bool sealLead(Player& p, ItemStack& art, std::string& why) {
  ItemStack* open = nullptr; ItemStack* plain = nullptr;
  for (auto& s : p.inv) {
    if (&s == &art || s.id != "lead_sheet" || s.empty()) continue;
    if (leadBoxed(s) >= 0 && leadBoxed(s) < 4) { open = &s; break; }
    if (!plain && leadBoxed(s) < 0) plain = &s;
  }
  ItemStack* sheet = open ? open : plain;
  if (!sheet) { why = "You need a lead sheet"; return false; }
  return packLead(art, *sheet, why);
}
float carryWeight(const Player& p) {
  float w = 0;
  for (auto& s : p.inv) w += itemWeight(s);
  for (auto& s : p.armor) w += itemWeight(s);
  for (auto& s : p.offhand) w += itemWeight(s);
  w += itemWeight(p.cursor);
  return w;
}
static float cond(const ItemStack& s) {
  const ItemDef* d = s.def(); if (!d) return 1;
  int dur = d->hasTool ? d->tool.durability : d->props.contains("armor") ? d->props["armor"].value("durability", 0) : 0;
  return dur > 0 ? std::clamp(1.f - (float)s.d / dur, 0.f, 1.f) : 1.f;
}
Resist resist(const Player& p) {
  Resist r;
  for (int slot = 0; slot < (int)p.armor.size() && slot < 2; slot++) {
    const ItemStack& s = p.armor[slot];
    const ZItem* z = s.empty() ? nullptr : zi(s.id);
    if (!z) continue;
    const json& e = z->raw.contains("suit") ? z->raw["suit"] : z->raw.contains("helmet") ? z->raw["helmet"] : json();
    if (!e.is_object()) continue;
    const float c = 0.4f + 0.6f * cond(s);   // a worn suit still keeps some out
    r.bullet += e.value("bullet", 0.f) * c; r.burn += e.value("burn", 0.f) * c; r.shock += e.value("shock", 0.f) * c;
    r.chem += e.value("chem", 0.f) * c; r.rad += e.value("rad", 0.f) * c; r.psy += e.value("psy", 0.f) * c;
    r.belt += e.value("belt", 0); r.carry += e.value("carry", 0.f);
  }
  for (const ItemStack* a : belt(p)) if (const ZItem* z = zi(a->id)) {
    const json& fx = z->raw["artifact"]["fx"];
    r.bullet += fx.value("bullet", 0.f); r.burn += fx.value("burn", 0.f); r.shock += fx.value("shock", 0.f);
    r.chem += fx.value("chem", 0.f); r.psy += fx.value("psy", 0.f); r.carry += fx.value("carry", 0.f);
  }
  auto cl = [](float& v) { v = std::clamp(v, -0.5f, 0.85f); };
  cl(r.bullet); cl(r.burn); cl(r.shock); cl(r.chem); cl(r.rad); cl(r.psy);
  return r;
}
// the belt: the first bag row's left slots (27..31), as many as the suit has hooks for
std::vector<const ItemStack*> belt(const Player& p) {
  std::vector<const ItemStack*> out;
  int n = 0;
  for (int slot = 0; slot < (int)p.armor.size() && slot < 2; slot++) if (const ZItem* z = p.armor[slot].empty() ? nullptr : zi(p.armor[slot].id)) if (z->raw.contains("suit")) n += z->raw["suit"].value("belt", 0);
  n = std::min(n, 5);
  for (int i = 0; i < n && 27 + i < (int)p.inv.size(); i++) {
    const ItemStack& s = p.inv[27 + i];
    if (const ZItem* z = s.empty() ? nullptr : zi(s.id); z && z->cat == "artifact") out.push_back(&s);
  }
  return out;
}
float carryCap(const Player& p) {
  const Resist r = resist(p);
  return 50.f + r.carry + (zget(p, "carryTime") > 0 ? zget(p, "carryBoost") : 0.f);
}
float walkCap(const Player& p) { return carryCap(p) + 50.f; }   // actor.ltx mass 50, walk 100; the same bonuses sit on both
float moveSlow(const Player& p) {
  if (p.creative()) return 1;
  const float w = carryWeight(p), walk = std::max(1.f, walkCap(p));
  float sp = 1.f;
  if (w > walk) sp = 0.06f;                              // IsCantWalkWeight: the legs stop
  else if (w + 10.f > walk) sp = 0.72f;                  // actor_status: within 10 kg of the walk cap
  if (zget(p, "limp") > 0.5f) sp = std::min(sp, 0.55f);  // limping_health / limping_power
  if (zget(p, "cantWalk") > 0.5f) sp = std::min(sp, 0.06f);
  return sp;
}
bool noSprint(const Player& p) {
  if (p.creative()) return false;
  if (zget(p, "cantSprint") > 0.5f) return true;         // cant_sprint_power 0.08, cleared at 0.10
  if (carryWeight(p) > walkCap(p)) return true;
  for (int slot = 0; slot < (int)p.armor.size() && slot < 2; slot++) if (const ZItem* z = p.armor[slot].empty() ? nullptr : zi(p.armor[slot].id)) if (z->raw.contains("suit") && z->raw["suit"].value("noSprint", false)) return true;
  return false;
}

// ---------------------------------------------------------------- emissions
// every EM_CYCLE seconds of the world clock: a warning, the wave, the aftermath
constexpr double EM_CYCLE = 2700, EM_WARN = 90, EM_WAVE = 70, EM_AFTER = 60, EM_FIRST = 1500;
static double worldClock(const Level& lv) { return lv.info.day * DAY_LEN + lv.info.time; }
i64 emissionCycle(const Level& lv) { const double t = worldClock(lv) - EM_FIRST; return t < 0 ? -1 : (i64)std::floor(t / EM_CYCLE); }
Emission emission(const Level& lv) {
  Emission e;
  if (!zoneWorld(lv) || lv.info.rules.value("emissions", true) == false) return e;
  const double t = worldClock(lv) - EM_FIRST;
  if (t < -EM_WARN) { e.left = (float)(-t - EM_WARN); return e; }
  const double c = std::fmod(std::fmod(t, EM_CYCLE) + EM_CYCLE, EM_CYCLE);   // 0 = the wave starts
  const double untilWave = c < EM_WAVE + EM_AFTER ? EM_CYCLE - c : EM_CYCLE - c;
  if (c < EM_WAVE) { e.phase = 2; e.t = (float)c; e.left = (float)(EM_WAVE - c); e.strength = (float)std::min(1.0, std::min(c / 8, (EM_WAVE - c) / 8)); }
  else if (c < EM_WAVE + EM_AFTER) { e.phase = 3; e.t = (float)(c - EM_WAVE); e.left = (float)(EM_WAVE + EM_AFTER - c); e.strength = (float)(0.6 * (1 - (c - EM_WAVE) / EM_AFTER)); }
  else if (untilWave <= EM_WARN) { e.phase = 1; e.t = (float)(EM_WARN - untilWave); e.left = (float)untilWave; e.strength = (float)(0.15 + 0.45 * (1 - untilWave / EM_WARN)); }
  else { e.phase = 0; e.left = (float)(untilWave - EM_WARN); }
  return e;
}
bool underCover(Level& lv, int realm, vec3 feet, float eye) {
  if (realm != REALM_MAIN) return true;   // the bunker
  World& w = lv.world(realm);
  const int x = ifloor(feet.x), z = ifloor(feet.z), y0 = ifloor(feet.y + eye) + 1;
  for (int y = y0; y < y0 + 40; y++) { const BlockId b = w.getBlock(x, y, z); if (b != AIR && reg().block(b).opaque) return true; }
  return false;
}
bool underCover(Level& lv, const Player& p) { return underCover(lv, p.realm, p.pos, p.eye); }

static bool storyFlag(const Level& lv, const char* k) { auto it = lv.info.data.find("zoneStory"); return it != lv.info.data.end() && it->is_object() && it->value(k, false); }
static bool arrayOff(const Level& lv) { return storyFlag(lv, "arrayOff"); }

static bool useZoneItem(PlayerController& pc, ItemStack& held, const ItemDef& def, bool repeat);

// ---------------------------------------------------------------- the body, every frame (local players)
namespace {
struct Body { float lastHp = -1, ours = 0; float pend = 0, geiger = 0, anomalyCd = 0, toastCd = 0, moneyCheck = 0, emToast = -1; i64 emWarned = -9; };
std::unordered_map<u32, Body>& bodies() { static std::unordered_map<u32, Body> b; return b; }
void hurt(Level& lv, Player& p, Body& b, float amount, const char* why) {
  b.pend += amount;
  if (b.pend >= 0.5f || (p.hp - b.pend <= 0)) {
    DamageSource src; src.type = why; src.knock = 0; src.bypass = true; src.bypassArmor = true;
    const float before = p.hp;
    damagePlayer(lv, p, b.pend, src);
    b.ours += before - p.hp;
    b.pend = 0;
  }
}
void applyBleed(Level& lv, Player& p, float amount) { hurt(lv, p, bodies()[p.id], amount, "bleeding"); }
} // namespace

// OpenXRay CWound: one size per place, 0..10. BleedingSpeed is the average of the open ones.
// HUD reads stats.zone.bleed (the sum, so the vignette still fills).
static const char* WOUND_AT[] = {"head", "torso", "arm", "leg"};
static json& woundsOf(Player& p) {
  json& w = zs(p)["wounds"];
  if (!w.is_object()) w = json::object();
  return w;
}
static void publishBleed(Player& p) {
  float sum = 0;
  const json& w = woundsOf(p);
  for (const char* k : WOUND_AT) sum += std::max(0.f, w.value(k, 0.f));
  zs(p)["bleed"] = std::clamp(sum, 0.f, 10.f);
}
void openWound(Player& p, const char* where, float amount) {
  if (!where || amount <= 0) return;
  bool known = false;
  for (const char* k : WOUND_AT) if (!std::strcmp(k, where)) known = true;
  if (!known) where = "torso";
  json& w = woundsOf(p);
  w[where] = std::clamp(w.value(where, 0.f) + amount, 0.f, 10.f);
  publishBleed(p);
}
static void noteHit(PlayerController& pc, const json& hit) {
  const std::string t = hit.value("type", "");
  // FireWound / Wound / Explosion / a fall open a wound. Burns, rad, psy, hunger and the bleed itself do not.
  const bool opens = t == "bullet" || t == "mob" || t == "explosion" || t == "fall" || t == "arrow" || t == "projectile";
  if (!opens) return;
  Player& p = pc.p;
  const float dealt = hit.value("dealt", 0.f);
  if (dealt <= 0) return;
  const char* loc = "torso";
  float scale = 1.5f;   // bip01_spine1 wound_scale
  if (t == "fall") { loc = "leg"; scale = 1.1f; }          // thigh
  else if (t == "explosion") { loc = "torso"; scale = 1.5f; }
  else if (hit.contains("ray") && hit["ray"].is_array() && hit["ray"].size() >= 6) {
    const json& r = hit["ray"];
    const float ox = r[0], oy = r[1], oz = r[2], dx = r[3], dy = r[4], dz = r[5];
    const float d2 = dx * dx + dz * dz;
    float tt = d2 > 1e-8f ? ((p.pos.x - ox) * dx + (p.pos.z - oz) * dz) / d2 : 0.f;
    if (tt < 0) tt = 0;
    const float hy = (oy + dy * tt) - p.pos.y;
    const float off = std::hypot((ox + dx * tt) - p.pos.x, (oz + dz * tt) - p.pos.z);
    const float H = std::max(0.6f, p.h);
    if (hy > 0.82f * H) { loc = "head"; scale = 2.f; }                              // bip01_head
    else if (off > 0.18f && hy > 0.55f * H && hy < 0.82f * H) { loc = "arm"; scale = 1.f; }  // upper arm
    else if (hy > 0.42f * H) { loc = "torso"; scale = 1.5f; }
    else { loc = "leg"; scale = 1.1f; }
  }
  const float rnd = 0.5f + std::fmod(std::fabs(std::sin(dealt * 12.9898f + p.pos.x * 78.233f + (float)p.id)), 1.f);  // AddWound rand 0.5..1.5
  const float before = zs(p).value("bleed", 0.f);
  openWound(p, loc, (dealt / std::max(1.f, p.maxHp)) * scale * rnd);
  if (zs(p).value("bleed", 0.f) > before + 0.02f) pc.toastMsg(std::string("Wounded: ") + loc, "warn");
}
// bleeding_v 0.0015 of a 0..1 body, wound_incarnation_v 0.0002, min_wound_size 0.0064.
// A medicine's boost_*_restore is per second on that same 0..1 scale (health and radiation).
static void tickWounds(Level& lv, Player& p, float dt) {
  json& Z = zs(p);
  json& B = Z["boost"];
  float heal = 0, radR = 0, inc = 0.0002f;
  if (B.is_object() && B.value("t", 0.f) > 0) {
    heal = B.value("heal", 0.f);
    radR = B.value("rad", 0.f);
    inc += B.value("wound", 0.f);
    B["t"] = B.value("t", 0.f) - dt;
  }
  for (const ItemStack* a : belt(p)) if (const ZItem* zz = zi(a->id)) inc += zz->raw["artifact"]["fx"].value("bleedHeal", 0.f) * 0.02f;
  json& w = woundsOf(p);
  float sum = 0, n = 0;
  for (const char* k : WOUND_AT) {
    float v = w.value(k, 0.f);
    if (v <= 0) continue;
    v -= inc * dt;
    if (v < 0.0064f) v = 0;
    w[k] = v;
    if (v > 0) { sum += v; n += 1; }
  }
  Z["bleed"] = std::clamp(sum, 0.f, 10.f);
  if (n > 0) applyBleed(lv, p, dt * (sum / n) * 0.0015f * std::max(1.f, p.maxHp));
  if (heal > 0) p.heal(heal * dt);
  if (radR > 0) Z["rad"] = std::max(0.f, Z.value("rad", 0.f) - radR * dt);
}

void adjustInput(PlayerController& pc, float dt, PlayerInput& in) {
  Level& lv = pc.lv; Player& p = pc.p;
  if (!zoneWorld(lv) || p.dead || dt <= 0) return;
  dt = std::min(dt, 0.25f);
  Body& b = bodies()[p.id];
  json& Z = zs(p);
  // rubles go straight into the wallet
  if ((b.moneyCheck -= dt) <= 0) {
    b.moneyCheck = 0.5f;
    for (auto& s : p.inv) if (!s.empty() && s.id == "rubles") { addMoney(p, s.n); pc.toastMsg("+" + std::to_string(s.n) + " RU", "rare"); s.clear(); }
  }
  const bool lampOn = p.armor.size() > 2 && p.armor[2].id == "headlamp";   // the head torch is worn in the inventory's LAMP slot
  if (in.torch) {
    if (lampOn) { Z["torch"] = !Z.value("torch", false); lv.fx->soundUi(p.id, "click"); }
    else pc.toastMsg("Wear a head torch first (inventory: the LAMP slot)", "warn");
  }
  if (!lampOn && Z.value("torch", false)) Z["torch"] = false;
  if (in.quickMed) {
    const bool wounded = Z.value("bleed", 0.f) > 0.01f;
    const bool radHigh = Z.value("rad", 0.f) > 25.f;
    const bool psyLow = Z.value("psy", 100.f) < 40.f;
    const char* order[6]; int n = 0;
    if (wounded) { order[n++] = "bandage"; order[n++] = "medkit_army"; order[n++] = "medkit_sci"; order[n++] = "medkit"; }
    else if (radHigh) order[n++] = "antirad";
    else if (psyLow) order[n++] = "psyblock";
    else if (p.hp < p.maxHp - 0.5f) { order[n++] = "medkit"; order[n++] = "medkit_army"; order[n++] = "medkit_sci"; }
    bool used = false;
    for (int i = 0; i < n && !used; i++) for (auto& s : p.inv) if (!s.empty() && s.id == order[i]) { if (const ItemDef* d = s.def()) used = useZoneItem(pc, s, *d, false); break; }
    if (!used) pc.toastMsg(n == 0 ? "You're not hurt" : wounded ? "No bandages" : radHigh ? "No anti-rad" : psyLow ? "No psy-block" : "No medkits", "warn");
  }
  storyTick(pc, dt, in.usePressed, in.sneak);
  jobsTick(pc, dt);
  if (p.creative() || p.mode == "spectator") return;
  // a blow that was not the Zone's own tick (hunger, rad, the bleed itself) opens a wound. sim/combat.cpp left the ray.
  if (p.data.contains("zoneHit") && p.data["zoneHit"].is_object()) { noteHit(pc, p.data["zoneHit"]); p.data.erase("zoneHit"); }
  b.ours = 0;
  const Resist R = resist(p);
  const bool moving = std::abs(in.mx) + std::abs(in.mz) > 0.1f;
  // ---- stamina and weight (CActorCondition::ConditionWalk / Jump / Stand, actor.ltx)
  // power is 0..1 there and 0..100 here. weight is carried / max_walk_weight.
  float st = Z.value("stamina", 100.f);
  const float walked = walkCap(p), specific = carryWeight(p) / std::max(1.f, walked);
  const bool tooHeavy = carryWeight(p) > walked;
  if (Z.value("cantSprint", 0.f) > 0.5f || tooHeavy) in.sprint = false;
  if (Z.value("cantWalk", 0.f) > 0.5f || tooHeavy) { in.sprint = false; in.jump = false; }
  if (moving && p.onGround) {
    float power = 0.0002f + 0.0044f * specific * (specific > 1.f ? 2.5f : 1.f);   // walk_power + walk_weight_power
    const bool sprinting = in.sprint;
    power *= sprinting ? 4.f : (!in.sneak ? 1.3f : 1.f);                            // sprint_k or accel_k
    st -= dt * power * 100.f;
  } else if (!moving) st -= dt * (-0.005f) * 100.f;                                 // stand_power, a slow refill
  if (in.jump && p.onGround && !pc.jumpWas) {
    const float jp = 0.018f + 0.2f * specific * (specific > 1.f ? 1.03f : 1.f);    // jump_power + jump_weight_power
    st -= jp * 100.f;
  }
  for (const ItemStack* a : belt(p)) if (const ZItem* z = zi(a->id)) st += z->raw["artifact"]["fx"].value("stamina", 0.f) * dt * 4;
  st = std::clamp(st, 0.f, 100.f); Z["stamina"] = st;
  bool cantS = Z.value("cantSprint", 0.f) > 0.5f;
  if (st < 8.f) cantS = true; else if (st > 10.f) cantS = false;                    // cant_sprint_power_begin / end
  bool cantW = Z.value("cantWalk", 0.f) > 0.5f;
  if (st < 0.15f) cantW = true; else if (st > 0.7f) cantW = false;                  // cant_walk_power 0.0015 / 0.007
  bool limp = Z.value("limp", 0.f) > 0.5f;
  if (st < 18.f || p.hp < 0.09f * p.maxHp) limp = true;
  else if (st > 29.f && p.hp > 0.12f * p.maxHp) limp = false;                       // limping_power 0.18/0.29, limping_health 0.09/0.12
  Z["cantSprint"] = cantS ? 1.f : 0.f; Z["cantWalk"] = cantW ? 1.f : 0.f; Z["limp"] = limp ? 1.f : 0.f;
  if (cantS || tooHeavy) in.sprint = false;
  if (cantW || tooHeavy) in.jump = false;
  if (tooHeavy && (b.toastCd -= dt) <= 0) { pc.toastMsg("You are carrying far too much to move", "warn"); b.toastCd = 6; }
  // ---- needs
  const float food = std::max(0.f, Z.value("food", 100.f) - dt * 100.f / (55 * 60));
  const float water = std::max(0.f, Z.value("water", 100.f) - dt * 100.f / (38 * 60) * (in.sprint ? 1.6f : 1.f));
  float sleep = Z.value("sleep", 100.f);
  sleep = p.sleeping ? std::min(100.f, sleep + dt * 3) : std::max(0.f, sleep - dt * 100.f / (150 * 60));
  Z["food"] = food; Z["water"] = water; Z["sleep"] = sleep;
  if (food <= 0) hurt(lv, p, b, dt * 0.05f, "hunger");
  if (water <= 0) hurt(lv, p, b, dt * 0.08f, "thirst");
  // ---- radiation: the ground, the anomalies' glow, the artifacts you carry
  const int x = ifloor(p.pos.x), z = ifloor(p.pos.z);
  float env = p.realm == REALM_MAIN ? radiationAt(x, z, (u32)lv.info.seed) : 0.f;
  if (p.realm == REALM_MAIN && ifloor(p.pos.y) - dimBase(DIM_OVERWORLD) < BASE - 6) env *= 0.3f;   // underground: the earth shields
  if (storyFlag(lv, "lightOut") && p.realm == REALM_MAIN) if (const Region* rg = regionAt(x, z)) if (!strcmp(rg->id, "turbines")) env *= 0.5f;   // the Ferryman's light going out thins the hum
  float artRad = 0;
  const auto onBelt = belt(p);
  // loose artifacts irradiate at 1.4x (no belt effect). On the belt, their own tax. Sealed in a lead sheet (tag.box) they are not in this loop.
  for (auto& s : p.inv) if (const ZItem* zz = s.empty() ? nullptr : zi(s.id); zz && zz->cat == "artifact") artRad += zz->raw["artifact"].value("rad", 0.f) * (std::find(onBelt.begin(), onBelt.end(), &s) != onBelt.end() ? 1.f : 1.4f);
  const json& boost = Z.value("boost", json::object());
  const float radGuard = (boost.is_object() && boost.value("t", 0.f) > 0) ? boost.value("radP", 0.f) : 0.f;   // scientific medkit
  const float psyGuardB = (boost.is_object() && boost.value("t", 0.f) > 0) ? boost.value("psy", 0.f) : 0.f;   // psy-block telepat protection
  float rad = Z.value("rad", 0.f);
  rad += (env * 0.6f * (1 - R.rad) * (1.f - radGuard) + artRad * 4) * dt;
  rad -= dt * 0.004f * 100 / 100;   // the body clears a little by itself
  rad = std::clamp(rad, 0.f, 100.f); Z["rad"] = rad;
  if (rad > 25) hurt(lv, p, b, dt * std::pow((rad - 25) / 75.f, 1.5f) * 0.45f, "radiation");
  Z["env"] = env;
  // ---- psy
  float psy = Z.value("psy", 100.f);
  float field = p.realm == REALM_MAIN ? psyAt(x, z) : 0.f;
  if (arrayOff(lv)) { if (const Region* ar = regionById("array")) if (std::hypot(float(x - ar->cx), float(z - ar->cz)) < 2500) field *= 0.1f; }
  if (storyFlag(lv, "lightOut") && p.realm == REALM_MAIN) if (const Region* rg = regionAt(x, z)) if (!strcmp(rg->id, "turbines")) field *= 0.35f;
  field = std::max(0.f, field - psyGuardB);
  float guard = Z.value("psyGuard", 0.f);
  if (guard > 0) { Z["psyGuard"] = guard - dt; field *= 0.15f; }
  psy = std::clamp(psy - field * (1 - R.psy) * dt * 5 + (field < 0.05f ? dt * 1.5f : 0), 0.f, 100.f);
  Z["psy"] = psy;
  if (psy <= 0) hurt(lv, p, b, dt * 0.8f, "psy");
  // ---- artifacts' gifts
  for (const ItemStack* a : belt(p)) if (const ZItem* zz = zi(a->id)) { const float rg = zz->raw["artifact"]["fx"].value("regen", 0.f); if (rg > 0) p.heal(rg * dt); }
  // ---- timers
  if (Z.value("carryTime", 0.f) > 0) Z["carryTime"] = Z.value("carryTime", 0.f) - dt;
  if (Z.value("drunk", 0.f) > 0) Z["drunk"] = Z.value("drunk", 0.f) - dt;
  // ---- anomalies: what you walk into (sim/zone_anomaly.cpp)
  anomalyBody(pc, dt, in, [&](float a, const char* why) { hurt(lv, p, b, a, why); });
  tickWounds(lv, p, dt);   // after a shard's cut, so the wound it just opened can bleed
  // ---- the emission
  const Emission em = emission(lv);
  if (em.phase == 1) {
    const i64 cyc = emissionCycle(lv) + 1;
    if (b.emWarned != cyc) { b.emWarned = cyc; pc.toastMsg("EMISSION COMING \xE2\x80\x94 get under a roof or underground!", "warn"); lv.fx->soundUi(p.id, "rift_hum"); }
  }
  if (em.phase == 2) {   // near the plant (CNPP North / South, the Generators) the wave hits earlier and harder
    const Region* rg = regionAt(ifloor(p.pos.x), ifloor(p.pos.z));
    const bool plant = rg && (!strcmp(rg->id, "npp") || !strcmp(rg->id, "npp_south") || !strcmp(rg->id, "turbines"));
    if (em.strength > (plant ? 0.25f : 0.5f) && !underCover(lv, p)) hurt(lv, p, b, dt * (plant ? 6.0f : 3.5f), "emission");
  }
  b.lastHp = p.hp;
  // geiger clicks (on this player's own machine)
  b.geiger += dt * (env + artRad * 20) * 5;
  if (b.geiger >= 1) { b.geiger = std::fmod(b.geiger, 1.f); lv.fx->soundUi(p.id, "click"); }
}

// ---------------------------------------------------------------- consumables, bolts, grenades, kits
ZoneUse zoneLastUse;
static bool useZoneItem(PlayerController& pc, ItemStack& held, const ItemDef& def, bool repeat) {
  const ZItem* z = zi(def.key);
  if (!z || repeat) return false;
  Level& lv = pc.lv; Player& p = pc.p;
  auto consume = [&] { if (!p.creative()) { if (--held.n <= 0) held.clear(); } };
  if (z->cat == "med" || z->cat == "food") {
    const json& u = z->raw["use"];
    // Anomaly boosters (items_medical.ltx / items_drugs.ltx). Rates are per second on a 0..1 body;
    // health maps onto maxHp, radiation onto 0..100. They do not clear the bar in one swallow.
    struct Boost { const char* id; float time, heal, wound, rad, psy, radP, eatHp; };
    static const Boost B[] = {
      {"bandage", 10.f, 0, 0.01f, 0, 0, 0, 0},
      {"medkit", 33.4f, 0.003f, 0, 0.00065f, 0, 0, 0},
      {"medkit_army", 33.4f, 0.005f, 0.005f, 0, 0, 0, 0},
      {"medkit_sci", 33.4f, 0.005f, 0.005f, 0.002f, 0.01f, 0.03f, 0},
      {"antirad", 10.8f, 0, 0, 0.0046f, 0, 0, -0.053f},
      {"psyblock", 1248.f, 0, 0, 0, 0.0109f, 0, 0},
    };
    const Boost* med = nullptr;
    for (const Boost& m : B) if (z->id == m.id) med = &m;
    if (med) {
      json& Z = zs(p);
      Z["boost"] = json{{"t", med->time}, {"heal", med->heal * std::max(1.f, p.maxHp)}, {"wound", med->wound},
                        {"rad", med->rad * 100.f}, {"psy", med->psy}, {"radP", med->radP}, {"id", med->id}};
      if (med->eatHp < 0) { DamageSource src; src.type = "med"; src.knock = 0; src.bypass = true; src.bypassArmor = true; damagePlayer(lv, p, -med->eatHp * std::max(1.f, p.maxHp), src); }
      pc.toastMsg(z->name, "");
      lv.fx->sound("tick", p.pos, 8);
      p.eatAnim = 0.6f;
      zoneLastUse.by = p.id; zoneLastUse.id = z->id; zoneLastUse.time = u.value("time", 1.5f); zoneLastUse.serial++;
      consume();
      return true;
    }
    if (u.contains("heal")) p.heal(u.value("heal", 0.f) * DMG_SCALE);
    if (u.contains("rad")) zadd(p, "rad", u.value("rad", 0.f));
    if (u.contains("psy")) zadd(p, "psy", -u.value("psy", 0.f));
    if (u.contains("food")) zadd(p, "food", u.value("food", 0.f));
    if (u.contains("water")) zadd(p, "water", u.value("water", 0.f));
    if (u.contains("sleep")) zadd(p, "sleep", u.value("sleep", 0.f));
    if (u.contains("stamina")) zadd(p, "stamina", u.value("stamina", 0.f));
    if (u.contains("psyGuard")) zset(p, "psyGuard", u.value("psyGuard", 0.f));
    if (u.contains("carryBoost")) { zset(p, "carryBoost", u.value("carryBoost", 0.f)); zset(p, "carryTime", u.value("carryTime", 300.f)); }
    if (u.contains("drunk")) zset(p, "drunk", u.value("drunk", 0.f));
    if (u.contains("sick") && std::fmod(lv.info.time * 7.3, 1.0) < u.value("sick", 0.f)) { zadd(p, "rad", 6); pc.toastMsg("That didn't sit well", "warn"); }
    pc.toastMsg(z->name, "");
    lv.fx->sound(z->cat == "food" ? "swap" : "tick", p.pos, 8);
    p.eatAnim = 0.6f;
    zoneLastUse.by = p.id; zoneLastUse.id = z->id; zoneLastUse.time = u.value("time", 1.5f); zoneLastUse.serial++;
    consume();
    return true;
  }
  if (z->cat == "detector") {   // the detector in the left hand, a bolt in the right: throw it
    for (ItemStack& b : p.inv) if (!b.empty() && b.id == "bolt") {
      if (lv.isRemote()) return false;
      const vec3 L = lookVector(p.yaw, p.pitch), from = p.pos + vec3(0, p.eye - 0.1f, 0) + L * 0.5f, v = L * 14.f + vec3(0, 2, 0);
      dropItem(lv, p.realm, ItemStack("bolt", 1), from, false, 0.f, &v, 1.0f);
      pc.swingArm();
      if (!p.creative() && --b.n <= 0) b.clear();
      return true;
    }
    return false;
  }
  if (z->cat == "throw" || z->cat == "grenade") {
    const vec3 L = lookVector(p.yaw, p.pitch);
    const vec3 from = p.pos + vec3(0, p.eye - 0.1f, 0) + L * 0.5f;
    if (z->cat == "throw") {
      if (lv.isRemote()) return false;   // the host throws it (useOnHost)
      const vec3 v = L * 14.f + vec3(0, 2, 0);
      dropItem(lv, p.realm, ItemStack(def.key, 1), from, false, 0.f, &v, 1.0f);
    } else spawnProjectile(lv, p.realm, from, L * 17.f + vec3(0, 3.f, 0), "cannonball", &p);
    pc.swingArm();
    consume();
    return true;
  }
  if (z->cat == "repair") {
    const json& r = z->raw["repair"];
    const bool weapon = r.value("kind", "weapon") == std::string("weapon");
    ItemStack* best = nullptr; float worst = 1;
    auto consider = [&](ItemStack& s) { const ZItem* t = s.empty() ? nullptr : zi(s.id); if (!t) return; if (weapon != (t->isGun || t->cat == "melee")) return; if (!weapon && t->cat != "suit" && t->cat != "helmet") return; const float c = cond(s); if (c < worst) { worst = c; best = &s; } };
    for (int i = 0; i < 9; i++) consider(p.inv[i]);
    for (auto& s : p.armor) consider(s);
    const float cap = r.value("cap", 70.f) / 100.f;
    if (!best || worst >= cap) { pc.toastMsg(weapon ? "No gun in your hotbar needs this kit" : "Nothing you wear needs this kit", "warn"); return true; }
    const ItemDef* bd = best->def();
    const int dur = bd->hasTool ? bd->tool.durability : bd->props["armor"].value("durability", 1);
    const float nc = std::min(cap, worst + r.value("amount", 25.f) / 100.f);
    best->d = std::max(0, (int)std::round(dur * (1 - nc)));
    pc.toastMsg("Repaired " + (zi(best->id) ? zi(best->id)->name : best->id) + " to " + std::to_string((int)std::round(nc * 100)) + "%", "");
    lv.fx->sound("anvil", p.pos, 10, 0.5f, 1.3f);
    consume();
    return true;
  }
  return false;
}

// the inventory screen uses a consumable from a slot (the same code as the use key)
bool useItemAt(PlayerController& pc, int slot) {
  Player& p = pc.p;
  if (!zoneWorld(pc.lv) || slot < 0 || slot >= (int)p.inv.size() || p.inv[slot].empty()) return false;
  const ItemDef* d = p.inv[slot].def();
  return d && useZoneItem(pc, p.inv[slot], *d, false);
}

// (detectors and artifacts: sim/zone_anomaly.cpp)

// ---------------------------------------------------------------- the bunker
constexpr int BUNKER_FLOOR = 100;   // band-local (gen/zone_bunker_gen.cpp builds the room here)
static int bunkerSlot(Level& lv, const std::string& name) {
  json& slots = lv.info.data["bunkerSlots"];
  if (!slots.is_object()) slots = json::object();
  if (!slots.contains(name)) slots[name] = (int)slots.size();
  return slots[name].get<int>();
}
bool travelBunker(Level& lv, Player& p) {
  if (lv.isRemote()) return false;
  auto put = [&](int realm, vec3 at) { lv.world(realm); p.realm = realm; p.pos = at; p.prevPos = at; p.vel = vec3(0); p.hasFallStart = false; lv.fx->sound("door_iron_close", at, 12); };
  if (p.realm == REALM_POCKET) {
    const Plan& P = zonePlan((u32)lv.info.seed);
    put(REALM_MAIN, vec3(P.hatchX + 0.5f, dimBase(DIM_OVERWORLD) + P.hatchY + 1.1f, P.hatchZ + 1.5f));
    lv.fx->message(p.id, "Back up into the Rookie Village", "");
  } else {
    const int slot = bunkerSlot(lv, p.name);
    put(REALM_POCKET, vec3(slot * 256 + 0.5f, (float)(dimBase(DIM_OVERWORLD) + BUNKER_FLOOR + 1), 2.5f));
    lv.fx->message(p.id, "Your bunker \xE2\x80\x94 dig it out, furnish it, keep your stash here. Emissions can't reach you down here.", "rare");
  }
  return true;
}

// ---------------------------------------------------------------- traders
// Anomaly trade_discounts.ltx: buy is what he pays you, sell is what you pay him.
// econ_1 (the default tourist economy): buy ×1.25, sell ×0.75. Restock is one in-game day (restock = 24 hours).
struct Disc { double buy, sell; };
static Disc discOf(const std::string& t) {
  if (t == "hamlet") return {0.20, 1.20};                         // Sidorovich, discount_trasher
  if (t == "molot" || t == "yanov" || t == "zaton") return {0.35, 1.00};  // discount_barman
  if (t == "kalina") return {0.35, 0.80};                         // discount_scientist
  if (t == "marsh") return {0.20, 1.10};                          // Clear Sky's Spore, discount_supplier
  return {0.10, 1.50};                                            // discount_generic
}
static bool wearsOut(const ZItem& z) { return z.isGun || z.cat == "melee" || z.cat == "suit" || z.cat == "helmet"; }
static double sectionFactor(const std::string& trader, const ZItem& z) {
  return (trader == "kalina" && z.cat == "artifact") ? 1.15 : 1.0;   // the ecologists' artefact line
}
double buyPrice(const std::string& id, const std::string& trader) {
  const ZItem* z = zi(id); if (!z) return 5;
  const Disc d = discOf(trader);
  // a fresh piece: cond 1, so pow(0.9+0.1, sell exponent 0.75) is 1
  return std::max(1.0, std::round(z->price * sectionFactor(trader, *z) * d.sell * 0.75));
}
double sellPrice(const ItemStack& s, const std::string& trader) {
  const ZItem* z = zi(s.id); if (!z || s.n <= 0) return 0;
  const Disc d = discOf(trader);
  const bool wears = wearsOut(*z);
  const double c = cond(s);
  if (wears && c < 0.50) return 0;   // buy_item_condition_factor: he will not take it
  const double curve = wears ? std::pow(c * 0.9 + 0.1, 2.25) : 1.0;   // buy_item_exponent; ammo and meds skip it
  return std::max(0.0, std::floor(z->price * sectionFactor(trader, *z) * d.buy * 1.25 * curve * s.n));
}
std::vector<TradeLine> traderStock(Level& lv, const std::string& trader, const Player* buyer) {
  std::vector<TradeLine> out;
  // 24 in-game hours is one Exclusion day (DAY_LEN), not an emission.
  int tier = 0;
  if (buyer && trader == "hamlet") {   // Sidorovich's supplies_1..5 at goodwill 0 / 400 / 600 / 800 / 1000
    const int s = standing(*buyer, F_DRIFTER);
    tier = s >= 1000 ? 4 : s >= 800 ? 3 : s >= 600 ? 2 : s >= 400 ? 1 : 0;
  }
  static const double CAP[] = {9000, 14000, 22000, 32000, 48000};
  static const double CH[] = {0.20, 0.26, 0.32, 0.38, 0.45};
  const bool sidor = trader == "hamlet";
  const double cap = sidor ? CAP[tier] : 30000;
  const double chance = sidor ? CH[tier] : 0.35;
  Mulberry32 r(hash32(lv.info.seed, (i32)lv.info.day, (i32)std::hash<std::string>()(trader), 5));
  static const char* basics[] = {"bandage", "medkit", "antirad", "bread", "sausage", "canned_stew", "water", "vodka", "energy", "bolt", "ammo_9x18", "ammo_12g_buck",
                                 "ammo_545", "repair_weapon", "repair_armor", "detector_echo", "headlamp", "pda"};
  for (const char* id : basics) out.push_back({id, zi(id) && zi(id)->stack > 1 ? std::min(20, zi(id)->stack) : 1, buyPrice(id, trader)});
  for (const ZItem* z : allZoneItems()) {
    if (z->cat == "artifact" || z->cat == "trophy" || z->cat == "money" || z->cat == "part") continue;
    if (z->cat == "detector" && z->raw["detector"].value("tier", 1) >= 4) continue;   // the picture box is the ecologists' alone
    if (z->price > cap) continue;
    if (r() < chance) out.push_back({z->id, z->stack > 1 ? std::min(z->stack, z->cat == "ammo" ? 60 : 5) : 1, buyPrice(z->id, trader)});
  }
  if (trader == "kalina") out.push_back({"detector_svarog", 1, buyPrice("detector_svarog", trader)});
  return out;
}

// ---------------------------------------------------------------- the host: artifacts, creatures and the emission
namespace {
struct ZoneSystem : LevelSystem {
  double scan = 0, hurtT = 0;
  const char* name() const override { return "zone"; }
  void tick(Level& lv, double dt) override {
    if (!zoneWorld(lv) || lv.isRemote()) return;
    scan -= dt;
    if (scan <= 0) { scan = 0.5; traders(lv); }
    // the wave kills what it catches outdoors (creatures near players)
    const Emission em = emission(lv);
    if (em.phase == 2 && em.strength > 0.5f && (hurtT -= dt) <= 0) {
      hurtT = 1;
      // who it catches first, then the damage: a death drops loot and a corpse into lv.entities(), which would move the
      // vector under a loop still walking it (the emission crash). The roof test is the player's (eye line, 40 blocks).
      std::vector<u32> caught;
      for (auto& e : lv.entities()) {
        if (e->removed || e->isPlayer() || e->realm != REALM_MAIN || e->type == "item" || e->ghost) continue;
        auto* c = dynamic_cast<Creature*>(e.get()); if (!c || !c->alive()) continue;
        if (!underCover(lv, c->realm, c->pos, c->h * 0.9f)) caught.push_back(e->id);
      }
      for (u32 id : caught) if (Entity* e = lv.entity(id); e && !e->removed) { DamageSource s; s.type = "emission"; s.knock = 0; s.bypass = true; damageEntity(lv, *e, 4, s); }
    }
  }
  void traders(Level& lv) {
    const Plan& P = zonePlan((u32)lv.info.seed);
    const int by = dimBase(DIM_OVERWORLD);
    struct Spot { const char* key; const char* smart; };
    static const Spot spots[] = {{"hamlet", nullptr}, {"molot", "100 Rads Bar"}, {"yanov", "Yanov Station"}, {"zaton", "Skadovsk"}, {"kalina", "Ecologists' bunker"}, {"marsh", "Clear Sky base"}};
    for (const Spot& s : spots) {
      vec3 at;
      if (!s.smart) at = vec3(P.traderX + 0.5f, by + P.traderY + 0.05f, P.traderZ + 0.5f);
      else {
        bool f = false;
        for (const Smart& m : P.smarts) if (m.name == s.smart) {
          at = vec3(m.x + 0.5f, by + m.y + 1.05f, m.z + 0.5f);
          // the Rostok hall: he stands behind the counter, not on the guitar fire the room gathers at
          if (std::string(s.key) == "molot" && m.x == RADS_FIRE_X && m.z == RADS_FIRE_Z) {
            at.x = RADS_BARMAN_X + 0.5f; at.z = RADS_BARMAN_Z + 0.5f;
          }
          f = true; break;
        }
        if (!f) continue;
      }
      bool nearP = false;
      for (auto& e : lv.entities()) if (e->isPlayer() && !e->removed && e->realm == REALM_MAIN && std::hypot(e->pos.x - at.x, e->pos.z - at.z) < 90) nearP = true;
      if (!nearP) continue;
      bool have = false;
      for (auto& e : lv.entities()) if (!e->removed && e->type == "z_npc_trader" && e->data.value("trader", "") == s.key) { have = true; break; }
      if (have) continue;
      if (s.smart) {   // on the floor where the place is (inside the user's buildings), else on top of what stands there
        World& w = lv.world(REALM_MAIN);
        const int fx = ifloor(at.x), fz = ifloor(at.z), fy = by + (int)std::floor(at.y - by - 1.05f);
        const int top = w.surfaceY(fx, fz, DIM_OVERWORLD); if (top <= 0) continue;
        at.y = top + 1.05f;
        for (int dy = -1; dy <= 4; dy++) if (w.getBlock(fx, fy + dy, fz) == AIR && w.getBlock(fx, fy + dy + 1, fz) == AIR && reg().tSolid[w.getBlock(fx, fy + dy - 1, fz)]) { at.y = fy + dy + 0.05f; break; }
      }
      if (Entity* e = spawnCreature(lv, "z_npc_trader", REALM_MAIN, at)) e->data["trader"] = s.key;
    }
  }
};
} // namespace

void registerZone() {
  static bool done = false; if (done) return; done = true;
  loadCatalogue();
  hooks().useItem.push_back([](PlayerController& pc, ItemStack& held, const ItemDef& def, bool repeat) { return zoneWorld(pc.lv) && useZoneItem(pc, held, def, repeat); });
  // a campfire cooks (ui/zone_craft.cpp; stoves, workbenches and the block game's stations are routed in ui/ui_main.cpp)
  hooks().useBlock.push_back([](PlayerController& pc, const RayHit& hit, ItemStack*, const ItemDef*) {
    if (!zoneWorld(pc.lv) || !hit.hit) return false;
    static const BlockId fire = reg().blockId("campfire"), soul = reg().blockId("soul_campfire");
    if (hit.id != fire || !fire) { if (hit.id != soul || !soul) return false; }
    pc.swingArm(); openScreen(pc.lv, pc.p, "zone_cook");
    return true;
  });
  // the hatch in the Rookie Village, and the one back up from inside the bunker
  hooks().useBlock.push_back([](PlayerController& pc, const RayHit& hit, ItemStack*, const ItemDef*) {
    if (!zoneWorld(pc.lv) || !hit.hit) return false;
    World& w = pc.lv.world(pc.p.realm);
    json* be = w.blockEntity(hit.block.x, hit.block.y, hit.block.z);
    if (!be || !be->is_object() || be->value("type", "") != "zone_hatch") return false;
    if (pc.lv.isRemote()) return false;   // the host does it (useOnHost carries the click)
    return travelBunker(pc.lv, pc.p);
  });
  registerLevelSystem([](Level& lv) -> std::shared_ptr<LevelSystem> { return zoneWorld(lv) ? std::make_shared<ZoneSystem>() : nullptr; });
  registerZoneAnomalies();
  registerZoneAlife();
  registerZoneStory();
  registerZoneNpcs();
  registerZoneJobs();
}

} // namespace dq::zone
