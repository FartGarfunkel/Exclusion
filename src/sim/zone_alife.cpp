// sim/zone_alife.cpp — A-Life: the Zone lives when nobody is looking. Every stalker squad and mutant pack on the
// map is simulated all the time (on the host). Offline, a squad walks the travel graph (roads and the tracks that
// join them, and a spur from each smart terrain) the way OpenXRay's detail path walks the game graph: a budget
// of walked metres, then the next vertex, and the squad stands where that edge has carried it. At a smart it
// rests, then patrols a short loop, then often moves on. Hostile squads that meet fight it out on paper. A smart
// respawns its own people only when it is short of them, the idle has passed, and no player is inside the respawn
// radius, so nothing is created at a player's feet. A squad becomes real creatures when a player is inside the
// online radius and that ground is loaded, and goes back to paper (keeping who's still alive and where) once
// every one of them is past the offline radius. The two radii are OpenXRay's switch: online = switch_distance *
// (1 - switch_factor), offline = switch_distance * (1 + switch_factor). Anomaly's alife section uses
// switch_distance 750 and switch_factor 0.1 (675 m on, 825 m off). This world streams, so a body also goes back
// to paper when the ground under it unloads. Emissions send squads under the nearest roof; those caught outside
// lose people. Garrisons stay home. Road dens stay where they were placed until someone meets them. Squads save
// with the world.
#include "sim/zone.h"
#include "sim/creature.h"
#include "sim/magic_combat.h"
#include "sim/combat.h"
#include "core/registry.h"
#include "gen/zone_plan.h"
#include <cmath>
#include <algorithm>
#include <map>
#include <queue>
#include <unordered_map>

namespace dq::zone {

namespace {
// Anomaly alife.ltx, as shipped for the modded exes: switch_distance 750, switch_factor 0.1.
constexpr float ALIFE_SWITCH_DISTANCE = 750.f, ALIFE_SWITCH_FACTOR = 0.1f;
constexpr float ONLINE_R = ALIFE_SWITCH_DISTANCE * (1.f - ALIFE_SWITCH_FACTOR);
constexpr float OFFLINE_R = ALIFE_SWITCH_DISTANCE * (1.f + ALIFE_SWITCH_FACTOR);
constexpr float WALK = 1.4f;
constexpr double STEP = 2.0;   // offline update period (s)
// Anomaly smart_terrain defaults: respawn_radius 150 (the actor is too close), respawn_idle 43200 game seconds
// (12 hours). Exclusion's day clock is DAY_LEN, so half a day is that idle.
constexpr float RESPAWN_RADIUS = 150.f;
constexpr double RESPAWN_IDLE = DAY_LEN * 0.5;
constexpr float ARRIVE = 8.f;          // standing on the smart
constexpr float PATROL_R = 16.f;       // a walk path round the camp, then back to the fire
constexpr float LOCAL_M = 64.f;        // already at the place: the last stretch is not a lap of the Zone
constexpr float GRAPH_STEP = 48.f;     // vertices along a road, short enough that a squad is always near one

struct Squad {
  int id = 0; int faction = F_DRIFTER; int members = 3; int tier = 1;
  std::string species;           // mutants: the creature key; stalkers: ""
  float x = 0, z = 0; int goal = -1, home = -1;
  float hold = 0;                // seconds left holding the goal
  bool online = false;
  bool gar = false;              // (alpha.9) a garrison: lives at its home for good (the plan's Smart::garrison), never wanders off
  bool stay = false;             // a road den: holds its ground until the player has met it (hold counts down only while online)
  bool hid = false;              // under a roof for this emission (a camp with a roof counts; an open fire does not)
  int job = 0;                   // 0 travelling the graph, 1 resting, 2 patrolling
  int leg = 0;
  int pathGoal = -2;
  std::vector<int> path;         // graph vertices still ahead (not saved: rebuilt from x,z and the goal)
  std::vector<u32> ents;
  json save() const { json j{{"id", id}, {"f", faction}, {"m", members}, {"t", tier}, {"sp", species}, {"x", x}, {"z", z}, {"g", goal}, {"h", home}, {"hold", hold}}; if (gar) j["gar"] = true; if (stay) j["stay"] = true; if (hid) j["hid"] = true; return j; }
  static Squad load(const json& j) { Squad s; s.id = j.value("id", 0); s.faction = j.value("f", 0); s.members = j.value("m", 3); s.tier = j.value("t", 1); s.species = j.value("sp", ""); s.x = j.value("x", 0.f); s.z = j.value("z", 0.f); s.goal = j.value("g", -1); s.home = j.value("h", -1); s.hold = j.value("hold", 0.f); s.gar = j.value("gar", false); s.stay = j.value("stay", false); s.hid = j.value("hid", false); return s; }
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

// The offline map. Roads and tracks are the edges a squad can walk; each smart terrain is tied to the nearest
// road by a spur cut into the same short vertices. OpenXRay registers a creature on a game-graph vertex and
// spends walked distance along the edge (CALifeMonsterDetailPathManager::follow_path). Here the squad's x,z is
// that point on the edge, which is where it stands up when a player comes close.
struct TravelGraph {
  struct Node { float x, z; int kind; };   // 0 road, 1 smart, 2 spur / bridge
  struct Edge { int to; float len; };
  std::vector<Node> nodes;
  std::vector<std::vector<Edge>> adj;
  std::vector<int> smartNode;
  std::unordered_map<i64, std::vector<int>> grid;

  static int cell(float v) { return (int)std::floor(v / 128.f); }
  static i64 ckey(int x, int z) { return (i64)(x + 100000) * 200003 + (z + 100000); }

  int addNode(float x, float z, int kind) {
    nodes.push_back({x, z, kind});
    adj.emplace_back();
    return (int)nodes.size() - 1;
  }
  void linkDirect(int a, int b) {
    if (a < 0 || b < 0 || a == b) return;
    for (const Edge& e : adj[a]) if (e.to == b) return;
    const float len = std::hypot(nodes[a].x - nodes[b].x, nodes[a].z - nodes[b].z);
    if (len < 0.25f) return;
    adj[a].push_back({b, len});
    adj[b].push_back({a, len});
  }
  void linkSub(int a, int b) {
    if (a < 0 || b < 0 || a == b) return;
    const float ax = nodes[a].x, az = nodes[a].z, bx = nodes[b].x, bz = nodes[b].z;
    const float dx = bx - ax, dz = bz - az, dist = std::hypot(dx, dz);
    const int steps = std::max(1, (int)std::ceil(dist / GRAPH_STEP));
    int prev = a;
    for (int i = 1; i <= steps; i++) {
      const int m = i == steps ? b : addNode(ax + dx * ((float)i / steps), az + dz * ((float)i / steps), 2);
      linkDirect(prev, m);
      prev = m;
    }
  }
  void rebuildGrid() {
    grid.clear();
    for (int i = 0; i < (int)nodes.size(); i++) grid[ckey(cell(nodes[i].x), cell(nodes[i].z))].push_back(i);
  }
  int nearest(float x, float z, int kind) const {
    if (nodes.empty()) return -1;
    const int cx = cell(x), cz = cell(z);
    int best = -1; float bd = 1e30f;
    for (int rad = 0; rad < 48; rad++) {
      // a later ring can still hold a closer vertex; stop once the ring itself is past the best
      if (best >= 0 && (float)std::max(0, rad - 1) * 128.f > bd + 128.f) break;
      for (int dz = -rad; dz <= rad; dz++) for (int dx = -rad; dx <= rad; dx++) {
        if (rad && std::max(std::abs(dx), std::abs(dz)) != rad) continue;
        auto it = grid.find(ckey(cx + dx, cz + dz));
        if (it == grid.end()) continue;
        for (int i : it->second) {
          if (kind >= 0 && nodes[i].kind != kind) continue;
          const float d = std::hypot(nodes[i].x - x, nodes[i].z - z);
          if (d < bd) { bd = d; best = i; }
        }
      }
    }
    if (best >= 0) return best;
    for (int i = 0; i < (int)nodes.size(); i++) {
      if (kind >= 0 && nodes[i].kind != kind) continue;
      const float d = std::hypot(nodes[i].x - x, nodes[i].z - z);
      if (d < bd) { bd = d; best = i; }
    }
    return best;
  }
  int components(std::vector<int>& comp) const {
    comp.assign(nodes.size(), -1);
    int C = 0;
    std::vector<int> stack;
    for (int i = 0; i < (int)nodes.size(); i++) if (comp[i] < 0) {
      stack.push_back(i); comp[i] = C;
      while (!stack.empty()) {
        const int u = stack.back(); stack.pop_back();
        for (const Edge& e : adj[u]) if (comp[e.to] < 0) { comp[e.to] = C; stack.push_back(e.to); }
      }
      C++;
    }
    return C;
  }
  // Join separate road nets. Nearby gaps use the grid; anything still apart is bridged at its closest vertices.
  void connect() {
    for (float radius : {96.f, 240.f, 520.f}) {
      std::vector<int> comp;
      const int C = components(comp);
      if (C <= 1) return;
      rebuildGrid();
      std::vector<int> ba(C, -1), bb(C, -1);
      std::vector<float> bd(C, 1e30f);
      const int cr = std::max(1, (int)std::ceil(radius / 128.f));
      for (int i = 0; i < (int)nodes.size(); i++) {
        const int c = comp[i], cx = cell(nodes[i].x), cz = cell(nodes[i].z);
        for (int dz = -cr; dz <= cr; dz++) for (int dx = -cr; dx <= cr; dx++) {
          auto it = grid.find(ckey(cx + dx, cz + dz));
          if (it == grid.end()) continue;
          for (int j : it->second) {
            if (comp[j] == c) continue;
            const float d = std::hypot(nodes[i].x - nodes[j].x, nodes[i].z - nodes[j].z);
            if (d > radius || d >= bd[c]) continue;
            bd[c] = d; ba[c] = i; bb[c] = j;
          }
        }
      }
      for (int c = 0; c < C; c++) if (ba[c] >= 0) linkSub(ba[c], bb[c]);
    }
    for (int guard = 0; guard < 48; guard++) {
      std::vector<int> comp;
      const int C = components(comp);
      if (C <= 1) return;
      std::vector<int> rep(C, -1);
      for (int i = 0; i < (int)nodes.size(); i++) if (rep[comp[i]] < 0) rep[comp[i]] = i;
      int ba = -1, bb = -1; float bd = 1e30f;
      for (int a = 0; a < C; a++) for (int b = a + 1; b < C; b++) {
        const float d = std::hypot(nodes[rep[a]].x - nodes[rep[b]].x, nodes[rep[a]].z - nodes[rep[b]].z);
        if (d < bd) { bd = d; ba = rep[a]; bb = rep[b]; }
      }
      if (ba < 0) return;
      // the real gap may sit away from the representatives: search those two components
      const int ca = comp[ba], cb = comp[bb];
      for (int i = 0; i < (int)nodes.size(); i++) if (comp[i] == ca) {
        for (int j = 0; j < (int)nodes.size(); j++) if (comp[j] == cb) {
          const float d = std::hypot(nodes[i].x - nodes[j].x, nodes[i].z - nodes[j].z);
          if (d < bd) { bd = d; ba = i; bb = j; }
        }
      }
      linkSub(ba, bb);
    }
  }
  std::vector<int> route(int src, int dst) const {
    if (src < 0 || dst < 0 || src >= (int)nodes.size() || dst >= (int)nodes.size()) return {};
    if (src == dst) return {src};
    const int n = (int)nodes.size();
    std::vector<float> g(n, 1e30f);
    std::vector<int> prev(n, -1);
    using P = std::pair<float, int>;
    std::priority_queue<P, std::vector<P>, std::greater<P>> pq;
    g[src] = 0; pq.push({0, src});
    while (!pq.empty()) {
      const float d = pq.top().first; const int u = pq.top().second; pq.pop();
      if (d != g[u]) continue;
      if (u == dst) break;
      for (const Edge& e : adj[u]) {
        const float nd = d + e.len;
        if (nd < g[e.to]) { g[e.to] = nd; prev[e.to] = u; pq.push({nd, e.to}); }
      }
    }
    if (g[dst] > 1e29f) return {};
    std::vector<int> path;
    for (int u = dst; u >= 0; u = prev[u]) { path.push_back(u); if (u == src) break; }
    std::reverse(path.begin(), path.end());
    if (path.empty() || path.front() != src) return {};
    return path;
  }
  float distanceTo(float x, float z) const {
    if (nodes.empty()) return 1e9f;
    const int n0 = nearest(x, z, -1);
    if (n0 < 0) return 1e9f;
    const float nd = std::hypot(nodes[n0].x - x, nodes[n0].z - z);
    const float rad = nd + GRAPH_STEP + 8.f;
    float best = nd;
    const int cx = cell(x), cz = cell(z), cr = (int)std::ceil(rad / 128.f) + 1;
    for (int dz = -cr; dz <= cr; dz++) for (int dx = -cr; dx <= cr; dx++) {
      auto it = grid.find(ckey(cx + dx, cz + dz));
      if (it == grid.end()) continue;
      for (int i : it->second) for (const Edge& e : adj[i]) {
        const float ax = nodes[i].x, az = nodes[i].z, bx = nodes[e.to].x, bz = nodes[e.to].z;
        const float abx = bx - ax, abz = bz - az, L2 = abx * abx + abz * abz;
        const float t = L2 > 1e-4f ? std::clamp(((x - ax) * abx + (z - az) * abz) / L2, 0.f, 1.f) : 0.f;
        best = std::min(best, std::hypot(x - (ax + abx * t), z - (az + abz * t)));
      }
    }
    return best;
  }
};

const TravelGraph& travelGraph(const Plan& P) {
  struct Cache { const Plan* plan = nullptr; TravelGraph g; };
  static Cache cache;
  if (cache.plan == &P) return cache.g;
  TravelGraph G;
  for (const Line& l : roads()) {
    if (l.kind == 3 || l.kind == 0 || l.pts.size() < 2) continue;
    int prev = -1;
    float acc = 0;
    auto emit = [&](float x, float z) {
      if (prev >= 0 && std::hypot(G.nodes[prev].x - x, G.nodes[prev].z - z) < 8.f) return;
      const int id = G.addNode(x, z, 0);
      if (prev >= 0) G.linkDirect(prev, id);
      prev = id;
    };
    emit(l.pts[0].x, l.pts[0].z);
    for (size_t i = 1; i < l.pts.size(); i++) {
      const float dx = l.pts[i].x - l.pts[i - 1].x, dz = l.pts[i].z - l.pts[i - 1].z, seg = std::hypot(dx, dz);
      if (seg < 0.5f) continue;
      float walked = 0;
      while (acc + (seg - walked) >= GRAPH_STEP) {
        const float need = GRAPH_STEP - acc;
        walked += need;
        const float t = walked / seg;
        emit(l.pts[i - 1].x + dx * t, l.pts[i - 1].z + dz * t);
        acc = 0;
      }
      acc += seg - walked;
    }
    emit(l.pts.back().x, l.pts.back().z);
  }
  G.rebuildGrid();
  const int roadN = (int)G.nodes.size();
  for (int i = 0; i < roadN; i++) {
    const int cx = TravelGraph::cell(G.nodes[i].x), cz = TravelGraph::cell(G.nodes[i].z);
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
      auto it = G.grid.find(TravelGraph::ckey(cx + dx, cz + dz));
      if (it == G.grid.end()) continue;
      for (int j : it->second) if (j > i && std::hypot(G.nodes[i].x - G.nodes[j].x, G.nodes[i].z - G.nodes[j].z) <= 56.f) G.linkDirect(i, j);
    }
  }
  G.smartNode.resize(P.smarts.size(), -1);
  for (int i = 0; i < (int)P.smarts.size(); i++) {
    const Smart& m = P.smarts[i];
    const int sn = G.addNode((float)m.x, (float)m.z, 1);
    G.smartNode[i] = sn;
    const int road = roadN ? G.nearest((float)m.x, (float)m.z, 0) : -1;
    if (road >= 0) G.linkSub(sn, road);
  }
  G.connect();
  G.rebuildGrid();
  int edges = 0; for (auto& a : G.adj) edges += (int)a.size();
  DQ_LOG("A-Life graph: %d vertices, %d edges, %d smarts", (int)G.nodes.size(), edges / 2, (int)P.smarts.size());
  cache.g = std::move(G);
  cache.plan = &P;
  return cache.g;
}

struct Alife : LevelSystem {
  std::vector<Squad> squads;
  std::map<int, int> occupancy;   // smart -> squad id holding it
  std::vector<double> smartNext;  // world-clock time a smart may next try to respawn
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
      // A den on a stalker's ground dies as soon as both come online (a gun reaches about 38 m).
      for (const Smart& m : P.smarts) if (m.kind != SM_LAIR && m.faction != F_MUTANT && std::hypot(m.x - px, m.z - pz) < 40.f) return true;
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
      // a newcomer only while the player is not standing on the place (Anomaly's respawn radius)
      if (!g->online && g->members < m.garrison && (full || rnd() < 0.5) && (full || !playerWithin(lv, (float)m.x, (float)m.z, RESPAWN_RADIUS)))
        g->members = full ? m.garrison : g->members + 1;
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

  bool playerWithin(Level& lv, float x, float z, float r) const {
    for (auto& e : lv.entities()) {
      auto* p = dynamic_cast<Player*>(e.get());
      if (!p || p->removed || p->dead || p->realm != REALM_MAIN) continue;
      if (std::hypot(p->pos.x - x, p->pos.z - z) < r) return true;
    }
    return false;
  }
  static int maxPop(const Smart& m) {
    if (m.kind == SM_FIELD || m.kind == SM_POST) return 0;   // placed visitors only; a post's people are its garrison
    if (m.kind == SM_LAIR) return 1;                          // a lair keeps one pack, the way a monster smart does
    if (m.kind == SM_BASE || m.kind == SM_HUB) return 3;
    return 1;
  }
  int popAt(int si) const {
    int n = 0;
    for (const Squad& s : squads) if (s.members > 0 && !s.gar && !s.stay && s.goal == si) n++;
    return n;
  }
  // who the smart's own spawn section is: its faction, or the mutant mix lairs already use
  void spawnAt(Level& lv, int i) {
    const Smart& m = plan(lv).smarts[i];
    if (m.kind == SM_LAIR || m.faction == F_MUTANT) {
      const std::string sp = mutantFor(m.danger, rnd(), m.x, m.z, rnd());
      add(F_MUTANT, packOf(sp, rnd()), m.danger, (float)m.x, (float)m.z, i, sp);
      return;
    }
    const int f = (m.faction == F_NONE || m.faction >= F_MUTANT) ? (rnd() < 0.72 ? F_DRIFTER : F_BANDIT) : (int)m.faction;
    add(f, 2 + (int)(rnd() * 3), m.danger, (float)m.x, (float)m.z, i);
  }
  static void stepToward(Squad& s, float tx, float tz, float budget) {
    const float dx = tx - s.x, dz = tz - s.z, d = std::hypot(dx, dz);
    if (d < 0.4f) { s.x = tx; s.z = tz; return; }
    const float v = std::min(d, budget);
    s.x += dx / d * v; s.z += dz / d * v;
  }
  // follow_path: spend `budget` metres along the vertices. The position stays on the current edge.
  void travel(Level& lv, Squad& s, float budget) {
    const Plan& P = plan(lv);
    const TravelGraph& G = travelGraph(P);
    if (s.goal < 0 || s.goal >= (int)G.smartNode.size()) return;
    if (s.pathGoal != s.goal || s.path.empty()) {
      const int src = G.nearest(s.x, s.z, -1);
      const int dst = G.smartNode[s.goal];
      s.path = G.route(src, dst);
      s.pathGoal = s.goal;
      s.leg = 0;
      if (s.path.empty()) { stepToward(s, (float)P.smarts[s.goal].x, (float)P.smarts[s.goal].z, budget); return; }
    }
    while (budget > 0.f && s.leg < (int)s.path.size()) {
      const TravelGraph::Node& n = G.nodes[s.path[s.leg]];
      const float dx = n.x - s.x, dz = n.z - s.z, d = std::hypot(dx, dz);
      if (d <= budget + 0.4f) { budget -= d; s.x = n.x; s.z = n.z; s.leg++; }
      else { s.x += dx / d * budget; s.z += dz / d * budget; budget = 0; }
    }
  }
  // one loop of a walk path round the smart, then rest again or take another smart
  void patrol(Level& lv, Squad& s, const Smart& m, float budget) {
    const int legs = 4;
    const float ang = (s.leg + (s.id % 5) * 0.17f) * (6.2831853f / legs);
    stepToward(s, (float)m.x + std::cos(ang) * PATROL_R, (float)m.z + std::sin(ang) * PATROL_R, budget);
    const float left = std::hypot(s.x - ((float)m.x + std::cos(ang) * PATROL_R), s.z - ((float)m.z + std::sin(ang) * PATROL_R));
    if (left > 1.2f) return;
    if (++s.leg < legs) return;
    s.leg = 0;
    if (rnd() < 0.55f) {
      s.goal = pickGoal(lv, s); s.hold = 0; s.job = 0; s.path.clear(); s.pathGoal = -2;
    } else { s.job = 1; s.hold = 80.f + (float)rnd() * 160.f; s.x = (float)m.x; s.z = (float)m.z; }
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
      // The siren (and the wave after it): people run to the nearest roof and stay there until the sky clears.
      // A camp with a roof counts, so a squad already under one does not walk off into the open. Mutants are not
      // people. Garrisons and road dens left this loop above. hold stays huge so arriving does not pick a new goal.
      const bool blow = em.phase == 1 || em.phase == 2;
      if (!blow) {
        if (s.hid) s.hid = false;
        if (s.hold > 1e8f) s.hold = 90.f + (float)rnd() * 30.f;
      } else if (s.faction != F_MUTANT) {
        const bool here = s.goal >= 0 && s.goal < (int)P.smarts.size() && smartShelters(P, s.goal)
                          && std::hypot(P.smarts[s.goal].x - s.x, P.smarts[s.goal].z - s.z) < 20.f;
        if (here) { s.hid = true; s.hold = 1e9f; }
        else {
          const int nb = nearestShelter(P, s.x, s.z);
          if (nb >= 0 && nb != s.goal) { s.goal = nb; s.job = 0; s.path.clear(); s.pathGoal = -2; }
          if (nb >= 0) s.hold = 1e9f;
          s.hid = false;
        }
      }
      if (s.goal < 0 || s.goal >= (int)P.smarts.size()) { s.goal = pickGoal(lv, s); s.job = 0; s.pathGoal = -2; }
      if (s.goal < 0 || s.goal >= (int)P.smarts.size()) continue;
      const Smart& g = P.smarts[s.goal];
      const float d = std::hypot(g.x - s.x, g.z - s.z);
      const float budget = WALK * step * (s.faction == F_MUTANT ? 1.3f : 1.f);
      // the shelter path stays a straight run: a graph detour would leave people in the open for the wave
      if (blow && s.faction != F_MUTANT && !s.hid) { s.job = 0; stepToward(s, (float)g.x, (float)g.z, budget); continue; }
      if (s.job == 2) { patrol(lv, s, g, budget); continue; }
      if (d <= ARRIVE) {
        if (s.job == 0) { s.job = 1; if (s.hold <= 0.f) s.hold = 90.f + (float)rnd() * 180.f; }
        s.x = (float)g.x; s.z = (float)g.z; s.path.clear();
        if (s.hold > 1e8f) continue;                 // under a roof, or a rest that must not pick a new goal
        if ((s.hold -= step) <= 0.f) { s.job = 2; s.leg = 0; }
      } else if (d < LOCAL_M) stepToward(s, (float)g.x, (float)g.z, budget);
      else { s.job = 0; travel(lv, s, budget); }
    }
    fights(lv, step);
    // ---- the emission's toll on those outside
    const i64 cyc = (i64)std::floor((lv.info.day * DAY_LEN + lv.info.time) / 2700);
    if (em.phase == 2 && em.strength > 0.8f && lastEmission != cyc) {
      lastEmission = cyc;
      int lost = 0;
      for (auto& s : squads) {
        if (s.online || s.goal < 0 || s.goal >= (int)P.smarts.size()) continue;   // online die by the wave itself; a road den (goal -1) is met in person
        const Smart& g = P.smarts[s.goal];
        const float d = std::hypot(g.x - s.x, g.z - s.z);
        bool covered;
        if (s.faction == F_MUTANT) covered = s.gar || (d < 20.f && (g.kind == SM_BASE || g.kind == SM_HUB || g.kind == SM_RUIN || g.kind == SM_LAIR));
        else {
          if (d < 20.f && smartShelters(P, s.goal)) s.hid = true;   // a roofed camp, not only a base
          covered = s.gar || s.hid;
        }
        if (!covered && rnd() < 0.7) { s.members -= 1 + (int)(rnd() * 2); lost++; }
      }
      pdaNewsAdd(lv, "The emission is over. " + std::to_string(lost) + " groups were caught in the open.");
      // packs breed back at their lairs, a fresh crop of artifacts waits — not under the player's feet
      for (int i = 0; i < (int)P.smarts.size(); i++) if (P.smarts[i].kind == SM_LAIR && rnd() < 0.35 && popAt(i) < maxPop(P.smarts[i]) && !playerWithin(lv, (float)P.smarts[i].x, (float)P.smarts[i].z, RESPAWN_RADIUS)) spawnAt(lv, i);
    }
    squads.erase(std::remove_if(squads.begin(), squads.end(), [&](const Squad& s) { return !s.online && s.members <= 0; }), squads.end());
    // try_respawn: under max population, idle elapsed, and the player outside respawn_radius
    respawnT -= step;
    if (respawnT <= 0) {
      respawnT = 240;
      garrisons(lv, false);
      const double clock = lv.info.day * DAY_LEN + lv.info.time;
      if (smartNext.size() != P.smarts.size()) smartNext.assign(P.smarts.size(), clock + RESPAWN_IDLE);
      for (int i = 0; i < (int)P.smarts.size(); i++) {
        const Smart& m = P.smarts[i];
        if (maxPop(m) <= 0 || clock < smartNext[i]) continue;
        smartNext[i] = clock + RESPAWN_IDLE;   // the attempt is spent, even when the player is standing here
        if (playerWithin(lv, (float)m.x, (float)m.z, RESPAWN_RADIUS)) continue;
        if (popAt(i) >= maxPop(m)) continue;
        spawnAt(lv, i);
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
          s.path.clear(); s.pathGoal = -2;   // the bodies moved; the next offline step rebuilds the graph path
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

bool smartShelters(const Plan& P, int index) {
  struct Cache { const Plan* plan = nullptr; std::vector<char> ok; };
  static Cache cache;
  if (cache.plan != &P) {
    cache.plan = &P;
    cache.ok.assign(P.smarts.size(), 0);
    for (int i = 0; i < (int)P.smarts.size(); i++) {
      const Smart& m = P.smarts[i];
      if (m.kind == SM_BASE || m.kind == SM_HUB || m.kind == SM_RUIN || m.kind == SM_LAIR) { cache.ok[i] = 1; continue; }
      if (m.kind != SM_CAMP) continue;
      for (int bi : P.near(m.x, m.z, 0)) {
        const Placed& p = P.s[bi];
        if (p.type == S_CAMP || p.type == S_STASH || p.type == S_HEAP || p.type == S_WRECK || p.type == S_FENCE_RING
            || p.type == S_PYLON || p.type == S_HELIPAD || p.type == S_HOME_HATCH) continue;
        if (m.x < p.x0 || m.x >= p.x0 + p.W || m.z < p.z0 || m.z >= p.z0 + p.D) continue;
        if (p.mc || p.h >= 4) { cache.ok[i] = 1; break; }
      }
    }
  }
  return index >= 0 && index < (int)cache.ok.size() && cache.ok[index];
}
int nearestShelter(const Plan& P, float x, float z) {
  int best = -1; float bd = 1e9f;
  for (int i = 0; i < (int)P.smarts.size(); i++) {
    if (!smartShelters(P, i)) continue;
    const float d = std::hypot(P.smarts[i].x - x, P.smarts[i].z - z);
    if (d < bd) { bd = d; best = i; }
  }
  return best;
}

float alifeGraphDistance(Level& lv, float x, float z) {
  if (!zoneWorld(lv)) return -1.f;
  return travelGraph(zonePlan((u32)lv.info.seed)).distanceTo(x, z);
}

std::vector<SquadView> alifeSquads(Level& lv) {
  std::vector<SquadView> out;
  if (Alife* a = alifeOf(lv)) for (auto& s : a->squads) {
    const Plan& P = zonePlan((u32)lv.info.seed);
    out.push_back({vec3(s.x, 0, s.z), s.faction, s.members, s.goal >= 0 && s.goal < (int)P.smarts.size() ? P.smarts[s.goal].name : "", s.online, s.id, s.species, s.hid});
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
