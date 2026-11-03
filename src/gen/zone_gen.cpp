// gen/zone_gen.cpp — the "zone" world type: the Overworld band is the Zone (gen/zone_map.h, zone_plan.h); the
// other three bands are empty. Terrain, water, roads and the railway, the border fence, every building from the
// site plan, forests and undergrowth, and the scorched ground of anomaly fields, chunk by chunk.
#include "gen/dimgens.h"
#include "gen/zone_plan.h"
#include "gen/zone_dress.h"
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
            if (mid && n < 0.35) ch.set(x, H + 1, z, n < 0.15 ? B.zTallGrass : B.dryGrass);
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
      if (p.type == S_CAMP || p.type == S_STASH || p.type == S_WRECK || p.type == S_HEAP) {
        const int mx = p.x0 + p.W / 2, mz = p.z0 + p.D / 2;
        const Ground g = groundAt(seed_, mx, mz);
        p.y = (int)g.h + 1;
        // the builder drops a sunk hull one more block; on water that leaves it just under the surface
        if (p.type == S_WRECK && wreckLieOf(p.variant) == WL_SUNK && g.water) p.y = g.waterY;
      }
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
      else if (n < 0.14 || (n < 0.4 && coarseAt(CL_OPEN, (float)wx, (float)wz) > 0.75f)) top = B.dryDirt;
      else if (n < 0.2) top = B.coarse;
      else if (kd != K_RED_FOREST && n < 0.55 && forestAt(wx, wz) > 0.55) top = B.zLitter;   // fallen leaves under the trees
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
    // hot ground reads as dead soil. Roads and the plant's paving stay what they are.
    if (!g.water && !g.road) {
      const float dose = radiationAt(wx, wz, seed_);
      if (dose >= 0.7f && (top == B.grass || top == B.dryDirt || top == B.moss || top == B.peat || top == B.litter || top == B.zLitter))
        top = dose >= 1.3f ? B.coarse : B.podzol;
    }
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

  // the ground cover: tall grass of three heights, dead weeds, white dandelion clocks and meadow flowers in drifts, ferns under
  // the trees, reeds and cattails all along the water, weeds through the cracked yards
  void undergrowth(Chunk& ch, int ox, int oz, const Ground* G, const u8* occupied, const WCol* C, const bool* has) const {
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
      if (occupied[z * 16 + x]) continue;
      const Ground& g = G[z * 16 + x];
      const u8 cov = has[z * 16 + x] ? C[z * 16 + x].cover : 0;
      if (g.water && cov == 7 && lily_ && g.waterY < 254 && ch.get(x, g.waterY + 1, z) == AIR) { ch.set(x, g.waterY + 1, z, lily_); continue; }   // lily pads
      if (g.water || g.road || g.cut) continue;
      const int H = (int)std::round(g.h);
      if (ch.get(x, H + 1, z) != AIR) continue;
      const BlockId top = ch.get(x, H, z);
      const bool hard = top == B.zPaving || top == B.zAsphaltCr || top == B.gravel || top == B.concrete2 || top == B.andesite || top == B.stone;
      if (!hard && top != B.grass && top != B.dryDirt && top != B.podzol && top != B.litter && top != B.moss && top != B.coarse && top != B.mud && top != B.zLitter && top != B.sand && top != B.peat) continue;
      const int wx = ox + x, wz = oz + z;
      const double n = hashFloat((i32)seed_, wx, wz, 31), n2 = hashFloat((i32)seed_, wx, wz, 33);
      if (radiationAt(wx, wz, seed_) > 0.75f) {   // a Geiger patch grows weeds, not a meadow
        if (n < 0.4) ch.set(x, H + 1, z, n2 < 0.55 ? B.deadBush : B.zWeeds);
        continue;
      }
      const double drift = grove_.fbm2(wx / 21.0 + 40, wz / 21.0 - 17, 2);   // patches: flowers in drifts, weeds in stands
      const Kind k = g.region ? g.region->kind : K_WILD;
      const bool shore = g.riverD < 5 || (k == K_MARSH && g.h < WATER + 1.2f);
      const bool free2 = ch.get(x, H + 2, z) == AIR;
      auto dbl = [&](BlockId id) { if (free2) { ch.set(x, H + 1, z, id); ch.set(x, H + 2, z, id); ch.setMeta(x, H + 2, z, 1); } else ch.set(x, H + 1, z, B.tallGrass); };
      if (cov && cov != 9 && !hard) {   // what the user grew there
        const double c2 = n2;
        switch (cov) {
          case 1: if (n < 0.75) { const BlockId p = c2 < 0.45 ? B.tallGrass : c2 < 0.7 ? B.zTallGrass : c2 < 0.85 ? B.tallGrass2 : B.dryGrass; if (p == B.tallGrass2) dbl(p); else ch.set(x, H + 1, z, p); } break;
          case 2: if (c2 < 0.35) dbl(B.largeFern); else ch.set(x, H + 1, z, B.fern); break;
          case 3: ch.set(x, H + 1, z, c2 < 0.45 ? B.zFlowers : c2 < 0.75 ? B.zDandelion : B.daisy); break;
          case 4: dbl(c2 < 0.5 ? B.tallGrass2 : c2 < 0.75 ? B.zWeeds : B.largeFern); break;
          case 5: if (c2 < 0.5) ch.set(x, H + 1, z, B.deadBush); else dbl(B.zWeeds); break;
          case 6: ch.set(x, H + 1, z, c2 < 0.5 ? B.dryGrass : c2 < 0.8 ? B.zTallGrass : B.zWeeds); break;
          case 8: dbl(c2 < 0.4 ? B.cattail : B.zReeds); break;
          default: break;
        }
        continue;
      }
      if (hard) {   // weeds through the cracks
        if (n < 0.1) ch.set(x, H + 1, z, B.zTallGrass); else if (n < 0.16) ch.set(x, H + 1, z, B.tallGrass); else if (n < 0.19) dbl(B.zWeeds); else if (n < 0.21) ch.set(x, H + 1, z, B.zShrub);
        continue;
      }
      if (shore && n < (g.riverD < 2.5f ? 0.8 : 0.5)) { dbl(n2 < 0.35 ? B.cattail : B.zReeds); continue; }   // reeds and cattails all along the water
      if (top == B.sand) { if (n < 0.2) ch.set(x, H + 1, z, B.zTallGrass); continue; }
      BlockId p = AIR;
      if (k == K_RED_FOREST && g.rw > 0.15f) p = n < 0.1 ? B.deadBush : n < 0.32 ? B.dryGrass : n < 0.42 ? B.zTallGrass : n < 0.47 ? B.zWeeds : AIR;
      else if (k == K_MARSH) p = n < 0.4 ? B.tallGrass : n < 0.55 ? B.zTallGrass : n < 0.65 ? B.tallGrass2 : n < 0.72 ? B.fern : n < 0.76 ? B.zShrub : AIR;
      else if (top == B.zLitter || top == B.podzol || top == B.litter) p = n < 0.16 ? B.fern : n < 0.22 ? B.largeFern : n < 0.3 ? B.zShrub : n < 0.42 ? B.tallGrass : n < 0.46 ? B.zTallGrass : AIR;   // under the trees
      else if (drift > 0.32 && n < 0.62) p = n < 0.46 ? B.zDandelion : n < 0.54 ? B.zFlowers : B.tallGrass;                   // a white drift
      else if (drift < -0.38 && n < 0.7) p = n < 0.34 ? B.zWeeds : n < 0.5 ? B.zTallGrass : n < 0.6 ? B.deadBush : B.dryGrass;   // dead weeds
      else {   // the meadow: three heights of grass, flowers here and there
        const double open = layerAt(ML_OPEN, (float)wx, (float)wz);
        p = n < 0.3 ? B.tallGrass : n < 0.46 ? B.zTallGrass : n < 0.56 ? B.tallGrass2 : n < 0.63 ? B.dryGrass : n < 0.665 ? B.zFlowers : n < 0.685 ? B.zDandelion
          : n < 0.7 ? B.daisy : n < 0.72 ? B.zWeeds : n < 0.735 ? B.zShrub : n < 0.745 ? B.fern : n < 0.75 ? B.deadBush : n < 0.75 + open * 0.15 ? B.tallGrass : AIR;
      }
      if (p == B.tallGrass2 || p == B.zWeeds || p == B.largeFern) dbl(p);
      else if (p != AIR) ch.set(x, H + 1, z, p);
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
