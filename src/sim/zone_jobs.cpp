// sim/zone_jobs.cpp — the Zone's people as people (STALKER: Call of Pripyat): each faction's standing with you
// (stats.zone.rep on top of the factions' own relations), what a dead stalker leaves (his pack, a zone_body_pack block
// opened with the loot screen: his gun, rounds for it, what he carried, now and then his PDA), stashes (a hidden crate at a
// building, placed by the host once its ground is loaded, marked on your map by a found PDA, a rumour or a job), jobs from
// traders and the leaders of camps and bases (data/zone_jobs.json: hunt a pack, clear out enemies, fetch an artifact
// or trophies, find a stash; paid in money, kit and standing; shown in the PDA's tasks), and the rumours a stalker tells
// (drawn from A-Life: packs, anomaly fields, enemies about). The dialogue and the PDA are ui/zone_talk.cpp, ui/zone_pda.cpp.
// Kills, bodies and stashes are the host's; the per-player numbers ride the player's stats.
#include "sim/zone.h"
#include "sim/zone_anomalies.h"
#include "sim/controller.h"
#include "sim/creature.h"
#include "sim/gameplay.h"
#include "gen/zone_plan.h"
#include "core/registry.h"
#include "common/fs.h"
#include "common/noise.h"
#include <cmath>
#include <algorithm>

namespace dq::zone {

namespace {
json& Zs(Player& p) { json& z = p.stats["zone"]; if (!z.is_object()) z = json::object(); return z; }
const json* Zc(const Player& p) { auto it = p.stats.find("zone"); return it == p.stats.end() || !it->is_object() ? nullptr : &*it; }
float flatDist(vec3 a, float x, float z) { return std::hypot(a.x - x, a.z - z); }
u32 strHash(const std::string& s) { u32 h = 2166136261u; for (char c : s) { h ^= (u8)c; h *= 16777619u; } return h; }
std::string fill(std::string s, const std::map<std::string, std::string>& v) {
  for (auto& [k, x] : v) { const std::string t = "{" + k + "}"; for (size_t i; (i = s.find(t)) != std::string::npos;) s.replace(i, t.size(), x); }
  return s;
}
const char* FAMILY[5] = {"thermal", "electric", "gravity", "chemical", "psy"};   // AnoFamily order
const char* familyKey(int f) { return FAMILY[std::clamp(f, 0, 4)]; }
// north is -z (the plant), east +x
std::string compass(float dx, float dz) {
  static const char* D[8] = {"north", "north-east", "east", "south-east", "south", "south-west", "west", "north-west"};
  const float a = std::atan2(dx, -dz);   // 0 north, clockwise
  return D[((int)std::lround(a / (PI / 4)) + 8) % 8];
}
std::string placeNear(Level& lv, float x, float z) {
  const Plan& P = zonePlan((u32)lv.info.seed);
  const Smart* best = nullptr; float bd = 260;
  for (const Smart& s : P.smarts) { if (s.kind == SM_FIELD) continue; const float d = std::hypot(s.x - x, s.z - z); if (d < bd && !s.name.empty()) { bd = d; best = &s; } }   // (fields are all "Anomaly field")
  return best ? best->name : regionName((int)x, (int)z);
}
std::string mutantName(const std::string& species, bool plural) {
  std::string l = reg().mobs.is_object() && reg().mobs.contains(species) ? reg().mobs[species].value("label", species) : species;
  if (l.rfind("a ", 0) == 0) l = l.substr(2); else if (l.rfind("an ", 0) == 0) l = l.substr(3);
  if (plural) { if (!l.empty() && l.back() == 'y') l = l.substr(0, l.size() - 1) + "ies"; else if (l.back() != 's') l += "s"; }
  return l;
}
std::string trophyName(const std::string& id) { const ZItem* z = zi(id); return z ? z->name : id; }
const json& templates() {
  static json T = [] { auto j = readJson(dataDir() / "zone_jobs.json"); return j && j->is_object() ? j->value("jobs", json::array()) : json::array(); }();
  return T;
}
std::string giverId(const Entity& e) {
  if (e.type == "z_npc_trader") return "trader:" + e.data.value("trader", std::string("hamlet"));
  return std::string("f:") + factionKey(npcFactionOf(e)) + ":" + std::to_string(e.data.value("alife", -1));
}
bool hasItems(const Player& p, const std::string& id, const std::string& family, int n) {
  int have = 0;
  for (auto& s : p.inv) {
    if (s.empty()) continue;
    if (!id.empty() && s.id == id) have += s.n;
    if (!family.empty()) if (const ZItem* z = zi(s.id); z && z->cat == "artifact" && z->raw.contains("artifact") && z->raw["artifact"].value("family", "") == family) have += s.n;
  }
  return have >= n;
}
void takeItems(Player& p, const std::string& id, const std::string& family, int n) {
  for (auto& s : p.inv) {
    if (n <= 0) break;
    if (s.empty()) continue;
    bool ok = !id.empty() && s.id == id;
    if (!family.empty()) if (const ZItem* z = zi(s.id); z && z->cat == "artifact" && z->raw.contains("artifact") && z->raw["artifact"].value("family", "") == family) ok = true;
    if (!ok) continue;
    const int k = std::min(n, s.n); s.n -= k; n -= k; if (s.n <= 0) s.clear();
  }
}

// ---- stashes: the level's list (host places them) and each player's known ones ----
int makeStash(Level& lv, int x, int z, int y0, int danger) {
  json& a = lv.info.data["zoneStashes"]; if (!a.is_array()) a = json::array();
  json& n = lv.info.data["zoneStashNext"]; const int id = n.is_number() ? n.get<int>() : 1; n = id + 1;
  a.push_back(json{{"id", id}, {"x", x}, {"z", z}, {"y0", y0}, {"d", danger}, {"placed", false}});
  return id;
}
void knowStash(Player& p, int id, int x, int z, const std::string& name) {
  json& k = Zs(p)["stashes"]; if (!k.is_array()) k = json::array();
  for (auto& s : k) if (s.value("id", 0) == id) return;
  k.push_back(json{{"id", id}, {"x", x}, {"z", z}, {"name", name}, {"found", false}});
}
// a spot for a stash: inside a building 250..1200 m from `near` (else beside a ruin)
bool stashSpot(Level& lv, vec3 near, u32 salt, int& x, int& z, int& y0, int& danger, std::string& name) {
  const Plan& P = zonePlan((u32)lv.info.seed);
  Mulberry32 r(hash32((i32)lv.info.seed, (i32)salt, (i32)near.x, (i32)near.z));
  std::vector<int> c;
  for (int i = 0; i < (int)P.s.size(); i++) {
    const Placed& b = P.s[i]; if (b.W < 5 || b.D < 5 || b.ruin > 0.8f) continue;
    const float d = std::hypot(b.x0 + b.W * 0.5f - near.x, b.z0 + b.D * 0.5f - near.z);
    if (d > 250 && d < 1200) c.push_back(i);
  }
  if (!c.empty()) {
    const Placed& b = P.s[c[(size_t)(r() * c.size()) % c.size()]];
    x = b.x0 + 1 + (int)(r() * (b.W - 2)); z = b.z0 + 1 + (int)(r() * (b.D - 2)); y0 = b.y; danger = b.danger;
    name = placeNear(lv, (float)x, (float)z); return true;
  }
  for (const Smart& s : P.smarts) if (s.kind == SM_RUIN && std::hypot(s.x - near.x, s.z - near.z) < 2000) {
    x = s.x + 3; z = s.z + 2; y0 = s.y + 1; danger = s.danger; name = s.name; return true;
  }
  return false;
}

// ---- what a dead stalker carried ----
std::string ammoFor(const std::string& gun) {
  const ZItem* g = zi(gun); if (!g || !g->isGun) return "";
  std::string a = "ammo_" + g->gun.cal; if (g->gun.cal == "12g") a = "ammo_12g_buck";
  return reg().item(a) ? a : "";
}
void dropBody(Level& lv, Creature& c) {
  static const BlockId PACK = reg().blockId("zone_body_pack");
  if (!PACK || c.realm != REALM_MAIN) return;
  World& w = lv.world(c.realm);
  const int x = ifloor(c.pos.x), z = ifloor(c.pos.z); int y = ifloor(c.pos.y + 0.1f);
  int k = 0; while (k < 3 && !reg().tReplaceable[w.getBlock(x, y, z)]) { y++; k++; }
  if (k == 3 || w.getBlock(x, y, z) == PACK) return;
  json items = json::array(); for (int i = 0; i < 27; i++) items.push_back(nullptr);
  int slot = 0;
  auto put = [&](const ItemStack& s) { if (!s.empty() && slot < 27 && reg().item(s.id)) items[slot++] = s.toJson(); };
  if (const ZItem* g = c.heldItem.empty() ? nullptr : zi(c.heldItem); g && g->isGun) {
    ItemStack gs(c.heldItem, 1);
    if (const ItemDef* d = reg().item(c.heldItem); d && d->hasTool) gs.d = (int)(d->tool.durability * (0.3f + frand() * 0.45f));
    put(gs);
    if (const std::string a = ammoFor(c.heldItem); !a.empty()) put(ItemStack(a, std::max(4, (int)(g->gun.mag * (0.4f + frand() * 1.2f)))));
  }
  for (auto& d : c.T->drops) if (frand() < d.chance) { const int n = d.lo + (int)std::floor(frand() * (d.hi - d.lo + 1)); if (n > 0) put(ItemStack(d.id, n)); }
  if (frand() < 0.15f) put(ItemStack("pda_stalker", 1));
  w.setBlock(x, y, z, PACK);
  w.setBlockEntity(x, y, z, json{{"type", "backpack"}, {"stack", ItemStack("leather_backpack", 1).toJson()}, {"yaw", c.yaw}, {"items", items},
                                 {"zone", "body"}, {"who", factionName(npcFactionOf(c))}});
  c.data["noDrops"] = true;   // (it's all in the pack)
}

void killed(Level& lv, Creature& c, const DamageSrc& src) {
  const bool npc = c.type.rfind("z_npc_", 0) == 0;
  const bool mutant = !npc && c.type.rfind("z_", 0) == 0;
  if (!npc && !mutant) return;
  if (npc && c.type != "z_npc_trader") dropBody(lv, c);
  auto* kp = dynamic_cast<Player*>(lv.entity(src.attacker));
  if (!kp) return;
  Player& p = *kp; json& Z = Zs(p);
  json& K = Z["kills"]; if (!K.is_object()) K = json::object();
  const int vf = npc ? npcFactionOf(c) : (int)F_MUTANT;
  K[mutant ? "mutants" : "stalkers"] = K.value(mutant ? "mutants" : "stalkers", 0) + 1;
  K[factionKey(vf)] = K.value(factionKey(vf), 0) + 1;
  // standing: his people remember (less if he was shooting at you), their enemies approve
  if (npc && vf < F_MUTANT) {
    const int before = attitude(p, vf);
    bool grudge = false; if (c.data.contains("grudges")) for (auto& g : c.data["grudges"]) if (g.is_number() && g.get<u32>() == p.id) grudge = true;
    addRep(p, vf, grudge || before < 0 ? -60 : -300);
    for (int f = 0; f < F_MUTANT; f++) if (f != vf && factionRelation(f, vf) < 0) addRep(p, f, 40);
    if (attitude(p, vf) < before) lv.fx->message(p.id, std::string(factionName(vf)) + (attitude(p, vf) < 0 ? " now see you as an enemy." : " no longer trust you."), "warn");
  }
  // jobs: a pack hunted, enemies put down
  json& J = Z["jobs"]; if (!J.is_array()) return;
  for (auto& j : J) {
    if (j.value("state", "") != "go") continue;
    const std::string t = j.value("type", "");
    const bool hit = (t == "hunt" && mutant && (j.value("sp", "").empty() || j.value("sp", "") == c.type)) || (t == "camp" && npc && vf == j.value("vs", -1));
    if (!hit || flatDist(c.pos, j.value("tx", 0.f), j.value("tz", 0.f)) > j.value("r", 150.f)) continue;
    j["have"] = j.value("have", 0) + 1;
    if (j["have"].get<int>() >= j.value("need", 1)) { j["state"] = "back"; lv.fx->message(p.id, j.value("title", std::string("Job")) + ": done. Report back for your pay.", "rare"); }
  }
}

// the host puts the stashes in the world once their ground is loaded
struct StashSys : LevelSystem {
  const char* name() const override { return "zoneStashes"; }
  double t = 0;
  void tick(Level& lv, double dt) override {
    if (lv.isRemote() || (t += dt) < 1.5) return;
    t = 0;
    json& a = lv.info.data["zoneStashes"]; if (!a.is_array()) return;
    static const BlockId CHEST = reg().blockId("chest");
    World& w = lv.world(REALM_MAIN); const int by = dimBase(DIM_OVERWORLD);
    for (auto& s : a) {
      if (s.value("placed", false)) continue;
      const int x = s.value("x", 0), z = s.value("z", 0), y0 = by + s.value("y0", 64);
      if (!w.isLoaded(x, y0, z)) continue;
      int y = -1;
      for (int k = -2; k < 10 && y < 0; k++) { const int q = y0 + k; if (w.getBlock(x, q, z) == AIR && w.getBlock(x, q + 1, z) == AIR && reg().tSolid[w.getBlock(x, q - 1, z)]) y = q; }
      if (y < 0) { y = w.surfaceY(x, z, DIM_OVERWORLD) + 1; if (y <= 0) continue; }
      Mulberry32 r(hash32((i32)lv.info.seed, s.value("id", 0), x, z));
      w.setBlock(x, y, z, CHEST);
      w.setBlockEntity(x, y, z, json{{"type", "chest"}, {"items", zoneLoot("stash", s.value("d", 2), r)}, {"zone", "stash"}});
      s["placed"] = true; s["y"] = y;
      DQ_LOG("zone: stash %d placed at %d %d %d", s.value("id", 0), x, y, z);
    }
  }
};
} // namespace

// ---------------------------------------------------------------- standing
int standing(const Player& p, int f) {
  if (f == F_MUTANT) return -1000;
  if (f < 0 || f >= F_MUTANT) return 0;
  const int mine = faction(p), r = factionRelation(f, mine);
  int v = f == mine ? 900 : r < 0 ? -800 : r > 0 ? 700 : 0;
  if (const json* Z = Zc(p)) if (auto it = Z->find("rep"); it != Z->end() && it->is_object()) v += it->value(factionKey(f), 0);
  return std::clamp(v, -1000, 1000);
}
int attitude(const Player& p, int f) { const int s = standing(p, f); return s <= REP_HOSTILE ? -1 : s >= REP_FRIENDLY ? 1 : 0; }
void addRep(Player& p, int f, int v) {
  if (f < 0 || f >= F_MUTANT) return;
  json& r = Zs(p)["rep"]; if (!r.is_object()) r = json::object();
  r[factionKey(f)] = std::clamp(r.value(factionKey(f), 0) + v, -2000, 2000);
}
int traderFaction(const std::string& t) { return t == "kalina" ? F_INSTITUTE : F_DRIFTER; }
int npcFactionOf(const Entity& e) {
  if (e.type == "z_npc_trader") return traderFaction(e.data.value("trader", std::string("hamlet")));
  if (e.type.rfind("z_npc_", 0) != 0) return F_NONE;
  if (e.data.is_object() && e.data.contains("faction") && e.data["faction"].is_number()) return e.data["faction"].get<int>();
  static const std::map<std::string, int> M = {{"z_npc_bandit", F_BANDIT}, {"z_npc_army", F_ARMY}, {"z_npc_bastion", F_BASTION}, {"z_npc_opensky", F_OPENSKY},
                                               {"z_npc_merc", F_MERC}, {"z_npc_choir", F_CHOIR}, {"z_npc_scientist", F_INSTITUTE}};
  auto it = M.find(e.type); return it == M.end() ? (int)F_DRIFTER : it->second;
}
std::string npcName(const Entity& e) {
  if (e.type == "z_npc_trader") {
    static const std::map<std::string, const char*> N = {{"hamlet", "Moroz"}, {"molot", "The barman"}, {"yanov", "Yanov's quartermaster"}, {"zaton", "The Volga's skipper"}, {"kalina", "Dr. Herman"}};
    auto it = N.find(e.data.value("trader", std::string("hamlet"))); return it == N.end() ? "Trader" : it->second;
  }
  static const char* FIRST[] = {"Sasha", "Vova", "Kolya", "Petro", "Misha", "Andriy", "Yura", "Dima", "Taras", "Oleg", "Bohdan", "Seryi", "Lyosha", "Ivan", "Stepan", "Fedir"};
  static const char* NICK[] = {"Rook", "Mole", "Lantern", "Ash", "Tinker", "Sparrow", "Crutch", "Hook", "Owl", "Beard", "Rusty", "Professor", "Lucky", "Hare", "Silent", "Badger", "Kettle", "Boots"};
  const u32 h = e.id * 2654435761u;
  return std::string(FIRST[(h >> 8) % 16]) + " " + NICK[(h >> 16) % 18];
}
bool npcFriendlyTo(const Entity& n, const Player& p) {
  if (n.removed) return false;
  if (auto* c = dynamic_cast<const Creature*>(&n); !c || !c->alive()) return false;
  const int f = npcFactionOf(n);
  if (f == F_NONE || f >= F_MUTANT) return false;
  if (n.data.is_object() && n.data.contains("grudges")) for (auto& g : n.data["grudges"]) if (g.is_number() && g.get<u32>() == p.id) return false;
  return n.type == "z_npc_trader" || attitude(p, f) >= 0;
}
bool npcIsLeader(Level& lv, const Entity& n) {
  if (n.type == "z_npc_trader") return true;
  const int sq = n.data.value("alife", -1); if (sq < 0) return false;
  const SquadGoal g = alifeGoal(lv, sq);
  if (!g.ok || !g.arrived || (g.kind != SM_BASE && g.kind != SM_HUB && g.kind != SM_CAMP)) return false;
  for (auto& e : lv.entities()) if (!e->removed && e->id < n.id && e->data.is_object() && e->data.value("alife", -2) == sq && e->hp > 0) return false;
  return true;
}

// ---------------------------------------------------------------- jobs
std::vector<JobOffer> jobOffers(Level& lv, const Player& p, const Entity& giver) {
  std::vector<JobOffer> out;
  const int gf = npcFactionOf(giver);
  const bool trader = giver.type == "z_npc_trader";
  const std::string gid = giverId(giver), who = npcName(giver);
  Mulberry32 r(hash32((i32)strHash(gid), (i32)lv.info.day, (i32)lv.info.seed, 77));
  std::vector<const json*> T;
  for (auto& t : templates()) for (auto& g : t.value("givers", json::array())) if (g == (trader ? "trader" : factionKey(gf))) { T.push_back(&t); break; }
  for (size_t i = T.size(); i > 1; i--) std::swap(T[i - 1], T[(size_t)(r() * i) % i]);
  const auto squads = alifeSquads(lv);
  const Plan& P = zonePlan((u32)lv.info.seed);
  for (const json* tp : T) {
    if (out.size() >= 3) break;
    const json& t = *tp; const std::string type = t.value("type", "");
    json j{{"type", type}, {"giver", who}, {"gid", gid}, {"f", gf}, {"gx", giver.pos.x}, {"gz", giver.pos.z}, {"have", 0}, {"need", 1}, {"state", "go"}, {"r", 150}};
    std::map<std::string, std::string> v{{"giver", who}};
    std::string sub;
    if (type == "hunt" || type == "camp") {
      const SquadView* best = nullptr; float bd = type == "hunt" ? 1500.f : 2500.f;
      for (const SquadView& q : squads) {
        if (q.members <= 0) continue;
        if (type == "hunt" ? (q.faction != F_MUTANT || q.species.empty()) : (q.faction >= F_MUTANT || q.faction == gf || factionRelation(gf, q.faction) >= 0)) continue;
        const float d = flatDist(giver.pos, q.pos.x, q.pos.z); if (d < 60 || d > bd) continue;
        bd = d; best = &q;
      }
      if (!best) continue;
      j["tx"] = best->pos.x; j["tz"] = best->pos.z; j["need"] = std::clamp(best->members, 2, 5);
      if (type == "hunt") { j["sp"] = best->species; v["mutants"] = mutantName(best->species, true); }
      else { j["vs"] = best->faction; v["faction"] = factionName(best->faction); }
      v["place"] = placeNear(lv, best->pos.x, best->pos.z); sub = std::to_string(best->id);
    } else if (type == "artifact") {
      const int fam = (int)(r() * 5) % 5; j["fam"] = familyKey(fam); v["family"] = familyKey(fam); sub = familyKey(fam);
    } else if (type == "trophy") {
      const json& L = t.value("trophies", json::array()); if (L.empty()) continue;
      const json& e = L[(size_t)(r() * L.size()) % L.size()];
      const int a = e[1].get<int>(), b = e[2].get<int>(), n = a + (int)(r() * (b - a + 1));
      j["item"] = e[0]; j["need"] = n; v["trophy"] = trophyName(e[0].get<std::string>()); sub = e[0].get<std::string>();
    } else if (type == "stash") {
      int x, z, y0, d; std::string name;
      if (!stashSpot(lv, giver.pos, strHash(gid) + (u32)lv.info.day, x, z, y0, d, name)) continue;
      j["tx"] = x + 0.5f; j["tz"] = z + 0.5f; j["sx"] = x; j["sz"] = z; j["sy"] = y0; j["sd"] = d; j["r"] = 4; v["place"] = name; sub = std::to_string(x) + "," + std::to_string(z);
    } else continue;
    v["need"] = std::to_string(j["need"].get<int>());
    JobOffer o; o.type = type; o.key = gid + ":" + std::to_string(lv.info.day) + ":" + type + ":" + sub;
    o.title = fill(t.value("title", type), v); o.text = fill(t.value("text", std::string()), v);
    const json& m = t.value("money", json::array({1000, 2000}));
    j["money"] = (int)std::round((m[0].get<int>() + r() * (m[1].get<int>() - m[0].get<int>())) / 50.0) * 50;
    j["rep"] = t.value("rep", 100);
    if (const json& it = t.value("items", json::array()); !it.empty()) j["items"] = json::array({it[(size_t)(r() * it.size()) % it.size()]});
    if (j.contains("tx")) j["where"] = v.count("place") ? v["place"] : "";
    j["key"] = o.key; j["title"] = o.title; j["text"] = o.text;
    o.job = std::move(j);
    bool taken = false;
    if (const json* Z = Zc(p)) { for (auto& x : Z->value("jobs", json::array())) if (x.value("key", "") == o.key) taken = true; for (auto& x : Z->value("jobsDone", json::array())) if (x == o.key) taken = true; }
    if (!taken) out.push_back(std::move(o));
  }
  (void)P;
  return out;
}

bool takeJob(Level& lv, Player& p, const JobOffer& o, std::string& why) {
  json& J = Zs(p)["jobs"]; if (!J.is_array()) J = json::array();
  if (J.size() >= 5) { why = "You have five jobs on already. Finish one first."; return false; }
  for (auto& x : J) if (x.value("key", "") == o.key) { why = "You took that one already."; return false; }
  json j = o.job;
  if (o.type == "stash") {
    const int id = makeStash(lv, j.value("sx", 0), j.value("sz", 0), j.value("sy", 64), j.value("sd", 2));
    j["stash"] = id; knowStash(p, id, j.value("sx", 0), j.value("sz", 0), j.value("where", std::string("A stash")));
  }
  J.push_back(j);
  Zs(p)["track"] = "job:" + o.key;
  return true;
}

std::vector<int> jobsToTurnIn(const Player& p, const Entity& npc) {
  std::vector<int> out;
  const json* Z = Zc(p); if (!Z || !Z->contains("jobs") || !(*Z)["jobs"].is_array()) return out;
  const std::string gid = giverId(npc); const int f = npcFactionOf(npc);
  const json& J = (*Z)["jobs"];
  for (int i = 0; i < (int)J.size(); i++) {
    const json& j = J[i];
    if (j.value("gid", "") != gid && j.value("f", -1) != f) continue;
    const std::string t = j.value("type", "");
    const bool ready = (t == "artifact" || t == "trophy") ? hasItems(p, j.value("item", ""), j.value("fam", ""), j.value("need", 1)) : j.value("state", "") == "back";
    if (ready) out.push_back(i);
  }
  return out;
}

bool turnInJob(Level& lv, Player& p, int idx, std::string& msg) {
  json& Z = Zs(p); json& J = Z["jobs"];
  if (!J.is_array() || idx < 0 || idx >= (int)J.size()) return false;
  json j = J[idx];
  const std::string t = j.value("type", "");
  if (t == "artifact" || t == "trophy") {
    if (!hasItems(p, j.value("item", ""), j.value("fam", ""), j.value("need", 1))) { msg = "You don't have it on you."; return false; }
    takeItems(p, j.value("item", ""), j.value("fam", ""), j.value("need", 1));
  }
  const int money = j.value("money", 0); addMoney(p, money);
  msg = "+" + std::to_string(money) + " RU";
  for (auto& it : j.value("items", json::array())) if (it.is_array() && it.size() == 2) {
    const ItemStack s(it[0].get<std::string>(), it[1].get<int>());
    if (p.give(s) > 0) dropItem(lv, p.realm, s, p.pos + vec3(0, 1, 0), true, (const vec3*)nullptr, 0.f);
    if (const ZItem* z = zi(s.id)) msg += ", " + z->name + (s.n > 1 ? " x" + std::to_string(s.n) : "");
  }
  addRep(p, j.value("f", (int)F_DRIFTER), j.value("rep", 100));
  msg += ", standing with " + std::string(factionName(j.value("f", (int)F_DRIFTER))) + " +" + std::to_string(j.value("rep", 100));
  J.erase(J.begin() + idx);
  json& D = Z["jobsDone"]; if (!D.is_array()) D = json::array(); D.push_back(j.value("key", "")); while (D.size() > 40) D.erase(D.begin());
  Z["jobsDoneN"] = Z.value("jobsDoneN", 0) + 1;
  if (Z.value("track", "") == "job:" + j.value("key", std::string())) Z.erase("track");
  return true;
}
void dropJob(Player& p, int idx) { json& J = Zs(p)["jobs"]; if (J.is_array() && idx >= 0 && idx < (int)J.size()) J.erase(J.begin() + idx); }

std::vector<Task> jobTasks(const Level&, const Player& p) {
  std::vector<Task> out;
  const json* Z = Zc(p); if (!Z) return out;
  for (auto& j : Z->value("jobs", json::array())) {
    Task t; t.id = "job:" + j.value("key", std::string()); t.title = j.value("title", std::string("Job"));
    const std::string ty = j.value("type", ""), who = j.value("giver", std::string("the giver"));
    const bool fetch = ty == "artifact" || ty == "trophy";
    t.text = j.value("text", std::string());
    if (j.value("state", "") == "back" || (fetch && hasItems(p, j.value("item", ""), j.value("fam", ""), j.value("need", 1)))) {
      t.text += "\nDone: report to " + who + " (or anyone of " + factionName(j.value("f", 0)) + ") for " + std::to_string(j.value("money", 0)) + " RU.";
      t.hasTarget = true; t.target = vec3(j.value("gx", 0.f), 0, j.value("gz", 0.f)); t.where = who;
    } else {
      if (ty == "hunt" || ty == "camp") t.text += "\nKilled " + std::to_string(j.value("have", 0)) + " / " + std::to_string(j.value("need", 1)) + ".";
      if (fetch) t.text += "\nReward " + std::to_string(j.value("money", 0)) + " RU from " + who + ".";
      if (j.contains("tx")) { t.hasTarget = true; t.target = vec3(j.value("tx", 0.f), 0, j.value("tz", 0.f)); t.where = j.value("where", std::string()); }
    }
    out.push_back(t);
  }
  return out;
}

std::vector<std::string> rumours(Level& lv, Player& p, const Entity& npc) {
  std::vector<std::string> out;
  const vec3 at = npc.pos;
  const SquadView* pack = nullptr; const SquadView* foe = nullptr; float pd = 900, fd = 1200;
  const auto squads = alifeSquads(lv);
  const int nf = npcFactionOf(npc);
  for (const SquadView& q : squads) {
    if (q.members <= 0) continue;
    const float d = flatDist(at, q.pos.x, q.pos.z); if (d < 40) continue;
    if (q.faction == F_MUTANT) { if (!q.species.empty() && d < pd) { pd = d; pack = &q; } }
    else if (factionRelation(nf, q.faction) < 0 && d < fd) { fd = d; foe = &q; }
  }
  auto far = [](float d) { char b[48]; std::snprintf(b, sizeof b, "%d m", (int)(std::round(d / 50) * 50)); return std::string(b); };
  if (pack) out.push_back("A pack of " + mutantName(pack->species, true) + " has been seen " + compass(pack->pos.x - at.x, pack->pos.z - at.z) + " of here, near " + placeNear(lv, pack->pos.x, pack->pos.z) + ". About " + far(pd) + " off. " + (pack->members > 3 ? "A big one." : "Not many, but enough."));
  if (foe) out.push_back(std::string(factionName(foe->faction)) + " are about " + compass(foe->pos.x - at.x, foe->pos.z - at.z) + ", by " + placeNear(lv, foe->pos.x, foe->pos.z) + ". " + std::to_string(foe->members) + " of them. Keep your head down.");
  const Plan& P = zonePlan((u32)lv.info.seed);
  const AnomalyField* fb = nullptr; float bd = 700;
  for (const AnomalyField& f : P.fields) { const float d = flatDist(at, (float)f.x, (float)f.z); if (d > 30 && d < bd) { bd = d; fb = &f; } }
  if (fb) out.push_back("There's a " + std::string(familyKey(fb->family)) + " field " + compass(fb->x - at.x, fb->z - at.z) + " of here, " + far(bd) + " off, by " + placeNear(lv, (float)fb->x, (float)fb->z) + ". " + (fb->tier >= 3 ? "Nasty. Throw your bolts." : "Small, but the last emission might have left something in it."));
  // now and then a stash, once per stalker
  json& seen = Zs(p)["rumoured"]; if (!seen.is_array()) seen = json::array();
  bool told = false; for (auto& s : seen) if (s == npc.id) told = true;
  if (!told) {
    seen.push_back(npc.id); while (seen.size() > 60) seen.erase(seen.begin());
    std::string where;
    if (hash32((i32)npc.id, (i32)lv.info.seed, 5, 5) % 100 < 40 && revealStash(lv, p, at, &where)) out.push_back("I'll tell you a secret. A friend of mine hid his things near " + where + " and never came back for them. It's on your PDA now. Don't say who told you.");
  }
  const Emission em = emission(lv);
  if (em.phase == 0 && em.left < 900) out.push_back("The next emission's close, maybe " + std::to_string((int)(em.left / 60) + 1) + " minutes. Know where you'll hide.");
  if (out.empty()) out.push_back("Quiet lately. Too quiet, if you ask me.");
  return out;
}

// ---------------------------------------------------------------- stashes
std::vector<StashView> knownStashes(const Player& p) {
  std::vector<StashView> out;
  if (const json* Z = Zc(p)) for (auto& s : Z->value("stashes", json::array()))
    out.push_back({s.value("id", 0), vec3(s.value("x", 0) + 0.5f, 0, s.value("z", 0) + 0.5f), s.value("name", std::string("Stash")), s.value("found", false)});
  return out;
}
bool revealStash(Level& lv, Player& p, vec3 near, std::string* where) {
  int x, z, y0, d; std::string name;
  const u32 salt = (u32)Zs(p).value("stashSalt", 0); Zs(p)["stashSalt"] = salt + 1;
  if (!stashSpot(lv, near, salt * 7919u + p.id, x, z, y0, d, name)) return false;
  const int id = makeStash(lv, x, z, y0, d);
  knowStash(p, id, x, z, name);
  if (where) *where = name;
  return true;
}

// ---------------------------------------------------------------- the owner's tick
void jobsTick(PlayerController& pc, float dt) {
  Player& p = pc.p; Level& lv = pc.lv;
  json& Z = Zs(p);
  float t = Z.value("jobsT", 0.f) - dt; Z["jobsT"] = t;
  if (t > 0) return;
  Z["jobsT"] = 0.5f;
  if (!Z.contains("day0")) Z["day0"] = lv.info.day;
  // artifacts found: more on you than before (picked up, looted)
  int arts = 0; for (auto& s : p.inv) if (!s.empty()) if (const ZItem* z = zi(s.id); z && z->cat == "artifact") arts += s.n;
  const int last = Z.value("artHeld", arts); if (arts > last) Z["artFound"] = Z.value("artFound", 0) + (arts - last); Z["artHeld"] = arts;
  if (p.realm != REALM_MAIN) return;
  // anomaly fields you've been near go on your map
  {
    const Plan& P = zonePlan((u32)lv.info.seed);
    json& F = Z["fields"]; if (!F.is_array()) F = json::array();
    for (int i = 0; i < (int)P.fields.size(); i++) {
      const AnomalyField& f = P.fields[i];
      if (flatDist(p.pos, (float)f.x, (float)f.z) > f.r + 60) continue;
      bool have = false; for (auto& x : F) if (x == i) { have = true; break; }
      if (!have) F.push_back(i);
    }
  }
  // stashes reached
  json& S = Z["stashes"];
  if (S.is_array()) for (auto& s : S) {
    if (s.value("found", false) || flatDist(p.pos, s.value("x", 0) + 0.5f, s.value("z", 0) + 0.5f) > 3.5f) continue;
    s["found"] = true; Z["stashFound"] = Z.value("stashFound", 0) + 1;
    pc.toastMsg("Stash found: " + s.value("name", std::string("a stash")), "rare");
    if (Z["jobs"].is_array()) for (auto& j : Z["jobs"]) if (j.value("stash", -1) == s.value("id", 0) && j.value("state", "") == "go") j["state"] = "back";
  }
}

void registerZoneJobs() {
  auto old = globalCreatureHooks().died;
  globalCreatureHooks().died = [old](Level& lv, Creature& c, const DamageSrc& src) {
    if (zoneWorld(lv) && !lv.isRemote()) killed(lv, c, src);
    if (old) old(lv, c, src);
  };
  // a dead stalker's PDA: his stash goes on your map
  hooks().useItem.push_back([](PlayerController& pc, ItemStack& held, const ItemDef& def, bool repeat) {
    if (repeat || def.key != "pda_stalker" || !zoneWorld(pc.lv)) return false;
    std::string where;
    if (revealStash(pc.lv, pc.p, pc.p.pos, &where)) pc.toastMsg("His PDA has a stash marked: " + where + ". It's on your map now.", "rare");
    else pc.toastMsg("Nothing on it but static.", "warn");
    if (!pc.p.creative() && --held.n <= 0) held.clear();
    return true;
  });
  registerLevelSystem([](Level& lv) -> std::shared_ptr<LevelSystem> { return zoneWorld(lv) ? std::make_shared<StashSys>() : nullptr; });
}

} // namespace dq::zone
