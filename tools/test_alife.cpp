// tools/test_alife.cpp — headless A-Life check (no graphics). A player leaves the Rookie Village,
// walks the Cordon road and the Garbage track, and meets mutant packs: dogs and rats on the way out,
// a tusker further along. The camp itself stays clear of mutants; its stalker garrison stays.
//   ./build/test_alife [--userdir DIR]
// Default render distance is 8 chunks (~128 m). Packs only become real where that ground is loaded
// and they stand within ONLINE_R, so this streams the same radius the game does.
#include "core/init.h"
#include "common/threadpool.h"
#include "sim/level.h"
#include "sim/player.h"
#include "sim/zone.h"
#include "gen/zone_map.h"
#include "gen/zone_plan.h"
#include <cmath>
#include <chrono>
#include <cstdio>
#include <string>

using namespace dq;
using namespace dq::zone;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) { g_pass++; printf("  ok   "); printf(__VA_ARGS__); printf("\n"); } \
  else { g_fail++; printf("  FAIL "); printf(__VA_ARGS__); printf("   [%s:%d]\n", __FILE__, __LINE__); } } while (0)

static float roadDist(float x, float z) {
  float best = 1e30f;
  for (const Line& l : roads()) {
    if (l.kind == 3 || l.pts.size() < 2) continue;
    best = std::min(best, distToLine(l, x, z));
  }
  return best;
}

static int living(Level& lv, const std::string& type, float x, float z, float r) {
  int n = 0;
  for (auto& e : lv.entities()) {
    if (!e || e->removed || e->hp <= 0 || e->realm != REALM_MAIN) continue;
    if (e->type != type) continue;
    if (std::hypot(e->pos.x - x, e->pos.z - z) <= r) n++;
  }
  return n;
}

static int livingMutants(Level& lv, float x, float z, float r) {
  int n = 0;
  for (auto& e : lv.entities()) {
    if (!e || e->removed || e->hp <= 0 || e->realm != REALM_MAIN) continue;
    if (e->type.rfind("z_", 0) != 0 || e->type.rfind("z_npc_", 0) == 0) continue;
    if (std::hypot(e->pos.x - x, e->pos.z - z) <= r) n++;
  }
  return n;
}

// Chunks the default 8-chunk view would have loaded around (x, z).
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

static void stand(Player& p, Level& lv, float x, float z) {
  streamAt(lv, x, z, 8);
  int y = lv.world(REALM_MAIN).surfaceY(ifloor(x), ifloor(z), DIM_OVERWORLD);
  if (y < 0) y = dimBase(DIM_OVERWORLD) + (int)std::round(groundAt((u32)lv.info.seed, (int)x, (int)z).h);
  p.pos = p.prevPos = vec3(x, y + 1.05f, z);
  p.vel = vec3(0); p.realm = REALM_MAIN; p.dead = false; p.hp = p.maxHp = 20; p.onGround = true;
}

static void sim(Level& lv, Player& p, double secs) {
  const double dt = TICK_DT;
  for (double t = 0; t < secs - 1e-9; t += dt) {
    p.hp = p.maxHp; p.dead = false;
    lv.tick(dt);
  }
}

int main(int argc, char** argv) {
  std::string ud = "/tmp/dq-test-alife";
  for (int i = 1; i < argc; i++) { std::string a = argv[i]; if (a == "--userdir" && i + 1 < argc) ud = argv[++i]; }
  std::error_code ec; fs::remove_all(ud, ec); fs::create_directories(ud);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("initCore failed\n"); return 99; }
  EventBus bus;
  ThreadPool pool(3);
  LevelInfo info; info.name = "AlifeTest"; info.seedText = "hello"; info.mode = "survival"; info.worldType = "zone";
  auto lv = Level::create(fs::path(ud) / "saves", info, &pool, &bus);
  auto pl = std::make_unique<Player>(); pl->name = "Tester"; pl->realm = REALM_MAIN;
  pl->pos = vec3(SPAWN_X + 0.5f, dimBase(DIM_OVERWORLD) + 80.f, SPAWN_Z + 0.5f);
  Player* p = static_cast<Player*>(lv->spawn(std::move(pl)));
  lv->tick(TICK_DT);   // A-Life populates on the first tick

  const auto squads = alifeSquads(*lv);
  int mutants = 0, nearSpawn = 0, cordonRoad = 0, garbageRoad = 0;
  float nearSpawnD = 1e9f; std::string nearSpawnSp;
  bool cordonDog = false, cordonRat = false, garbageTusk = false;
  SquadView dog{}, rat{}, tusk{};
  float dogD = 1e9f, ratD = 1e9f, tuskD = 1e9f;
  for (const SquadView& s : squads) {
    if (s.faction != F_MUTANT || s.members <= 0) continue;
    mutants++;
    const float ds = std::hypot(s.pos.x - SPAWN_X, s.pos.z - SPAWN_Z);
    const float rd = roadDist(s.pos.x, s.pos.z);
    const Region* r = regionAt((int)s.pos.x, (int)s.pos.z);
    if (ds < nearSpawnD) { nearSpawnD = ds; nearSpawnSp = s.species; }
    if (ds < 170) nearSpawn++;
    if (r && r->kind == K_VILLAGE && rd < 80 && ds > 150 && ds < 1100) {
      cordonRoad++;
      if (s.species == "z_scab_dog" && ds < dogD) { cordonDog = true; dog = s; dogD = ds; }
      if (s.species == "z_rad_rat" && ds < ratD) { cordonRat = true; rat = s; ratD = ds; }
    }
    if (r && r->kind == K_SCRAPYARD && rd < 80 && s.species == "z_tusker" && ds < tuskD) { garbageRoad++; garbageTusk = true; tusk = s; tuskD = ds; }
  }
  printf("roads %zu, squads %zu, mutant packs %d\n", roads().size(), squads.size(), mutants);
  printf("nearest mutant pack to the Rookie Village: %s at %.0f m\n", nearSpawnSp.empty() ? "(none)" : nearSpawnSp.c_str(), nearSpawnD);
  printf("mutant packs within 170 m of spawn: %d; beside the Cordon road (150-1100 m out): %d; tusker packs beside the Garbage track: %d\n",
         nearSpawn, cordonRoad, garbageTusk ? 1 : 0);
  const Region* garbage = regionById("scrapyard");
  for (const SquadView& s : squads) {
    if (s.faction != F_MUTANT || s.members <= 0) continue;
    const float ds = std::hypot(s.pos.x - SPAWN_X, s.pos.z - SPAWN_Z);
    if (ds > 1600.f || roadDist(s.pos.x, s.pos.z) > 80.f) continue;
    const Region* r = regionAt((int)s.pos.x, (int)s.pos.z);
    if (!r || (r->kind != K_VILLAGE && r->kind != K_SCRAPYARD)) continue;
    const float dg = garbage ? std::hypot(s.pos.x - garbage->cx, s.pos.z - garbage->cz) : -1.f;
    printf("  road pack %s x%d at %.0f %.0f (%.0f m from the village, %.0f m from the Garbage, %s)\n",
           s.species.c_str(), s.members, s.pos.x, s.pos.z, ds, dg, r->name);
  }
  CHECK(mutants > 0, "the map has mutant packs on paper (%d)", mutants);
  CHECK(nearSpawn == 0, "the camp fire is clear on paper (%d packs within 170 m)", nearSpawn);
  CHECK(cordonDog && cordonRat, "dogs and rats den beside the Cordon road, past the village (dog %d rat %d)", cordonDog, cordonRat);
  CHECK(garbageTusk, "a tusker pack dens beside the Garbage track");
  // The road leaves the village for the highway, then runs north. A den on a side track
  // (the Meadow, the swamps) is not that walk: these three step toward the Garbage's spine.
  if (garbage && cordonDog && cordonRat && garbageTusk) {
    const float spawnG = std::hypot(SPAWN_X - garbage->cx, SPAWN_Z - garbage->cz);
    const float dogG = std::hypot(dog.pos.x - garbage->cx, dog.pos.z - garbage->cz);
    const float ratG = std::hypot(rat.pos.x - garbage->cx, rat.pos.z - garbage->cz);
    const float tuskG = std::hypot(tusk.pos.x - garbage->cx, tusk.pos.z - garbage->cz);
    CHECK(dogG < spawnG - 40.f && dog.pos.z < SPAWN_Z - 40.f, "the dogs are north of the village, toward the Garbage (%.0f m closer)", spawnG - dogG);
    CHECK(ratG < dogG - 120.f && rat.pos.z < dog.pos.z, "the rats are further up that road (%.0f m closer to the Garbage than the dogs)", dogG - ratG);
    CHECK(tuskG < ratG - 120.f && tusk.pos.z < rat.pos.z, "the tusker is further still (%.0f m from the Garbage)", tuskG);
    CHECK(std::abs(dog.pos.x - garbage->cx) < 160.f && std::abs(rat.pos.x - garbage->cx) < 160.f && std::abs(tusk.pos.x - garbage->cx) < 160.f,
          "the packs sit on the road north, not a side track (x %.0f %.0f %.0f)", dog.pos.x, rat.pos.x, tusk.pos.x);
  }

  auto visit = [&](const char* what, const SquadView& s, const char* type, float fromSpawn) {
    if (s.members <= 0) { CHECK(false, "%s pack to visit", what); return; }
    printf("walking to %s at %.0f %.0f (%.0f m from the village)\n", what, s.pos.x, s.pos.z, fromSpawn);
    const auto t0 = std::chrono::steady_clock::now();
    stand(*p, *lv, s.pos.x + 24.f, s.pos.z + 8.f);
    const double genS = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const int top = lv->world(REALM_MAIN).surfaceY(ifloor(p->pos.x), ifloor(p->pos.z), DIM_OVERWORLD);
    printf("  ground loaded in %.1f s, surfaceY %d, chunks %zu, player %.0f m from spawn\n", genS, top, lv->world(REALM_MAIN).chunks().size(),
           std::hypot(p->pos.x - SPAWN_X, p->pos.z - SPAWN_Z));
    sim(*lv, *p, 6);
    const int n = living(*lv, type, s.pos.x, s.pos.z, 55);
    sim(*lv, *p, 4);
    const int still = living(*lv, type, s.pos.x, s.pos.z, 70);
    CHECK(n >= 1, "%s came online beside the player (%d %s within 55 m)", what, n, type);
    CHECK(still >= 1, "%s still alive 4 s later (%d)", what, still);
  };

  // The camp: stalkers who live there stay; mutants do not mob the fire.
  {
    printf("at the Rookie Village\n");
    const auto t0 = std::chrono::steady_clock::now();
    stand(*p, *lv, SPAWN_X + 0.5f, SPAWN_Z + 0.5f);
    printf("  village ground loaded in %.1f s, chunks %zu\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), lv->world(REALM_MAIN).chunks().size());
    sim(*lv, *p, 8);
    const int stalkers = living(*lv, "z_npc_drifter", SPAWN_X, SPAWN_Z, 50);
    const int mut = livingMutants(*lv, SPAWN_X, SPAWN_Z, 90);
    CHECK(stalkers >= 1, "the Rookie Village garrison is still there (%d loners within 50 m)", stalkers);
    CHECK(mut == 0, "no mutant swarm at the camp (%d within 90 m)", mut);
  }

  const float moved = std::hypot(p->pos.x - SPAWN_X, p->pos.z - SPAWN_Z);
  printf("player has moved %.0f m from the camp fire\n", moved);
  visit("Cordon dogs", dog, "z_scab_dog", dogD);
  CHECK(std::hypot(p->pos.x - SPAWN_X, p->pos.z - SPAWN_Z) > 120.f, "the dog pack came online after leaving the village (%.0f m out)", std::hypot(p->pos.x - SPAWN_X, p->pos.z - SPAWN_Z));
  visit("Cordon rats", rat, "z_rad_rat", ratD);
  visit("Garbage tusker", tusk, "z_tusker", tuskD);

  printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
