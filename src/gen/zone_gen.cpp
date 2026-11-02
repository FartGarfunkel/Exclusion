// gen/zone_gen.cpp — the "zone" world type: the Overworld band is the Zone (gen/zone_map.h, zone_plan.h); the
// other three bands are empty. Terrain, water, roads and the railway, the border fence, every building from the
// site plan, forests and undergrowth, and the scorched ground of anomaly fields, chunk by chunk.
#include "gen/dimgens.h"
#include "gen/zone_plan.h"
#include "gen/zone_dress.h"
#include "core/registry.h"
#include <cmath>
#include <algorithm>

namespace dq {
namespace zone {
namespace {

inline float sstep(float t) { t = std::clamp(t, 0.f, 1.f); return t * t * (3 - 2 * t); }

class ZoneGen : public DimGenerator {
public:
  explicit ZoneGen(const WorldGenOptions& o) : seed_((u32)o.seed) { B.init(); plan_ = &zonePlan(seed_); }

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
      ch.climate[i * 2] = t; ch.climate[i * 2 + 1] = h;
    }
    u8 occupied[256] = {};
    // ---- ground, water, roads
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
      const Ground& g = G[z * 16 + x];
      const int wx = ox + x, wz = oz + z;
      const int H = (int)std::round(g.h);
      if (!insideZone(wx, wz)) { column(ch, x, z, H, B.dirt, B.dirt); continue; }
      const Region* r = g.region;
      const Kind k = r ? r->kind : K_WILD;
      const double n = hashFloat((i32)seed_, wx, wz, 3);
      BlockId top = B.grass, under = B.dirt;
      if (k == K_MARSH && g.rw > 0.1f) top = n < 0.4 ? B.mud : n < 0.6 ? B.peat : n < 0.75 ? B.moss : B.grass;
      else if (k == K_RED_FOREST && g.rw > 0.15f) top = n < 0.35 ? B.coarse : n < 0.65 ? B.podzol : B.litter;
      else if (k == K_SCRAPYARD && g.rw > 0.3f) top = n < 0.4 ? B.gravel : n < 0.7 ? B.coarse : B.dryDirt;
      else if (k == K_GORGE && g.rw > 0.3f && H > BASE + 4) { top = n < 0.5 ? B.andesite : B.coarse; under = B.andesite; }
      else if (n < 0.18) top = B.dryDirt; else if (n < 0.24) top = B.coarse;
      else if (k != K_RED_FOREST && n < 0.5 && forestAt(wx, wz) > 0.55) top = B.zLitter;   // fallen leaves under the birches
      if (g.water && H >= WATER - 3) { top = n < 0.5 ? B.mud : B.sand; }
      else if (g.water) top = B.gravel;
      column(ch, x, z, H, top, under);
      if (g.water) for (int y = H + 1; y <= WATER; y++) ch.set(x, y, z, B.water);
      // roads and the railway (bridges where they cross water)
      if (g.road) {
        occupied[z * 16 + x] = 1;
        const bool bridge = g.water || H < WATER;
        const int ry = bridge ? WATER + 3 : H;
        if (bridge) {
          ch.set(x, ry, z, g.road == 3 ? B.steel : B.concrete2);
          if (((wx + wz) & 7) == 0) for (int y = H + 1; y < ry; y++) ch.set(x, y, z, B.concrete);
        } else {
          for (int y = H + 1; y <= H + 5; y++) ch.set(x, y, z, AIR);
          if (g.road == 1) roadSurface(ch, x, z, H, wx, wz, g.roadD, n);
          else if (g.road == 2) {   // two ruts, grass up the middle, puddles
            const bool mid = g.roadD < 0.8f;
            ch.set(x, H, z, mid ? (n < 0.6 ? B.dryDirt : B.grass) : n < 0.6 ? B.path : n < 0.85 ? B.coarse : B.mud);
            if (mid && n < 0.35) ch.set(x, H + 1, z, n < 0.15 ? B.zTallGrass : B.dryGrass);
            else if (!mid && n > 0.975) ch.set(x, H + 1, z, B.puddle);
          }
          else { ch.set(x, H, z, B.gravel); ch.set(x, H + 1, z, g.roadD < 1.6f ? B.gravel : AIR); }
        }
        if (g.road == 3 && g.roadD < 1.6f) {
          float dx = 0, dz = 1; railDir(wx, wz, dx, dz);
          ch.set(x, (bridge ? ry : H + 1) + 1, z, B.rail);
          ch.setMeta(x, (bridge ? ry : H + 1) + 1, z, std::abs(dx) > std::abs(dz) ? 1 : 0);
        }
      }
      // the fence round the Zone
      const int edge = std::min({wx - MIN_X, MAX_X - wx, wz - MIN_Z, MAX_Z - wz});
      if (edge >= 24 && edge <= 25) {
        occupied[z * 16 + x] = 1;
        const bool gate = std::abs(wx) < 6 && wz > 0;
        if (!gate) {   // concrete fence panels, barbed wire on the outer row
          for (int y = H + 1; y <= H + 3; y++) ch.set(x, y, z, B.zFence);
          if (edge == 24) ch.set(x, H + 4, z, B.zBarbed);
        }
      }
      if (edge < 24) occupied[z * 16 + x] = 1;
    }
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
        const int H = (int)std::round(G[z * 16 + x].h);
        if (G[z * 16 + x].water || G[z * 16 + x].road) continue;
        const double n = hashFloat((i32)seed_, a.x + dx, a.z + dz, 5);
        BlockId s = a.family == A_THERMAL ? (n < 0.3 ? B.magma : B.coarse) : a.family == A_CHEMICAL ? (n < 0.5 ? B.mud : B.moss)
                  : a.family == A_GRAVITY ? (n < 0.5 ? B.gravel : B.coarse) : a.family == A_ELECTRIC ? (n < 0.4 ? B.andesite : B.coarse) : B.coarse;
        ch.set(x, H, z, s);
        occupied[z * 16 + x] = 2;   // no plants in an anomaly
      }
    }
    // ---- buildings
    Ctx c{ch, ox, oz, B, seed_};
    std::vector<int> here;
    for (int cz = oz >> 6; cz <= (oz + 15) >> 6; cz++) for (int cx = ox >> 6; cx <= (ox + 15) >> 6; cx++) {
      auto it = plan_->grid.find(cellKey(cx, cz)); if (it == plan_->grid.end()) continue;
      for (int i : it->second) if (std::find(here.begin(), here.end(), i) == here.end()) here.push_back(i);
    }
    std::sort(here.begin(), here.end());
    for (int i : here) {
      const Placed& p = plan_->s[i];
      if (p.x0 > ox + 15 + 8 || p.x0 + p.W < ox - 8 || p.z0 > oz + 15 + 8 || p.z0 + p.D < oz - 8) continue;
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
    // ---- forests, the roadside (power lines, lamps), undergrowth
    trees(ch, ox, oz, G, occupied);
    zoneRoadside(ch, ox, oz, B, seed_, *plan_);
    undergrowth(ch, ox, oz, G, occupied);
  }

  void sample(int wx, int wz, ColumnSample& s) const override {
    s = ColumnSample();
    const Ground g = groundAt(seed_, wx, wz);
    s.height = (i16)std::round(g.h);
    s.top = g.road == 1 ? B.zAsphalt : g.road ? B.path : g.water ? B.mud : B.grass;
    s.under = B.dirt;
    if (g.water) { s.water = WATER; s.fluid = B.water; }
    s.temp = 168; s.humid = 70;
    if (g.region && g.region->kind == K_RED_FOREST && g.rw > 0.15f) { s.temp = 250; s.humid = 8; }
    // tall landmarks show in the distance as solid columns
    for (int i : plan_->near(wx, wz, 0)) {
      const Placed& p = plan_->s[i];
      if (p.h < 14 || p.type == S_FENCE_RING || p.type == S_PYLON || p.type == S_RADAR) continue;
      if (wx >= p.x0 && wx < p.x0 + p.W && wz >= p.z0 && wz < p.z0 + p.D) {
        if (p.type == S_COOLING || p.type == S_CHIMNEY) {
          const float R = p.W / 2.f, dx = wx - p.x0 - R, dz = wz - p.z0 - R;
          if (dx * dx + dz * dz > R * R * (p.type == S_COOLING ? 0.5f : 1.f)) continue;
        }
        s.height = (i16)std::min(250, p.y + p.h - 1);
        s.top = (p.type == S_REACTOR || p.type == S_COOLING || p.type == S_PANEL || p.type == S_PUBLIC) ? B.concrete : p.type == S_OFFICE || p.type == S_FACTORY || p.type == S_CHIMNEY ? B.brick : B.rust;
        s.under = s.top; s.water = -1;
      }
    }
  }

  int spawnHeight(int wx, int wz) const override { return (int)std::round(groundAt(seed_, wx, wz).h) + 1; }

private:
  u32 seed_;
  Pal B;
  const Plan* plan_;

  void column(Chunk& ch, int x, int z, int H, BlockId top, BlockId under) const {
    H = std::clamp(H, 2, 250);
    ch.set(x, 0, z, B.bedrock);
    for (int y = 1; y < H - 3; y++) ch.set(x, y, z, B.stone);
    for (int y = std::max(1, H - 3); y < H; y++) ch.set(x, y, z, under);
    ch.set(x, H, z, top);
  }

  static void railDir(int wx, int wz, float& dx, float& dz) {
    // the railway is the last line in roads()
    const Line& l = roads().back();
    float best = 1e30f;
    for (size_t i = 0; i + 1 < l.pts.size(); i++) {
      const Pt a = l.pts[i], b = l.pts[i + 1];
      const float ux = b.x - a.x, uz = b.z - a.z, L2 = ux * ux + uz * uz;
      const float t = std::clamp(((wx - a.x) * ux + (wz - a.z) * uz) / std::max(1.f, L2), 0.f, 1.f);
      const float px = a.x + ux * t - wx, pz = a.z + uz * t - wz, d = px * px + pz * pz;
      if (d < best) { best = d; const float n = std::sqrt(L2) + 1e-6f; dx = ux / n; dz = uz / n; }
    }
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

  // forest density (0..1): big patches of woodland, thinner round settlements
  double forestAt(int x, int z) const { return 0.18 + noiseF(x / 520.0, z / 520.0) * 0.55; }

  void trees(Chunk& ch, int ox, int oz, const Ground* G, const u8* occupied) const {
    // one candidate per 4x4 cell, drawn wherever its crown reaches into this chunk
    for (int cz = (oz - 7) / 4 - 1; cz <= (oz + 22) / 4; cz++) for (int cx = (ox - 7) / 4 - 1; cx <= (ox + 22) / 4; cx++) {
      const int tx = cx * 4 + (int)(hashFloat((i32)seed_, cx, cz, 21) * 4), tz = cz * 4 + (int)(hashFloat((i32)seed_, cx, cz, 22) * 4);
      if (tx < ox - 5 || tx > ox + 20 || tz < oz - 5 || tz > oz + 20) continue;
      const bool local = tx >= ox && tx < ox + 16 && tz >= oz && tz < oz + 16;
      const Ground g = local ? G[(tz - oz) * 16 + (tx - ox)] : groundAt(seed_, tx, tz);
      if (g.water || g.road || g.roadD < 3 || g.riverD < 3 || !insideZone(tx, tz)) continue;
      if (std::min({tx - MIN_X, MAX_X - tx, tz - MIN_Z, MAX_Z - tz}) < 30) continue;
      if (local && occupied[(tz - oz) * 16 + (tx - ox)]) continue;
      if (!local) {   // a neighbour's tree: skip it if it would stand in a building
        bool blocked = false;
        for (int i : plan_->near(tx, tz, 2)) { const Placed& p = plan_->s[i]; if (p.type != S_FENCE_RING && p.type != S_PYLON && tx >= p.x0 - 1 && tx <= p.x0 + p.W && tz >= p.z0 - 1 && tz <= p.z0 + p.D) { blocked = true; break; } }
        if (blocked) continue;
      }
      const Region* r = g.region;
      const Kind k = r ? r->kind : K_WILD;
      double dens = forestAt(tx, tz) * 0.85;
      if (k == K_RED_FOREST) dens = 0.6; else if (k == K_MARSH) dens *= 0.4; else if (k == K_SCRAPYARD || k == K_NPP || k == K_GENERATORS) dens *= 0.35;
      if (r && k != K_RED_FOREST && k != K_MARSH && k != K_WILD) dens *= (1 - sstep((g.rw - 0.2f) / 0.3f) * 0.85);
      const double roll = hashFloat((i32)seed_, cx, cz, 23);
      if (roll > dens) continue;
      const int base = (int)std::round(g.h) + 1;
      if (roll > dens * 0.95) {   // a trunk that came down: 3..5 blocks along x or z where the ground is level
        const bool alongZ = hashFloat((i32)seed_, cx, cz, 26) < 0.5; const int n = 3 + (int)(hashFloat((i32)seed_, cx, cz, 27) * 3);
        const int ex = alongZ ? tx : tx + n - 1, ez = alongZ ? tz + n - 1 : tz;
        const Ground ge = groundAt(seed_, ex, ez);
        if (!ge.water && !ge.road && (int)std::round(ge.h) + 1 == base) zoneFallenTrunk(ch, ox, oz, B, tx, base, tz, n, alongZ);
        continue;
      }
      const double kind = hashFloat((i32)seed_, cx, cz, 24);
      TreeKind t;
      if (k == K_RED_FOREST) t = kind < 0.55 ? T_RED_PINE : kind < 0.85 ? T_BURNT : T_DEAD;
      else if (k == K_MARSH) t = kind < 0.45 ? T_DEAD : kind < 0.9 ? T_BIRCH : T_PINE;
      else if (k == K_SCRAPYARD || k == K_NPP || k == K_GENERATORS) t = kind < 0.5 ? T_DEAD : T_BIRCH;
      else t = kind < 0.42 ? T_BIRCH : kind < 0.7 ? T_PINE : kind < 0.88 ? T_SPRUCE : T_DEAD;
      zoneTree(ch, ox, oz, B, seed_, tx, base, tz, t);
    }
  }

  void undergrowth(Chunk& ch, int ox, int oz, const Ground* G, const u8* occupied) const {
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
      if (occupied[z * 16 + x]) continue;
      const Ground& g = G[z * 16 + x];
      if (g.water || g.road) continue;
      const int H = (int)std::round(g.h);
      if (ch.get(x, H + 1, z) != AIR) continue;
      const BlockId top = ch.get(x, H, z);
      if (top != B.grass && top != B.dryDirt && top != B.podzol && top != B.litter && top != B.moss && top != B.coarse && top != B.mud && top != B.zLitter && top != B.sand) continue;
      const int wx = ox + x, wz = oz + z;
      const double n = hashFloat((i32)seed_, wx, wz, 31), patch = hashFloat((i32)seed_, wx >> 3, wz >> 3, 32);
      const Kind k = g.region ? g.region->kind : K_WILD;
      const bool shore = g.riverD < 4 || (k == K_MARSH && g.h < WATER + 1.2f);
      BlockId p = AIR;
      if (shore && n < 0.45) {   // reeds by the water, two blocks tall
        if (ch.get(x, H + 2, z) == AIR) { ch.set(x, H + 1, z, B.zReeds); ch.set(x, H + 2, z, B.zReeds); ch.setMeta(x, H + 2, z, 1); }
        continue;
      }
      if (top == B.sand) continue;
      if (k == K_RED_FOREST && g.rw > 0.15f) p = n < 0.1 ? B.deadBush : n < 0.28 ? B.dryGrass : n < 0.34 ? B.zTallGrass : AIR;
      else if (k == K_MARSH) p = n < 0.4 ? B.tallGrass : n < 0.52 ? B.zTallGrass : n < 0.6 ? B.fern : n < 0.63 ? B.zShrub : AIR;
      else if (top == B.zLitter) p = n < 0.12 ? B.fern : n < 0.2 ? B.zShrub : n < 0.3 ? B.tallGrass : AIR;
      else if (patch < 0.35) p = n < 0.4 ? B.zTallGrass : n < 0.62 ? B.tallGrass : n < 0.72 ? B.dryGrass : AIR;   // an overgrown meadow
      else p = n < 0.24 ? B.tallGrass : n < 0.36 ? B.dryGrass : n < 0.42 ? B.zTallGrass : n < 0.45 ? B.fern : n < 0.475 ? B.zShrub : n < 0.485 ? B.deadBush : AIR;
      if (p != AIR) ch.set(x, H + 1, z, p);
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
