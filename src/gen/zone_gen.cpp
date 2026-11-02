// gen/zone_gen.cpp — the "zone" world type: the Overworld band is the Zone (gen/zone_map.h, zone_plan.h); the
// other three bands are empty. Terrain, water, roads and the railway, the border fence, every building from the
// site plan, forests and undergrowth, and the scorched ground of anomaly fields, chunk by chunk.
#include "gen/dimgens.h"
#include "gen/zone_plan.h"
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
          if (g.road == 1) ch.set(x, H, z, (g.roadD > 3.2f) ? B.curb : (n < 0.06 ? B.crack : n < 0.08 ? B.gravel : B.asphalt));
          else if (g.road == 2) ch.set(x, H, z, n < 0.65 ? B.path : B.coarse);
          else { ch.set(x, H, z, B.gravel); ch.set(x, H + 1, z, g.roadD < 1.6f ? B.gravel : AIR); }
        }
        if (g.road == 3 && g.roadD < 1.6f) {
          float dx = 0, dz = 1; railDir(wx, wz, dx, dz);
          ch.set(x, (bridge ? ry : H + 1) + 1, z, B.rail);
          ch.setMeta(x, (bridge ? ry : H + 1) + 1, z, std::abs(dx) > std::abs(dz) ? 1 : 0);
        }
        if (g.road == 1 && !bridge && n > 0.985) ch.set(x, H + 1, z, B.puddle);
      }
      // the fence round the Zone
      const int edge = std::min({wx - MIN_X, MAX_X - wx, wz - MIN_Z, MAX_Z - wz});
      if (edge >= 24 && edge <= 25) {
        occupied[z * 16 + x] = 1;
        const bool gate = std::abs(wx) < 6 && wz > 0;
        if (!gate) for (int y = H + 1; y <= H + 3; y++) ch.set(x, y, z, y == H + 3 ? B.bars : B.concrete);
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
    // ---- forests and undergrowth
    plants(ch, ox, oz, G, occupied);
  }

  void sample(int wx, int wz, ColumnSample& s) const override {
    s = ColumnSample();
    const Ground g = groundAt(seed_, wx, wz);
    s.height = (i16)std::round(g.h);
    s.top = g.road == 1 ? B.asphalt : g.road ? B.path : g.water ? B.mud : B.grass;
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

  void plants(Chunk& ch, int ox, int oz, const Ground* G, const u8* occupied) const {
    // trees: one candidate per 5x5 cell, drawn wherever their canopy reaches into this chunk
    for (int cz = (oz - 6) / 5 - 1; cz <= (oz + 21) / 5; cz++) for (int cx = (ox - 6) / 5 - 1; cx <= (ox + 21) / 5; cx++) {
      const int tx = cx * 5 + (int)(hashFloat((i32)seed_, cx, cz, 21) * 5), tz = cz * 5 + (int)(hashFloat((i32)seed_, cx, cz, 22) * 5);
      if (tx < ox - 4 || tx > ox + 19 || tz < oz - 4 || tz > oz + 19) continue;
      const bool local = tx >= ox && tx < ox + 16 && tz >= oz && tz < oz + 16;
      const Ground g = local ? G[(tz - oz) * 16 + (tx - ox)] : groundAt(seed_, tx, tz);
      if (g.water || g.road || g.roadD < 3 || g.riverD < 3 || !insideZone(tx, tz)) continue;
      if (local && occupied[(tz - oz) * 16 + (tx - ox)]) continue;
      if (!local) {   // a neighbour's tree: skip it if it would stand in a building
        bool blocked = false;
        for (int i : plan_->near(tx, tz, 2)) { const Placed& p = plan_->s[i]; if (p.type != S_FENCE_RING && p.type != S_PYLON && tx >= p.x0 - 1 && tx <= p.x0 + p.W && tz >= p.z0 - 1 && tz <= p.z0 + p.D) { blocked = true; break; } }
        if (blocked) continue;
      }
      const Region* r = g.region;
      const Kind k = r ? r->kind : K_WILD;
      // forest density: big patches of pine and birch, thinner round settlements
      double dens = 0.18 + noiseF(tx / 520.0, tz / 520.0) * 0.55;
      if (k == K_RED_FOREST) dens = 0.75; else if (k == K_MARSH) dens *= 0.4; else if (k == K_SCRAPYARD || k == K_NPP || k == K_GENERATORS) dens *= 0.35;
      if (r && k != K_RED_FOREST && k != K_MARSH && k != K_WILD) dens *= (1 - sstep((g.rw - 0.2f) / 0.3f) * 0.85);
      if (hashFloat((i32)seed_, cx, cz, 23) > dens) continue;
      const int base = (int)std::round(g.h) + 1;
      const double kind = hashFloat((i32)seed_, cx, cz, 24);
      if (k == K_RED_FOREST) tree(ch, ox, oz, tx, base, tz, kind < 0.7 ? 3 : 0, cx, cz);
      else tree(ch, ox, oz, tx, base, tz, kind < 0.6 ? 0 : kind < 0.9 ? 1 : 2, cx, cz);
    }
    // undergrowth
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
      if (occupied[z * 16 + x]) continue;
      const Ground& g = G[z * 16 + x];
      if (g.water || g.road) continue;
      const int H = (int)std::round(g.h);
      if (ch.get(x, H + 1, z) != AIR) continue;
      const BlockId top = ch.get(x, H, z);
      if (top != B.grass && top != B.dryDirt && top != B.podzol && top != B.litter && top != B.moss && top != B.coarse && top != B.mud) continue;
      const double n = hashFloat((i32)seed_, ox + x, oz + z, 31);
      const Kind k = g.region ? g.region->kind : K_WILD;
      BlockId p = AIR;
      if (k == K_RED_FOREST && g.rw > 0.15f) p = n < 0.12 ? B.deadBush : n < 0.3 ? B.dryGrass : AIR;
      else if (k == K_MARSH) p = n < 0.45 ? B.tallGrass : n < 0.55 ? B.fern : AIR;
      else p = n < 0.28 ? B.tallGrass : n < 0.42 ? B.dryGrass : n < 0.46 ? B.fern : n < 0.47 ? B.deadBush : AIR;
      if (p != AIR) ch.set(x, H + 1, z, p);
    }
  }

  double noiseF(double x, double z) const { return 0.5 + 0.5 * std::sin(x * 1.7 + std::sin(z * 1.3) * 2.1) * std::cos(z * 1.1 + std::sin(x * 0.9) * 1.7); }

  // 0 pine, 1 birch, 2 dead, 3 red (rust-needled pine)
  void tree(Chunk& ch, int ox, int oz, int tx, int ty, int tz, int kind, int cx, int cz) const {
    auto put = [&](int x, int y, int z, BlockId id, bool soft) {
      const int lx = x - ox, lz = z - oz;
      if ((unsigned)lx >= 16 || (unsigned)lz >= 16 || (unsigned)y >= 256) return;
      if (soft && ch.get(lx, y, lz) != AIR) return;
      ch.set(lx, y, lz, id);
    };
    const int H = kind == 1 ? 7 + (int)(hashFloat((i32)seed_, cx, cz, 25) * 4) : 9 + (int)(hashFloat((i32)seed_, cx, cz, 25) * 7);
    const BlockId log = kind == 1 ? B.birchLog : kind == 2 ? B.charred : B.pineLog;
    for (int y = 0; y < H; y++) put(tx, ty + y, tz, log, false);
    if (kind == 2) { if (H > 9) { put(tx + 1, ty + H - 3, tz, log, true); put(tx - 1, ty + H - 5, tz, log, true); } return; }
    const BlockId lv = kind == 1 ? B.birchLeaves : kind == 3 ? B.deadLeaves : B.pineLeaves;
    if (kind == 1) {
      for (int y = H - 4; y <= H; y++) { const int r = y >= H - 1 ? 1 : 2; for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) if (dx * dx + dz * dz <= r * r + 1 && hashFloat(tx + dx, ty + y, tz + dz, (i32)seed_) < 0.85) put(tx + dx, ty + y, tz + dz, lv, true); }
      return;
    }
    // a pine: bare trunk, then thin tiers of needles narrowing to the top
    for (int y = H / 2; y <= H; y++) {
      const int r = std::max(0, (H - y) / 3 + ((y & 1) ? 0 : 1));
      for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++)
        if (dx * dx + dz * dz <= r * r && (kind != 3 || hashFloat(tx + dx, ty + y, tz + dz, (i32)seed_) < 0.55)) put(tx + dx, ty + y, tz + dz, lv, true);
    }
    put(tx, ty + H, tz, lv, true);
  }
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
