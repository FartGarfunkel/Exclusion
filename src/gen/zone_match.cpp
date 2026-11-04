// gen/zone_match.cpp — reads data/zone_match/layout.bin (positions fitted from Anomaly's smart terrains,
// anomaly zones, radiation volumes, inventory boxes and a few props). No retail meshes.
#include "gen/zone_match.h"
#include "gen/zone_plan.h"
#include "gen/zone_build.h"
#include "common/fs.h"
#include "core/registry.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <unordered_map>

namespace dq::zone {
namespace {

struct Sm { int x, z; u8 kind, fac, slots; std::string name; };
struct Ano { int x, z; u8 r, fam, tier, count; };
struct Rad { int x, z, r; float peak; };

struct Match {
  bool ok = false;
  std::vector<Sm> smarts;
  std::vector<Ano> anos;
  std::vector<Rad> rads;
  std::vector<MatchStash> stashes;
  std::vector<MatchFurn> furn;
  std::unordered_map<i64, std::vector<int>> radGrid;
};

const Match& matchData() {
  static const Match m = [] {
    Match m;
    auto bin = readBinary(dataDir() / "zone_match" / "layout.bin");
    if (!bin || bin->size() < 28 || std::memcmp(bin->data(), "ZMAT", 4) != 0) return m;
    const u8* p = bin->data() + 4;
    const u8* end = bin->data() + bin->size();
    auto need = [&](size_t n) { return p + n <= end; };
    auto ru32 = [&] { ::dq::u32 v; std::memcpy(&v, p, 4); p += 4; return v; };
    auto ri32 = [&] { ::dq::i32 v; std::memcpy(&v, p, 4); p += 4; return v; };
    auto ru16 = [&] { ::dq::u16 v; std::memcpy(&v, p, 2); p += 2; return v; };
    auto ru8 = [&] { return *p++; };
    if (!need(24)) return m;
    if (ru32() != 1) return m;
    const u32 ns = ru32(), na = ru32(), nr = ru32(), nt = ru32(), nf = ru32();
    if (ns > 20000 || na > 20000 || nr > 20000 || nt > 20000 || nf > 20000) return m;
    m.smarts.reserve(ns); m.anos.reserve(na); m.rads.reserve(nr); m.stashes.reserve(nt); m.furn.reserve(nf);
    for (u32 i = 0; i < ns; i++) {
      if (!need(13)) return Match{};
      Sm s; s.x = ri32(); s.z = ri32(); s.kind = ru8(); s.fac = ru8(); s.slots = ru8(); (void)ru8();
      const u8 nl = ru8();
      if (!need(nl)) return Match{};
      s.name.assign(reinterpret_cast<const char*>(p), nl); p += nl;
      if (s.kind > SM_POST) s.kind = SM_CAMP;
      if (s.fac > F_MUTANT) s.fac = F_NONE;
      if (!s.slots) s.slots = 2;
      if (s.name.empty()) s.name = "Camp";
      m.smarts.push_back(std::move(s));
    }
    for (u32 i = 0; i < na; i++) {
      if (!need(12)) return Match{};
      Ano a; a.x = ri32(); a.z = ri32(); a.r = ru8(); a.fam = ru8(); a.tier = ru8(); a.count = ru8();
      if (a.fam >= A_COUNT) a.fam = A_THERMAL;
      if (!a.r) a.r = 3;
      if (!a.count) a.count = 1;
      m.anos.push_back(a);
    }
    for (u32 i = 0; i < nr; i++) {
      if (!need(12)) return Match{};
      Rad r; r.x = ri32(); r.z = ri32(); r.r = ru16(); r.peak = ru16() / 1000.f;
      if (r.r < 1) continue;
      m.rads.push_back(r);
    }
    for (u32 i = 0; i < nt; i++) {
      if (!need(13)) return Match{};
      MatchStash s; s.x = ri32(); s.z = ri32(); s.y = ri32(); s.danger = ru8();
      m.stashes.push_back(s);
    }
    for (u32 i = 0; i < nf; i++) {
      if (!need(13)) return Match{};
      MatchFurn f; f.x = ri32(); f.z = ri32(); f.y = ri32(); f.kind = ru8();
      if (f.kind > 10) continue;
      m.furn.push_back(f);
    }
    for (int i = 0; i < (int)m.rads.size(); i++) {
      const Rad& r = m.rads[i];
      const int x0 = (r.x - r.r) >> 7, x1 = (r.x + r.r) >> 7, z0 = (r.z - r.r) >> 7, z1 = (r.z + r.r) >> 7;
      for (int cz = z0; cz <= z1; cz++) for (int cx = x0; cx <= x1; cx++) m.radGrid[cellKey(cx, cz)].push_back(i);
    }
    m.ok = true;
    DQ_LOG("zone match: file has %zu smarts, %zu anomalies, %zu radiation volumes, %zu stashes, %zu furniture",
           m.smarts.size(), m.anos.size(), m.rads.size(), m.stashes.size(), m.furn.size());
    return m;
  }();
  return m;
}

float segDist(float x, float z, float ax, float az, float bx, float bz) {
  const float dx = bx - ax, dz = bz - az, L2 = dx * dx + dz * dz;
  const float t = L2 <= 1.f ? 0.f : std::clamp(((x - ax) * dx + (z - az) * dz) / L2, 0.f, 1.f);
  return std::hypot(ax + dx * t - x, az + dz * t - z);
}

} // namespace

bool zoneMatchLoaded() { return matchData().ok; }

float zoneMatchHot(int x, int z) {
  const Match& m = matchData();
  if (!m.ok) return 0.f;
  auto it = m.radGrid.find(cellKey(x >> 7, z >> 7));
  if (it == m.radGrid.end()) return 0.f;
  float hot = 0.f;
  for (int i : it->second) {
    const Rad& h = m.rads[i];
    const float dx = float(x - h.x), dz = float(z - h.z), d = std::sqrt(dx * dx + dz * dz);
    if (d >= (float)h.r) continue;
    const float t = 1.f - d / (float)h.r;
    hot = std::max(hot, t * t * h.peak);
  }
  return hot;
}

const std::vector<MatchStash>& zoneMatchStashes() { return matchData().stashes; }
const std::vector<MatchFurn>& zoneMatchFurn() { return matchData().furn; }

void zoneMatchApply(Plan& P, u32 seed) {
  const Match& m = matchData();
  if (!m.ok) return;
  const int storyN = (int)P.smarts.size();
  int placed = 0, people = 0, nearStory = 0, lairRoad = 0;
  for (const Sm& s : m.smarts) {
    if (!insideZone(s.x, s.z)) continue;
    if (s.kind == SM_LAIR) {
      const Region* r = regionAt(s.x, s.z);
      const bool cordon = r && (r->kind == K_VILLAGE || r->kind == K_SCRAPYARD);
      if (std::hypot(float(s.x - SPAWN_X), float(s.z - SPAWN_Z)) < 185.f || cordon
          || segDist((float)s.x, (float)s.z, (float)SPAWN_X, (float)SPAWN_Z, -4176.f, 2224.f) < 320.f) {
        lairRoad++; continue;
      }
    }
    bool busy = false;
    for (int i = 0; i < storyN; i++) {
      const Smart& e = P.smarts[i];
      if (std::abs(e.x - s.x) > 48 || std::abs(e.z - s.z) > 48) continue;
      if (std::hypot(float(e.x - s.x), float(e.z - s.z)) < 48.f) { busy = true; break; }
    }
    if (busy) { nearStory++; continue; }
    Smart n;
    n.x = s.x; n.z = s.z; n.y = (int)std::round(groundCoarse(seed, s.x, s.z).h);
    n.kind = (SmartKind)s.kind; n.faction = s.fac; n.slots = s.slots; n.garrison = 0;
    const Region* r = regionAt(s.x, s.z);
    n.region = r ? (int)(r - &regions()[0]) : -1;
    n.danger = r ? (u8)r->danger : (u8)wildDanger(s.x, s.z);
    n.name = s.name;
    P.smarts.push_back(std::move(n));
    placed++; people += s.slots;
  }
  for (const Ano& a : m.anos) {
    if (!insideZone(a.x, a.z)) continue;
    AnomalyField f;
    f.x = a.x; f.z = a.z; f.y = (int)std::round(groundCoarse(seed, a.x, a.z).h);
    f.r = a.r; f.family = a.fam; f.tier = a.tier; f.count = a.count;
    const int idx = (int)P.fields.size();
    P.fields.push_back(f);
    const int rad = f.r + 8;
    for (int cz = (f.z - rad) >> 6; cz <= (f.z + rad) >> 6; cz++)
      for (int cx = (f.x - rad) >> 6; cx <= (f.x + rad) >> 6; cx++)
        P.fieldGrid[cellKey(cx, cz)].push_back(idx);
  }
  DQ_LOG("zone match: placed %d smarts (%d people) and %d anomalies; %d sat on a story camp, %d lairs kept off the Cordon road",
         placed, people, (int)m.anos.size(), nearStory, lairRoad);
}

void dressMatchedFurniture(Ctx& c) {
  static const char* key[][2] = {
    {"zone_fridge", "zone_fridge_top"}, {"zone_wardrobe", "zone_wardrobe_top"}, {"zone_locker", "zone_locker_top"},
    {"zone_desk", nullptr}, {"zone_cupboard", nullptr}, {"zone_bedside", nullptr}, {"zone_filing", nullptr},
    {"zone_toolbox", nullptr}, {"zone_weapon_case", nullptr}, {"zone_ammo_box", nullptr}, {"zone_first_aid", nullptr},
  };
  static const char* lootKind[] = {"kitchen", "bedroom", "military", "office", "kitchen", "bedroom", "office", "workshop", "military", "military", "medical"};
  for (const MatchFurn& f : zoneMatchFurn()) {
    if (!c.inChunk(f.x, f.z) || f.kind > 10) continue;
    int y = -1;
    for (int dy : {0, -1, 1, -2, 2}) {
      const int yy = f.y + dy;
      if (yy < 2 || yy > 250) continue;
      const BlockId below = c.get(f.x, yy - 1, f.z), at = c.get(f.x, yy, f.z);
      if (!below || !reg().tSolid[below]) continue;
      if (at != AIR && !reg().tReplaceable[at]) continue;
      if (f.kind <= 2) {
        const BlockId up = c.get(f.x, yy + 1, f.z);
        if (up != AIR && !reg().tReplaceable[up]) continue;
      }
      y = yy; break;
    }
    if (y < 0) continue;
    const BlockId b = reg().blockId(key[f.kind][0]);
    if (!b) continue;
    const BlockId top = key[f.kind][1] ? reg().blockId(key[f.kind][1]) : 0;
    c.put(f.x, y, f.z, b, 0);
    if (top) c.put(f.x, y + 1, f.z, top, 0);
    json items = json::array();
    for (int i = 0; i < 9; i++) items.push_back(nullptr);
    c.entity(f.x, y, f.z, json{{"type", "chest"}, {"items", std::move(items)}, {"zone", lootKind[f.kind]}});
  }
}

} // namespace dq::zone
