// gen/zone_gen.cpp — the "zone" world type: the Overworld band is the Zone (gen/zone_map.h, zone_plan.h); the
// other three bands are empty. Terrain, water, roads and the railway, the border fence, every building from the
// site plan, forests and undergrowth, and the scorched ground of anomaly fields, chunk by chunk.
#include "gen/dimgens.h"
#include "gen/zone_plan.h"
#include "gen/zone_dress.h"
#include "gen/zone_details.h"
#include "sim/zone_anomalies.h"
#include "gen/zone_layers.h"
#include "gen/zone_world.h"
#include "world/dirs.h"
#include "core/registry.h"
#include "common/noise.h"
#include <cmath>
#include <algorithm>

namespace dq {
namespace zone {
void zoneWorldBuild(Ctx& c, const WTile& t, const Ground* G, u8* occupied);
u32 zoneWorldTop(const WTile& t, int i, int& topY);
namespace {

inline float sstep(float t) { t = std::clamp(t, 0.f, 1.f); return t * t * (3 - 2 * t); }

class ZoneGen : public DimGenerator {
public:
  explicit ZoneGen(const WorldGenOptions& o) : seed_((u32)o.seed), grove_((u32)o.seed ^ 0x6a09e667u) { B.init(); lily_ = reg().blockId("lily_pad"); sphereShell_ = reg().blockId("sea_lantern"); if (!sphereShell_) sphereShell_ = B.glowstone; plan_ = &zonePlan(seed_); }

  void generate(Chunk& ch) override {
    const int ox = ch.cx * CS, oz = ch.cz * CS;
    Ground G[256];
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) G[z * 16 + x] = groundAt(seed_, ox + x, oz + z);
    // drab climate: dry olive grass, rust-brown in the Red Forest, darker by the marsh
    for (int i = 0; i < 256; i++) {
      const Region* r = G[i].region; u8 t = 168, h = 70;
      if (r && r->kind == K_RED_FOREST && G[i].rw > 0.15f) { t = 250; h = 8; }
      else if (r && r->kind == K_MARSH) { t = 150; h = 150; }
      else if (r && (r->kind == K_NPP || r->kind == K_GENERATORS)) { t = 190; h = 40; }
      else if (hashFloat((i32)seed_, (ox + (i & 15)) / 48, (oz + (i >> 4)) / 48, 17) < 0.22) { t = 215; h = 28; }   // autumn-tinted stands
      ch.climate[i * 2] = t; ch.climate[i * 2 + 1] = h;
    }
    u8 occupied[256] = {};
    // the user's world under this chunk (a chunk is always inside one tile)
    const std::shared_ptr<const WTile> T = worldTileAt(ox, oz);
    WCol C[256]; bool has[256];
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) has[z * 16 + x] = T && worldColumn(*T, ox + x, oz + z, C[z * 16 + x]);
    int Hs[256], slope[256];
    for (int i = 0; i < 256; i++) Hs[i] = (int)std::round(G[i].h);
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
      int s = 0; const int h = Hs[z * 16 + x];
      if (x > 0) s = std::max(s, std::abs(h - Hs[z * 16 + x - 1])); if (x < 15) s = std::max(s, std::abs(h - Hs[z * 16 + x + 1]));
      if (z > 0) s = std::max(s, std::abs(h - Hs[z * 16 + x - 16])); if (z < 15) s = std::max(s, std::abs(h - Hs[z * 16 + x + 16]));
      slope[z * 16 + x] = s;
    }
    // ---- ground and water: the world's surface in our blocks
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
      const int i = z * 16 + x;
      const Ground& g = G[i];
      const int wx = ox + x, wz = oz + z, H = Hs[i];
      const double n = hashFloat((i32)seed_, wx, wz, 3);
      BlockId top = B.grass, under = B.dirt;
      if (has[i]) surface(g, C[i], wx, wz, n, slope[i], top, under);
      else if (n < 0.3) top = B.dryDirt;
      if (g.cut) { top = n < 0.55 ? B.mossy : B.cobble; under = B.cobble; }   // a pad's stone-faced bank
      column(ch, x, z, H, top, under);
      if (g.road == 1 && !g.water && top == B.zAsphalt) {
        const bool edge = edgeOfRoad(G, C, has, x, z);
        const Kind rk = g.region ? g.region->kind : K_WILD;
        if (edge && n < 0.75 && (rk == K_CITY || rk == K_CLOSED_TOWN || rk == K_GHOST_TOWN || rk == K_HUB || rk == K_JUPITER || rk == K_NPP)) ch.set(x, H, z, B.curb);   // kerbs in the towns
        else roadSurface(ch, x, z, H, wx, wz, edge ? 3.f : 0.f, n);
      }
      if (g.road) occupied[i] = 1;
      if (g.water) for (int y = H + 1; y <= g.waterY; y++) ch.set(x, y, z, B.water);
      // the tracks laid between places: two ruts with grass up the middle, a plank bridge over water
      if ((g.road == 2 || g.water) && !(has[i] && (C[i].nruns || worldSurfRoad(C[i])))) {
        const float td = trackDist(wx, wz);
        if (td < 0) {
          occupied[i] = 1;
          if (g.water) {
            ch.set(x, g.waterY + 1, z, B.plank); ch.setMeta(x, g.waterY + 1, z, 0);
            if (((wx * 7 + wz * 3) & 7) == 0) for (int y = H + 1; y <= g.waterY; y++) ch.set(x, y, z, B.log);
          } else {
            const bool mid = td < -1.2f;
            ch.set(x, H, z, mid ? (n < 0.6 ? B.dryDirt : B.grass) : n < 0.6 ? B.path : n < 0.85 ? B.coarse : B.mud);
            for (int y = H + 1; y <= H + 4; y++) ch.set(x, y, z, AIR);
            if (mid && n < 0.8) ch.set(x, H + 1, z, dryAt(wx, wz) < dryShare(g.region ? g.region->kind : K_WILD, g.rw) ? B.dryGrass : n < 0.12 ? B.zGrassMid : B.tallGrass);   // the strip up the middle
          }
        }
      }
      // the fence round the Zone (roads go through: a gap where one crosses)
      const int edge = std::min({wx - MIN_X, MAX_X - wx, wz - MIN_Z, MAX_Z - wz});
      if (edge >= 24 && edge <= 25 && !g.road) {
        occupied[i] = 1;
        for (int y = H + 1; y <= H + 3; y++) ch.set(x, y, z, B.zFence);
        if (edge == 24) ch.set(x, H + 4, z, B.zBarbed);
      }
      if (edge < 24) occupied[i] = 1;
    }
    Ctx c{ch, ox, oz, B, seed_};
    // ---- the user's buildings (gen/zone_world_blocks.cpp)
    if (T) zoneWorldBuild(c, *T, G, occupied);
    // ---- anomaly fields: scorched and churned ground
    std::vector<Anomaly> an;
    if (auto it = plan_->fieldGrid.find(cellKey(ox >> 6, oz >> 6)); it != plan_->fieldGrid.end())
      for (int fi : it->second) fieldAnomalies(seed_, plan_->fields[fi], an);
    for (const Anomaly& a : an) {
      const int R = (int)std::ceil(a.r + 1);
      for (int dz = -R; dz <= R; dz++) for (int dx = -R; dx <= R; dx++) {
        const int x = a.x + dx - ox, z = a.z + dz - oz;
        if ((unsigned)x >= 16 || (unsigned)z >= 16) continue;
        if (dx * dx + dz * dz > (a.r + 0.5f) * (a.r + 0.5f)) continue;
        const int H = Hs[z * 16 + x];
        if (G[z * 16 + x].water || G[z * 16 + x].road || (has[z * 16 + x] && C[z * 16 + x].nruns)) continue;
        const double n = hashFloat((i32)seed_, a.x + dx, a.z + dz, 5);
        BlockId s = a.family == A_THERMAL ? (n < 0.3 ? B.magma : B.coarse) : a.family == A_CHEMICAL ? (n < 0.5 ? B.mud : B.moss)
                  : a.family == A_GRAVITY ? (n < 0.5 ? B.gravel : B.coarse) : a.family == A_ELECTRIC ? (n < 0.4 ? B.andesite : B.coarse) : B.coarse;
        ch.set(x, H, z, s);
        occupied[z * 16 + x] = 2;   // no plants in an anomaly
      }
    }
    dressAnomalies(ch, ox, oz, B, seed_, G, occupied);   // each anomaly's own ground: pools, scorch, glass, poppies, the moss tree (sim/zone_anomalies.cpp)
    // ---- our own buildings (what the user's world hasn't got)
    std::vector<int> here;
    for (int cz = oz >> 6; cz <= (oz + 15) >> 6; cz++) for (int cx = ox >> 6; cx <= (ox + 15) >> 6; cx++) {
      auto it = plan_->grid.find(cellKey(cx, cz)); if (it == plan_->grid.end()) continue;
      for (int i : it->second) if (!plan_->s[i].mc && std::find(here.begin(), here.end(), i) == here.end()) here.push_back(i);
    }
    std::sort(here.begin(), here.end());
    for (int i : here) {
      Placed p = plan_->s[i];
      if (p.x0 > ox + 15 + 8 || p.x0 + p.W < ox - 8 || p.z0 > oz + 15 + 8 || p.z0 + p.D < oz - 8) continue;
      // the unlevelled ones stand on the real ground at their middle (the same in every chunk)
      if (p.type == S_CAMP || p.type == S_STASH || p.type == S_WRECK || p.type == S_HEAP) p.y = (int)groundAt(seed_, p.x0 + p.W / 2, p.z0 + p.D / 2).h + 1;
      // the pad: clear the footprint above the floor and lay foundations under it
      const int x0 = std::max(p.x0, ox), x1 = std::min(p.x0 + p.W - 1, ox + 15), z0 = std::max(p.z0, oz), z1 = std::min(p.z0 + p.D - 1, oz + 15);
      for (int wz = z0; wz <= z1; wz++) for (int wx = x0; wx <= x1; wx++) {
        occupied[(wz - oz) * 16 + (wx - ox)] = 1;
        if (!p.pad) continue;
        const int x = wx - ox, z = wz - oz;
        for (int y = p.y; y < std::min(255, p.y + std::max(p.h, 6)); y++) ch.set(x, y, z, AIR);
        for (int y = p.y - 1; y > p.y - 10 && y > 0; y--) { const BlockId b = ch.get(x, y, z); if (b != AIR && b != B.water) break; ch.set(x, y, z, B.cobble); }
        ch.set(x, p.y - 1, z, p.type == S_HUT || p.type == S_BARN ? B.coarse : B.concrete2);
      }
      buildStructure(c, p);
    }
    // ---- the Generators' energy spheres: a glowing ball on a steel frame
    for (const auto& sp : plan_->spheres) {
      if (sp[0] < ox - 8 || sp[0] > ox + 23 || sp[2] < oz - 8 || sp[2] > oz + 23) continue;
      const int gy = sp[1];
      for (int dz = -4; dz <= 4; dz++) for (int dx = -4; dx <= 4; dx++) {
        const int x = sp[0] + dx - ox, z = sp[2] + dz - oz;
        if ((unsigned)x >= 16 || (unsigned)z >= 16) continue;
        occupied[z * 16 + x] = 2;
        if (std::abs(dx) == 3 && std::abs(dz) == 3) for (int y = gy + 1; y <= gy + 7; y++) ch.set(x, y, z, B.steel);   // the legs
        if ((std::abs(dx) == 3 && std::abs(dz) <= 3) || (std::abs(dz) == 3 && std::abs(dx) <= 3)) { ch.set(x, gy + 7, z, B.steel); ch.set(x, gy, z, B.concrete2); }
        for (int y = gy + 8; y <= gy + 14; y++) { const float r2 = dx * dx + dz * dz + (y - gy - 11) * (y - gy - 11); if (r2 <= 8.5f) ch.set(x, y, z, r2 <= 3.f ? B.glowstone : sphereShell_); }
      }
    }
    // ---- the user's trees (ours), the roadside (power lines, lamps), undergrowth
    trees(ch, ox, oz, G, occupied, an);
    zoneRoadside(ch, ox, oz, B, seed_, *plan_);
    zoneDetailsChunk(c, G, occupied);   // (alpha.10) the vehicles' collision cells, the railways, the block work (gen/zone_details.cpp)
    undergrowth(ch, ox, oz, G, occupied, C, has);
    keepPassagesClear(ch);
  }

  // (alpha.9) nothing we place may block a way through: a prop or a piece of loot furniture (ours, or what stands where they
  // had a chest) at a ladder's foot or top, in front of a door, on or under a trapdoor or over a stair step goes (both halves
  // of a two-tall one). Inside the chunk (a passage at its edge keeps what stands across the border)
  void keepPassagesClear(Chunk& ch) const {
    static const std::vector<u8> cls = [] {
      const Registry& R = reg(); std::vector<u8> v(R.blocks.size(), 0);
      static const char* PROPS[] = {"zone_barrel", "zone_barrel_blue", "zone_tyres", "zone_sandbags", "zone_fire_barrel", "barrel", "chest", "loose_stones", "zone_junk"};
      for (const BlockDef& b : R.blocks) {
        const int sk = R.tShape[b.id];
        if (sk == ShapeKind::Ladder || sk == ShapeKind::Door || sk == ShapeKind::Trapdoor || sk == ShapeKind::Stairs) { v[b.id] = 1; continue; }
        bool prop = b.has("container") || b.has("furnTop") || b.has("zoneBreak") || b.has("placedBackpack");
        for (const char* k : PROPS) if (b.key == k) prop = true;
        if (prop && b.id != AIR) v[b.id] = 2;
      }
      return v;
    }();
    const Registry& R = reg();
    auto at = [&](int x, int y, int z) -> BlockId { return (unsigned)x < 16 && (unsigned)z < 16 && (unsigned)y < 256 ? ch.get(x, y, z) : AIR; };
    auto isProp = [&](BlockId b) { return b < cls.size() && cls[b] == 2; };
    auto take = [&](int x, int y, int z) {
      const BlockId b = at(x, y, z);
      if (!isProp(b)) return;
      ch.set(x, y, z, AIR); ch.blockEntities.erase(colIdx(x, y, z));
      if (R.block(b).has("furnTop") && isProp(at(x, y - 1, z))) { ch.set(x, y - 1, z, AIR); ch.blockEntities.erase(colIdx(x, y - 1, z)); }
      if (y + 1 < 256 && R.block(at(x, y + 1, z)).has("furnTop")) ch.set(x, y + 1, z, AIR);
    };
    auto ring = [&](int x, int y, int z) { for (int k = 0; k < 4; k++) take(x + DIRX[k], y, z + DIRZ[k]); };
    for (int y = 1; y < 254; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
      const BlockId b = ch.get(x, y, z);
      if (b >= cls.size() || cls[b] != 1) continue;
      switch (R.tShape[b]) {
        case ShapeKind::Ladder:
          if (at(x, y - 1, z) != b) ring(x, y, z);                                          // its foot
          if (at(x, y + 1, z) != b) { take(x, y + 1, z); take(x, y + 2, z); ring(x, y + 1, z); }   // its top: the way off it
          break;
        case ShapeKind::Door: ring(x, y, z); break;
        case ShapeKind::Trapdoor: take(x, y + 1, z); take(x, y + 2, z); take(x, y - 1, z); break;
        default: if (!(ch.getMeta(x, y, z) & 4)) { take(x, y + 1, z); take(x, y + 2, z); } break;   // stairs: headroom over each step (not under-stairs)
      }
    }
  }

  void sample(int wx, int wz, ColumnSample& s) const override {
    s = ColumnSample();
    const Ground g = groundAt(seed_, wx, wz);
    s.height = (i16)std::round(g.h);
    s.top = g.road == 1 ? B.zAsphalt : g.road ? B.gravel : g.water ? B.mud : B.grass;
    s.under = B.dirt;
    if (g.water) { s.water = (i16)g.waterY; s.fluid = B.water; }
    s.temp = 168; s.humid = 70;
    if (g.region && g.region->kind == K_RED_FOREST && g.rw > 0.15f) { s.temp = 250; s.humid = 8; }
    // the user's buildings from afar: their top block
    if (auto t = worldTileAt(wx, wz)) {
      int ty = 0; const BlockId b = zoneWorldTop(*t, WTile::at(wx, wz), ty);
      if (b && ty > s.height && (int)std::round(g.h) == t->ground[WTile::at(wx, wz)]) { s.height = (i16)ty; s.top = b; s.under = b; s.water = -1; return; }
    }
    if (!g.water && !g.road && insideZone(wx, wz)) {   // the woods from afar: a dark floor and a lumpy canopy as tall as the trees
      const double f = forestAt(wx, wz);
      if (f > 0.3) s.top = g.region && g.region->kind == K_RED_FOREST ? B.podzol : B.zLitter;
      if (hashFloat((i32)seed_, wx, wz, 29) < f * 0.9 - 0.1) {
        const bool conifer = (g.region && g.region->kind == K_RED_FOREST) || hashFloat((i32)seed_, wx >> 4, wz >> 4, 24) < 0.45;
        s.tree = 1; s.treeTop = (i16)(s.height + (conifer ? 14 : 10) + (int)(hashFloat((i32)seed_, wx >> 2, wz >> 2, 28) * 5));
        s.leaves = g.region && g.region->kind == K_RED_FOREST ? B.zRedNeedles : conifer ? B.zPineNeedles : B.zBirchLeaves;
      }
    }
    // our tall landmarks show in the distance as solid columns
    for (int i : plan_->near(wx, wz, 0)) {
      const Placed& p = plan_->s[i];
      if (p.mc || p.h < 14 || p.type == S_FENCE_RING || p.type == S_PYLON || p.type == S_RADAR) continue;
      if (wx >= p.x0 && wx < p.x0 + p.W && wz >= p.z0 && wz < p.z0 + p.D) {
        if (p.type == S_COOLING || p.type == S_CHIMNEY) {
          const float R = p.W / 2.f, dx = wx - p.x0 - R, dz = wz - p.z0 - R;
          if (dx * dx + dz * dz > R * R * (p.type == S_COOLING ? 0.5f : 1.f)) continue;
        }
        s.height = (i16)std::min(250, p.y + p.h - 1);
        s.top = (p.type == S_REACTOR || p.type == S_COOLING || p.type == S_PANEL || p.type == S_PUBLIC) ? B.concrete : p.type == S_OFFICE || p.type == S_FACTORY || p.type == S_CHIMNEY ? B.brick : B.rust;
        s.under = s.top; s.water = -1; s.tree = 0;
      }
    }
  }

  int spawnHeight(int wx, int wz) const override { return (int)std::round(groundAt(seed_, wx, wz).h) + 1; }

private:
  u32 seed_;
  SimplexNoise grove_;
  Pal B;
  const Plan* plan_;
  BlockId lily_ = 0, sphereShell_ = 0;

  void column(Chunk& ch, int x, int z, int H, BlockId top, BlockId under) const {
    H = std::clamp(H, 2, 250);
    ch.set(x, 0, z, B.bedrock);
    for (int y = 1; y < H - 3; y++) ch.set(x, y, z, B.stone);
    for (int y = std::max(1, H - 3); y < H; y++) ch.set(x, y, z, under);
    ch.set(x, H, z, top);
  }

  // the world's surface block in ours: the fields, the paths, the paved and gravel roads, sand, rock, the beds under the water
  void surface(const Ground& g, const WCol& c, int wx, int wz, double n, int sl, BlockId& top, BlockId& under) const {
    const std::string& full = c.surf ? *c.surf : std::string();
    const size_t k = full.find(':'); const std::string nm = full.substr(0, k); const int d = k == std::string::npos ? 0 : std::atoi(full.c_str() + k + 1);
    const Region* r = g.region; const Kind kd = r ? r->kind : K_WILD;
    top = B.grass; under = B.dirt;
    if (g.water) {   // the bed
      top = nm == "sand" ? B.sand : nm == "gravel" ? B.gravel : nm == "clay" || nm == "dirt" ? B.mud : (c.w - c.g > 3 ? B.gravel : n < 0.5 ? B.mud : B.sand);
      under = top == B.sand ? B.sand : B.dirt;
      return;
    }
    if (nm == "grass" || nm.empty()) {
      if (kd == K_MARSH && g.rw > 0.1f) top = n < 0.3 ? B.mud : n < 0.5 ? B.peat : n < 0.65 ? B.moss : B.grass;
      else if (kd == K_RED_FOREST && g.rw > 0.15f) top = n < 0.35 ? B.coarse : n < 0.65 ? B.podzol : B.litter;
      else if (kd == K_SCRAPYARD && g.rw > 0.4f) top = n < 0.3 ? B.coarse : n < 0.55 ? B.dryDirt : B.grass;
      else {   // (alpha.10) dry tops and bare earth in patches, never speckled column by column
        const double dp = dryAt(wx, wz) + (n - 0.5) * 0.16;
        if (dp < dryShare(kd, g.rw) * 0.5 || (dp < dryShare(kd, g.rw) * 0.8 && coarseAt(CL_OPEN, (float)wx, (float)wz) > 0.75f)) top = B.dryDirt;
        else if (grove_.noise2(wx / 5.0 - 70, wz / 5.0 + 13) > 0.6 && n < 0.6) top = B.coarse;
        else if (kd != K_RED_FOREST && forestAt(wx, wz) > 0.55 && grove_.noise2(wx / 4.0 + 9, wz / 4.0 - 31) + (n - 0.5) * 0.3 > -0.15) top = B.zLitter;   // fallen leaves under the trees
      }
      if (sl >= 4) { top = n < 0.4 ? B.andesite : n < 0.75 ? B.coarse : B.stone; under = B.andesite; }   // bare rock on the steeps
    }
    else if (nm == "dirt") top = d == 1 ? B.coarse : d == 2 ? B.podzol : n < 0.45 ? B.coarse : n < 0.8 ? B.dryDirt : B.path;
    else if (nm == "grass_path") top = B.path;
    else if (nm == "farmland" || nm == "mycelium") top = n < 0.6 ? B.dryDirt : B.coarse;
    else if (nm == "sand") { top = B.sand; under = B.sand; }
    else if (nm == "gravel") { top = n < 0.12 ? B.coarse : B.gravel; under = B.gravel; }
    else if (nm == "clay") top = B.mud;
    else if (nm == "soul_sand") { top = B.zAsphalt; under = B.gravel; }
    else if (nm == "cobblestone" || nm == "mossy_cobblestone") { top = nm[0] == 'm' || n < 0.2 ? B.mossy : n < 0.3 ? B.gravel : B.cobble; under = B.cobble; }
    else if (nm == "double_stone_slab" || nm == "stonebrick") { top = n < 0.6 ? B.zPaving : n < 0.85 ? B.concrete2 : B.zAsphaltCr; under = B.cobble; }
    else if (nm == "stone") {
      if (g.road == 1) { top = d == 5 || d == 6 ? (n < 0.7 ? B.zPaving : B.concrete2) : B.zAsphalt; under = B.gravel; }
      else { top = d == 5 ? B.andesite : n < 0.3 ? B.andesite : B.stone; under = B.stone; }
    }
    else if (nm == "sandstone") { top = B.sand; under = B.sand; }
    else if (nm == "snow" || nm == "ice" || nm == "packed_ice") top = B.gravel;
    else { top = B.stone; under = B.stone; }   // ores, bedrock, the odd block the export calls ground
  }
  // a paved column with unpaved land beside it (its edge crumbles)
  static bool edgeOfRoad(const Ground* G, const WCol* C, const bool* has, int x, int z) {
    for (int k = 0; k < 4; k++) {
      const int nx = x + DIRX[k], nz = z + DIRZ[k];
      if ((unsigned)nx >= 16 || (unsigned)nz >= 16) continue;
      if (!G[nz * 16 + nx].road && !G[nz * 16 + nx].water) return true;
    }
    (void)C; (void)has;
    return false;
  }

  // worn asphalt: cracked patches, crumbling edges with the grass coming in, potholes, leaves and rubble lying on it
  void roadSurface(Chunk& ch, int x, int z, int H, int wx, int wz, float roadD, double n) const {
    const double patch = hashFloat((i32)seed_, wx >> 2, wz >> 2, 8), n2 = hashFloat((i32)seed_, wx, wz, 9);
    BlockId s = B.zAsphalt;
    if (roadD > 2.6f) s = n < 0.22 ? B.dryDirt : n < 0.3 ? B.grass : n < 0.42 ? B.gravel : B.zAsphaltCr;
    else if (patch < 0.3) s = n < 0.7 ? B.zAsphaltCr : B.zAsphalt;
    else if (n < 0.1) s = B.zAsphaltCr;
    if (roadD <= 2.6f && n > 0.992) {   // a pothole
      ch.set(x, H, z, AIR); ch.set(x, H - 1, z, n2 < 0.5 ? B.mud : B.gravel);
      if (n2 < 0.35) ch.set(x, H, z, B.puddle);
      return;
    }
    if (patch > 0.86 && n < 0.45 && forestAt(wx, wz) > 0.45) s = B.zLitter;
    ch.set(x, H, z, s);
    if (s == B.grass || s == B.dryDirt) { if (n2 < 0.5) ch.set(x, H + 1, z, n2 < 0.2 ? B.zTallGrass : B.dryGrass); }
    else if (n2 > 0.992) { ch.set(x, H + 1, z, B.zDebris); ch.setMeta(x, H + 1, z, (u8)(n2 * 4000) & 3); }
  }

  // forest density (0..1): the traced map's woods (gen/zone_layers.h), a little noise so their edges fray
  double forestAt(int x, int z) const { return std::clamp(0.02 + layerAt(ML_FOREST, (float)x, (float)z) * 0.95 + (noiseF(x / 90.0, z / 90.0) - 0.5) * 0.12, 0.0, 1.0); }

  // the user's trees, ours in their places: each drawn wherever its crown reaches into this chunk, standing on the final
  // ground (groundAt: their ground, our pads' levelling), left out on roads, in water, by an anomaly or in one of our
  // buildings, so every chunk makes the same choice for it. Some come down as fallen trunks or stay as bushes.
  void trees(Chunk& ch, int ox, int oz, const Ground* G, const u8* occupied, const std::vector<Anomaly>& an0) const {
    // the anomalies round the chunk (a tree's neighbours have to see the same ones)
    std::vector<Anomaly> an = an0; an.clear();
    std::vector<int> seenF;
    for (int cz = (oz - 24) >> 6; cz <= (oz + 39) >> 6; cz++) for (int cx = (ox - 24) >> 6; cx <= (ox + 39) >> 6; cx++)
      if (auto it = plan_->fieldGrid.find(cellKey(cx, cz)); it != plan_->fieldGrid.end())
        for (int fi : it->second) if (std::find(seenF.begin(), seenF.end(), fi) == seenF.end()) { seenF.push_back(fi); fieldAnomalies(seed_, plan_->fields[fi], an); }
    for (int cz = (oz - 6) >> 4; cz <= (oz + 21) >> 4; cz++) for (int cx = (ox - 6) >> 4; cx <= (ox + 21) >> 4; cx++) {
      const auto t = worldTile(cx >> 5, cz >> 5);
      if (!t) continue;
      const int ci = (cz & 31) * 32 + (cx & 31);
      for (u32 k = t->treeOff[ci]; k < t->treeOff[ci + 1]; k++) {
        const WTree& w = t->trees[k];
        const int tx = t->rx * 512 + w.x, tz = t->rz * 512 + w.z;
        if (tx < ox - 6 || tx > ox + 21 || tz < oz - 6 || tz > oz + 21) continue;
        const bool local = tx >= ox && tx < ox + 16 && tz >= oz && tz < oz + 16;
        if (local && occupied[(tz - oz) * 16 + (tx - ox)] == 1 && !G[(tz - oz) * 16 + (tx - ox)].road) {
          // (a column of ours or theirs: only skip if something is built there)
          bool built = false;
          for (int i : plan_->near(tx, tz, 1)) if (!plan_->s[i].mc && plan_->s[i].type != S_FENCE_RING && plan_->s[i].type != S_PYLON) built = true;
          if (built) continue;
        }
        if (!local) {
          bool blocked = false;
          for (int i : plan_->near(tx, tz, 2)) { const Placed& p = plan_->s[i]; if (!p.mc && p.type != S_FENCE_RING && p.type != S_PYLON) { blocked = true; break; } }
          if (blocked) continue;
        } else {
          bool blocked = false;
          for (int i : plan_->near(tx, tz, 2)) { const Placed& p = plan_->s[i]; if (!p.mc && p.type != S_FENCE_RING && p.type != S_PYLON) { blocked = true; break; } }
          if (blocked) continue;
        }
        if (zonePropNear(seed_, tx, tz, 2)) continue;   // (alpha.10) not through a wreck
        bool hot = false;
        for (const Anomaly& a : an) if ((a.x - tx) * (a.x - tx) + (a.z - tz) * (a.z - tz) < (a.r + 3) * (a.r + 3)) { hot = true; break; }
        if (hot) continue;
        const Ground g = local ? G[(tz - oz) * 16 + (tx - ox)] : groundAt(seed_, tx, tz);
        if (g.water || g.road || g.cut) continue;
        const int base = (int)std::round(g.h) + 1;
        const Kind kr = g.region ? g.region->kind : K_WILD;
        const double roll = hashFloat((i32)seed_, tx, tz, 23), kind = hashFloat((i32)seed_, tx, tz, 24);
        if (roll > 0.72) {   // a bush where they had a tree, or nothing
          if (roll < 0.86) zoneBush(ch, ox, oz, B, seed_, tx, base, tz, kr == K_RED_FOREST ? 2 : kind < 0.45 ? 0 : kind < 0.9 ? 1 : 2);
          continue;
        }
        if (roll > 0.70) {   // a trunk that came down, where the ground is level
          const bool alongZ = hashFloat((i32)seed_, tx, tz, 26) < 0.5; const int n = 3 + (int)(hashFloat((i32)seed_, tx, tz, 27) * 3);
          const int ex = alongZ ? tx : tx + n - 1, ez = alongZ ? tz + n - 1 : tz;
          const Ground ge = groundAt(seed_, ex, ez);
          if (!ge.water && !ge.road && (int)std::round(ge.h) + 1 == base) zoneFallenTrunk(ch, ox, oz, B, tx, base, tz, n, alongZ);
          continue;
        }
        TreeKind tk;
        if (!w.leaves) tk = T_DEAD;
        else if (kr == K_RED_FOREST && g.rw > 0.1f) tk = kind < 0.55 ? T_RED_PINE : kind < 0.85 ? T_BURNT : T_DEAD;
        else if ((kr == K_NPP || kr == K_GENERATORS || kr == K_SCRAPYARD) && kind < 0.35) tk = T_DEAD;
        else if (kr == K_MARSH && kind < 0.3) tk = T_DEAD;
        else switch (w.kind) {
          case 1: tk = kind < 0.6 ? T_PINE : T_SPRUCE; break;          // spruce: our pines and spruces
          case 2: tk = kind < 0.85 ? T_BIRCH : T_YOUNG; break;          // birch
          case 5: tk = kind < 0.5 ? T_SPRUCE : T_PINE; break;           // dark oak
          case 3: case 4: tk = T_POPLAR; break;
          default: tk = kind < 0.35 ? T_BIRCH : kind < 0.6 ? T_YOUNG : kind < 0.8 ? T_POPLAR : kind < 0.95 ? T_PINE : T_DEAD; break;   // oak
        }
        zoneTree(ch, ox, oz, B, seed_, tx, base, tz, tk);
      }
    }
  }

  // (alpha.10) the ground cover as in the games (docs/zone_details.md 1e): grass in tufts and patches (a patch mask at ~9 m and
  // cellular clumps at ~2 m, never one random per column), three heights (tall in the clumps' hearts, short at their edges), dry or
  // green by patch with the share by region, thick along walls, fences and wrecks and at the forest's edge, thin under the trees,
  // trampled bare at doors, ladders and camp fires (ash round the fire), reeds then sedge along the water, weeds only in the cracks
  static double dryShare(Kind k, float rw) {
    double s = 0.42;
    switch (k) {
      case K_VILLAGE: case K_FARMSTEAD: case K_GORGE: s = 0.35; break;
      case K_SCRAPYARD: case K_YARD: case K_MILITARY: s = 0.5; break;
      case K_MARSH: s = 0.15; break;
      case K_LAKE: case K_BACKWATER: s = 0.22; break;
      case K_RED_FOREST: s = 1.0; break;
      case K_RADAR: case K_NPP: case K_GENERATORS: s = 0.55; break;
      default: break;
    }
    return 0.42 + (s - 0.42) * std::clamp(rw * 2.5f, 0.f, 1.f);
  }
  // 0..1 per patch (12 m): under dryShare = dry
  double dryAt(int wx, int wz) const { return std::clamp(0.5 + 0.8 * grove_.noise2(wx / 12.0 + 311.0, wz / 12.0 - 97.0), 0.0, 1.0); }
  // cellular clumps: 1 at a tuft's heart .. 0 at its edge and in the gaps (features on a jittered 2.3 m grid)
  float clumpAt(int wx, int wz) const {
    const float S = 2.3f, fx = wx / S, fz = wz / S; const int ix = (int)std::floor(fx), iz = (int)std::floor(fz);
    float best = 9.f;
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
      const int cx = ix + dx, cz = iz + dz;
      const float px = cx + 0.15f + 0.7f * (float)hashFloat((i32)seed_, cx, cz, 41), pz = cz + 0.15f + 0.7f * (float)hashFloat((i32)seed_, cx, cz, 42);
      const float r = 0.5f + 0.55f * (float)hashFloat((i32)seed_, cx, cz, 43);
      best = std::min(best, std::hypot(fx - px, fz - pz) / r);
    }
    return std::clamp(1.f - best, 0.f, 1.f);
  }

  void undergrowth(Chunk& ch, int ox, int oz, const Ground* G, const u8* occupied, const WCol* C, const bool* has) const {
    const Registry& R = reg();
    static const BlockId fireBarrel = reg().blockId("zone_fire_barrel");
    int Hs[256];
    for (int i = 0; i < 256; i++) Hs[i] = (int)std::round(G[i].h);
    // what stands at each column's foot: doors, ladders, fires (worn ground round them) and walls (thick growth beside them)
    u8 worn[256] = {}, wall[256] = {};
    auto wear = [&](int cx, int cz, float r, float r2, bool ash) {
      const int R2 = (int)std::ceil(r2);
      for (int dz = -R2; dz <= R2; dz++) for (int dx = -R2; dx <= R2; dx++) {
        const int x = cx + dx, z = cz + dz; if ((unsigned)x >= 16 || (unsigned)z >= 16) continue;
        const float d = std::sqrt((float)(dx * dx + dz * dz));
        if (ash && d <= 1.6f) { worn[z * 16 + x] = 2; continue; }
        if (d <= r || (d <= r2 && hashFloat((i32)seed_, ox + x, oz + z, 37) < 0.9 * (1 - (d - r) / std::max(0.1f, r2 - r)))) worn[z * 16 + x] = std::max<u8>(worn[z * 16 + x], 1);
      }
    };
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
      const int i = z * 16 + x, H = Hs[i];
      if (H < 1 || H > 252) continue;
      for (int y = H + 1; y <= H + 2; y++) {
        const BlockId b = ch.get(x, y, z);
        if (b == AIR) continue;
        const int sk = R.tShape[b];
        if (y == H + 1 && (b == B.campfire || (fireBarrel && b == fireBarrel))) wear(x, z, 1.8f, 4.2f, b == B.campfire);
        else if (sk == ShapeKind::Door && y == H + 1) wear(x, z, 1.5f, 2.6f, false);
        else if (sk == ShapeKind::Ladder && y == H + 1) wear(x, z, 1.0f, 1.8f, false);
        else if (y == H + 1 && R.tSolid[b] && !R.tLiquid[b] && R.tRender[b] != 3 && ch.get(x, H + 2, z) != AIR) wall[i] = 1;   // a wall, fence, wreck or trunk
      }
    }
    // distance to open water inside the chunk (chamfer), and to a wall
    float bank[256], wd[256];
    for (int i = 0; i < 256; i++) { bank[i] = G[i].water ? 0.f : 99.f; wd[i] = wall[i] ? 0.f : 99.f; }
    for (float* d : {bank, wd}) for (int pass = 0; pass < 2; pass++)
      for (int k = 0; k < 256; k++) {
        const int i = pass ? 255 - k : k, x = i & 15, z = i >> 4, s = pass ? 1 : -1;
        auto rel = [&](int dx, int dz, float c) { const int nx = x + dx, nz = z + dz; if ((unsigned)nx < 16 && (unsigned)nz < 16) d[i] = std::min(d[i], d[nz * 16 + nx] + c); };
        rel(s, 0, 1.f); rel(0, s, 1.f); rel(s, s, 1.41f); rel(-s, s, 1.41f);
      }
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
      const int i = z * 16 + x;
      if (occupied[i]) continue;
      const Ground& g = G[i];
      const int wx = ox + x, wz = oz + z;
      const u8 cov = has[i] ? C[i].cover : 0;
      const Kind k = g.region ? g.region->kind : K_WILD;
      if (g.water) {   // lily pads: the user's, and clusters on the still backwaters
        if (lily_ && g.waterY < 254 && ch.get(x, g.waterY + 1, z) == AIR && (cov == 7 || ((k == K_MARSH || k == K_BACKWATER || k == K_LAKE) && g.waterY - g.h <= 2.5f &&
            grove_.noise2(wx / 6.0 + 17, wz / 6.0 - 5) > 0.45 && hashFloat((i32)seed_, wx, wz, 34) < 0.45))) ch.set(x, g.waterY + 1, z, lily_);
        continue;
      }
      if (g.road || g.cut) continue;
      const int H = Hs[i];
      if (H < 1 || H > 252 || ch.get(x, H + 1, z) != AIR) continue;
      const BlockId top = ch.get(x, H, z);
      const bool soft = top == B.grass || top == B.dryDirt || top == B.podzol || top == B.litter || top == B.moss || top == B.coarse || top == B.mud || top == B.zLitter || top == B.peat;
      if (worn[i]) { if (soft || top == B.path) ch.set(x, H, z, worn[i] == 2 ? B.zAsh : B.zDirtWorn); continue; }
      const double n = hashFloat((i32)seed_, wx, wz, 31), n2 = hashFloat((i32)seed_, wx, wz, 33), n3 = hashFloat((i32)seed_, wx, wz, 35);
      const bool free2 = ch.get(x, H + 2, z) == AIR;
      auto dbl = [&](BlockId id, BlockId fallback) { if (free2) { ch.set(x, H + 1, z, id); ch.set(x, H + 2, z, id); ch.setMeta(x, H + 2, z, 1); } else ch.set(x, H + 1, z, fallback); };
      // hard ground: weeds in the cracks only
      if (!soft) {
        double d = 0;
        if (top == B.zAsphaltCr || top == B.zPaving) d = 0.06; else if (top == B.gravel || top == B.andesite) d = 0.12; else if (top == B.sand) d = 0.05;
        if (n >= d) continue;
        if (top == B.sand) ch.set(x, H + 1, z, n2 < 0.5 ? B.zTallGrass : B.dryGrass);
        else ch.set(x, H + 1, z, n2 < 0.4 ? B.dryGrass : n2 < 0.7 ? B.tallGrass : n2 < 0.85 ? B.zTallGrass : B.zShrub);
        continue;
      }
      const float cl = clumpAt(wx, wz);
      const double patch = sstep((float)(0.5 + 0.7 * grove_.noise2(wx / 9.0 - 211, wz / 9.0 + 77)) * 1.25f - 0.15f);
      const double share = dryShare(k, g.rw);
      const bool dead = (k == K_RED_FOREST && g.rw > 0.15f) || radiationAt(wx, wz, seed_) > 1.0f;
      const bool dry = dead || dryAt(wx, wz) + (n3 - 0.5) * 0.08 < share;
      const float bk = std::min(bank[i], g.riverD + 1.0f);
      const bool marshLow = k == K_MARSH && g.h < WATER + 1.2f;
      const double f = forestAt(wx, wz);
      const bool verge = g.roadD < 2.2f, nearWall = wd[i] <= 1.6f;
      // the short / mid / tall grass of this patch's colour
      const BlockId shortG = dead ? B.zDeadGrass : dry ? B.dryGrass : B.tallGrass, midG = dead ? B.zDeadGrass : dry ? B.zTallGrass : B.zGrassMid;
      const BlockId tallG = dead ? B.zWeeds : dry ? B.zDryTall : B.tallGrass2;
      auto grass = [&](int height) { if (height >= 2) dbl(tallG, midG); else ch.set(x, H + 1, z, height ? midG : shortG); };
      if (bk <= 1.6f || (marshLow && cov == 8)) {   // the water's edge: reeds and cattails in stands
        if (n < 0.9 * (0.55 + 0.45 * cl)) dbl(n2 < 0.3 ? B.cattail : B.zReeds, B.zSedge);
        continue;
      }
      if (bk <= 4.f || marshLow) {   // the wet bank: sedge, a willow now and then, grass between
        if (n < 0.7 * (0.45 + 0.55 * cl)) {
          if (n2 < 0.04 && !marshLow) zoneBush(ch, ox, oz, B, seed_, wx, H + 1, wz, 1);
          else if (n2 < 0.62) ch.set(x, H + 1, z, B.zSedge); else grass(n2 < 0.8 ? 1 : 2);
        }
        continue;
      }
      if (verge) {   // the verge: tall dry grass and nettle
        if (n < 0.75 * (0.5 + 0.5 * cl)) { if (n2 < 0.22) ch.set(x, H + 1, z, B.zNettle); else if (n2 < 0.62) dbl(dead ? B.zWeeds : B.zDryTall, B.zTallGrass); else grass(n2 < 0.85 ? 1 : 0); }
        continue;
      }
      if (nearWall) {   // along walls, fences and wrecks, where the mower never got: tall, nettle, a shrub now and then
        if (n < 0.9 * (0.55 + 0.45 * cl)) { if (n2 < 0.065) ch.set(x, H + 1, z, B.zShrub); else if (n2 < 0.38 && !dead) ch.set(x, H + 1, z, B.zNettle); else grass(n2 < 0.75 ? 2 : 1); }
        continue;
      }
      if (f > 0.62) {   // under the trees: ferns, the odd shrub, a little grass, mostly litter
        const double d = cov == 2 ? 0.45 : 0.12;
        if (n < d * (0.4 + 0.6 * patch)) { if (n2 < 0.5) { if (n2 < 0.15) dbl(B.largeFern, B.fern); else ch.set(x, H + 1, z, B.fern); } else if (n2 < 0.62) ch.set(x, H + 1, z, B.zShrub); else grass(n2 < 0.85 ? 0 : 1); }
        continue;
      }
      if (f > 0.3) {   // the forest's edge: tall grass, ferns, shrubs
        if (n < 0.7 * (0.35 + 0.65 * patch) * std::min(1.f, 0.35f + cl)) { if (n2 < 0.18) ch.set(x, H + 1, z, B.fern); else if (n2 < 0.24) ch.set(x, H + 1, z, B.zShrub); else grass(cl > 0.5f ? (n2 < 0.55 ? 2 : 1) : n2 < 0.5 ? 1 : 0); }
        continue;
      }
      // the open meadow
      double d = 0.45 + (cov == 1 || cov == 4 || cov == 6 ? 0.2 : 0.0);
      if (k == K_FARMSTEAD) d += 0.15;
      if (n >= d * (0.25 + 0.75 * patch) * std::min(1.f, cl * 1.7f)) continue;
      if (cov == 2) { ch.set(x, H + 1, z, B.fern); continue; }
      if (cov == 5) { if (n2 < 0.5) ch.set(x, H + 1, z, B.deadBush); else dbl(B.zWeeds, B.zTallGrass); continue; }
      if (!dead && (cov == 3 || (grove_.noise2(wx / 5.0 + 50, wz / 5.0 + 50) > 0.5 && n2 < 0.4))) {   // flowers in small drifts
        ch.set(x, H + 1, z, n3 < 0.45 ? B.zDandelion : n3 < 0.85 ? B.zFlowers : B.daisy); continue;
      }
      if (dry && !dead && n2 > 0.97) { dbl(B.zWeeds, B.zTallGrass); continue; }   // last year's weeds standing in the dry patches
      const bool heart = cl > 0.55f;
      grass(heart ? (n2 < 0.3 ? 2 : n2 < 0.75 ? 1 : 0) : (n2 < 0.25 ? 1 : 0));
    }
  }

  double noiseF(double x, double z) const { return 0.5 + 0.5 * std::sin(x * 1.7 + std::sin(z * 1.3) * 2.1) * std::cos(z * 1.1 + std::sin(x * 0.9) * 1.7); }
};

} // namespace
} // namespace zone

void registerZoneWorldGen() {
  WorldGen::registerType("zone", [](int d, const WorldGenOptions& o) -> std::unique_ptr<DimGenerator> {
    if (d == DIM_OVERWORLD) return std::make_unique<zone::ZoneGen>(o);
    return std::make_unique<VoidGen>();
  });
}

} // namespace dq
