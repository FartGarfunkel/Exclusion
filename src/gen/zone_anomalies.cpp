// gen/zone_anomalies.cpp — the anomaly roster: each anomaly of a field gets its kind (by its family, the ground it
// stands on and a hash), its reach, its ground height or pool, a skip's partner; and the ground round it is dressed
// for it when the chunk is generated. See gen/zone_anomalies.h. The behaviour is sim/zone_anomaly.cpp, the tells
// render/zone_anomaly_fx.cpp.
#include "gen/zone_anomalies.h"
#include "gen/zone_dress.h"
#include "core/registry.h"
#include <mutex>
#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace dq::zone {

static const AnoDef DEFS[AK_COUNT] = {
  {"undertow", "Undertow", A_GRAVITY, "Drags everything toward its core, then bursts. A dust devil and a gust give it away; a bolt sets it off, and it needs a few seconds to gather again."},
  {"kickback", "Kickback", A_GRAVITY, "A wall you can't see that throws you back. The air shimmers and leaves swirl over it; a bolt knocks it out for a moment."},
  {"spinner", "Spinner", A_GRAVITY, "Lifts whatever steps in, spins it and tears it apart. Debris turns slowly over it."},
  {"shardfield", "Shardfield", A_GRAVITY, "Glass hanging in the air, ringing faintly. It only cuts what hurries: walk. Bolts pass straight through."},
  {"skip", "Skip", A_GRAVITY, "Two folds in space: step into one and you stumble out of the other. A bolt shows you where."},
  {"flare_vent", "Flare vent", A_THERMAL, "A crack that spits a pillar of flame at whatever comes near. Embers drift over it; a bolt sets it off."},
  {"wanderfire", "Wanderfire", A_THERMAL, "A ball of fire walking its own loop. It chases for a while, and a bolt will lead it off."},
  {"steam_vent", "Steam vent", A_THERMAL, "Scalding steam on a beat. Count, then cross."},
  {"ash_moss", "Ash moss", A_THERMAL, "Grey moss hanging from a dead tree. It burns whatever brushes it."},
  {"arc_nest", "Arc nest", A_ELECTRIC, "Gathers a charge with a crackle and lets go at anything close. A bolt drains it for a few seconds."},
  {"ball_lightning", "Ball lightning", A_ELECTRIC, "A humming ball of light rolling along its path, striking whatever it meets."},
  {"flashpop", "Flashpop", A_ELECTRIC, "Hidden in the grass: a blinding pop and a jolt. A spark now and then is the only sign. A bolt pops it."},
  {"sparkpool", "Sparkpool", A_ELECTRIC, "Shallow water with a current running through it."},
  {"acid_pool", "Acid pool", A_CHEMICAL, "Glowing green water that eats flesh and leaves you hot. Bolts just sink."},
  {"gas_pocket", "Gas pocket", A_CHEMICAL, "Puffs a toxic cloud when it's disturbed. Each bolt empties it a little."},
  {"blister", "Blister", A_CHEMICAL, "A drifting bubble that follows you and bursts into smaller ones."},
  {"mire", "Mire", A_CHEMICAL, "Sucking mud that slows you and swallows you. Jump to get out."},
  {"psy_field", "Psy field", A_PSY, "Voices that are not there. The mind bleeds here."},
  {"lullfield", "Lullfield", A_PSY, "A patch of poppies. First you get drowsy, then you sleep."},
  {"wisps", "Will-o'-wisps", A_PSY, "Lights over the ground at night that lead the mind astray."},
};
const AnoDef& anoDef(int kind) { return DEFS[std::clamp(kind, 0, AK_COUNT - 1)]; }

namespace {
u8 pickKind(u32 seed, const Anomaly& a, const Ground& g) {
  const double h = hashFloat((i32)seed, a.x, a.z, 41), s = hashFloat((i32)seed, a.z, a.x, 43);
  const bool marsh = g.region && g.region->kind == K_MARSH && g.rw > 0.1f;
  // strays: a patch of poppies or a few lights in any field
  if (a.family != A_PSY && !g.water) { if (s < 0.035) return AK_LULL; if (s < 0.05 && a.tier >= 2) return AK_WISPS; }
  switch (a.family) {
    case A_GRAVITY: return h < 0.34 ? AK_UNDERTOW : h < 0.54 ? AK_KICKBACK : h < 0.71 ? AK_SPINNER : h < 0.87 ? AK_SHARDS : AK_SKIP;
    case A_THERMAL: if (g.water) return AK_STEAM; return h < 0.38 ? AK_FLARE : h < 0.55 ? AK_WANDERFIRE : h < 0.78 ? AK_STEAM : AK_ASHMOSS;
    case A_ELECTRIC: if (g.water) return AK_SPARKPOOL; return h < 0.4 ? AK_ARCNEST : h < 0.58 ? AK_BALL : h < 0.84 ? AK_FLASHPOP : AK_SPARKPOOL;
    case A_CHEMICAL: if (g.water) return h < 0.5 ? AK_ACID : AK_MIRE; if (marsh && h < 0.3) return AK_MIRE;
      return h < 0.35 ? AK_ACID : h < 0.62 ? AK_GAS : h < 0.82 ? AK_BLISTER : AK_MIRE;
    default: return h < 0.4 ? AK_PSYFIELD : h < 0.72 ? AK_LULL : AK_WISPS;
  }
}
float kindRadius(u8 k, double h) {
  static const float R[AK_COUNT][2] = {{2.5f, 4.5f}, {1.8f, 3.f}, {1.5f, 2.2f}, {3.f, 5.f}, {1.f, 1.2f}, {1.f, 1.6f}, {3.f, 6.f}, {0.9f, 1.2f}, {2.6f, 2.6f},
                                       {2.f, 3.4f}, {5.f, 8.f}, {1.2f, 1.4f}, {2.f, 3.5f}, {1.6f, 3.f}, {2.f, 3.4f}, {4.f, 5.f}, {2.5f, 4.2f}, {3.f, 6.f}, {5.f, 8.f}, {6.f, 9.f}};
  return R[k][0] + (R[k][1] - R[k][0]) * (float)h;
}
bool poolKind(u8 k) { return k == AK_SPARKPOOL || k == AK_ACID || k == AK_MIRE; }
int poolDepth(u8 k) { return k == AK_MIRE ? 2 : 1; }
// the lowest ground over the disc and a ring round it (so the water can't spill); false where a road or open water is in the way
bool poolLevel(u32 seed, int cx, int cz, float r, int& Wy) {
  const float E = r + 1.5f; const int R = (int)std::ceil(E); Wy = 1 << 20;
  for (int dz = -R; dz <= R; dz++) for (int dx = -R; dx <= R; dx++) {
    if (dx * dx + dz * dz > E * E) continue;
    const Ground g = groundAt(seed, cx + dx, cz + dz);
    if (g.road || g.water) return false;
    Wy = std::min(Wy, (int)std::round(g.h));
  }
  return Wy < (1 << 20);
}
void build(u32 seed, int fi, std::vector<ZAnomaly>& out) {
  const Plan& P = zonePlan(seed);
  if (fi < 0 || fi >= (int)P.fields.size()) return;
  const AnomalyField& f = P.fields[fi];
  std::vector<Anomaly> base; fieldAnomalies(seed, f, base);
  for (const Anomaly& a : base) {
    const Ground g = groundAt(seed, a.x, a.z);
    ZAnomaly z{};
    z.x = a.x; z.z = a.z; z.tier = a.tier; z.field = fi;
    u8 k = pickKind(seed, a, g);
    const int H = (int)std::round(g.h);
    z.y = H + 1;
    if (g.water) { z.pool = true; z.y = WATER + 1; }
    z.r = kindRadius(k, hashFloat((i32)seed, a.x, a.z, 44));
    if (!g.water && poolKind(k)) {   // dig it a pool, or make it something else
      int Wy;
      if (poolLevel(seed, a.x, a.z, z.r, Wy) && H - Wy <= 3) { z.pool = true; z.y = Wy + 1; }
      else { k = k == AK_SPARKPOOL ? AK_FLASHPOP : AK_GAS; z.r = kindRadius(k, hashFloat((i32)seed, a.x, a.z, 44)); }
    }
    if (k == AK_ASHMOSS && g.road) k = AK_FLARE;
    if (k == AK_SKIP) {   // and its partner, a few strides off
      const double an = hashFloat((i32)seed, a.x, a.z, 45) * 6.2832, d = 7 + hashFloat((i32)seed, a.x, a.z, 46) * 9;
      const int px = a.x + (int)std::round(std::cos(an) * d), pz = a.z + (int)std::round(std::sin(an) * d);
      const Ground gp = groundAt(seed, px, pz);
      if (gp.water || g.water) { k = AK_KICKBACK; z.r = kindRadius(k, 0.5); }
      else {
        z.tx = px; z.ty = (int)std::round(gp.h) + 1; z.tz = pz;
        ZAnomaly w = z; w.x = px; w.z = pz; w.y = z.ty; w.tx = a.x; w.ty = z.y; w.tz = a.z; w.kind = k; w.family = A_GRAVITY;
        w.id = hash32((i32)seed, px, pz, k + 100);
        out.push_back(w);
      }
    }
    z.kind = k; z.family = DEFS[k].family;
    z.id = hash32((i32)seed, a.x, a.z, k + 100);
    out.push_back(z);
  }
}
} // namespace

const std::vector<ZAnomaly>& zoneFieldAnomalies(u32 seed, int fi) {
  static std::mutex m;
  static std::unordered_map<u64, std::vector<ZAnomaly>> cache;   // (node based: references stay put)
  const u64 key = ((u64)seed << 32) | (u32)fi;
  { std::lock_guard<std::mutex> l(m); auto it = cache.find(key); if (it != cache.end()) return it->second; }
  std::vector<ZAnomaly> v; build(seed, fi, v);
  std::lock_guard<std::mutex> l(m);
  return cache.emplace(key, std::move(v)).first->second;
}

static void fieldsNear(const Plan& P, int x, int z, int r, std::vector<int>& fis) {
  for (int cz = (z - r) >> 6; cz <= (z + r) >> 6; cz++) for (int cx = (x - r) >> 6; cx <= (x + r) >> 6; cx++) {
    auto it = P.fieldGrid.find(cellKey(cx, cz)); if (it == P.fieldGrid.end()) continue;
    for (int fi : it->second) if (std::find(fis.begin(), fis.end(), fi) == fis.end()) fis.push_back(fi);
  }
}
void zoneAnomaliesNear(u32 seed, int x, int z, int r, std::vector<const ZAnomaly*>& out) {
  const Plan& P = zonePlan(seed);
  std::vector<int> fis; fieldsNear(P, x, z, r, fis);
  for (int fi : fis) {
    const AnomalyField& f = P.fields[fi];
    if (std::abs(f.x - x) > f.r + r + 30 || std::abs(f.z - z) > f.r + r + 30) continue;
    for (const ZAnomaly& a : zoneFieldAnomalies(seed, fi)) if (std::abs(a.x - x) <= r + a.r && std::abs(a.z - z) <= r + a.r) out.push_back(&a);
  }
}

// ---------------------------------------------------------------- the dressing, chunk by chunk
void dressAnomalies(Chunk& ch, int ox, int oz, const Pal& B, u32 seed, const Ground* G, u8* occupied) {
  const Plan& P = zonePlan(seed);
  std::vector<int> fis; fieldsNear(P, ox + 8, oz + 8, 40, fis);
  if (fis.empty()) return;
  static const BlockId poppy = reg().blockId("flower_red"), vent = reg().blockId("steam_vent"), budS = reg().blockId("small_amethyst_bud"),
                       budM = reg().blockId("medium_amethyst_bud"), embers = reg().blockId("smouldering_ash"), ashB = reg().blockId("ash"),
                       lime0 = reg().blockId("lime_terracotta");
  const BlockId lime = lime0 ? lime0 : B.moss, ash = ashB ? ashB : B.coarse;
  auto plant = [&](int x, int y, int z, BlockId b) { if (b && y < 255 && ch.get(x, y, z) == AIR) ch.set(x, y, z, b); };
  for (int fi : fis) for (const ZAnomaly& a : zoneFieldAnomalies(seed, fi)) {
    const float E = (a.kind == AK_ASHMOSS ? 3.f : a.r) + 1.5f;
    if (a.x + E < ox || a.x - E > ox + 15 || a.z + E < oz || a.z - E > oz + 15) continue;
    const bool dug = a.pool && !groundAt(seed, a.x, a.z).water;
    const int Wy = a.y - 1, depth = poolDepth(a.kind);
    const int R = (int)std::ceil(E);
    for (int dz = -R; dz <= R; dz++) for (int dx = -R; dx <= R; dx++) {
      const int x = a.x + dx - ox, z = a.z + dz - oz;
      if ((unsigned)x >= 16 || (unsigned)z >= 16) continue;
      const float d = std::sqrt(float(dx * dx + dz * dz));
      if (d > E) continue;
      const int i = z * 16 + x;
      if (occupied[i] == 1) continue;
      const Ground& g = G[i];
      const int H = (int)std::round(g.h);
      const bool inner = d <= a.r + 0.5f, wet = g.water;
      const double n = hashFloat((i32)seed, a.x + dx, a.z + dz, 5), n2 = hashFloat((i32)seed, a.x + dx, a.z + dz, 6);
      auto top = [&](BlockId b) { if (!wet && b) ch.set(x, H, z, b); };
      switch (a.kind) {
        case AK_UNDERTOW:
          if (inner) { top(n < 0.45 ? B.coarse : n < 0.75 ? B.gravel : B.dryDirt); if (d > 1.2f && n2 > 0.93 && !wet) plant(x, H + 1, z, B.zDebris); }
          else if (n < 0.4 && !wet) plant(x, H + 1, z, B.dryGrass);
          break;
        case AK_KICKBACK: if (inner) { top(n < 0.5 ? B.zLitter : B.coarse); if (n2 > 0.9 && !wet) plant(x, H + 1, z, B.deadBush); } break;
        case AK_SPINNER: if (inner) top(n < 0.4 ? B.gravel : n < 0.8 ? B.coarse : B.looseStones); break;
        case AK_SHARDS:
          if (inner) { top(n < 0.45 ? B.gravel : n < 0.8 ? B.coarse : B.andesite); if (!wet) plant(x, H + 1, z, n2 < 0.1 ? budS : n2 < 0.15 ? budM : AIR); }
          break;
        case AK_SKIP: if (d <= 1.6f && d >= 0.6f) top(B.andesite); break;
        case AK_FLARE:
          if (d < 0.8f) top(embers ? embers : B.magma);
          else if (inner) top(n < 0.35 ? B.magma : n < 0.7 ? B.coarse : ash);
          else if (n < 0.35) top(B.dryDirt); else if (n2 < 0.3 && !wet) plant(x, H + 1, z, B.dryGrass);
          break;
        case AK_WANDERFIRE: if (std::abs(d - a.r) < 1.3f) top(n < 0.5 ? B.coarse : n < 0.8 ? B.dryDirt : ash); break;
        case AK_STEAM: if (d < 0.75f) { if (!wet && vent) ch.set(x, H, z, vent); } else if (d < 2.3f) top(n < 0.5 ? B.gravel : B.coarse); break;
        case AK_ASHMOSS: if (d <= 3.2f) top(n < 0.45 ? ash : n < 0.75 ? B.coarse : B.zLitter); break;
        case AK_ARCNEST:
          if (inner) { top(n < 0.45 ? B.andesite : B.coarse); if (!wet && (d < 0.7f || n2 > 0.95)) plant(x, H + 1, z, B.zRebar); }
          break;
        case AK_BALL: {
          const float th = std::atan2((float)dz, (float)dx), path = a.r * (1 + 0.22f * std::sin(3 * th));
          if (std::abs(d - path) < 0.9f && n < 0.75) top(n < 0.4 ? B.coarse : B.andesite);
          break;
        }
        case AK_FLASHPOP: if (d < 1.f && n < 0.5) top(B.coarse); break;
        case AK_SPARKPOOL: case AK_ACID: case AK_MIRE:
          if (dug && d <= a.r + 0.5f) {   // the pool: water over a bed, the ground above it cleared
            for (int y = Wy + 1; y <= std::max(H, Wy) + 2 && y < 255; y++) ch.set(x, y, z, AIR);
            for (int y = Wy - depth + 1; y <= Wy; y++) ch.set(x, y, z, B.water);
            ch.set(x, Wy - depth, z, a.kind == AK_MIRE ? B.mud : a.kind == AK_ACID ? (n < 0.5 ? B.moss : B.mud) : B.gravel);
          } else if (!inner || !a.pool) {   // the rim
            if (a.kind == AK_MIRE) { top(n < 0.5 ? B.mud : B.peat); if (n2 > 0.72 && !wet) plant(x, H + 1, z, B.zReeds); }
            else if (a.kind == AK_ACID) { top(n < 0.3 ? lime : n < 0.6 ? B.moss : B.mud); if (n2 > 0.88 && !wet) plant(x, H + 1, z, B.deadBush); }
            else { top(n < 0.5 ? B.coarse : B.gravel); if (n2 > 0.93 && !wet) plant(x, H + 1, z, B.zRebar); }
          }
          break;
        case AK_GAS: if (inner) { top(n < 0.5 ? B.mud : n < 0.8 ? B.peat : B.coarse); if (n2 > 0.9 && !wet) plant(x, H + 1, z, B.deadBush); } break;
        case AK_BLISTER: if (d <= 2.5f) top(n < 0.5 ? B.moss : B.mud); break;
        case AK_PSYFIELD: if (inner) { if (n < 0.3) top(B.coarse); if (n2 > 0.94 && !wet) plant(x, H + 1, z, B.deadBush); } break;
        case AK_LULL:
          if (inner && !wet) { top(B.grass); plant(x, H + 1, z, n2 < 0.42 ? poppy : n2 < 0.58 ? B.zTallGrass : AIR); }
          break;
        case AK_WISPS: if (inner) { if (n < 0.3) top(B.moss); if (!wet) plant(x, H + 1, z, n2 > 0.92 ? B.deadBush : n2 < 0.06 ? B.fern : AIR); } break;
        default: break;
      }
      if (inner) occupied[i] = 2;   // no trees or undergrowth in an anomaly
    }
    if (a.kind == AK_ASHMOSS) zoneTree(ch, ox, oz, B, seed, a.x, a.y, a.z, T_DEAD);   // the tree the moss hangs from
  }
}

} // namespace dq::zone
