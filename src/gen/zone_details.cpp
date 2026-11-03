// gen/zone_details.cpp — the set pieces of docs/zone_details.md, from data/zone_details.json (see gen/zone_details.h).
// Entry kinds ("k"): a kit id (one prop: x, z, f facing N/NE/E.. or degrees, st lie state, v variant, look, roll, pitch, sink, y,
// tint); "row" (n props from x,z stepping dx,dz, kits from `set`, states from `st`, cycled); "scatter" (n props at random in
// `box` [x0,z0,x1,z1]); "rail" (a track along `pts`, on the ground or at a fixed `y`); "blocks" (a box [x0,y0,z0,x1,y1,z1]
// of block `b`, meta `m`; `soft`: only into air and plants); "keepout" (no planner wrecks in `box`).
#include "gen/zone_details.h"
#include "gen/zone_world.h"
#include "core/registry.h"
#include "common/fs.h"
#include <cmath>
#include <algorithm>
#include <mutex>
#include <memory>
#include <map>

namespace dq::zone {
namespace {

constexpr float D2R = 3.14159265f / 180.f;

std::vector<PropKit> makeKits() {
  std::vector<PropKit> k;
  auto add = [&](const char* id, float L, float W, float H, std::vector<PropBox> col, bool rail = false) { k.push_back({id, L, W, H, std::move(col), rail}); };
  auto car = [&](const char* id, float L, float W, float H) { add(id, L, W, H, {{-L / 2, .22f, -W / 2, L / 2, .85f, W / 2}, {-L * .27f, .85f, -W / 2 + .1f, L * .15f, H, W / 2 - .1f}}); };
  auto van = [&](const char* id, float L, float W, float H) { add(id, L, W, H, {{-L / 2, .3f, -W / 2, L / 2, H, W / 2}}); };
  // a lorry: the load (bed, tilt, tank) behind the cab
  auto truck = [&](const char* id, float L, float W, float H, float cabL, float cabH, float load) {
    add(id, L, W, H, {{-L / 2, .55f, -W / 2, L / 2 - cabL, load, W / 2}, {L / 2 - cabL, .55f, -W / 2 + .15f, L / 2, cabH, W / 2 - .15f}});
  };
  car("car_zhiguli", 4.1f, 1.6f, 1.4f); car("car_moskvich", 4.2f, 1.6f, 1.5f); car("car_volga", 4.7f, 1.8f, 1.5f); car("car_zaporozhets", 3.7f, 1.5f, 1.4f);
  add("uaz_469", 4.0f, 1.8f, 2.0f, {{-2, .4f, -.9f, 2, 1.2f, .9f}, {-1.9f, 1.2f, -.85f, .55f, 1.95f, .85f}});
  van("uaz_452", 4.4f, 1.9f, 2.1f); van("raf_2203", 4.9f, 1.9f, 2.0f);
  truck("zil_130", 6.7f, 2.5f, 2.4f, 2.4f, 2.3f, 1.6f); truck("zil_131", 7.0f, 2.5f, 2.9f, 2.5f, 2.5f, 2.9f); truck("ural_4320", 7.4f, 2.5f, 2.9f, 2.9f, 2.6f, 2.9f);
  truck("kraz_255", 8.6f, 2.8f, 3.0f, 3.4f, 2.9f, 2.0f); truck("gaz_53", 6.4f, 2.4f, 2.2f, 2.3f, 2.2f, 1.5f);
  truck("fire_zil", 7.6f, 2.5f, 3.0f, 2.5f, 2.6f, 2.7f); truck("tanker_zil", 7.0f, 2.5f, 2.6f, 2.4f, 2.4f, 2.6f);
  van("bus_laz695", 9.2f, 2.5f, 3.0f); van("bus_paz672", 7.2f, 2.4f, 2.9f); van("bus_ikarus", 16.5f, 2.5f, 3.1f);
  add("trolleybus", 11.8f, 2.5f, 3.4f, {{-5.9f, .3f, -1.25f, 5.9f, 3.1f, 1.25f}});
  add("btr_70", 7.5f, 2.8f, 2.3f, {{-3.75f, .35f, -1.4f, 3.75f, 2.0f, 1.4f}});
  add("btr_80", 7.6f, 2.9f, 2.4f, {{-3.8f, .35f, -1.45f, 3.8f, 2.05f, 1.45f}});
  add("brdm_2", 5.8f, 2.4f, 2.3f, {{-2.9f, .35f, -1.2f, 2.9f, 1.9f, 1.2f}});
  add("bmp_2", 6.7f, 3.1f, 2.5f, {{-3.35f, 0, -1.55f, 3.35f, 1.9f, 1.55f}});
  add("t_72", 9.5f, 3.6f, 2.2f, {{-4.75f, 0, -1.8f, 2.15f, 1.5f, 1.8f}, {-3.0f, 1.5f, -1.1f, .2f, 2.2f, 1.1f}});
  add("mi_8", 18.0f, 2.5f, 4.8f, {{-5.5f, .25f, -1.25f, 6.5f, 3.3f, 1.25f}, {-11.5f, 1.9f, -.4f, -5.5f, 2.9f, .4f}});
  add("tractor_mtz", 3.8f, 1.9f, 2.8f, {{-1.9f, 0, -.95f, 1.9f, 1.4f, .95f}, {-1.6f, 1.4f, -.7f, -.2f, 2.8f, .7f}});
  add("tractor_t40", 3.1f, 1.6f, 2.4f, {{-1.55f, 0, -.8f, 1.55f, 1.3f, .8f}});
  add("dozer_t130", 5.5f, 3.2f, 3.1f, {{-2.75f, 0, -1.6f, 2.75f, 1.8f, 1.6f}, {-2.2f, 1.8f, -.8f, -.6f, 3.1f, .8f}});
  add("excavator", 9.0f, 3.0f, 3.5f, {{-2.2f, 0, -1.5f, 2.2f, 3.2f, 1.5f}});
  add("crane_truck", 10.0f, 2.5f, 3.4f, {{-5, .55f, -1.25f, 5, 3.0f, 1.25f}});
  add("loco_tem2", 16.9f, 3.1f, 4.6f, {{-8.45f, .3f, -1.4f, 8.45f, 4.6f, 1.4f}}, true);
  add("loco_chme3", 17.2f, 3.1f, 4.6f, {{-8.6f, .3f, -1.4f, 8.6f, 4.6f, 1.4f}}, true);
  add("boxcar", 14.7f, 3.2f, 4.7f, {{-7.35f, .5f, -1.6f, 7.35f, 4.7f, 1.6f}}, true);
  add("gondola", 13.9f, 3.2f, 3.5f, {{-6.95f, .5f, -1.6f, 6.95f, 3.5f, 1.6f}}, true);
  add("tankcar", 12.0f, 3.0f, 4.4f, {{-6, .5f, -1.5f, 6, 4.2f, 1.5f}}, true);
  add("hopper", 14.0f, 3.2f, 3.9f, {{-7, .5f, -1.6f, 7, 3.9f, 1.6f}}, true);
  add("flatcar", 14.6f, 3.2f, 1.5f, {{-7.3f, .5f, -1.6f, 7.3f, 1.5f, 1.6f}}, true);
  add("passcar", 24.0f, 3.1f, 4.2f, {{-12, .5f, -1.55f, 12, 4.2f, 1.55f}}, true);
  add("boat_row", 4.0f, 1.4f, 0.6f, {{-2, 0, -.7f, 2, .6f, .7f}});
  add("bus_wreck_shell", 9.2f, 2.5f, 2.6f, {{-4.6f, 0, -1.25f, 4.6f, 2.6f, 1.25f}});
  return k;
}

struct RailLine { std::vector<vec2> pts; int y = -1; int x0, z0, x1, z1; };
struct BlockFill { BlockId b; u8 meta; int x0, y0, z0, x1, y1, z1; bool soft; };

struct Details {
  std::vector<Prop> props;
  std::unordered_map<i64, std::vector<int>> grid;        // 64-block cell -> props whose footprint touches it
  std::vector<RailLine> rails;
  std::vector<BlockFill> fills;
  std::unique_ptr<std::once_flag[]> once;
  std::unique_ptr<mat4[]> M;
  std::unique_ptr<std::array<float, 6>[]> box;            // resolved world AABB: x0, y0, z0, x1, y1, z1
};

float facingDeg(const json& f) {
  if (f.is_number()) return f.get<float>();
  if (!f.is_string()) return 0;
  static const std::map<std::string, float> F = {{"E", 0}, {"NE", 45}, {"N", 90}, {"NW", 135}, {"W", 180}, {"SW", 225}, {"S", 270}, {"SE", 315},
                                                 {"E-W", 0}, {"W-E", 180}, {"N-S", 270}, {"S-N", 90}};
  auto it = F.find(f.get<std::string>());
  return it == F.end() ? 0.f : it->second;
}

// a lie state (docs/zone_details.md 0) onto a prop: its look, roll, pitch, sink
void lieState(Prop& p, const std::string& st, double r1, double r2) {
  if (st == "burnt") { p.look = PL_BURNT; p.sink = .22f; }
  else if (st == "stripped") { p.look = PL_STRIPPED; p.sink = .3f; }
  else if (st == "side-L" || st == "side-R") { p.roll = st == "side-L" ? -90.f : 90.f; p.look = PL_STRIPPED; p.level = true; }
  else if (st == "roof") { p.roll = 180.f; p.look = r1 < .5 ? PL_BURNT : PL_STRIPPED; p.level = true; p.sink = .1f; }
  else if (st == "ditch") { p.roll = (r1 < .5 ? -1.f : 1.f) * (10.f + 15.f * (float)r2); p.pitch = -8.f; p.sink = .3f; p.look = r2 < .5 ? PL_STRIPPED : PL_OK; }
  else if (st == "sunk") { p.sink = .5f + .7f * (float)r1; p.look = r2 < .5 ? PL_BURNT : PL_STRIPPED; p.roll = (float)(r2 - .5) * 10.f; }
  else if (st == "stack") { p.sink = -1.35f; p.roll = (float)(r1 - .5) * 8.f; p.look = r2 < .6 ? PL_BURNT : PL_STRIPPED; p.level = true; }
  else if (st == "jack") { p.yaw += (r1 < .5 ? -1.f : 1.f) * (15.f + 20.f * (float)r2); p.roll = 5.f; p.sink = -.15f; p.level = true; }
}

Prop propFrom(const json& e, int kit, u32 seed, int salt) {
  Prop p; p.kit = (u16)kit;
  p.x = e.value("x", 0.f) + .5f; p.z = e.value("z", 0.f) + .5f;
  p.yaw = e.contains("f") ? facingDeg(e["f"]) : 0.f;
  p.var = (u8)e.value("v", 0); p.tint = (u8)e.value("tint", (int)(hashFloat((i32)seed, (int)p.x, (int)p.z, 61 + salt) * 255));
  const double r1 = hashFloat((i32)seed, (int)p.x, (int)p.z, 62 + salt), r2 = hashFloat((i32)seed, (int)p.x, (int)p.z, 63 + salt);
  p.yaw += (float)(r1 - .5) * 6.f;   // nothing was parked square
  lieState(p, e.value("st", std::string("ok")), r1, r2);
  if (e.contains("look")) { const std::string l = e["look"].get<std::string>(); p.look = l == "burnt" ? PL_BURNT : l == "stripped" ? PL_STRIPPED : PL_OK; }
  if (e.contains("roll")) p.roll = e["roll"].get<float>();
  if (e.contains("pitch")) p.pitch = e["pitch"].get<float>();
  if (e.contains("sink")) p.sink = e["sink"].get<float>();
  if (e.contains("yaw")) p.yaw = e["yaw"].get<float>();
  if (e.contains("y")) { p.y = e["y"].get<float>(); p.level = true; }
  if (e.value("level", false)) p.level = true;
  return p;
}

// the planner's wrecks (zone_plan wreck(): 0 car, 1 truck, 2 bus, 3 APC, 4 helicopter, 5 tractor) as props
void plannerWrecks(Details& D, u32 seed, const std::vector<std::array<float, 4>>& keep) {
  static const char* SETS[6][6] = {{"car_zhiguli", "car_moskvich", "car_volga", "car_zaporozhets", "uaz_469", "uaz_452"},
                                   {"zil_130", "zil_131", "ural_4320", "gaz_53", "kraz_255", "zil_130"},
                                   {"bus_laz695", "bus_paz672", "bus_laz695", "bus_paz672", "raf_2203", "bus_laz695"},
                                   {"btr_70", "btr_80", "brdm_2", "bmp_2", "btr_70", "uaz_469"},
                                   {"mi_8", "mi_8", "mi_8", "mi_8", "mi_8", "mi_8"},
                                   {"tractor_mtz", "tractor_t40", "tractor_mtz", "gaz_53", "tractor_mtz", "tractor_t40"}};
  const Plan& P = zonePlan(seed);
  for (const Placed& s : P.s) {
    if (s.type != S_WRECK) continue;
    const float cx = s.x0 + s.W * .5f, cz = s.z0 + s.D * .5f;
    bool skip = false;
    for (const auto& b : keep) if (cx >= b[0] && cx <= b[2] && cz >= b[1] && cz <= b[3]) { skip = true; break; }
    if (skip) continue;
    const double r0 = hashFloat((i32)s.seed, 1, 2, 3), r1 = hashFloat((i32)s.seed, 4, 5, 6), r2 = hashFloat((i32)s.seed, 7, 8, 9);
    const int k = propKitIndex(SETS[std::min<int>(s.variant, 5)][(int)(r0 * 6) % 6]);
    if (k < 0) continue;
    Prop p; p.kit = (u16)k; p.x = cx; p.z = cz; p.tint = (u8)(r1 * 255);
    p.yaw = (s.W >= s.D ? 0.f : 90.f) + (r1 < .5 ? 180.f : 0.f) + (float)(r2 - .5) * 24.f;
    p.var = (u8)(r2 * 3);
    lieState(p, r1 < .12 ? "ditch" : r1 < .5 ? "burnt" : r1 < .85 ? "stripped" : "ok", r2, r0);
    D.props.push_back(p);
  }
}

std::unique_ptr<Details> load(u32 seed) {
  auto D = std::make_unique<Details>();
  const Registry& R = reg();
  std::vector<std::array<float, 4>> keep;
  auto j = readJson(dataDir() / "zone_details.json");
  if (!j || !j->is_object() || !(*j)["items"].is_array()) DQ_WARN("zone: data/zone_details.json missing");
  else {
    int salt = 0;
    for (const json& e : (*j)["items"]) {
      if (!e.is_object()) continue;
      const std::string k = e.value("k", std::string());
      salt++;
      if (k == "rail") {
        RailLine L; L.y = e.value("y", -1);
        for (const json& q : e["pts"]) if (q.is_array() && q.size() >= 2) L.pts.push_back(vec2(q[0].get<float>(), q[1].get<float>()));
        if (L.pts.size() < 2) continue;
        L.x0 = L.z0 = INT32_MAX; L.x1 = L.z1 = INT32_MIN;
        for (vec2 v : L.pts) { L.x0 = std::min(L.x0, (int)v.x - 3); L.x1 = std::max(L.x1, (int)v.x + 3); L.z0 = std::min(L.z0, (int)v.y - 3); L.z1 = std::max(L.z1, (int)v.y + 3); }
        D->rails.push_back(std::move(L));
      } else if (k == "blocks") {
        const BlockId b = R.blockId(e.value("b", std::string("stone")));
        const json& bx = e["box"];
        if (!bx.is_array() || bx.size() < 6) continue;
        int v[6]; for (int q = 0; q < 6; q++) v[q] = bx[q].get<int>();
        D->fills.push_back({b, (u8)e.value("m", 0), std::min(v[0], v[3]), std::min(v[1], v[4]), std::min(v[2], v[5]), std::max(v[0], v[3]), std::max(v[1], v[4]), std::max(v[2], v[5]), e.value("soft", false)});
      } else if (k == "keepout") {
        const json& bx = e["box"];
        if (bx.is_array() && bx.size() >= 4) keep.push_back({bx[0].get<float>(), bx[1].get<float>(), bx[2].get<float>(), bx[3].get<float>()});
      } else if (k == "row" || k == "scatter") {
        std::vector<int> set; std::vector<std::string> st;
        for (const json& s : e["set"]) { const int ki = propKitIndex(s.get<std::string>()); if (ki >= 0) set.push_back(ki); else DQ_WARN("zone details: unknown kit %s", s.get<std::string>().c_str()); }
        if (e.contains("st")) for (const json& s : e["st"]) st.push_back(s.get<std::string>());
        if (set.empty()) continue;
        const int n = e.value("n", 1);
        float bx[4] = {0, 0, 0, 0};
        if (k == "scatter") { const json& b = e["box"]; for (int q = 0; q < 4; q++) bx[q] = b[q].get<float>(); keep.push_back({bx[0], bx[1], bx[2], bx[3]}); }
        for (int q = 0; q < n; q++) {
          const double h1 = hashFloat((i32)seed, salt, q, 71), h2 = hashFloat((i32)seed, salt, q, 72), h3 = hashFloat((i32)seed, salt, q, 73);
          if (h3 < e.value("skip", 0.0)) continue;
          json pe = e;
          if (k == "row") { pe["x"] = e.value("x", 0.f) + e.value("dx", 0.f) * q; pe["z"] = e.value("z", 0.f) + e.value("dz", 0.f) * q; }
          else { pe["x"] = bx[0] + (bx[2] - bx[0]) * h1; pe["z"] = bx[1] + (bx[3] - bx[1]) * h2; if (!e.contains("f")) pe["f"] = h3 * 360.0; }
          if (!st.empty()) pe["st"] = st[(k == "row" ? q : (int)(h3 * 977)) % st.size()];
          pe.erase("box");
          Prop p = propFrom(pe, set[(k == "row" ? q : (int)(h1 * 131 + h2 * 71)) % set.size()], seed, salt * 31 + q);
          if (k == "scatter" && !e.contains("f")) p.yaw = (float)(h3 * 360.0);
          D->props.push_back(p);
        }
      } else {
        const int ki = propKitIndex(k);
        if (ki < 0) { DQ_WARN("zone details: unknown kit %s", k.c_str()); continue; }
        D->props.push_back(propFrom(e, ki, seed, salt));
        const Prop& p = D->props.back();
        keep.push_back({p.x - 12, p.z - 12, p.x + 12, p.z + 12});
      }
    }
  }
  plannerWrecks(*D, seed, keep);
  const size_t n = D->props.size();
  D->once.reset(new std::once_flag[n]); D->M.reset(new mat4[n]); D->box.reset(new std::array<float, 6>[n]);
  const auto& K = propKits();
  for (size_t i = 0; i < n; i++) {   // index by a generous footprint (the resolved box is known only later)
    const Prop& p = D->props[i]; const float r = std::hypot(K[p.kit].L, K[p.kit].W) * .5f + 2;
    for (int cz = (int)std::floor((p.z - r) / 64); cz <= (int)std::floor((p.z + r) / 64); cz++)
      for (int cx = (int)std::floor((p.x - r) / 64); cx <= (int)std::floor((p.x + r) / 64); cx++) D->grid[cellKey(cx, cz)].push_back((int)i);
  }
  DQ_LOG("zone details: %zu props, %zu rail lines, %zu block fills", n, D->rails.size(), D->fills.size());
  return D;
}

Details& details(u32 seed) {
  static std::mutex mu;
  static std::map<u32, std::unique_ptr<Details>> cache;
  std::lock_guard<std::mutex> lk(mu);
  auto& d = cache[seed];
  if (!d) d = load(seed);
  return *d;
}

mat4 rotOf(const Prop& p, float yaw, float pitch, float roll) {
  (void)p;
  mat4 R = glm::rotate(mat4(1), yaw * D2R, vec3(0, 1, 0));
  R = glm::rotate(R, pitch * D2R, vec3(0, 0, 1));
  return glm::rotate(R, roll * D2R, vec3(1, 0, 0));
}

void resolve(Details& D, u32 seed, int i) {
  const Prop& p = D.props[i]; const PropKit& K = propKits()[p.kit];
  float pitch = p.pitch, roll = p.roll, base;
  if (p.y >= 0) base = p.y;
  else {
    const float c = std::cos(p.yaw * D2R), s = std::sin(p.yaw * D2R);
    const vec2 F(c, -s), Rt(s, c);   // its front and right side on the map
    auto gh = [&](vec2 o) { const Ground g = groundAt(seed, (int)std::floor(p.x + o.x), (int)std::floor(p.z + o.y)); return g.h; };
    const float hc = gh(vec2(0)), hf = gh(F * (K.L * .4f)), hb = gh(-F * (K.L * .4f)), hr = gh(Rt * (K.W * .4f)), hl = gh(-Rt * (K.W * .4f));
    base = hc + 1;
    if (!p.level) {
      if (std::abs(hf - hb) <= 4) { pitch += std::clamp(std::atan2(hf - hb, K.L * .8f) / D2R, -18.f, 18.f); base = (hf + hb + 2 * hc) * .25f + 1; }
      if (std::abs(hl - hr) <= 2) roll += std::clamp(std::atan2(hl - hr, K.W * .8f) / D2R, -10.f, 10.f);
    }
  }
  if (K.rail && p.roll == 0 && p.pitch == 0 && p.sink <= 0) base += .3f;   // on the rail heads
  const mat4 R = rotOf(p, p.yaw, pitch, roll);
  float minY = 1e9f, lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
  vec3 cs[8];
  for (int q = 0; q < 8; q++) { cs[q] = vec3(R * vec4(q & 1 ? K.L / 2 : -K.L / 2, q & 2 ? K.H : 0, q & 4 ? K.W / 2 : -K.W / 2, 1)); minY = std::min(minY, cs[q].y); }
  const vec3 at(p.x, base - minY - p.sink, p.z);
  D.M[i] = glm::translate(mat4(1), at) * R;
  for (int q = 0; q < 8; q++) for (int a = 0; a < 3; a++) { lo[a] = std::min(lo[a], cs[q][a] + at[a]); hi[a] = std::max(hi[a], cs[q][a] + at[a]); }
  D.box[i] = {lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]};
}

} // namespace

const std::vector<PropKit>& propKits() { static const std::vector<PropKit> k = makeKits(); return k; }
int propKitIndex(const std::string& id) {
  const auto& k = propKits();
  for (size_t i = 0; i < k.size(); i++) if (id == k[i].id) return (int)i;
  return -1;
}
const std::vector<Prop>& zoneProps(u32 seed) { return details(seed).props; }

void zonePropsIn(u32 seed, int x0, int z0, int x1, int z1, std::vector<int>& out) {
  out.clear();
  Details& D = details(seed);
  for (int cz = z0 >> 6; cz <= (z1 >> 6); cz++) for (int cx = x0 >> 6; cx <= (x1 >> 6); cx++) {
    auto it = D.grid.find(cellKey(cx, cz)); if (it == D.grid.end()) continue;
    for (int i : it->second) if (std::find(out.begin(), out.end(), i) == out.end()) out.push_back(i);
  }
}

const mat4& zonePropMatrix(u32 seed, int i) {
  Details& D = details(seed);
  std::call_once(D.once[i], [&] { resolve(D, seed, i); });
  return D.M[i];
}

bool zonePropNear(u32 seed, int x, int z, int r) {
  Details& D = details(seed);
  auto it = D.grid.find(cellKey(x >> 6, z >> 6)); if (it == D.grid.end()) return false;
  const auto& K = propKits();
  for (int i : it->second) {
    const Prop& p = D.props[i];
    const float c = std::cos(p.yaw * D2R), s = std::sin(p.yaw * D2R), dx = x + .5f - p.x, dz = z + .5f - p.z;
    const float a = dx * c - dz * s, b = dx * s + dz * c;   // along its length, across it
    if (std::abs(a) <= K[p.kit].L * .5f + r && std::abs(b) <= K[p.kit].W * .5f + r) return true;
  }
  return false;
}

void zoneDetailsChunk(Ctx& c, const Ground* G, u8* occupied) {
  Details& D = details(c.seed);
  const Registry& R = reg();
  const int ox = c.ox, oz = c.oz;
  auto soft = [&](BlockId b) { return b == AIR || R.block(b).replaceable || R.tRender[b] == 3 || R.block(b).has("isLeaves"); };
  // ---- the props' collision cells
  static thread_local std::vector<int> near;
  zonePropsIn(c.seed, ox, oz, ox + 15, oz + 15, near);
  const auto& K = propKits();
  for (int i : near) {
    const mat4& M = zonePropMatrix(c.seed, i);
    const auto& bx = D.box[i];
    const int x0 = std::max(ox, (int)std::floor(bx[0])), x1 = std::min(ox + 15, (int)std::floor(bx[3]));
    const int z0 = std::max(oz, (int)std::floor(bx[2])), z1 = std::min(oz + 15, (int)std::floor(bx[5]));
    if (x0 > x1 || z0 > z1) continue;
    const int y0 = std::max(1, (int)std::floor(bx[1])), y1 = std::min(254, (int)std::floor(bx[4]));
    const mat4 inv = glm::inverse(M);
    const auto& col = K[D.props[i].kit].col;
    auto inside = [&](vec3 w) {
      const vec3 l = vec3(inv * vec4(w, 1));
      for (const PropBox& b : col) if (l.x > b.x0 + .05f && l.x < b.x1 - .05f && l.y > b.y0 && l.y < b.y1 && l.z > b.z0 + .05f && l.z < b.z1 - .05f) return true;
      return false;
    };
    for (int wz = z0; wz <= z1; wz++) for (int wx = x0; wx <= x1; wx++) {
      bool any = false;
      for (int y = y0; y <= y1; y++) {
        const bool lo = inside(vec3(wx + .5f, y + .25f, wz + .5f)), hi = inside(vec3(wx + .5f, y + .75f, wz + .5f));
        if (!lo && !hi) continue;
        any = true;
        if (!soft(c.get(wx, y, wz))) continue;
        c.put(wx, y, wz, lo && !hi ? c.B.zPropHalf : c.B.zPropSolid);
      }
      if (any) occupied[(wz - oz) * 16 + (wx - ox)] = 3;
    }
  }
  // ---- the railways: ballast under three columns, the track (one block, its sleepers reaching over the sides) in the middle
  for (const RailLine& L : D.rails) {
    if (L.x1 < ox || L.x0 > ox + 15 || L.z1 < oz || L.z0 > oz + 15) continue;
    for (size_t s = 0; s + 1 < L.pts.size(); s++) {
      const vec2 a = L.pts[s], b = L.pts[s + 1];
      if (std::max(a.x, b.x) + 3 < ox || std::min(a.x, b.x) - 3 > ox + 15 || std::max(a.y, b.y) + 3 < oz || std::min(a.y, b.y) - 3 > oz + 15) continue;
      const bool alongX = std::abs(b.x - a.x) >= std::abs(b.y - a.y);
      const int n = (int)std::round(alongX ? std::abs(b.x - a.x) : std::abs(b.y - a.y));
      for (int q = 0; q <= n; q++) {
        const float t = n ? (float)q / n : 0.f;
        const int X = (int)std::floor(a.x + (b.x - a.x) * t + .5f), Z = (int)std::floor(a.y + (b.y - a.y) * t + .5f);
        if (X < ox - 1 || X > ox + 16 || Z < oz - 1 || Z > oz + 16) continue;
        int y = L.y;
        if (y < 0) { const Ground g = (X >= ox && X < ox + 16 && Z >= oz && Z < oz + 16) ? G[(Z - oz) * 16 + (X - ox)] : groundAt(c.seed, X, Z); if (g.water) continue; y = (int)std::round(g.h); }
        for (int sd = -1; sd <= 1; sd++) {
          const int cx = alongX ? X : X + sd, cz = alongX ? Z + sd : Z;
          if (!c.inChunk(cx, cz)) continue;
          const int ci = (cz - oz) * 16 + (cx - ox);
          if (L.y < 0) {
            const int top = (int)std::round(G[ci].h);
            if (top > y + 1 || G[ci].water) continue;
            for (int yy = top + 1; yy <= y; yy++) c.put(cx, yy, cz, c.B.zBallast);
            c.put(cx, y, cz, c.B.zBallast);
          }
          for (int yy = y + 1; yy <= y + 3; yy++) if (soft(c.get(cx, yy, cz)) && c.get(cx, yy, cz) != AIR && c.get(cx, yy, cz) != c.B.zRail) c.put(cx, yy, cz, AIR);
          if (sd == 0 && soft(c.get(cx, y + 1, cz))) c.put(cx, y + 1, cz, c.B.zRail, alongX ? 1 : 0);
          occupied[ci] = std::max<u8>(occupied[ci], 1);
        }
      }
    }
  }
  // ---- block work on the user's buildings (catwalks, stairs, scaffolds, lights)
  for (const BlockFill& f : D.fills) {
    if (f.x1 < ox || f.x0 > ox + 15 || f.z1 < oz || f.z0 > oz + 15) continue;
    for (int wz = std::max(f.z0, oz); wz <= std::min(f.z1, oz + 15); wz++) for (int wx = std::max(f.x0, ox); wx <= std::min(f.x1, ox + 15); wx++) {
      for (int y = std::max(1, f.y0); y <= std::min(254, f.y1); y++) if (!f.soft || soft(c.get(wx, y, wz))) c.put(wx, y, wz, f.b, f.meta);
      occupied[(wz - oz) * 16 + (wx - ox)] = std::max<u8>(occupied[(wz - oz) * 16 + (wx - ox)], 1);
    }
  }
}

} // namespace dq::zone
