// tools/test_shelter.cpp — headless emission check (no graphics).
// An NPC standing in the open starts toward the nearest roof when the siren does.
// A squad already at a camp that has a roof keeps its people when the wave hits.
//   ./build/test_shelter [--userdir DIR]
#include "core/init.h"
#include "core/registry.h"
#include "common/threadpool.h"
#include "sim/level.h"
#include "sim/player.h"
#include "sim/creature.h"
#include "sim/zone.h"
#include "gen/zone_map.h"
#include "gen/zone_plan.h"
#include "world/world.h"
#include <cmath>
#include <cstdio>
#include <string>

using namespace dq;
using namespace dq::zone;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) { g_pass++; printf("  ok   "); printf(__VA_ARGS__); printf("\n"); } \
  else { g_fail++; printf("  FAIL "); printf(__VA_ARGS__); printf("   [%s:%d]\n", __FILE__, __LINE__); } } while (0)

static bool areaLoaded(World& w, float x, float z, int R) {
  const int pcx = ifloor(x) >> 4, pcz = ifloor(z) >> 4;
  for (int dz = -R; dz <= R; dz++) for (int dx = -R; dx <= R; dx++) {
    if (dx * dx + dz * dz > R * R) continue;
    if (w.surfaceY((pcx + dx) * 16 + 8, (pcz + dz) * 16 + 8, DIM_OVERWORLD) < 0) return false;
  }
  return true;
}

static void streamAt(Level& lv, float x, float z, int R) {
  World& w = lv.world(REALM_MAIN);
  const vec3 anchor(x, dimBase(DIM_OVERWORLD) + 80.f, z);
  std::vector<StreamAnchor> a = {{anchor, R, false}};
  for (int i = 0; i < 4000 && !areaLoaded(w, x, z, R); i++) w.stream(a, 30);
}

static void sim(Level& lv, Player& p, double secs) {
  const double dt = TICK_DT;
  for (double t = 0; t < secs - 1e-9; t += dt) {
    p.hp = p.maxHp; p.dead = false;
    lv.tick(dt);
  }
}

// The same test the player uses: a solid block within 40 of the eyes. The bunker is not this realm.
static bool roofOver(Level& lv, float x, float y, float z) {
  World& w = lv.world(REALM_MAIN);
  const int y0 = ifloor(y + 1.62f) + 1;
  for (int yy = y0; yy < y0 + 40; yy++) {
    const BlockId b = w.getBlock(ifloor(x), yy, ifloor(z));
    if (b != AIR && reg().block(b).opaque) return true;
  }
  return false;
}

static bool buildingNear(const Plan& P, const Smart& m) {
  for (const Placed& p : P.s) {
    if (p.type == S_CAMP || p.type == S_STASH || p.type == S_HEAP || p.type == S_WRECK) continue;
    if (m.x >= p.x0 - 14 && m.x < p.x0 + p.W + 14 && m.z >= p.z0 - 14 && m.z < p.z0 + p.D + 14) return true;
  }
  for (const AnomalyField& f : P.fields)
    if (std::hypot(f.x - m.x, f.z - m.z) < f.r + 24.f) return true;
  return false;
}

int main(int argc, char** argv) {
  std::string ud = "/tmp/dq-test-shelter";
  for (int i = 1; i < argc; i++) { std::string a = argv[i]; if (a == "--userdir" && i + 1 < argc) ud = argv[++i]; }
  std::error_code ec; fs::remove_all(ud, ec); fs::create_directories(ud);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("initCore failed\n"); return 99; }
  EventBus bus;
  ThreadPool pool(2);
  LevelInfo info; info.name = "ShelterTest"; info.seedText = "hello"; info.mode = "survival"; info.worldType = "zone";
  auto lv = Level::create(fs::path(ud) / "saves", info, &pool, &bus);
  lv->info.rules["daylightCycle"] = false;
  lv->info.rules["emissions"] = true;

  const Plan& P = zonePlan((u32)lv->info.seed);
  int hut = -1, open = -1;
  for (int i = 0; i < (int)P.smarts.size(); i++) {
    if (P.smarts[i].name == "Forester's hut") hut = i;
  }
  for (int i = 0; i < (int)P.smarts.size(); i++) {
    const Smart& m = P.smarts[i];
    if (m.kind != SM_CAMP || m.garrison || m.name == "Forester's hut") continue;
    if (!insideZone(m.x - 16, m.z) || !insideZone(m.x + 16, m.z)) continue;
    if (buildingNear(P, m)) continue;
    open = i; break;
  }
  CHECK(hut >= 0, "the plan has the Forester's hut");
  CHECK(open >= 0, "the plan has an open camp clear of roofs and anomaly fields");
  if (hut < 0 || open < 0) { printf("%d passed, %d failed\n", g_pass, g_fail); return 1; }
  CHECK(smartShelters(P, hut), "the Forester's hut counts as a roof");
  CHECK(!smartShelters(P, open), "the open camp does not count as a roof");

  const Smart& H = P.smarts[hut];
  const Smart& O = P.smarts[open];
  printf("hut %d at %d %d (%s); open camp %d at %d %d (%s)\n", hut, H.x, H.z, H.name.c_str(), open, O.x, O.z, O.name.c_str());

  json squads = json::array();
  int next = 1;
  for (int n = 0; n < 4; n++) {
    squads.push_back(json{{"id", next}, {"f", (int)F_DRIFTER}, {"m", 4}, {"t", 1}, {"sp", ""},
                          {"x", (float)H.x}, {"z", (float)H.z}, {"g", hut}, {"h", hut}, {"hold", 5000.f}});
    next++;
  }
  const int openId = next;
  squads.push_back(json{{"id", openId}, {"f", (int)F_DRIFTER}, {"m", 3}, {"t", 1}, {"sp", ""},
                        {"x", (float)O.x}, {"z", (float)O.z}, {"g", open}, {"h", open}, {"hold", 5000.f}});
  next++;
  lv->info.data["alife"] = json{{"squads", squads}, {"next", next}};

  auto pl = std::make_unique<Player>(); pl->name = "Tester"; pl->realm = REALM_MAIN;
  pl->pos = vec3(O.x + 0.5f, dimBase(DIM_OVERWORLD) + 80.f, O.z + 0.5f);
  Player* p = static_cast<Player*>(lv->spawn(std::move(pl)));

  printf("streaming the open camp\n");
  streamAt(*lv, (float)O.x, (float)O.z, 4);
  World& w = lv->world(REALM_MAIN);
  const int gy = dimBase(DIM_OVERWORLD) + (int)std::lround(groundAt((u32)lv->info.seed, O.x, O.z).h);
  const BlockId stone = reg().blockId("stone");
  const int rx = O.x + 9, rz = O.z;
  for (int dz = -36; dz <= 36; dz++) for (int dx = -36; dx <= 36; dx++) {
    w.setBlock(O.x + dx, gy, O.z + dz, stone, 0, SetFlags::NoNotify | SetFlags::NoRemesh | SetFlags::NoLight);
    for (int y = 1; y <= 45; y++) w.setBlock(O.x + dx, gy + y, O.z + dz, AIR, 0, SetFlags::NoNotify | SetFlags::NoRemesh | SetFlags::NoLight);
  }
  for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++)
    w.setBlock(rx + dx, gy + 3, rz + dz, stone, 0, SetFlags::NoNotify | SetFlags::NoRemesh | SetFlags::NoLight);

  const int top = w.surfaceY(O.x, O.z, DIM_OVERWORLD);
  p->pos = p->prevPos = vec3(O.x + 0.5f, top + 1.05f, O.z + 0.5f);
  p->vel = vec3(0); p->realm = REALM_MAIN; p->dead = false; p->hp = p->maxHp = 20; p->onGround = true;
  printf("pad surface %d, ground block %d, roof at %d %d %d\n", top, gy, rx, gy + 3, rz);

  sim(*lv, *p, 3.5);
  Creature* npc = nullptr;
  float npcD = 1e9f;
  int stalkers = 0;
  for (auto& e : lv->entities()) {
    auto* c = dynamic_cast<Creature*>(e.get());
    if (!c || c->removed || c->hp <= 0 || c->data.value("alife", -1) != openId) continue;
    stalkers++;
    const float d = std::hypot(c->pos.x - (rx + 0.5f), c->pos.z - (rz + 0.5f));
    if (!npc || d < npcD) { npc = c; npcD = d; }
  }
  CHECK(stalkers >= 1, "a stalker came online at the open camp (%d)", stalkers);
  if (!npc) { printf("%d passed, %d failed\n", g_pass, g_fail); return 1; }
  const bool openSky = !roofOver(*lv, npc->pos.x, npc->pos.y, npc->pos.z);
  CHECK(openSky, "that stalker is in the open (sitting %d, at %.1f %.1f)", npc->sitting ? 1 : 0, npc->pos.x, npc->pos.z);
  const float d0 = std::hypot(npc->pos.x - (rx + 0.5f), npc->pos.z - (rz + 0.5f));
  const u32 npcId = npc->id;

  // Warning: world clock 4110, just as the siren starts (wave at 1500, cycle 2700, warn 90).
  lv->info.day = 3; lv->info.time = 510;
  const Emission warn = emission(*lv);
  CHECK(warn.phase == 1, "the clock is in the warning (phase %d)", warn.phase);
  sim(*lv, *p, 2.5);
  npc = nullptr;
  for (auto& e : lv->entities()) if (e && e->id == npcId) npc = dynamic_cast<Creature*>(e.get());
  CHECK(npc && npc->hp > 0 && !npc->removed, "the stalker is still alive after the siren");
  if (npc) {
    const float d1 = std::hypot(npc->pos.x - (rx + 0.5f), npc->pos.z - (rz + 0.5f));
    const bool under = roofOver(*lv, npc->pos.x, npc->pos.y, npc->pos.z);
    printf("  stalker moved %.2f m toward the roof (%.2f -> %.2f), sitting %d, under roof %d\n", d0 - d1, d0, d1, npc->sitting ? 1 : 0, under ? 1 : 0);
    CHECK(d1 < d0 - 3.f, "the stalker in the open moves toward cover when the warning starts (closed %.2f m)", d0 - d1);
  }

  // The wave itself. Squads at the hut never came online; a roofed camp must not lose anyone.
  lv->info.day = 1; lv->info.time = 320;
  const Emission wave = emission(*lv);
  CHECK(wave.phase == 2 && wave.strength > 0.8f, "the clock is in the wave (phase %d strength %.2f)", wave.phase, wave.strength);
  sim(*lv, *p, 3.0);
  int hutSquads = 0, hutMembers = 0, hutSheltered = 0, hutNamed = 0;
  for (const SquadView& s : alifeSquads(*lv)) {
    if (s.id < 1 || s.id > 4) continue;
    hutSquads++;
    hutMembers += s.members;
    if (s.sheltered) hutSheltered++;
    if (s.goal == "Forester's hut") hutNamed++;
    printf("  hut squad %d members %d goal %s sheltered %d at %.0f %.0f\n", s.id, s.members, s.goal.c_str(), s.sheltered ? 1 : 0, s.pos.x, s.pos.z);
  }
  CHECK(hutSquads == 4 && hutMembers == 16, "a squad at a camp with a roof survives the wave (%d squads, %d people)", hutSquads, hutMembers);
  CHECK(hutNamed == 4 && hutSheltered == 4, "those squads stayed at the hut and were marked sheltered (%d named, %d sheltered)", hutNamed, hutSheltered);
  bool told = false;
  for (const std::string& line : pdaNews(*lv)) if (line.find("caught in the open") != std::string::npos) told = true;
  CHECK(told, "the emission still reports who was caught in the open");

  printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
