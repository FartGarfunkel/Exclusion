// Headless checks for the Zone's ground cover and the 100 Rads bar.
// Grass must come out in clumps: not a carpet, not the same mask on adjacent rows, not a constant stride.
// The Rostok hall at (-4498, 1324) must contain a real loot-furniture block (zone fridge, wardrobe, ...).
// Run from the build directory: build/test_zone_dress
#include "core/init.h"
#include "core/registry.h"
#include "gen/generator.h"
#include "common/noise.h"
#include "common/fs.h"
#include "world/chunk.h"
#include "world/dims.h"
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace dq;

static int fails = 0;
static void check(bool ok, const std::string& what) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) fails++;
}

static int floordiv16(int v) { return floordiv(v, 16); }

static bool isPlant(const std::string& k) {
  return k == "tall_grass" || k == "dry_grass" || k == "zone_tall_dry_grass" || k == "tall_grass_double"
      || k == "fern" || k == "large_fern" || k == "dead_bush" || k == "zone_dead_weeds" || k == "zone_shrub"
      || k == "zone_dandelion_clocks" || k == "zone_meadow_flowers" || k == "oxeye_daisy" || k == "azure_bluet";
}
static bool isGround(const std::string& k) {
  return k == "grass" || k == "dirt" || k == "dirt_with_dry_grass" || k == "coarse_dirt" || k == "podzol"
      || k == "dirt_with_coniferous_litter" || k == "zone_leaf_litter" || k == "moss_block" || k == "peat";
}
static bool isWater(const std::string& k) { return k == "water" || k == "ice"; }

struct GrassStats {
  int eligible = 0, plants = 0, identical = 0, strideRows = 0;
};

static GrassStats measureGrass(DimGenerator* gen, int cx0, int cz0) {
  const Registry& R = reg();
  std::unique_ptr<Chunk> tile[2][2];
  for (int dz = 0; dz < 2; dz++) for (int dx = 0; dx < 2; dx++) {
    tile[dz][dx] = std::make_unique<Chunk>(DIM_OVERWORLD, cx0 + dx, cz0 + dz);
    gen->generate(*tile[dz][dx]);
  }
  auto at = [&](int x, int y, int z) -> BlockId {
    if ((unsigned)y >= 256) return AIR;
    return tile[z >> 4][x >> 4]->get(x & 15, y, z & 15);
  };
  auto key = [&](int x, int y, int z) -> const std::string& { return R.block(at(x, y, z)).key; };
  bool plant[32][32] = {};
  bool elig[32][32] = {};
  GrassStats s;
  for (int z = 0; z < 32; z++) for (int x = 0; x < 32; x++) {
    int surf = -1;
    for (int y = 40; y < 140; y++) {
      if (!isGround(key(x, y, z))) continue;
      const std::string& above = key(x, y + 1, z);
      if (above == "air" || isPlant(above)) surf = y;
    }
    if (surf < 0) continue;
    bool wet = false;
    for (int k = 0; k < 4; k++) {
      const int nx = x + (k == 0 ? 1 : k == 1 ? -1 : 0), nz = z + (k == 2 ? 1 : k == 3 ? -1 : 0);
      if ((unsigned)nx >= 32 || (unsigned)nz >= 32) continue;
      if (isWater(key(nx, surf, nz)) || isWater(key(nx, surf + 1, nz))) wet = true;
    }
    if (wet) continue;
    elig[z][x] = true;
    s.eligible++;
    if (isPlant(key(x, surf + 1, z))) { plant[z][x] = true; s.plants++; }
  }
  // The old hash repeated a whole row. A clump may match its neighbour once; a stripe matches for many rows.
  int streak = 1;
  uint32_t prev = 0;
  bool have = false;
  for (int z = 0; z < 32; z++) {
    uint32_t mask = 0; int n = 0;
    for (int x = 0; x < 32; x++) if (plant[z][x]) { mask |= 1u << x; n++; }
    if (n >= 6 && have && mask == prev) { if (++streak > s.identical) s.identical = streak; }
    else streak = 1;
    if (n >= 6) { prev = mask; have = true; } else have = false;
  }
  for (int z = 0; z < 32; z++) {
    std::vector<int> xs;
    for (int x = 0; x < 32; x++) if (plant[z][x]) xs.push_back(x);
    if ((int)xs.size() < 6 || xs.back() - xs.front() < 16) continue;
    const int gap = xs[1] - xs[0];
    if (gap < 2 || gap > 6) continue;
    bool same = true;
    for (size_t i = 1; i < xs.size(); i++) if (xs[i] - xs[i - 1] != gap) same = false;
    if (same) s.strideRows++;
  }
  return s;
}

static int countFurniture(DimGenerator* gen) {
  const Registry& R = reg();
  auto furn = [](const std::string& k) {
    return k == "zone_fridge" || k == "zone_wardrobe" || k == "zone_locker" || k == "zone_desk" || k == "zone_cupboard"
        || k == "zone_bedside" || k == "zone_filing" || k == "zone_toolbox" || k == "zone_weapon_case"
        || k == "zone_ammo_box" || k == "zone_first_aid";
  };
  const int x0 = -4498, z0 = 1324, x1 = -4389, z1 = 1371;
  int n = 0;
  std::string seen;
  for (int cz = floordiv16(z0); cz <= floordiv16(z1); cz++) {
    for (int cx = floordiv16(x0); cx <= floordiv16(x1); cx++) {
      bool need = false;
      for (int dz = 0; dz < 16 && !need; dz++) for (int dx = 0; dx < 16 && !need; dx++) {
        const int wx = cx * 16 + dx, wz = cz * 16 + dz;
        if (wx >= x0 && wx <= x1 && wz >= z0 && wz <= z1) need = true;
      }
      if (!need) continue;
      // The dressed shed is the north end. Skip the rest of the hall so the check stays on that room.
      if (cz > floordiv16(1343) || cx < floordiv16(-4496) || cx > floordiv16(-4432)) continue;
      Chunk ch(DIM_OVERWORLD, cx, cz);
      gen->generate(ch);
      for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
        const int wx = cx * 16 + x, wz = cz * 16 + z;
        if (wx < x0 || wx > x1 || wz < z0 || wz > z1) continue;
        for (int y = 60; y <= 120; y++) {
          const std::string& k = R.block(ch.get(x, y, z)).key;
          if (!furn(k)) continue;
          n++;
          if (seen.find(k) == std::string::npos) { if (!seen.empty()) seen += ","; seen += k; }
        }
      }
    }
  }
  printf("bar furniture %d kinds: %s\n", n, seen.c_str());
  return n;
}

int main(int argc, char** argv) {
  std::string ud = argc > 1 ? argv[1] : (fs::temp_directory_path() / "dq_test_zone_dress").string();
  fs::remove_all(ud);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }
  WorldGenOptions opt;
  opt.worldType = "zone";
  opt.seed = seedFromString("hello");
  WorldGen world(opt);
  DimGenerator* gen = world.forDim(DIM_OVERWORLD);
  if (!gen) { printf("FAIL no zone generator\n"); return 1; }

  const int spots[][2] = {{-272, 243}, {-271, 249}, {-276, 236}};
  bool grassOk = false;
  for (auto& sp : spots) {
    GrassStats s = measureGrass(gen, sp[0], sp[1]);
    const double dens = s.eligible ? (double)s.plants / s.eligible : 0;
    printf("grass chunk %d,%d eligible %d plants %d density %.3f identical-rows %d stride-rows %d\n",
           sp[0], sp[1], s.eligible, s.plants, dens, s.identical, s.strideRows);
    if (s.eligible < 80 || s.plants < 24) continue;
    const bool shape = s.identical < 4 && s.strideRows < 2 && dens >= 0.08 && dens <= 0.42;
    if (shape) { grassOk = true; break; }
  }
  check(grassOk, "open grass is patchy: not a carpet, not identical rows, not a uniform stride");
  check(countFurniture(gen) >= 1, "100 Rads bar footprint contains a loot furniture block");
  printf("%s (%d fails)\n", fails ? "FAILED" : "passed", fails);
  return fails ? 1 : 0;
}
