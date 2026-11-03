// gen/zone_world.cpp — reads the user's world (see zone_world.h): the tiles on demand into an LRU cache, the coarse layers
// once. A tile is inflated, its runs indexed per column and its natural stacks folded into the ground (the same rule as
// tools/zone/mc_layers.py: a column topped by natural blocks that are mostly solid down to the ground is ground there).
#include "gen/zone_world.h"
#include "common/fs.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <list>
#include <map>
#include <mutex>
#include <unordered_set>

namespace dq::zone {
namespace {

constexpr int N = 512, NN = N * N, CACHE = 40;

const std::unordered_set<std::string>& naturalSet() {
  static const std::unordered_set<std::string> s = {"stone", "grass", "dirt", "sand", "gravel", "clay", "sandstone", "red_sandstone", "coal_ore",
    "iron_ore", "gold_ore", "redstone_ore", "lit_redstone_ore", "lapis_ore", "diamond_ore", "emerald_ore", "quartz_ore", "monster_egg", "bedrock",
    "snow", "ice", "packed_ice", "mycelium", "farmland", "grass_path", "hardened_clay"};
  return s;
}
std::string base(const std::string& n) { const size_t c = n.find(':'); return c == std::string::npos ? n : n.substr(0, c); }

std::shared_ptr<WTile> loadTile(int rx, int rz) {
  auto bin = readBinary(dataDir() / "zone_world" / ("t." + std::to_string(rx) + "." + std::to_string(rz) + ".bin"));
  if (!bin) return nullptr;
  std::vector<u8> b;
  // ZWT1: no underground part; ZWT2 (alpha.9): each column's underground runs after the built ones
  if (!zDecompress(bin->data(), bin->size(), b, (size_t)12 << 20) || b.size() < 6 || (std::memcmp(b.data(), "ZWT1", 4) != 0 && std::memcmp(b.data(), "ZWT2", 4) != 0)) { DQ_WARN("zone world: bad tile %d %d", rx, rz); return nullptr; }
  const bool v2 = b[3] == '2';
  auto t = std::make_shared<WTile>(); t->rx = rx; t->rz = rz;
  size_t p = 4; auto u16at = [&](size_t q) { return (u16)(b[q] | (b[q + 1] << 8)); };
  const int np = u16at(p); p += 2;
  for (int i = 0; i < np && p < b.size(); i++) { const int l = b[p]; t->names.emplace_back((const char*)&b[p + 1], l); p += 1 + l; }
  if (p + (size_t)NN * 5 + 4 > b.size()) return nullptr;
  t->ground.assign(b.begin() + p, b.begin() + p + NN); p += NN;
  t->water.assign(b.begin() + p, b.begin() + p + NN); p += NN;
  t->surf.resize(NN); for (int i = 0; i < NN; i++) t->surf[i] = u16at(p + 2 * i); p += 2 * NN;
  t->cover.assign(b.begin() + p, b.begin() + p + NN); p += NN;
  const u32 nt = b[p] | (b[p + 1] << 8) | (b[p + 2] << 16) | ((u32)b[p + 3] << 24); p += 4;
  std::vector<WTree> tr(nt);
  for (u32 i = 0; i < nt && p + 7 <= b.size(); i++, p += 7) tr[i] = {u16at(p), u16at(p + 2), b[p + 4], b[p + 5], b[p + 6]};
  // trees by chunk
  t->treeOff.assign(1025, 0);
  for (const WTree& w : tr) t->treeOff[((w.z >> 4) & 31) * 32 + ((w.x >> 4) & 31) + 1]++;
  for (int i = 0; i < 1024; i++) t->treeOff[i + 1] += t->treeOff[i];
  t->trees.resize(nt); { std::vector<u32> at(t->treeOff.begin(), t->treeOff.end() - 1); for (const WTree& w : tr) t->trees[at[((w.z >> 4) & 31) * 32 + ((w.x >> 4) & 31)]++] = w; }
  // which palette entries are natural, which are a road's full block
  std::vector<u8> nat(t->names.size()), road(t->names.size());
  static const std::unordered_set<std::string> ROADFULL = {"soul_sand", "cobblestone", "mossy_cobblestone", "double_stone_slab", "stonebrick", "gravel", "stone"};
  for (size_t i = 0; i < t->names.size(); i++) { const std::string k = base(t->names[i]); nat[i] = naturalSet().count(k) ? 1 : 0; road[i] = ROADFULL.count(k) ? 1 : 0; }
  auto natural = [&](u16 pal) { return pal < nat.size() && nat[pal]; };
  // the runs, per column; fold the natural stacks into the ground; MC y -> ours
  t->runOff.resize(NN + 1); t->runs.reserve(1 << 16);
  std::vector<WRun> col;
  for (int i = 0; i < NN; i++) {
    t->runOff[i] = (u32)t->runs.size();
    if (p >= b.size()) continue;
    u32 n = 0; int sh = 0; u8 v;
    do { v = b[p++]; n |= (u32)(v & 127) << sh; sh += 7; } while ((v & 128) && p < b.size());
    col.clear();
    for (u32 k = 0; k < n && p + 4 <= b.size(); k++, p += 4) col.push_back({b[p], b[p + 1], u16at(p + 2)});
    int g = t->ground[i];
    if (!col.empty() && g > 0) {
      const WRun& top = col.back();
      const int ctop = top.y0 + top.len, cfirst = col.front().y0, span = ctop - g;
      int tot = 0, natc = 0;
      for (const WRun& r : col) { tot += r.len; if (natural(r.pal)) natc += r.len; }
      const bool contig = cfirst == g + 1 && tot == ctop - cfirst;
      if (natural(top.pal) && ((contig && span <= 8) || (natc * 3 >= span && tot * 4 >= span * 3))) { g = ctop - 1; t->surf[i] = top.pal; col.clear(); }
      else if (col.size() == 1 && cfirst == g + 1 && top.len == 1 && top.pal < road.size() && road[top.pal]) { g = g + 1; t->surf[i] = top.pal; col.clear(); }
    }
    t->ground[i] = g > 0 ? (u8)std::min(250, g + MC_DY) : 0;
    if (t->water[i]) t->water[i] = (u8)std::min(250, t->water[i] + MC_DY);
    for (const WRun& r : col) {
      const int y0 = r.y0 + MC_DY; if (y0 > 254) continue;
      t->runs.push_back({(u8)y0, (u8)std::min<int>(r.len, 255 - y0), r.pal});
    }
  }
  t->runOff[NN] = (u32)t->runs.size();
  t->runs.shrink_to_fit();
  if (v2) {
    t->uOff.resize(NN + 1);
    for (int i = 0; i < NN; i++) {
      t->uOff[i] = (u32)t->uruns.size();
      if (p >= b.size()) continue;
      u32 n = 0; int sh = 0; u8 v;
      do { v = b[p++]; n |= (u32)(v & 127) << sh; sh += 7; } while ((v & 128) && p < b.size());
      for (u32 k = 0; k < n && p + 4 <= b.size(); k++, p += 4) {
        const int y0 = b[p] + MC_DY; if (y0 > 254) continue;
        t->uruns.push_back({(u8)y0, (u8)std::min<int>(b[p + 1], 255 - y0), u16at(p + 2)});
      }
    }
    t->uOff[NN] = (u32)t->uruns.size();
  }
  return t;
}

struct Cache {
  std::mutex m;
  std::map<i64, std::shared_ptr<const WTile>> tiles;
  std::list<i64> lru;
  std::unordered_set<i64> missing;
  int have = -1;   // the world's data present at all
};
Cache& cache() { static Cache c; return c; }
i64 key(int rx, int rz) { return ((i64)rx << 32) ^ (u32)rz; }

struct Coarse {
  int w = 0, h = 0, cell = 8, x0 = 0, z0 = 0;
  std::vector<u8> data;
  std::vector<WSite> sites;
  std::vector<Line> roads;
  std::vector<std::array<int, 3>> chests;
};
const Coarse& coarse() {
  static Coarse c; static std::once_flag f;
  std::call_once(f, [] {
    auto meta = readJson(dataDir() / "zone_world" / "coarse.json");
    auto bin = readBinary(dataDir() / "zone_world" / "coarse.bin");
    if (!meta || !bin) { DQ_WARN("data/zone_world/coarse.* missing (run tools/zone/mc_layers.py)"); return; }
    const json& m = *meta;
    c.w = m.value("w", 0); c.h = m.value("h", 0); c.cell = m.value("cell", 8); c.x0 = m.value("x0", 0); c.z0 = m.value("z0", 0);
    if (!zDecompress(bin->data(), bin->size(), c.data, (size_t)c.w * c.h * CL_COUNT) || c.data.size() != (size_t)c.w * c.h * CL_COUNT) { DQ_WARN("coarse.bin: bad size"); c.w = c.h = 0; c.data.clear(); }
    if (m.contains("sites")) for (auto& s : m["sites"])
      c.sites.push_back({s.value("x", 0), s.value("z", 0), s.value("w", 1), s.value("d", 1), s.value("y", 66), s.value("h", 3), s.value("n", 1), s.value("cls", std::string("hut"))});
    if (m.contains("roads")) for (auto& r : m["roads"]) {
      Line l; l.kind = (u8)r.value("kind", 2); l.width = r.value("width", 4.f);
      for (auto& q : r["pts"]) l.pts.push_back({q[0].get<float>(), q[1].get<float>()});
      if (l.pts.size() >= 2) c.roads.push_back(std::move(l));
    }
    if (m.contains("chests")) for (auto& q : m["chests"]) c.chests.push_back({q[0].get<int>(), q[1].get<int>(), q[2].get<int>()});
    DQ_LOG("zone world: coarse %dx%d, %zu sites, %zu roads, %zu chests", c.w, c.h, c.sites.size(), c.roads.size(), c.chests.size());
  });
  return c;
}

} // namespace

bool mcNatural(const std::string& name) { return naturalSet().count(base(name)) > 0; }

bool worldHasData() { return !coarse().data.empty(); }

std::shared_ptr<const WTile> worldTile(int rx, int rz) {
  // the last tile each thread used, without the lock
  thread_local i64 lastK = 0x7fffffffffffffffLL; thread_local std::shared_ptr<const WTile> last;
  const i64 k = key(rx, rz);
  if (k == lastK) return last;
  Cache& C = cache();
  std::lock_guard<std::mutex> g(C.m);
  if (C.missing.count(k)) { lastK = k; last = nullptr; return nullptr; }
  auto it = C.tiles.find(k);
  if (it == C.tiles.end()) {
    std::shared_ptr<const WTile> t = loadTile(rx, rz);
    if (!t) { C.missing.insert(k); lastK = k; last = nullptr; return nullptr; }
    it = C.tiles.emplace(k, std::move(t)).first;
    C.lru.push_front(k);
    while ((int)C.tiles.size() > CACHE) { const i64 old = C.lru.back(); C.lru.pop_back(); C.tiles.erase(old); }
  } else { C.lru.remove(k); C.lru.push_front(k); }
  lastK = k; last = it->second;
  return last;
}

bool worldColumn(const WTile& t, int x, int z, WCol& c) {
  const int i = WTile::at(x, z);
  if (!t.ground[i]) return false;
  c.g = t.ground[i]; c.w = t.water[i]; c.cover = t.cover[i];
  c.surf = t.surf[i] < t.names.size() ? &t.names[t.surf[i]] : nullptr;
  c.runs = t.runs.data() + t.runOff[i]; c.nruns = (int)(t.runOff[i + 1] - t.runOff[i]);
  if (!t.uOff.empty()) { c.under = t.uruns.data() + t.uOff[i]; c.nunder = (int)(t.uOff[i + 1] - t.uOff[i]); } else { c.under = nullptr; c.nunder = 0; }
  return true;
}
bool worldColumn(int x, int z, WCol& c) {
  auto t = worldTileAt(x, z);
  return t && worldColumn(*t, x, z, c);
}

float coarseAt(CoarseLayer l, float x, float z) {
  const Coarse& c = coarse();
  if (!c.w) return l == CL_HEIGHT ? 66.f / 255.f : 0.f;
  const float fx = (x - c.x0) / c.cell - 0.5f, fz = (z - c.z0) / c.cell - 0.5f;
  const int ix = (int)std::floor(fx), iz = (int)std::floor(fz);
  const float tx = fx - ix, tz = fz - iz;
  const u8* p = c.data.data() + (size_t)l * c.w * c.h;
  auto at = [&](int a, int b) { a = std::clamp(a, 0, c.w - 1); b = std::clamp(b, 0, c.h - 1); return (float)p[(size_t)b * c.w + a]; };
  return ((at(ix, iz) * (1 - tx) + at(ix + 1, iz) * tx) * (1 - tz) + (at(ix, iz + 1) * (1 - tx) + at(ix + 1, iz + 1) * tx) * tz) / 255.f;
}

const std::vector<WSite>& worldSites() { return coarse().sites; }
const std::vector<Line>& worldRoads() { return coarse().roads; }
const std::vector<std::array<int, 3>>& worldChests() { return coarse().chests; }

} // namespace dq::zone
