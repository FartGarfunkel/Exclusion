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
  if (auto* p = dynamic_cast<const Player*>(&e)) return attitude(*p, a) < 0;   // a player: his own standing with them (sim/zone_jobs.cpp)
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

// ---- a stalker's life when nobody is shooting: the camp fire, the guitar, the stories, the night's sleep; posts held
// and walked; anomaly fields swept for artifacts; the road between. What the squad is doing comes from A-Life.
const char* STORIES[] = {
  "Heard the Wolves took the brick works again. Third time this month.",
  "My cousin went north past the Array. His PDA still pings sometimes. From the plant.",
  "A bolt's the best friend you'll ever have. Doesn't talk back, doesn't eat.",
  "Moroz gave me half for a Moth Wing. Half! Said it was 'used'.",
  "They say the Ferryman wasn't lucky. He just knew where the Zone was looking.",
  "Saw a shroud at Lake Kalina. Or it saw me. Hard to tell with those.",
  "Don't drink from the marsh. Unless you want to glow on the way home.",
  "Emission's coming. I can feel it in my teeth.",
  "Rookie, if you hear dogs, climb something. Anything.",
  "The Furnace's barman waters the vodka. Says it's for our health.",
  "Found a Hollow Star in a bus at Rassokha once. Bought this jacket with it. Then the jacket got shot.",
  "Freedom think the Zone's a gift. Duty think it's a disease. I think it's Tuesday.",
  "Play the one about the girl from Pripyat. No, the sad one.",
  "Two stalkers walk into an anomaly. That's the whole joke.",
  "Every time I swear it's my last trip north, the Zone laughs.",
  "The scientists at Kalina pay in cash and questions. Mostly questions.",
  "Old Grach dug a fridge out of the Scrapyard. Still cold. Nobody's opened it.",
  "Monolith prayers come over the radio some nights. Turn it off. Trust me.",
  "Bandits on the gorge road. Go round by the marsh, it's only wet.",
  "When the sky goes red you've got two minutes. One if you're slow.",
};
const char* HUNT[] = {"Detector's going mad over here.", "Careful, burner on your left.", "Throw a bolt, I'm not stepping there.", "Something's glowing in the grass...", "Empty. The last emission must have moved it."};
const char* GREET[] = {"Good hunting, stalker.", "Sit down, warm up.", "Hey! Watch the fire.", "Got any vodka?", "Mind your step out there."};

void say(Level& lv, Creature& c, const char* line, u32 colour = 0xE6DCC4) {
  lv.fx->particles("floatText", c.pos + vec3(0, c.h + 0.5f, 0), 1, json{{"text", line}, {"color", colour}});
}

OptMove life(Level& lv, Creature& c, Entity* nearP, float distP, float dt) {
  json& D = c.data;
  const int sq = D.value("alife", -1);
  // the squad's place, looked up now and then
  float gT = D.value("goalT", 0.f) - dt; D["goalT"] = gT;
  if (gT <= 0 || !D.contains("gk")) {
    D["goalT"] = 2.f;
    const SquadGoal g = alifeGoal(lv, sq);
    if (!g.ok) { D.erase("gk"); return std::nullopt; }
    D["gx"] = g.pos.x; D["gy"] = g.pos.y; D["gz"] = g.pos.z; D["gk"] = g.kind; D["gr"] = g.radius;
  }
  if (!D.contains("gk")) return std::nullopt;
  const vec3 G(D.value("gx", 0.f), D.value("gy", 0.f), D.value("gz", 0.f));
  const int kind = D.value("gk", 0); const float rad = D.value("gr", 10.f);
  const float night = [&] { const double f = std::fmod(lv.info.time, DAY_LEN) / DAY_LEN; return (f > 0.52 && f < 0.95) ? 1.f : 0.f; }();
  const u32 me = c.id;
  auto walkTo = [&](vec3 to, float speed) -> OptMove {
    const vec3 d = to - c.pos; const float L = std::hypot(d.x, d.z);
    c.sitting = false; c.sleeping = false; D["guitar"] = false;
    if (L < 0.7f) return std::nullopt;
    Move m; m.want = speed; m.dir = std::atan2(-d.x, -d.z); return m;
  };
  const vec3 toG = G - c.pos;
  const float dG = std::hypot(toG.x, toG.z);
  // on the road: walk there together (a loose file, each a little to the side)
  if (dG > rad + 4) {
    const float side = ((me % 5) - 2.f) * 1.4f;
    const vec3 n = glm::normalize(vec3(-toG.z, 0, toG.x) + vec3(1e-4f));
    return walkTo(G + n * side, 0.5f);
  }
  // greet a player who comes close
  if (nearP && distP < 6 && !D.value("greeted", false)) { D["greeted"] = true; say(lv, c, GREET[me % 5]); }
  if (nearP && distP > 25) D["greeted"] = false;
  const float tnow = (float)std::fmod(lv.info.day * DAY_LEN + lv.info.time, 100000.0);
  switch (kind) {
    case SM_CAMP: case SM_HUB: {
      // a seat in the ring round the fire, facing in
      const float a = (me % 7) / 7.f * 6.2832f + (me % 3) * 0.3f, r = kind == SM_HUB ? 4.5f + (me % 3) : 2.8f + (me % 2) * 0.6f;
      const vec3 seat = G + vec3(std::cos(a) * r, 0, std::sin(a) * r);
      if (auto m = walkTo(seat, 0.4f)) return m;
      const vec3 in = G - c.pos;
      c.yaw = std::atan2(-in.x, -in.z);
      // night: most of the camp sleeps; one keeps watch
      if (night > 0 && me % 4 != 0) { c.sleeping = true; c.sitting = false; D["guitar"] = false; Move m; m.want = 0; m.dir = c.yaw; return m; }
      c.sleeping = false; c.sitting = true;
      // the guitar: whoever sits at the camp's lowest-numbered seat plays a few songs, then rests
      const bool player = (me % 7) == 0 || (me % 7) == 3;
      const float cyc = std::fmod(tnow + (me % 13) * 37.f, 300.f);
      D["guitar"] = player && cyc < 190;
      if (D["guitar"].get<bool>() && std::fmod(tnow, 2.f) < dt) lv.fx->particles("note", c.pos + vec3(0, c.h, 0), 1);
      // a story now and then
      float st = D.value("talkT", 8.f + (me % 17)) - dt; D["talkT"] = st;
      if (st <= 0) { D["talkT"] = 25.f + (me % 11) * 3; if (!D.value("guitar", false)) say(lv, c, STORIES[(me + (u32)(tnow / 30)) % (sizeof(STORIES) / sizeof(STORIES[0]))]); }
      Move m; m.want = 0; m.dir = c.yaw; return m;
    }
    case SM_BASE: case SM_POST: {
      // guards: half hold the post looking out, half walk the line
      const float a = (me % 6) / 6.f * 6.2832f;
      if (me % 2 == 0) {
        const vec3 post = G + vec3(std::cos(a) * rad * 0.7f, 0, std::sin(a) * rad * 0.7f);
        if (auto m = walkTo(post, 0.4f)) return m;
        c.sitting = false; c.yaw = std::atan2(-std::cos(a), -std::sin(a)) + 3.14159f;
        Move m; m.want = 0; m.dir = c.yaw; return m;
      }
      const float pa = a + tnow * 0.05f;
      return walkTo(G + vec3(std::cos(pa) * rad, 0, std::sin(pa) * rad), 0.35f).value_or(Move{0, c.yaw});
    }
    case SM_FIELD: {
      // artifact hunting: slow sweeps through the field, a mutter when the detector finds something
      const float k = std::floor(tnow / 9.f) + me * 7;
      const float a = std::fmod(k * 2.39996f, 6.2832f), r = rad * (0.3f + std::fmod(k * 0.618f, 0.7f));
      float ht = D.value("huntT", 12.f) - dt; D["huntT"] = ht;
      if (ht <= 0) { D["huntT"] = 18.f + (me % 9); say(lv, c, HUNT[(me + (u32)k) % 5], 0xB4E68C); }
      return walkTo(G + vec3(std::cos(a) * r, 0, std::sin(a) * r), 0.3f).value_or(Move{0, c.yaw});
    }
    default: {
      // a ruin, a lair: poke about the place
      const float k = std::floor(tnow / 12.f) + me * 3;
      const float a = std::fmod(k * 2.39996f, 6.2832f);
      return walkTo(G + vec3(std::cos(a) * rad * 0.6f, 0, std::sin(a) * rad * 0.6f), 0.35f).value_or(Move{0, c.yaw});
    }
  }
}

OptMove npcBrain(Level& lv, Creature& c, Entity* nearP, float distP, float, float, float dt) {
  if (!zoneWorld(lv) || lv.isRemote() || !c.alive()) return std::nullopt;
  json& D = c.data;
  // which of the faction's suits he wears (render/zone_creature_draw.cpp zoneNpcSkin; data.hw rides the snapshot)
  if (!D.contains("hw") || !D["hw"].is_object() || !D["hw"].contains("sk")) D["hw"]["sk"] = (int)(c.id * 2654435761u >> 16) % 16;   // a look among the faction's (render/zone_creature_draw.cpp zoneNpcSkin)
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
  if (!T) {
    c.aiming = false; if (c.state == "aim") c.state = "idle";
    if (OptMove m = life(lv, c, nearP, distP, dt)) return m;
    // (a hostile one with nothing to do stands his ground: the engine's stock hostile brain would charge in with fists and
    // drop him once nobody is within 72 blocks, which emptied A-Life's bandit squads as soon as they came online)
    if (c.T->hostile) return Move{0, c.yaw};
    return std::nullopt;
  }
  c.sitting = false; c.sleeping = false; D["guitar"] = false; c.state = "aim";
  const vec3 to = T->pos - c.pos;
  const float dist = glm::length(vec3(to.x, 0, to.z));
  const float yaw = std::atan2(-to.x, -to.z);
  c.yaw = yaw; c.aiming = true;
  c.headPitch = std::atan2((T->pos.y + T->h * 0.6f) - (c.pos.y + c.h * 0.85f), std::max(0.5f, dist));   // (the gun follows the eyes)
  const int tier = D.value("tier", 1);
  const Arms A = armsOf(npcFaction(c), tier);
  // fire
  float cd = D.value("fireT", 0.f) - dt; D["fireT"] = cd;
  if (cd <= 0 && dist < A.range && sees(lv, c, *T)) {
    const bool burst = frand() < 0.3f;
    D["fireT"] = (burst ? 0.12f : 1.f / A.rate) * (0.7f + frand() * 0.6f);
    const vec3 muzzle = c.pos + vec3(0, c.h * 0.8f, 0);
    lv.fx->sound("firework_blast", muzzle, 80, 0.7f, 1.1f + frand() * 0.2f);
    c.swingT = 0.12f;   // the kick and the muzzle flash (render/zone_creature_draw.cpp)
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
  registerZoneMutants();
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
