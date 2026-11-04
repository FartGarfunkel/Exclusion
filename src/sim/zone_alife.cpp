// sim/zone_alife.cpp — A-Life: the Zone lives when nobody is looking. Every stalker squad and mutant pack on the
// map is simulated all the time (on the host): squads pick a smart terrain to go to (a camp, a base, a ruin, a
// lair, an anomaly field to hunt artifacts in), walk there across the map at walking pace, hold it for a while,
// then move on. Hostile squads that meet fight it out on paper; the winners loot, the losers' factions send new
// squads from their bases; packs breed back at their lairs. A squad becomes real creatures when a
// player is inside the online radius and that ground is loaded, and goes back to paper (keeping who's
// still alive and where) once every one of them is past the offline radius. The two radii are OpenXRay's
// switch: online = switch_distance * (1 - switch_factor), offline = switch_distance * (1 + switch_factor).
// Anomaly's alife section uses switch_distance 750 and switch_factor 0.1 (675 m on, 825 m off). This
// world streams, so a body also goes back to paper when the ground under it unloads. Emissions send
// squads under cover; those caught outside lose people. Squads save with the world.
#include "sim/zone.h"
#include "sim/creature.h"
#include "sim/magic_combat.h"
#include "sim/combat.h"
#include "core/registry.h"
#include "gen/zone_plan.h"
#include <cmath>
#include <algorithm>
#include <map>

namespace dq::zone {

namespace {
// Anomaly alife.ltx, as shipped for the modded exes: switch_distance 750, switch_factor 0.1.
constexpr float ALIFE_SWITCH_DISTANCE = 750.f, ALIFE_SWITCH_FACTOR = 0.1f;
constexpr float ONLINE_R = ALIFE_SWITCH_DISTANCE * (1.f - ALIFE_SWITCH_FACTOR);
constexpr float OFFLINE_R = ALIFE_SWITCH_DISTANCE * (1.f + ALIFE_SWITCH_FACTOR);
constexpr float WALK = 1.4f;
constexpr double STEP = 2.0;   // offline update period (s)

struct Squad {
  int id = 0; int faction = F_DRIFTER; int members = 3; int tier = 1;
  std::string species;           // mutants: the creature key; stalkers: ""
  float x = 0, z = 0; int goal = -1, home = -1;
  float hold = 0;                // seconds left holding the goal
  bool online = false;
  bool gar = false;              // (alpha.9) a garrison: lives at its home for good (the plan's Smart::garrison), never wanders off
  bool stay = false;             // a road den: holds its ground until the player has met it (hold counts down only while online)
  std::vector<u32> ents;
  json save() const { json j{{"id", id}, {"f", faction}, {"m", members}, {"t", tier}, {"sp", species}, {"x", x}, {"z", z}, {"g", goal}, {"h", home}, {"hold", hold}}; if (gar) j["gar"] = true; if (stay) j["stay"] = true; return j; }
  static Squad load(const json& j) { Squad s; s.id = j.value("id", 0); s.faction = j.value("f", 0); s.members = j.value("m", 3); s.tier = j.value("t", 1); s.species = j.value("sp", ""); s.x = j.value("x", 0.f); s.z = j.value("z", 0.f); s.goal = j.value("g", -1); s.home = j.value("h", -1); s.hold = j.value("hold", 0.f); s.gar = j.value("gar", false); s.stay = j.value("stay", false); return s; }
};

const char* npcKey(int f) {
  switch (f) {
    case F_DRIFTER: return "z_npc_drifter"; case F_BANDIT: return "z_npc_bandit"; case F_ARMY: return "z_npc_army"; case F_BASTION: return "z_npc_bastion";
    case F_OPENSKY: return "z_npc_opensky"; case F_MERC: return "z_npc_merc"; case F_CHOIR: return "z_npc_choir"; case F_INSTITUTE: return "z_npc_scientist";
    default: return "z_scab_dog";
  }
}
// what lairs where: the common run by danger, and the place's own (sim/zone_mutants.cpp has their brains) — the brute in the Red
// Forest and the labs, the hurler in the closed town, the sanatorium and the bunkers, the two-headed cat in the far north
std::string mutantFor(int danger, double r, int x = 0, int z = 0, double r2 = 1) {
  static const char* T[5][4] = {{"z_scab_dog", "z_scab_dog", "z_rad_rat", "z_tusker"}, {"z_scab_dog", "z_tusker", "z_hogspawn", "z_rad_rat"},
                                {"z_hogspawn", "z_lurcher", "z_crawler", "z_husk"}, {"z_lurcher", "z_shroud", "z_crawler", "z_husk"}, {"z_shroud", "z_puppeteer", "z_crawler", "z_lurcher"}};
  const Region* R = regionAt(x, z);
  const Kind k = R ? R->kind : K_WILD;
  if (k == K_RED_FOREST && r2 < 0.3) return "z_stoneback";
  if ((k == K_INSTITUTE || k == K_LAKE || k == K_JUPITER || k == K_NPP) && r2 < 0.2) return r2 < 0.08 ? "z_stoneback" : "z_hurler";
  if ((k == K_CLOSED_TOWN || k == K_HOSPITAL || k == K_GENERATORS) && r2 < 0.28) return "z_hurler";
  if (z < -6500 && danger >= 3 && r2 < 0.42) return "z_ridgecat";
  if (k == K_SCRAPYARD && r2 < 0.5) return "z_scab_dog";
  if (k == K_MARSH && r2 < 0.35) return r2 < 0.15 ? "z_hogspawn" : "z_rad_rat";
  if (k == K_GRAVEYARD && r2 < 0.3) return "z_husk";
  return T[std::clamp(danger, 1, 5) - 1][std::min(3, (int)(r * 4))];
}
// how many go about together: packs of dogs and rats, a few hogs or husks, the big ones alone
int packOf(const std::string& sp, double r) {
  if (sp == "z_scab_dog") return 3 + (int)(r * 4);
  if (sp == "z_rad_rat") return 4 + (int)(r * 5);
  if (sp == "z_lurcher" || sp == "z_tusker" || sp == "z_crawler") return 2 + (int)(r * 2);
  if (sp == "z_husk") return 2 + (int)(r * 4);
  if (sp == "z_hogspawn" || sp == "z_ridgecat") return 1 + (int)(r * 2);
  return 1;   // shroud, puppeteer, stoneback, hurler
}
bool hasMob(const std::string& k) { return reg().mobs.is_object() && reg().mobs.contains(k); }

struct Alife : LevelSystem {
  std::vector<Squad> squads;
  std::map<int, int> occupancy;   // smart -> squad id holding it
  int nextId = 1; double acc = 0, respawnT = 0, switchT = 0; bool loaded = false;
  Mulberry32 rnd{12345};
  i64 lastEmission = -9;

  const char* name() const override { return "alife"; }

  const Plan& plan(Level& lv) { return zonePlan((u32)lv.info.seed); }

  void load(Level& lv) {
    loaded = true; rnd = Mulberry32(hash32(lv.info.seed, 7, 7, 7));
    // creatures that were online when the world last saved go back to paper (their squad remembers them)
    for (auto& e : lv.entities()) if (!e->removed && e->data.is_object() && e->data.contains("alife")) e->removed = true;
    const json& j = lv.info.data.value("alife", json());
    if (j.is_object() && j.contains("squads")) {
      for (auto& s : j["squads"]) squads.push_back(Squad::load(s));
      nextId = j.value("next", 1);
    } else populate(lv);
    garrisons(lv, true);
    roadPacks(lv);
  }
  // Dens beside the roads a rookie actually walks. Lairs sit in the woods, often half a kilometre from the
  // highway. Without these, the Cordon road and the Garbage track are empty. The camp fire stays clear
  // (nothing placed within ~175 m). Where they stand is fixed; only the online radius decides when they wake.
  void roadPacks(Level& lv) {
    const Plan& P = plan(lv);
    const u32 seed = (u32)lv.info.seed;
    const Region* garbage = regionById("scrapyard");
    if (!garbage) return;
    const float gx = (float)garbage->cx, gz = (float)garbage->cz;
    // samples of the traced roads, then the one chain that leaves the Rookie Village toward the Garbage
    struct Node { float x, z, nx, nz; };
    std::vector<Node> nodes;
    for (const Line& l : roads()) {
      if (l.kind == 3 || l.pts.size() < 2) continue;
      float along = 0, next = 20;
      for (size_t i = 1; i < l.pts.size(); i++) {
        const float dx = l.pts[i].x - l.pts[i - 1].x, dz = l.pts[i].z - l.pts[i - 1].z, seg = std::hypot(dx, dz);
        if (seg < 1.f) { along += seg; continue; }
        while (along + seg >= next) {
          const float t = (next - along) / seg;
          nodes.push_back({l.pts[i - 1].x + dx * t, l.pts[i - 1].z + dz * t, -dz / seg, dx / seg});
          next += 32.f;
        }
        along += seg;
      }
    }
    std::vector<char> used(nodes.size(), 0);
    float x = (float)SPAWN_X, z = (float)SPAWN_Z, goalD = std::hypot(gx - x, gz - z);
    std::vector<Node> path;
    auto pick = [&](float minD, float maxD, bool closer) -> int {
      int best = -1; float bestG = 1e9f;
      for (int i = 0; i < (int)nodes.size(); i++) if (!used[i]) {
        const float d = std::hypot(nodes[i].x - x, nodes[i].z - z);
        if (d < minD || d > maxD) continue;
        const float g = std::hypot(nodes[i].x - gx, nodes[i].z - gz);
        if (closer && g > goalD - 6.f) continue;
        if (g < bestG) { bestG = g; best = i; }
      }
      return best;
    };
    for (int step = 0; step < 90 && goalD > 100.f; step++) {
      int i = pick(30.f, 70.f, true);
      if (i < 0) i = pick(20.f, 140.f, true);
      if (i < 0) i = pick(20.f, 140.f, false);
      if (i < 0) break;
      x = nodes[i].x; z = nodes[i].z; goalD = std::hypot(gx - x, gz - z);
      path.push_back(nodes[i]);
      for (int j = 0; j < (int)nodes.size(); j++) if (std::hypot(nodes[j].x - x, nodes[j].z - z) < 26.f) used[j] = 1;
    }
    auto crowded = [&](float px, float pz, float rad) {
      for (const Squad& s : squads) if (s.faction == F_MUTANT && s.members > 0 && std::hypot(s.x - px, s.z - pz) < rad) return true;
      for (const Smart& m : P.smarts) if (m.garrison && std::hypot(m.x - px, m.z - pz) < 64.f) return true;
      for (const AnomalyField& f : P.fields) if (std::hypot(f.x - px, f.z - pz) < f.r + 16.f) return true;
      return false;
    };
    auto nest = [&](const Node& c, const std::string& sp) -> bool {
      const float sgn = (hash32((i32)seed, (int)c.x, (int)c.z, 3) & 1) ? 1.f : -1.f;
      for (float side : {42.f * sgn, -42.f * sgn, 24.f * sgn}) {
        const float px = c.x + c.nx * side, pz = c.z + c.nz * side;
        if (!insideZone((int)px, (int)pz) || groundAt(seed, (int)px, (int)pz).water) continue;
        if (std::hypot(px - SPAWN_X, pz - SPAWN_Z) < 175.f) continue;
        const Region* r = regionAt((int)px, (int)pz);
        if (!r || (r->kind != K_VILLAGE && r->kind != K_SCRAPYARD)) continue;
        if (crowded(px, pz, 280)) continue;
        const int n = sp == "z_tusker" ? 2 : sp == "z_rad_rat" ? 4 : 3;
        Squad& s = add(F_MUTANT, n, std::max(1, (int)r->danger), px, pz, -1, sp);
        s.stay = true; s.hold = 1e9f; s.goal = -1;
        return true;
      }
      return false;
    };
    int dog = 0, rat = 0, tusk = 0, placed = 0;
    float walked = 0, next = 230.f;
    float px = (float)SPAWN_X, pz = (float)SPAWN_Z;
    for (const Node& n : path) {
      walked += std::hypot(n.x - px, n.z - pz); px = n.x; pz = n.z;
      if (walked < next) continue;
      const Region* r = regionAt((int)n.x, (int)n.z);
      const Kind k = r ? r->kind : K_WILD;
      std::string sp = (hash32((i32)seed, (int)n.x, (int)n.z, 1) & 1) ? "z_scab_dog" : "z_rad_rat";
      if (k == K_VILLAGE && !dog) sp = "z_scab_dog";
      else if (k == K_VILLAGE && !rat) sp = "z_rad_rat";
      else if (k == K_SCRAPYARD && !tusk) sp = "z_tusker";
      else if (k == K_SCRAPYARD) sp = (hash32((i32)seed, (int)n.x, (int)n.z, 2) % 4 == 0) ? "z_tusker" : sp;
      else if (k != K_VILLAGE) { next = walked + 380.f; continue; }
      if (nest(n, sp)) {
        placed++;
        if (sp == "z_scab_dog") dog++;
        if (sp == "z_rad_rat") rat++;
        if (sp == "z_tusker") tusk++;
        next = walked + 380.f;
      } else next = walked + 80.f;   // this verge was water or a camp: try a little further on
    }
    DQ_LOG("A-Life: %d road dens along %zu steps toward the Garbage (dogs %d, rats %d, tuskers %d)", placed, path.size(), dog, rat, tusk);
  }
  // (alpha.9) every garrisoned place has its people: a squad that lives there (an older save gets them now); one wiped out is
  // replaced, one that lost people gets a newcomer now and then, while nobody is looking
  void garrisons(Level& lv, bool full) {
    const Plan& P = plan(lv);
    for (int i = 0; i < (int)P.smarts.size(); i++) {
      const Smart& m = P.smarts[i];
      if (!m.garrison) continue;
      Squad* g = nullptr;
      for (auto& s : squads) if (s.gar && s.home == i) { g = &s; break; }
      if (!g) { Squad& s = add(m.faction, m.garrison, std::max<int>(1, m.danger), (float)m.x, (float)m.z, i); s.gar = true; s.hold = 1e9f; continue; }
      if (!g->online && g->members < m.garrison && (full || rnd() < 0.5)) g->members = full ? m.garrison : g->members + 1;
      if (!g->online) { g->x = (float)m.x; g->z = (float)m.z; }
      g->goal = i; g->hold = 1e9f;
    }
  }
  void save(Level& lv) override {
    json a = json::array();
    for (auto& s : squads) {
      Squad c = s;
      if (s.online) { int alive = 0; float sx = 0, sz = 0; for (u32 id : s.ents) if (Entity* e = lv.entity(id); e && !e->removed && e->hp > 0) { alive++; sx += e->pos.x; sz += e->pos.z; } if (alive) { c.members = alive; c.x = sx / alive; c.z = sz / alive; } }
      a.push_back(c.save());
    }
    lv.info.data["alife"] = json{{"squads", a}, {"next", nextId}};
  }

  Squad& add(int faction, int members, int tier, float x, float z, int home, const std::string& sp = "") {
    Squad s; s.id = nextId++; s.faction = faction; s.members = std::max(1, members); s.tier = tier; s.x = x; s.z = z; s.home = home; s.goal = home; s.hold = 60 + (float)rnd() * 300; s.species = sp;
    squads.push_back(s); return squads.back();
  }

  void populate(Level& lv) {
    const Plan& P = plan(lv);
    for (int i = 0; i < (int)P.smarts.size(); i++) {
      const Smart& m = P.smarts[i];
      if (m.kind == SM_LAIR) { const std::string sp = mutantFor(m.danger, rnd(), m.x, m.z, rnd()); add(F_MUTANT, packOf(sp, rnd()), m.danger, (float)m.x, (float)m.z, i, sp); continue; }
      if (m.faction == F_NONE || m.faction == F_MUTANT) {
        if (m.kind == SM_CAMP) add(rnd() < 0.72 ? F_DRIFTER : F_BANDIT, 2 + (int)(rnd() * 4), m.danger, (float)m.x, (float)m.z, i);
        else if (m.kind == SM_FIELD && rnd() < 0.18) add(rnd() < 0.85 ? F_DRIFTER : F_INSTITUTE, 2 + (int)(rnd() * 2), m.danger, (float)m.x + 20, (float)m.z, i);   // artifact hunters already out
        continue;
      }
      const int n = m.garrison ? 1 : m.kind == SM_BASE || m.kind == SM_HUB ? 3 : 1;   // (a garrisoned place has its own people: one visiting squad)
      for (int k = 0; k < n; k++) add(m.faction, std::max(2, m.slots / 2 - k), m.danger, (float)m.x + k * 6, (float)m.z, i);
    }
    // and people on the roads between: drifters going to and fro, a few gangs looking for them
    for (const Line& l : roads()) {
      if (l.kind == 3) continue;
      float along = 0;   // a chance every 400 m of road (the user's roads are traced finely)
      for (size_t i = 0; i < l.pts.size(); i++) {
        if (i) along += std::hypot(l.pts[i].x - l.pts[i - 1].x, l.pts[i].z - l.pts[i - 1].z);
        if (along < 400) continue;
        along = 0;
        if (rnd() > 0.55) continue;
        const int f = rnd() < 0.7 ? F_DRIFTER : rnd() < 0.6 ? F_BANDIT : F_ARMY;
        const Region* r = regionAt((int)l.pts[i].x, (int)l.pts[i].z);
        Squad& s = add(f, 2 + (int)(rnd() * 3), r ? r->danger : 2, l.pts[i].x, l.pts[i].z, -1);
        s.goal = -1; s.hold = 0;
      }
    }
    DQ_LOG("A-Life: %d squads on %d smart terrains", (int)squads.size(), (int)P.smarts.size());
  }

  // where a squad wants to go next
  int pickGoal(Level& lv, const Squad& s) {
    const Plan& P = plan(lv);
    int best = -1; double bestScore = -1;
    for (int k = 0; k < 24; k++) {
      const int i = (int)(rnd() * P.smarts.size());
      const Smart& m = P.smarts[i];
      const float d = std::hypot(m.x - s.x, m.z - s.z);
      if (d < 30 || d > (s.faction == F_MUTANT ? 1400 : 4500)) continue;
      double w = 1;
      if (s.faction == F_MUTANT) w = m.kind == SM_LAIR ? 4 : m.kind == SM_RUIN || m.kind == SM_FIELD ? 1.5 : 0.3;
      else {
        if (m.faction != F_NONE && factionRelation(s.faction, m.faction) < 0) w = s.faction == F_BANDIT || s.faction == F_CHOIR ? 0.4 : 0.15;   // raids now and then
        else if (m.faction == s.faction) w = 3;
        else if (m.kind == SM_FIELD) w = s.faction == F_DRIFTER || s.faction == F_INSTITUTE ? 3 : 0.6;   // artifact hunting
        else if (m.kind == SM_HUB) w = factionRelation(s.faction, F_DRIFTER) >= 0 ? 2.5 : 0;
        else if (m.kind == SM_CAMP) w = 2;
        else if (m.kind == SM_LAIR) w = 0.2;
        if (s.faction == F_CHOIR && m.danger < 4) w *= 0.05;    // the Choir keep to the centre
        if (s.faction == F_ARMY && m.danger > 3) w *= 0.2;
      }
      if (occupancy.count(i) && occupancy[i] != s.id) w *= 0.3;
      const double score = w / (1 + d / 900) * (0.5 + rnd());
      if (score > bestScore) { bestScore = score; best = i; }
    }
    return best >= 0 ? best : s.home;
  }

  void tick(Level& lv, double dt) override {
    if (!zoneWorld(lv) || lv.isRemote()) return;
    if (!loaded) load(lv);
    switchT -= dt;
    if (switchT <= 0) { switchT = 0.5; switching(lv); }
    acc += dt;
    if (acc < STEP) return;
    const float step = (float)acc; acc = 0;
    const Plan& P = plan(lv);
    const Emission em = emission(lv);
    // ---- move, hold, fight
    occupancy.clear();
    for (auto& s : squads) if (s.goal >= 0 && s.hold > 0) occupancy[s.goal] = s.id;
    for (auto& s : squads) {
      if (s.stay) {   // a road den keeps its ground; the clock runs only once someone is there to see it
        if (s.online) s.hold -= step;
        if (s.hold > 0) { s.goal = -1; continue; }
        s.stay = false;
      }
      if (s.gar) { s.goal = s.home; continue; }   // a garrison stays home (garrisons() brings it back to strength)
      if (s.online && s.members > 0 && s.goal >= 0 && s.goal < (int)P.smarts.size()) {
        const Smart& g = P.smarts[s.goal];
        if (std::hypot(g.x - s.x, g.z - s.z) < 18 && (s.hold -= step) <= 0) { s.goal = pickGoal(lv, s); s.hold = 240 + (float)rnd() * 600; }
        continue;
      }
      if (s.online || s.members <= 0) continue;
      if (em.phase == 1 && s.faction != F_MUTANT && s.goal >= 0 && P.smarts[s.goal].kind != SM_BASE && P.smarts[s.goal].kind != SM_HUB) {
        // run for cover: the nearest base or hub
        int nb = -1; float nd = 1e9f;
        for (int i = 0; i < (int)P.smarts.size(); i++) { const Smart& m = P.smarts[i]; if (m.kind != SM_BASE && m.kind != SM_HUB && m.kind != SM_RUIN) continue; const float d = std::hypot(m.x - s.x, m.z - s.z); if (d < nd) { nd = d; nb = i; } }
        if (nb >= 0) { s.goal = nb; s.hold = 0; }
      }
      if (s.goal < 0 || s.goal >= (int)P.smarts.size()) s.goal = pickGoal(lv, s);
      const Smart& g = P.smarts[s.goal];
      const float dx = g.x - s.x, dz = g.z - s.z, d = std::hypot(dx, dz);
      if (d > 8) { const float v = std::min(d, WALK * step * (s.faction == F_MUTANT ? 1.3f : 1.f)); s.x += dx / d * v; s.z += dz / d * v; }
      else if ((s.hold -= step) <= 0) { s.goal = pickGoal(lv, s); s.hold = 120 + (float)rnd() * (s.faction == F_MUTANT ? 300 : 600); }
    }
    fights(lv, step);
    company(lv, step);
    // ---- the emission's toll on those outside
    const i64 cyc = (i64)std::floor((lv.info.day * DAY_LEN + lv.info.time) / 2700);
    if (em.phase == 2 && em.strength > 0.8f && lastEmission != cyc) {
      lastEmission = cyc;
      int lost = 0;
      for (auto& s : squads) {
        if (s.online || s.goal < 0) continue;
        const Smart& g = P.smarts[s.goal];
        const bool covered = s.gar || (std::hypot(g.x - s.x, g.z - s.z) < 20 && (g.kind == SM_BASE || g.kind == SM_HUB || g.kind == SM_RUIN || g.kind == SM_LAIR));
        if (!covered && rnd() < 0.7) { s.members -= 1 + (int)(rnd() * 2); lost++; }
      }
      pdaNewsAdd(lv, "The emission is over. " + std::to_string(lost) + " groups were caught in the open.");
      // packs breed back at their lairs, a fresh crop of artifacts waits
      for (int i = 0; i < (int)P.smarts.size(); i++) if (P.smarts[i].kind == SM_LAIR && rnd() < 0.35) { const Smart& m = P.smarts[i]; const std::string sp = mutantFor(m.danger, rnd(), m.x, m.z, rnd()); add(F_MUTANT, packOf(sp, rnd()), m.danger, (float)m.x, (float)m.z, i, sp); }
    }
    squads.erase(std::remove_if(squads.begin(), squads.end(), [&](const Squad& s) { return !s.online && s.members <= 0; }), squads.end());
    // ---- factions send fresh squads from their bases
    respawnT -= step;
    if (respawnT <= 0) {
      respawnT = 240;
      garrisons(lv, false);
      int count[F_COUNT] = {};
      for (auto& s : squads) if (s.faction < F_COUNT) count[s.faction]++;
      static const int WANT[F_COUNT] = {110, 45, 22, 30, 30, 20, 45, 8, 320};
      for (int i = 0; i < (int)P.smarts.size(); i++) {
        const Smart& m = P.smarts[i];
        if ((m.kind == SM_BASE || m.kind == SM_HUB) && m.faction < F_MUTANT && count[m.faction] < WANT[m.faction] && rnd() < 0.5) { add(m.faction, 2 + (int)(rnd() * 3), m.danger, (float)m.x, (float)m.z, i); count[m.faction]++; }
      }
    }
  }

  // the Zone is never empty round a player who has left the camp: too few packs on paper within sight sends one
  // in just ahead of him, on ground the view distance has loaded (a pack dropped 200 m out never arrives — he
  // walks faster than it does, and the chunk it stands on is not loaded, so it never becomes creatures).
  // Dogs and rats in Cordon; a tusker once the road has reached the Garbage. The Rookie Village itself is left alone.
  double companyT = 8;
  void company(Level& lv, float step) {
    if ((companyT -= step) > 0) return;
    companyT = 45;
    const Plan& P = plan(lv);
    for (auto& e : lv.entities()) {
      auto* p = dynamic_cast<Player*>(e.get());
      if (!p || p->removed || p->dead || p->realm != REALM_MAIN) continue;
      if (std::hypot(p->pos.x - SPAWN_X, p->pos.z - SPAWN_Z) < 150.f) continue;
      int packs = 0, bands = 0;
      for (auto& s : squads) if (s.members > 0) {
        const float dd = std::hypot(s.x - p->pos.x, s.z - p->pos.z);
        if (s.faction == F_MUTANT) { if (dd < 200.f) packs++; }
        else if (dd < 320.f) bands++;
      }
      if (packs >= 1 && bands >= 1) continue;
      const bool mutant = packs < 1;
      float fx = -std::sin(p->yaw), fz = -std::cos(p->yaw);
      const float spd = std::hypot(p->vel.x, p->vel.z);
      if (spd > 0.6f) { fx = p->vel.x / spd; fz = p->vel.z / spd; }
      const float lx = -fz, lz = fx;
      for (int tries = 0; tries < 8; tries++) {
        const float d = 64.f + (float)rnd() * 22.f, side = ((float)rnd() - 0.5f) * 32.f;
        const int x = (int)(p->pos.x + fx * d + lx * side), z = (int)(p->pos.z + fz * d + lz * side);
        if (std::hypot(x - SPAWN_X, z - SPAWN_Z) < 165.f) continue;
        const Ground g = groundAt((u32)lv.info.seed, x, z);
        if (g.water || !insideZone(x, z)) continue;
        bool busy = false;
        for (const Smart& m : P.smarts) if (m.garrison && std::hypot(m.x - x, m.z - z) < 48) busy = true;
        for (auto& s : squads) if (s.members > 0 && std::hypot(s.x - x, s.z - z) < 40) busy = true;
        if (busy) continue;
        const Region* R = regionAt(x, z);
        const int danger = R ? R->danger : 2;
        const Kind k = R ? R->kind : K_WILD;
        int goal = -1; float gd = 1e9f;
        for (int i = 0; i < (int)P.smarts.size(); i++) {
          const Smart& m = P.smarts[i];
          if (m.kind == SM_BASE || m.kind == SM_HUB || m.garrison) continue;
          const float dd = std::hypot(m.x - x, m.z - z);
          if (dd < gd && dd > 24 && dd < 450) { gd = dd; goal = i; }
        }
        Squad* s;
        if (mutant) {
          std::string sp = rnd() < 0.55 ? "z_scab_dog" : "z_rad_rat";
          if (k == K_SCRAPYARD || danger >= 2) sp = rnd() < 0.45 ? "z_tusker" : sp;
          const int n = sp == "z_tusker" ? 2 : sp == "z_rad_rat" ? 4 : 3;
          s = &add(F_MUTANT, n, danger, (float)x, (float)z, -1, sp);
          s->stay = true; s->hold = 180;   // mill here a few minutes, then wander like any other pack
        } else {
          s = &add(rnd() < 0.75 ? F_DRIFTER : F_BANDIT, 2 + (int)(rnd() * 2), danger, (float)x, (float)z, -1);
          s->hold = 120;
        }
        s->goal = goal;
        DQ_LOG("A-Life: %s x%d sent in %.0f blocks off (packs %d, bands %d near)", s->species.empty() ? factionName(s->faction) : s->species.c_str(), s->members, d, packs, bands);
        break;
      }
    }
  }

  float newsT = 20;
  void fights(Level& lv, float step) {
    const Plan& P = plan(lv);
    newsT -= step;
    for (size_t a = 0; a < squads.size(); a++) for (size_t b = a + 1; b < squads.size(); b++) {
      Squad& A = squads[a]; Squad& B = squads[b];
      if (A.online || B.online || A.members <= 0 || B.members <= 0) continue;
      if (A.stay || B.stay) continue;   // a den waits on the ground; paper fights were wiping it before anyone arrived
      if (std::abs(A.x - B.x) > 45 || std::abs(A.z - B.z) > 45) continue;
      if (factionRelation(A.faction, B.faction) >= 0) continue;
      if (A.faction == F_MUTANT && B.faction == F_MUTANT) continue;
      // a round of the firefight: each side's strength takes members off the other
      const float sa = A.members * (0.8f + A.tier * 0.3f), sb = B.members * (0.8f + B.tier * 0.3f);
      if (rnd() < sb / (sa + sb) * 0.5 * step / STEP) A.members--;
      if (rnd() < sa / (sa + sb) * 0.5 * step / STEP) B.members--;
      if (A.members <= 0 || B.members <= 0) {
        Squad& W = A.members > 0 ? A : B; Squad& Lo = A.members > 0 ? B : A;
        if (W.faction != F_MUTANT) W.tier = std::min(5, W.tier + (rnd() < 0.3 ? 1 : 0));
        const std::string where = regionName((int)Lo.x, (int)Lo.z);
        auto who = [](const Squad& s) { return s.faction == F_MUTANT ? std::string("mutants") : std::string(factionName(s.faction)); };
        if (newsT <= 0) { newsT = 50 + (float)rnd() * 40; pdaNewsAdd(lv, who(Lo) + " wiped out by " + who(W) + " near " + where + "."); }   // (a line now and then, not every skirmish on the map at once)
        (void)P;
      }
    }
  }

  // ---- online / offline
  void switching(Level& lv) {
    std::vector<vec3> ps;
    for (auto& e : lv.entities()) if (auto* p = dynamic_cast<Player*>(e.get()); p && !p->removed && !p->dead && p->realm == REALM_MAIN) ps.push_back(p->pos);
    World& w = lv.world(REALM_MAIN);
    for (auto& s : squads) {
      float nd = 1e9f; for (const vec3& p : ps) nd = std::min(nd, std::hypot(p.x - s.x, p.z - s.z));
      if (!s.online && nd < ONLINE_R && s.members > 0) {
        // only where the ground is loaded
        const int gx = (int)s.x, gz = (int)s.z;
        const int top = w.surfaceY(gx, gz, DIM_OVERWORLD);
        if (top <= 0) continue;
        const std::string key = s.species.empty() ? npcKey(s.faction) : s.species;
        if (!hasMob(key)) continue;
        s.online = true; s.ents.clear();
        DQ_LOG("A-Life: %s x%d online %.0f blocks off", key.c_str(), s.members, nd);
        for (int k = 0; k < s.members; k++) {
          const float a = k * 2.3f, r = 1.5f + k * 0.8f;
          const int x = (int)std::floor(s.x + std::cos(a) * r), z = (int)std::floor(s.z + std::sin(a) * r);
          int y = w.surfaceY(x, z, DIM_OVERWORLD);
          if (y <= 0) continue;
          // stand on the ground, not on a roof or a tree (if the ground there is clear)
          const int gy = dimBase(DIM_OVERWORLD) + (int)std::round(groundAt((u32)lv.info.seed, x, z).h);
          if (gy < y && w.getBlock(x, gy + 1, z) == AIR && w.getBlock(x, gy + 2, z) == AIR) y = gy;
          if (Entity* e = spawnCreature(lv, key, REALM_MAIN, vec3(x + 0.5f, y + 1.05f, z + 0.5f))) {
            e->data["alife"] = s.id; e->data["faction"] = s.faction;
            // A-Life decides when they go back to paper (past the offline radius, or the chunk unloads).
            // The engine's own despawn takes a hostile nobody is within 96 blocks of, and the squad would count it dead.
            e->persistent = true; if (auto* cr = dynamic_cast<Creature*>(e)) cr->kept = true;
            if (s.tier > 1) { e->maxHp *= 1 + (s.tier - 1) * 0.15f; e->hp = e->maxHp; }
            s.ents.push_back(e->id);
          }
        }
        if (s.ents.empty()) { s.online = false; continue; }   // nothing stood up: stay on paper and try again, don't count the pack dead
      } else if (s.online) {
        // CSE_ALifeOnlineOfflineGroup::try_switch_offline: stay up while any member is still inside the offline radius.
        int alive = 0; float sx = 0, sz = 0, near = 1e9f; bool ground = w.surfaceY((int)s.x, (int)s.z, DIM_OVERWORLD) > 0;
        for (u32 id : s.ents) if (Entity* e = lv.entity(id); e && !e->removed && e->hp > 0) {
          alive++; sx += e->pos.x; sz += e->pos.z;
          for (const vec3& p : ps) near = std::min(near, std::hypot(p.x - e->pos.x, p.z - e->pos.z));
          if (w.surfaceY((int)e->pos.x, (int)e->pos.z, DIM_OVERWORLD) > 0) ground = true;
        }
        if (alive) { s.x = sx / alive; s.z = sz / alive; }
        s.members = alive;
        if (alive == 0) { s.online = false; s.ents.clear(); continue; }
        if (near > OFFLINE_R || !ground) {
          for (u32 id : s.ents) if (Entity* e = lv.entity(id)) e->removed = true;
          s.ents.clear(); s.online = false;
        }
      }
    }
  }
};
Alife* alifeOf(Level& lv) { return lv.system<Alife>(); }
} // namespace

SquadGoal alifeGoal(Level& lv, int squadId) {
  SquadGoal g;
  Alife* a = alifeOf(lv); if (!a) return g;
  const Plan& P = zonePlan((u32)lv.info.seed);
  for (auto& s : a->squads) if (s.id == squadId) {
    if (s.goal < 0 || s.goal >= (int)P.smarts.size()) return g;
    const Smart& m = P.smarts[s.goal];
    g.ok = true; g.pos = vec3(m.x + 0.5f, dimBase(DIM_OVERWORLD) + m.y, m.z + 0.5f); g.kind = m.kind; g.faction = s.faction; g.size = s.members;
    g.radius = m.kind == SM_FIELD ? 16.f : m.kind == SM_BASE ? 14.f : 10.f;
    g.arrived = std::hypot(m.x - s.x, m.z - s.z) < g.radius + 6;
    return g;
  }
  return g;
}

std::vector<SquadView> alifeSquads(Level& lv) {
  std::vector<SquadView> out;
  if (Alife* a = alifeOf(lv)) for (auto& s : a->squads) {
    const Plan& P = zonePlan((u32)lv.info.seed);
    out.push_back({vec3(s.x, 0, s.z), s.faction, s.members, s.goal >= 0 && s.goal < (int)P.smarts.size() ? P.smarts[s.goal].name : "", s.online, s.id, s.species});
  }
  return out;
}

std::vector<std::string> pdaNews(Level& lv) {
  std::vector<std::string> out;
  const json& n = lv.info.data.value("zoneNews", json::array());
  if (n.is_array()) for (auto& l : n) if (l.is_string()) out.push_back(l);
  return out;
}
void pdaNewsAdd(Level& lv, const std::string& line) {
  json& n = lv.info.data["zoneNews"];
  if (!n.is_array()) n = json::array();
  n.push_back(line);
  while (n.size() > 40) n.erase(n.begin());
  if (lv.fx) lv.fx->message(0, "PDA: " + line, "");
}

void registerZoneAlife() {
  registerLevelSystem([](Level& lv) -> std::shared_ptr<LevelSystem> { return zoneWorld(lv) ? std::make_shared<Alife>() : nullptr; });
}

} // namespace dq::zone
