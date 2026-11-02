// sim/zone_npc.cpp — the Zone's people when they are online (A-Life puts them in the world near players): who
// they fight (factions' standings, anyone who shot at them, mutants), keeping a fighting distance, taking cover
// behind their aim, and firing the gun they carry (hit-scan, worse aim far off and at a moving target). Traders
// stand at their counters (spawned by the host where the plan put them) and open the trade screen when used.
#include "sim/zone.h"
#include "sim/creature.h"
#include "sim/combat.h"
#include "sim/physics.h"
#include "sim/magic_combat.h"
#include "core/registry.h"
#include "gen/zone_plan.h"
#include <cmath>
#include <random>

namespace dq::zone {

namespace {
std::mt19937& rng() { static thread_local std::mt19937 g{std::random_device{}()}; return g; }
float frand() { return std::uniform_real_distribution<float>(0, 1)(rng()); }

int npcFaction(const Creature& c) {
  if (c.data.is_object() && c.data.contains("faction")) return c.data.value("faction", (int)F_DRIFTER);
  const std::string& k = c.type;
  if (k == "z_npc_bandit") return F_BANDIT; if (k == "z_npc_army") return F_ARMY; if (k == "z_npc_bastion") return F_BASTION;
  if (k == "z_npc_opensky") return F_OPENSKY; if (k == "z_npc_merc") return F_MERC; if (k == "z_npc_choir") return F_CHOIR;
  if (k == "z_npc_scientist") return F_INSTITUTE;
  return F_DRIFTER;
}
int factionOf(const Entity& e) {
  if (auto* p = dynamic_cast<const Player*>(&e)) return faction(*p);
  if (auto* c = dynamic_cast<const Creature*>(&e)) return c->type.rfind("z_npc_", 0) == 0 ? npcFaction(*c) : F_MUTANT;
  return F_NONE;
}
bool angryAt(const Creature& c, u32 id) {
  if (!c.data.is_object() || !c.data.contains("grudges")) return false;
  for (auto& g : c.data["grudges"]) if (g.is_number() && g.get<u32>() == id) return true;
  return false;
}
bool hostileTo(const Creature& c, const Entity& e) {
  if (e.removed) return false;
  if (auto* o = dynamic_cast<const Creature*>(&e)) { if (!o->alive()) return false; }
  else if (auto* p = dynamic_cast<const Player*>(&e)) { if (p->dead || p->creative() || p->mode == "spectator") return false; }
  else return false;
  if (angryAt(c, e.id)) return true;
  const int a = npcFaction(c), b = factionOf(e);
  if (b == F_NONE) return false;
  if (b == F_MUTANT) return true;
  return factionRelation(a, b) < 0;
}
bool sees(Level& lv, const Entity& from, const Entity& to) {
  World& w = lv.world(from.realm);
  const glm::dvec3 o(from.pos.x, from.pos.y + from.h * 0.9f, from.pos.z);
  const vec3 t = to.pos + vec3(0, to.h * 0.75f, 0);
  const vec3 d = t - vec3(o); const float L = glm::length(d);
  if (L < 0.5f) return true;
  const RayHit h = raycastBlocks(w, o, d / L, L);
  return !h.hit || h.dist >= L - 0.5f;
}
// what each faction carries: damage per shot (catalogue scale), shots per second, how far they'll fight
struct Arms { float dmg, rate, range; };
Arms armsOf(int f, int tier) {
  Arms a{22, 1.6f, 38};
  switch (f) {
    case F_BANDIT: a = {18, 1.4f, 30}; break;
    case F_ARMY: a = {25, 2.0f, 45}; break;
    case F_BASTION: case F_OPENSKY: a = {25, 2.2f, 45}; break;
    case F_MERC: a = {26, 2.4f, 48}; break;
    case F_CHOIR: a = {30, 2.4f, 55}; break;
    case F_INSTITUTE: a = {12, 1.0f, 20}; break;
    default: break;
  }
  a.dmg *= 1 + (tier - 1) * 0.1f;
  return a;
}

OptMove npcBrain(Level& lv, Creature& c, Entity*, float, float, float, float dt) {
  if (!zoneWorld(lv) || lv.isRemote() || !c.alive()) return std::nullopt;
  json& D = c.data;
  float scan = D.value("scanT", 0.f) - dt; D["scanT"] = scan;
  u32 tid = D.value("tgt", 0u);
  Entity* T = tid ? lv.entity(tid) : nullptr;
  if (T && (T->removed || T->realm != c.realm || !hostileTo(c, *T) || glm::length(T->pos - c.pos) > 60)) { T = nullptr; D["tgt"] = 0; }
  if (scan <= 0) {
    D["scanT"] = 0.6f + frand() * 0.4f;
    const Arms A = armsOf(npcFaction(c), 1);
    Entity* best = nullptr; float bd = A.range;
    for (auto& e : lv.entities()) {
      Entity* E = e.get();
      if (E == &c || E->realm != c.realm || E->type == "item") continue;
      const float d = glm::length(E->pos - c.pos);
      if (d >= bd || !hostileTo(c, *E)) continue;
      if (!sees(lv, c, *E)) continue;
      bd = d; best = E;
    }
    if (best) { T = best; D["tgt"] = best->id; }
  }
  if (!T) return std::nullopt;
  const vec3 to = T->pos - c.pos;
  const float dist = glm::length(vec3(to.x, 0, to.z));
  const float yaw = std::atan2(-to.x, -to.z);
  c.yaw = yaw; c.aiming = true;
  const int tier = D.value("tier", 1);
  const Arms A = armsOf(npcFaction(c), tier);
  // fire
  float cd = D.value("fireT", 0.f) - dt; D["fireT"] = cd;
  if (cd <= 0 && dist < A.range && sees(lv, c, *T)) {
    const bool burst = frand() < 0.3f;
    D["fireT"] = (burst ? 0.12f : 1.f / A.rate) * (0.7f + frand() * 0.6f);
    const vec3 muzzle = c.pos + vec3(0, c.h * 0.8f, 0);
    lv.fx->sound("firework_blast", muzzle, 80, 0.7f, 1.1f + frand() * 0.2f);
    lv.fx->particles("smoke", muzzle - glm::normalize(to + vec3(1e-4f)) * -0.6f, 1);
    float chance = std::clamp(0.8f - dist / (A.range * 1.4f), 0.12f, 0.8f);
    if (glm::length(vec3(T->vel.x, 0, T->vel.z)) > 3) chance *= 0.65f;
    if (auto* p = dynamic_cast<Player*>(T); p && p->sneaking) chance *= 0.8f;
    if (frand() < chance) {
      const float dmg = A.dmg * DMG_SCALE * (0.8f + frand() * 0.4f);
      if (auto* p = dynamic_cast<Player*>(T)) {
        DamageSource s; s.type = "bullet"; s.attacker = c.id; s.knock = 0.6f;
        s.hasRay = true; s.rayO = muzzle; s.rayD = glm::normalize(to);
        // the suit's bullet protection
        const float res = resist(*p).bullet;
        damagePlayer(lv, *p, dmg * (1 - res * 0.6f), s);
      } else {
        DamageSource s; s.type = "bullet"; s.attacker = c.id; s.knock = 0.8f; s.tool = "gun";
        damageEntity(lv, *T, dmg, s);
      }
    }
  }
  // keep a fighting distance: close in from far, back off if they're on top of us, else sidestep
  Move m; m.dir = yaw;
  if (dist > A.range * 0.6f) m.want = 1;
  else if (dist < 6) { m.want = 1; m.dir = yaw + 3.14159f; }
  else { m.want = 0.35f; m.dir = yaw + ((c.id & 1) ? 1.4f : -1.4f); }
  return m;
}

// whoever shoots a stalker makes an enemy of him and his squad nearby
void npcHurt(Level& lv, Creature& c, float dealt, const DamageSrc& src) {
  if (!src.attacker || lv.isRemote()) return;
  auto grudge = [&](Creature& who) {
    json& g = who.data["grudges"]; if (!g.is_array()) g = json::array();
    for (auto& x : g) if (x.is_number() && x.get<u32>() == src.attacker) return;
    g.push_back(src.attacker); who.data["tgt"] = src.attacker;
  };
  grudge(c);
  const int sq = c.data.value("alife", -1);
  if (sq >= 0) for (auto& e : lv.entities()) if (auto* o = dynamic_cast<Creature*>(e.get()); o && o != &c && o->data.value("alife", -2) == sq) grudge(*o);
  (void)dealt;
}
} // namespace

void registerZoneNpcs() {
  for (const char* k : {"z_npc_drifter", "z_npc_bandit", "z_npc_army", "z_npc_bastion", "z_npc_opensky", "z_npc_merc", "z_npc_choir", "z_npc_scientist"}) {
    CreatureHooks h;
    h.brain = npcBrain;
    h.afterHurt = npcHurt;
    setCreatureHooks(k, h);
  }
  // the traders: using one opens the trade screen on that player's own machine (ui/zone_screens.cpp watches for it)
  CreatureHooks t;
  t.brain = [](Level&, Creature& c, Entity* p, float dist, float, float, float) -> OptMove {
    if (p && dist < 8) { const vec3 d = p->pos - c.pos; c.yaw = std::atan2(-d.x, -d.z); }
    Move m; m.want = 0; m.dir = c.yaw; return m;   // stays behind the counter
  };
  t.beforeHurt = [](Level&, Creature&, float& amount, const DamageSrc&) { amount = 0; return false; };   // the Zone's traders are not for shooting
  t.onUse = [](Level&, Player&, Creature&, bool) { return true; };
  setCreatureHooks("z_npc_trader", t);
}

} // namespace dq::zone
