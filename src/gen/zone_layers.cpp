// gen/zone_layers.cpp — reads data/zone_map/ (see zone_layers.h and tools/zone/trace_map.py).
#include "gen/zone_layers.h"
#include "common/fs.h"
#include <cmath>
#include <algorithm>
#include <mutex>

namespace dq::zone {
namespace {

struct Layers {
  int w = 0, h = 0, n = 0; float cell = 28, x0 = 0, z0 = 0;
  int idx[ML_COUNT] = {-1, -1, -1, -1, -1};
  std::vector<u8> data;
  std::vector<MapSite> sites;
  std::vector<Line> roads, rivers, rails;
};

const Layers& L() {
  static Layers l; static std::once_flag f;
  std::call_once(f, [] {
    auto meta = readJson(dataDir() / "zone_map" / "map.json");
    auto bin = readBinary(dataDir() / "zone_map" / "layers.bin");
    if (!meta || !bin) { DQ_WARN("data/zone_map missing: the Zone has no traced layout"); return; }
    const json& m = *meta;
    l.w = m.value("w", 0); l.h = m.value("h", 0); l.cell = m.value("cell", 28.f); l.x0 = m.value("x0", 0.f); l.z0 = m.value("z0", 0.f);
    static const char* names[ML_COUNT] = {"water", "wdist", "forest", "open", "built"};
    if (m.contains("layers")) for (size_t i = 0; i < m["layers"].size(); i++)
      for (int k = 0; k < ML_COUNT; k++) if (m["layers"][i] == names[k]) l.idx[k] = (int)i;
    l.n = m.contains("layers") ? (int)m["layers"].size() : 0;
    if (!zDecompress(bin->data(), bin->size(), l.data, (size_t)l.w * l.h * l.n) || l.data.size() != (size_t)l.w * l.h * l.n) {
      DQ_WARN("data/zone_map/layers.bin: bad size"); l.w = l.h = 0; l.data.clear();
    }
    if (m.contains("sites")) for (auto& s : m["sites"]) {
      MapSite t{s.value("x", 0.f), s.value("z", 0.f), s.value("r", 100.f), s.value("n", 0), s.value("built", 0.f), s.value("forest", 0.f), {}};
      if (s.contains("dots")) for (auto& d : s["dots"]) t.dots.push_back({d[0].get<float>(), d[1].get<float>()});
      l.sites.push_back(std::move(t));
    }
    if (m.contains("roads")) for (auto& r : m["roads"]) {
      Line ln; ln.kind = (u8)r.value("kind", 2); ln.width = r.value("width", 4.f);
      for (auto& p : r["pts"]) ln.pts.push_back({p[0].get<float>(), p[1].get<float>()});
      if (ln.pts.size() >= 2) l.roads.push_back(std::move(ln));
    }
    if (auto lj = readJson(dataDir() / "zone_map" / "lines.json")) {
      const float sc = lj->value("scale", 7.f);
      auto read = [&](const char* key, u8 kind, float w, std::vector<Line>& out) {
        if (lj->contains(key)) for (auto& r : (*lj)[key]) {
          Line ln; ln.kind = kind; ln.width = r.value("w", w);
          for (auto& p : r["pts"]) ln.pts.push_back({l.x0 + p[0].get<float>() * sc, l.z0 + p[1].get<float>() * sc});
          if (ln.pts.size() >= 2) out.push_back(std::move(ln));
        }
      };
      read("rivers", 0, 18.f, l.rivers); read("rails", 3, 4.f, l.rails);
    }
    DQ_LOG("zone map: %dx%d layers, %zu roads, %zu sites, %zu rivers, %zu rail lines", l.w, l.h, l.roads.size(), l.sites.size(), l.rivers.size(), l.rails.size());
  });
  return l;
}

} // namespace

float layerAt(MapLayer k, float x, float z) {
  const Layers& l = L();
  if (!l.w || l.idx[k] < 0) return 0.f;
  const float fx = (x - l.x0) / l.cell - 0.5f, fz = (z - l.z0) / l.cell - 0.5f;
  const int ix = (int)std::floor(fx), iz = (int)std::floor(fz);
  const float tx = fx - ix, tz = fz - iz;
  const u8* p = l.data.data() + (size_t)l.idx[k] * l.w * l.h;
  auto at = [&](int a, int b) { a = std::clamp(a, 0, l.w - 1); b = std::clamp(b, 0, l.h - 1); return (float)p[(size_t)b * l.w + a]; };
  const float v = (at(ix, iz) * (1 - tx) + at(ix + 1, iz) * tx) * (1 - tz) + (at(ix, iz + 1) * (1 - tx) + at(ix + 1, iz + 1) * tx) * tz;
  return v / 255.f;
}

const std::vector<MapSite>& mapSites() { return L().sites; }
std::vector<Line> mapRoads() { return L().roads; }
std::vector<Line> mapRivers() { return L().rivers; }
std::vector<Line> mapRails() { return L().rails; }

} // namespace dq::zone
