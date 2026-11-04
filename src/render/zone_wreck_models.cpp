// render/zone_wreck_models.cpp — the Zone's wrecks as original voxel models. A Zhiguli-like sedan,
// a UAZ-like jeep, a short bus, a BTR-like APC, a transport helicopter, a tractor and a locomotive.
// Geometry is built here from boxes and cylinders (the same Canvas the guns and items use). Nothing is
// taken from Anomaly, Misery or STALZONE.
//
// The generator (gen/zone_build.cpp bWreck) stamps an invisible zone_wreck_hull on the authored spot.
// This layer draws the vehicle on that same footprint. Length runs along local +x, the nose at x = L.
// The lie (side, roof, ditch, jack, sunk, stack) is a matrix in that frame, then the placed yaw.
#include "render/zone_wreck_models.h"
#include "assets/zone_model_kit.h"
#include "assets/voxel.h"
#include "gen/zone_plan.h"
#include "world/dims.h"
#include "client/session.h"
#include "sim/zone.h"
#include "render/gl.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>

namespace dq {
namespace {
using namespace zk;

constexpr int V = 8;   // voxels per block; one voxel is 12.5 cm

struct Look {
  int body = 0, glass = 0, steel = 0, rubber = 0, lamp = 0, tail = 0, chrome = 0, cloth = 0, dark = 0, stripe = 0, roof = 0;
  bool wheels = true, glassOn = true, clothOn = true, rotor = true, burnt = false, stripped = false;
  int salt = 0;
};

int paintOf(int kind, int paint) {
  static const int car[5] = {0xC4B08A, 0x4A5634, 0x3A5268, 0xC8C6BC, 0x6E3830};
  static const int bus[5] = {0xD2C6A4, 0xE4E2D8, 0x3A4E68, 0x7A3030, 0x3E4A32};
  static const int apc[5] = {0x3E4C2E, 0x4A5A34, 0x556240, 0x2E3A24, 0x5A6844};
  static const int tr[5] = {0x8E2C28, 0x2E4A6A, 0xC6A43A, 0x3E5A30, 0x6A6E74};
  static const int loco[5] = {0x2E4A32, 0x6E3030, 0xD8D2C2, 0x4A5560, 0x5A5844};
  paint = (paint % 5 + 5) % 5;
  if (kind == zone::WK_APC) return apc[paint];
  if (kind == zone::WK_BUS) return bus[paint];
  if (kind == zone::WK_TRACTOR) return tr[paint];
  if (kind == zone::WK_LOCO) return loco[paint];
  return car[paint];
}

Look makeLook(int kind, int cond, int paint) {
  Look L;
  L.salt = paint * 17 + kind * 5 + 3;
  L.burnt = cond == 1;
  L.stripped = cond == 2;
  const bool bare = L.burnt || L.stripped;
  L.body = L.burnt ? 0x2A2622 : paintOf(kind, paint);
  L.glass = 0x8EAEBE;
  L.steel = L.burnt ? 0x3A3836 : 0x6A6E74;
  L.rubber = 0x1A1C1E;
  L.chrome = L.burnt ? 0x4A4846 : 0xC8CCD2;
  L.lamp = L.burnt ? 0x3A342C : (0xF2E6C0 | GLOW);
  L.tail = L.burnt ? 0x3A2824 : (0xC43028 | GLOW);
  L.cloth = 0x6A6844;
  L.dark = 0x16181A;
  L.stripe = kind == zone::WK_LOCO ? 0xE6E2D6 : 0x8E3030;
  L.roof = (kind == zone::WK_LOCO && !L.burnt) ? 0x8E2C28 : L.body;
  L.wheels = !bare;
  L.glassOn = !bare;
  L.clothOn = !bare;
  L.rotor = !bare;
  return L;
}

Col metalCol(int base, int salt) {
  return Col(ZCF {
    double k = 0.78 + 0.22 * hash3(x, y, z, salt);
    if (hash3(x >> 1, y, z >> 1, salt + 1) < 0.04) k *= 0.7;
    return shadeC(base, k);
  });
}
Col bodyCol(const Look& L) {
  const int base = L.body, salt = L.salt;
  const bool burnt = L.burnt, stripped = L.stripped;
  return Col(ZCF {
    if (burnt) {
      if (hash3(x >> 2, y >> 2, z >> 2, salt + 3) < 0.07) return -1;
      int c = shadeC(base, 0.42 + 0.7 * hash3(x, y, z, salt + 9));
      if (hash3(x >> 1, y, z >> 1, salt + 4) < 0.16) c = mixC(c, 0x5A3018, 0.6);
      return c;
    }
    if (stripped && y > 6 && hash3(x >> 3, y >> 3, z >> 3, salt + 6) < 0.18) return -1;
    int c = base;
    if (hash3(x, y, z, salt + 2) < 0.035) c = mixC(c, 0x6A3A22, 0.8);
    if (y < 4) c = shadeC(c, 0.8);
    else c = shadeC(c, 0.93 + 0.08 * hash3(x >> 2, y, z >> 2, salt));
    return c;
  });
}
Col glassCol(const Look& L) {
  const int g = L.glass, salt = L.salt;
  return Col(ZCF {
    if (((x + y) & 3) == 0) return shadeC(g, 0.72);
    if (hash3(x, y, z, salt) < 0.05) return shadeC(g, 0.5);
    return shadeC(g, 1.02);
  });
}

// A model in upright vehicle space: x along the length (nose at +x), y up, z across.
struct M {
  Canvas c;
  Look L;
  Col body, glass, steel, rubber, chrome, cloth;
  M(int X, int Y, int Z, Look look)
      : c(X, Y, Z), L(std::move(look)), body(bodyCol(L)), glass(glassCol(L)), steel(metalCol(L.steel, L.salt)),
        rubber(metalCol(L.rubber, L.salt + 2)), chrome(metalCol(L.chrome, L.salt + 1)), cloth(metalCol(L.cloth, L.salt + 4)) {}
  void box(int x0, int y0, int z0, int x1, int y1, int z1, const Col& col) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    if (z0 > z1) std::swap(z0, z1);
    c.box(x0, y0, z0, x1, y1, z1, col);
  }
  void cut(int x0, int y0, int z0, int x1, int y1, int z1) { box(x0, y0, z0, x1, y1, z1, Col(-1)); }
  void cell(int bx, int by, int bz, const Col& col) {
    box(bx * V, by * V, bz * V, bx * V + V - 1, by * V + V - 1, bz * V + V - 1, col);
  }
  // scoop a wheel arch out of the outer flanks (z), so the tyre reads. rad is in voxels.
  void arch(float cx, float cy, float rad, int z0, int z1) {
    const int x0 = (int)std::floor(cx - rad) - 1, x1 = (int)std::ceil(cx + rad) + 1;
    const int y0 = (int)std::floor(cy - rad) - 1, y1 = (int)std::ceil(cy + rad) + 1;
    const float r2 = rad * rad;
    for (int z = z0; z <= z1; z++) for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
      const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
      if (dx * dx + dy * dy <= r2) c.g.set(x, y, z, 0);
    }
  }
  void tyre(float cx, float cy, float rad, int z0, int z1) {
    if (!L.wheels || rad < 1.f) return;
    const int rubberC = L.rubber, steelC = L.chrome, salt = L.salt;
    c.cylZ(cx, cy, rad, rad, z0, z1, Col([=](int x, int y, int z) -> int {
      const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
      const float d = std::sqrt(dx * dx + dy * dy);
      if (d > rad - 1.35f) {
        const int ang = (int)std::floor(std::atan2(dy, dx) * 5.5f);
        return shadeC(rubberC, (ang & 1) ? 0.62 : 1.0);
      }
      if (d < rad * 0.22f) return shadeC(steelC, 1.05);
      if (d < rad * 0.48f) return shadeC(0x2C2E32, 0.85 + 0.2 * hash3(x, y, z, salt));
      return 0x121416;
    }));
  }
  void lampX(int x0, int x1, float cy, float cz, float r, bool tail = false) {
    const int bezel = L.burnt ? L.dark : L.chrome;
    const int glow = tail ? L.tail : L.lamp;
    c.cylX(cy, cz, r + 0.8f, r + 0.8f, x0, x1, Col(bezel));
    c.cylX(cy, cz, r, r, x0, x1, Col(glow));
  }
};

// ---------------------------------------------------------------- sedan (Zhiguli): long bonnet, short boot, round lamps
void buildCar(M& m) {
  for (int x = 0; x < 5; x++) m.cell(x, 0, 1, m.body);
  m.cell(0, 0, 0, m.body); m.cell(0, 0, 2, m.body);
  m.cell(4, 0, 0, m.body); m.cell(4, 0, 2, m.body);
  for (int z = 0; z < 3; z++) for (int x = 0; x < 5; x++) m.cell(x, 1, z, m.body);
  m.box(0, 0, 9, 39, 3, 14, Col(m.L.dark));                 // belly
  m.cut(12, 8, 6, 21, 13, 17);                              // cabin
  m.box(12, 8, 6, 17, 10, 10, m.cloth);
  m.box(12, 10, 6, 14, 13, 10, m.cloth);
  m.box(12, 8, 14, 17, 10, 17, m.cloth);
  m.box(12, 10, 14, 14, 13, 17, m.cloth);
  m.c.cylX(11.f, 12.f, 2.1f, 2.1f, 19, 21, m.steel);        // wheel
  // side glass, then a split screen and a rear window
  m.cut(10, 9, 0, 15, 14, 6); m.cut(18, 9, 0, 22, 14, 6);
  m.cut(10, 9, 17, 15, 14, 23); m.cut(18, 9, 17, 22, 14, 23);
  m.cut(22, 9, 3, 25, 14, 10); m.cut(22, 9, 13, 25, 14, 20);
  m.cut(0, 10, 6, 2, 13, 17);
  if (m.L.glassOn) {
    m.box(10, 9, 0, 15, 14, 1, m.glass); m.box(18, 9, 0, 22, 14, 1, m.glass);
    m.box(10, 9, 22, 15, 14, 23, m.glass); m.box(18, 9, 22, 22, 14, 23, m.glass);
    m.box(24, 9, 3, 25, 14, 10, m.glass); m.box(24, 9, 13, 25, 14, 20, m.glass);
    m.box(0, 10, 6, 1, 13, 17, m.glass);
  }
  m.cut(0, 15, 0, 6, 15, 23);                               // boot lid sits under the roof
  m.cut(36, 14, 0, 39, 15, 23);                             // bonnet rake
  m.cut(38, 12, 0, 39, 13, 23);
  m.box(8, 8, 0, 8, 14, 0, m.steel); m.box(16, 8, 0, 16, 14, 0, m.steel);
  m.box(8, 8, 23, 8, 14, 23, m.steel); m.box(16, 8, 23, 16, 14, 23, m.steel);
  m.arch(12.f, 5.f, 6.4f, 0, 4); m.arch(12.f, 5.f, 6.4f, 19, 23);
  m.arch(28.f, 5.f, 6.4f, 0, 4); m.arch(28.f, 5.f, 6.4f, 19, 23);
  m.tyre(12.f, 5.f, 4.8f, 0, 7); m.tyre(12.f, 5.f, 4.8f, 16, 23);
  m.tyre(28.f, 5.f, 4.8f, 0, 7); m.tyre(28.f, 5.f, 4.8f, 16, 23);
  m.box(0, 1, 1, 2, 4, 22, m.chrome);
  m.box(37, 1, 1, 39, 4, 22, m.chrome);
  m.box(37, 5, 7, 39, 10, 16, Col(m.L.dark));
  for (int i = 0; i < 4; i++) m.box(38, 6 + i, 7, 39, 6 + i, 16, m.chrome);
  m.lampX(36, 39, 7.f, 4.f, 2.3f);
  m.lampX(36, 39, 7.f, 20.f, 2.3f);
  m.lampX(0, 2, 8.f, 4.f, 1.5f, true);
  m.lampX(0, 2, 8.f, 20.f, 1.5f, true);
  m.box(14, 10, 0, 16, 11, 1, m.chrome);
  m.box(14, 10, 22, 16, 11, 23, m.chrome);
}

// ---------------------------------------------------------------- UAZ-like pickup: flat face, round lamps, canvas bed, three axles
void buildJeep(M& m) {
  for (int x = 0; x < 8; x++) m.cell(x, 0, 1, m.body);
  for (int x = 0; x <= 4; x++) for (int z = 0; z < 3; z++) m.cell(x, 1, z, m.body);
  if (m.L.clothOn) for (int x = 0; x <= 4; x++) { m.cell(x, 2, 0, m.body); m.cell(x, 2, 2, m.body); }
  for (int y = 1; y <= 2; y++) for (int z = 0; z < 3; z++) for (int x = 5; x <= 7; x++) m.cell(x, y, z, m.body);
  m.box(0, 8, 8, 39, 9, 15, Col(m.L.dark));                 // bed floor
  m.cut(16, 8, 8, 23, 15, 15);                              // the crate's cell
  m.cut(42, 10, 8, 58, 20, 15);                             // cab
  m.box(44, 10, 9, 52, 13, 14, m.cloth);
  m.box(44, 12, 9, 47, 16, 14, m.cloth);
  m.c.cylX(14.f, 12.f, 2.2f, 2.2f, 54, 56, m.steel);
  m.cut(41, 10, 0, 54, 14, 6); m.cut(41, 10, 17, 54, 14, 23);
  m.cut(60, 12, 9, 63, 20, 11); m.cut(60, 12, 13, 63, 20, 15);
  if (m.L.glassOn) {
    m.box(41, 10, 0, 54, 14, 1, m.glass); m.box(41, 10, 22, 54, 14, 23, m.glass);
    m.box(62, 12, 9, 63, 20, 11, m.glass); m.box(62, 12, 13, 63, 20, 15, m.glass);
  }
  // vertical-slat grille under the upright screen, lamps on the flat wings
  m.box(61, 8, 9, 63, 11, 15, Col(m.L.dark));
  for (int i = 0; i < 4; i++) m.box(62, 8, 9 + i * 2, 63, 11, 9 + i * 2, m.chrome);
  m.lampX(60, 63, 12.f, 4.f, 2.5f);
  m.lampX(60, 63, 12.f, 20.f, 2.5f);
  m.box(61, 8, 0, 63, 11, 2, m.chrome);
  m.box(61, 8, 21, 63, 11, 23, m.chrome);
  if (m.L.clothOn) {
    m.box(0, 16, 0, 39, 22, 6, m.cloth);
    m.box(0, 16, 17, 39, 22, 23, m.cloth);
    for (int x = 0; x < 40; x += 4) {
      m.box(x, 16, 0, x, 21, 6, Col(shadeC(m.L.cloth, 0.72)));
      m.box(x, 16, 17, x, 21, 23, Col(shadeC(m.L.cloth, 0.72)));
    }
    m.box(0, 21, 0, 39, 23, 7, Col(0x6B4A2C));
    m.box(0, 21, 16, 39, 23, 23, Col(0x6B4A2C));
    m.box(2, 20, 4, 38, 21, 19, m.cloth);                   // a tarp over the bed
  }
  if (m.L.wheels) {
    m.c.cylX(12.f, 12.f, 5.f, 5.f, 0, 2, m.rubber);         // spare on the tailgate
    m.c.cylX(12.f, 12.f, 2.f, 2.f, 0, 2, m.chrome);
  }
  m.c.cylY(50.f, 2.f, 0.6f, 0.6f, 16, 23, m.steel);         // aerial
  for (float cx : {12.f, 28.f, 52.f}) {
    m.arch(cx, 5.f, 6.2f, 0, 4); m.arch(cx, 5.f, 6.2f, 19, 23);
    m.tyre(cx, 5.f, 4.8f, 0, 7); m.tyre(cx, 5.f, 4.8f, 16, 23);
  }
  m.box(4, 9, 1, 8, 14, 4, Col(0x4A5A30));                  // a can on the side
}

// ---------------------------------------------------------------- bus: a shell you can walk through, a row of windows, one door
void buildBus(M& m) {
  constexpr int L = 11, W = 4;
  for (int x = 0; x < L; x++) { m.cell(x, 0, 1, m.body); m.cell(x, 0, 2, m.body); }
  for (int y = 1; y <= 3; y++) for (int x = 0; x < L; x++) for (int z = 0; z < W; z++) {
    const bool skin = z == 0 || z == W - 1 || y == 3 || x == 0 || x == L - 1;
    if (!skin || (y == 1 && x == 3 && z == 0)) continue;
    m.cell(x, y, z, m.body);
  }
  m.box(4, 6, 8, 84, 7, 23, Col(m.L.dark));                 // floor
  m.cut(25, 16, 0, 30, 20, 7);                              // the door reaches the window band
  m.box(24, 8, 0, 24, 20, 2, m.steel);
  m.box(31, 8, 0, 31, 20, 2, m.steel);
  for (int x = 1; x < L - 1; x++) if (x % 2 == 0) {
    const int x0 = x * V + 1, x1 = x * V + 6;
    m.cut(x0, 17, 0, x1, 22, 6); m.cut(x0, 17, 25, x1, 22, 31);
    if (m.L.glassOn) { m.box(x0, 17, 0, x1, 22, 1, m.glass); m.box(x0, 17, 30, x1, 22, 31, m.glass); }
  }
  m.cut(84, 17, 9, 87, 22, 22);                             // windscreen
  if (m.L.glassOn) m.box(86, 17, 9, 87, 22, 22, m.glass);
  m.cut(0, 17, 10, 2, 21, 21);                              // rear glass
  if (m.L.glassOn) m.box(0, 17, 10, 1, 21, 21, m.glass);
  m.box(85, 24, 10, 87, 29, 21, Col(0x14160F));             // route board
  if (!m.L.burnt) m.box(87, 25, 12, 87, 28, 19, Col(0xE8D48A | GLOW));
  if (!m.L.burnt) {
    m.box(6, 14, 0, 82, 16, 1, Col(m.L.stripe));
    m.box(6, 14, 30, 82, 16, 31, Col(m.L.stripe));
  }
  m.cut(86, 29, 0, 87, 31, 31);                             // a small rake on the front crown
  for (int x = 1; x <= 9; x++) if (x != 3) {
    const int x0 = x * V + 1, x1 = x * V + 6;
    m.box(x0, 8, 8, x1, 11, 12, m.cloth);
    m.box(x0, 11, 8, x0 + 2, 16, 12, m.cloth);
    m.box(x0, 8, 19, x1, 11, 23, m.cloth);
    m.box(x1 - 2, 11, 19, x1, 16, 23, m.cloth);
  }
  m.arch(20.f, 5.5f, 6.5f, 0, 5); m.arch(20.f, 5.5f, 6.5f, 26, 31);
  m.arch(68.f, 5.5f, 6.5f, 0, 5); m.arch(68.f, 5.5f, 6.5f, 26, 31);
  m.tyre(20.f, 5.5f, 5.1f, 0, 3); m.tyre(20.f, 5.5f, 5.1f, 4, 7);     // duals at the back
  m.tyre(20.f, 5.5f, 5.1f, 24, 27); m.tyre(20.f, 5.5f, 5.1f, 28, 31);
  m.tyre(68.f, 5.5f, 5.1f, 1, 6); m.tyre(68.f, 5.5f, 5.1f, 25, 30);
  m.lampX(84, 87, 12.f, 4.f, 2.2f); m.lampX(84, 87, 12.f, 28.f, 2.2f);
  m.lampX(0, 2, 11.f, 11.f, 1.4f, true); m.lampX(0, 2, 11.f, 20.f, 1.4f, true);
}

// ---------------------------------------------------------------- BTR-like: boat hull, four axles, a turret and a short gun
void buildApc(M& m) {
  for (int x = 0; x < 8; x++) { m.cell(x, 0, 1, m.body); m.cell(x, 0, 2, m.body); }
  for (int x = 0; x < 8; x++) for (int z = 0; z < 4; z++) {
    if (x == 7 && (z == 0 || z == 3)) continue;             // the nose narrows
    m.cell(x, 1, z, m.body);
  }
  for (int x = 3; x <= 4; x++) for (int z = 1; z <= 2; z++) m.cell(x, 2, z, m.body);
  m.cut(16, 8, 8, 23, 15, 15);                              // hatch the crate sits in
  // round the turret off the 2x2 block
  for (int z = 8; z <= 23; z++) for (int y = 16; y <= 23; y++) for (int x = 24; x <= 39; x++) {
    const float dx = x + 0.5f - 32.f, dz = z + 0.5f - 16.f;
    if (dx * dx + dz * dz > 7.3f * 7.3f) m.c.g.set(x, y, z, 0);
  }
  m.c.cylY(32.f, 16.f, 2.4f, 2.4f, 22, 23, Col(m.L.dark));  // hatch
  for (int i = 0; i < 8; i++) {                             // glacis and the cut-back cheeks
    m.cut(56 + i, 0, 8, 63, i, 23);
    m.cut(56 + i, 8, 0, 63, 15, i);
    m.cut(56 + i, 8, 31 - i, 63, 15, 31);
  }
  m.box(0, 14, 0, 52, 15, 1, m.steel);
  m.box(0, 14, 30, 52, 15, 31, m.steel);
  m.box(62, 11, 11, 63, 13, 14, Col(m.L.dark));             // vision slits
  m.box(62, 11, 17, 63, 13, 20, Col(m.L.dark));
  m.lampX(58, 63, 7.f, 12.f, 1.8f);
  m.lampX(58, 63, 7.f, 19.f, 1.8f);
  if (m.L.rotor) {
    m.box(40, 17, 9, 48, 23, 14, m.body);                   // mantlet
    m.c.cylX(20.f, 12.f, 1.6f, 1.6f, 44, 62, m.steel);
  }
  for (float cx : {12.f, 28.f, 44.f, 52.f}) {
    m.arch(cx, 5.f, 6.2f, 0, 4); m.arch(cx, 5.f, 6.2f, 27, 31);
    m.tyre(cx, 5.f, 4.9f, 0, 7); m.tyre(cx, 5.f, 4.9f, 24, 31);
    m.box((int)cx - 5, 9, 0, (int)cx + 5, 12, 2, m.body);   // a lip over the tyre
    m.box((int)cx - 5, 9, 29, (int)cx + 5, 12, 31, m.body);
  }
}

// ---------------------------------------------------------------- transport helicopter: box cabin, skids, tail, a drooping rotor
void buildHeli(M& m) {
  m.box(40, 0, 8, 87, 2, 15, m.steel);                      // skid shoes, the bottoms of the hull cells
  m.box(40, 0, 24, 87, 2, 31, m.steel);
  m.c.cylX(4.f, 12.f, 1.5f, 1.5f, 46, 82, m.steel);
  m.c.cylX(4.f, 28.f, 1.5f, 1.5f, 46, 82, m.steel);
  m.box(50, 3, 11, 53, 8, 13, m.steel); m.box(74, 3, 11, 77, 8, 13, m.steel);
  m.box(50, 3, 27, 53, 8, 29, m.steel); m.box(74, 3, 27, 77, 8, 29, m.steel);
  m.box(32, 8, 8, 87, 23, 31, m.body);                      // cabin, blocks x 4..10, y 1..2, z 1..3
  m.cut(38, 11, 12, 80, 20, 27);
  // cheek and nose: glass down the centre, body on the cheeks, the tip is glass
  for (int y = 1; y <= 2; y++) {
    m.cell(11, y, 1, m.body); m.cell(11, y, 3, m.body);
    m.cell(12, y, 1, m.body); m.cell(12, y, 3, m.body);
    if (m.L.glassOn) {
      m.cell(11, y, 2, m.glass); m.cell(12, y, 2, m.glass); m.cell(13, y, 2, m.glass);
    }
  }
  m.cut(100, 8, 8, 111, 12, 10); m.cut(100, 8, 29, 111, 12, 31);   // nose cheeks fall away
  m.cut(106, 8, 14, 111, 11, 25);
  m.cut(108, 20, 14, 111, 23, 25);
  for (int i = 0; i < 4; i++) {                             // cabin windows
    const int x0 = 44 + i * 9;
    m.cut(x0, 13, 8, x0 + 5, 19, 12);
    m.cut(x0, 13, 27, x0 + 5, 19, 31);
    if (m.L.glassOn) {
      m.box(x0, 13, 8, x0 + 5, 19, 9, m.glass);
      m.box(x0, 13, 30, x0 + 5, 19, 31, m.glass);
    }
  }
  m.cut(62, 10, 8, 74, 20, 12);                             // side door
  m.box(62, 10, 8, 74, 13, 10, m.steel);
  if (m.L.glassOn) m.box(62, 14, 8, 74, 19, 9, m.glass);
  m.box(48, 22, 14, 72, 23, 25, m.steel);                   // engine deck
  m.c.cylX(20.f, 9.f, 1.2f, 1.2f, 50, 76, Col(m.L.dark));
  m.box(2, 18, 18, 18, 22, 21, m.body);                      // tail boom, thinning toward the fin
  m.box(14, 17, 17, 40, 23, 22, m.body);
  m.box(8, 24, 18, 15, 38, 21, m.body);                     // fin
  m.cut(8, 35, 18, 10, 38, 21);
  if (m.L.rotor) {
    m.c.cylY(60.f, 20.f, 2.6f, 2.6f, 26, 38, m.steel);      // mast
    m.box(32, 27, 18, 39, 31, 21, m.steel);                 // blades, drooping a block at the tips
    m.box(80, 27, 18, 87, 31, 21, m.steel);
    m.box(40, 32, 18, 79, 36, 21, m.steel);
    m.box(58, 27, 0, 63, 31, 7, m.steel);
    m.box(58, 27, 32, 63, 31, 39, m.steel);
    m.box(58, 32, 8, 63, 36, 31, m.steel);
    m.c.cylZ(12.f, 30.f, 6.f, 6.f, 15, 17, m.steel);        // tail rotor
    m.c.cylZ(12.f, 30.f, 1.8f, 1.8f, 15, 18, m.chrome);
  }
}

// ---------------------------------------------------------------- tractor: huge rear tyres, a narrow nose, a cab
void buildTractor(M& m) {
  m.tyre(12.f, 7.2f, 7.f, 0, 7);
  m.tyre(12.f, 7.2f, 7.f, 16, 23);
  m.tyre(36.f, 3.6f, 3.5f, 0, 7);
  m.tyre(36.f, 3.6f, 3.5f, 16, 23);
  m.box(8, 12, 0, 16, 15, 7, m.body);                        // rear fenders
  m.box(8, 12, 16, 16, 15, 23, m.body);
  m.box(10, 6, 6, 14, 9, 17, m.steel);                      // rear axle
  for (int x = 2; x <= 4; x++) m.cell(x, 1, 1, m.body);
  m.cell(1, 2, 1, m.body); m.cell(2, 2, 1, m.body); m.cell(3, 2, 1, m.body);
  m.box(18, 8, 9, 39, 15, 14, m.body);                      // hood over the spine
  m.cut(34, 13, 9, 39, 15, 14);
  m.cut(37, 11, 10, 39, 14, 13);
  m.cut(22, 17, 9, 23, 22, 14);                             // screen
  m.cut(17, 17, 8, 21, 21, 9); m.cut(17, 17, 14, 21, 21, 15);
  if (m.L.glassOn) {
    m.box(22, 17, 9, 23, 22, 14, m.glass);
    m.box(17, 17, 8, 21, 21, 8, m.glass);
    m.box(17, 17, 15, 21, 21, 15, m.glass);
  } 
  m.box(10, 16, 10, 15, 18, 13, m.cloth);                   // seat
  m.box(10, 18, 10, 12, 22, 13, m.cloth);
  m.c.cylX(15.f, 12.f, 2.f, 2.f, 18, 19, m.steel);
  m.c.cylY(30.f, 14.f, 1.3f, 1.3f, 15, 27, Col(m.L.dark));  // stack
  m.lampX(37, 39, 11.f, 10.5f, 1.4f);
  m.lampX(37, 39, 11.f, 12.5f, 1.4f);
  m.box(16, 8, 10, 22, 11, 13, m.steel);                    // hitch
  m.box(14, 9, 11, 16, 11, 12, m.steel);
}

// ---------------------------------------------------------------- locomotive: long hood, a cab, a white stripe, bogies
void buildLoco(M& m) {
  for (int x = 0; x < 16; x++) for (int z = 0; z < 4; z++) m.cell(x, 1, z, m.body);
  for (int y = 2; y <= 3; y++) for (int x = 0; x <= 9; x++) { m.cell(x, y, 1, m.body); m.cell(x, y, 2, m.body); }
  if (!m.L.burnt) for (int x = 0; x <= 13; x++) {
    m.cell(x, 2, 0, Col(m.L.stripe));
    m.cell(x, 2, 3, Col(m.L.stripe));
  }
  for (int y = 2; y <= 4; y++) for (int x = 10; x <= 13; x++) for (int z = 0; z < 4; z++) {
    if (y == 4 || z == 0 || z == 3 || x == 10 || x == 13) m.cell(x, y, z, m.body);
  }
  for (int x = 14; x <= 15; x++) { m.cell(x, 2, 1, m.body); m.cell(x, 2, 2, m.body); }
  m.cut(4, 29, 8, 76, 31, 9);                               // round the hood's shoulders
  m.cut(4, 29, 22, 76, 31, 23);
  m.cut(4, 30, 10, 76, 31, 11);
  m.cut(4, 30, 20, 76, 31, 21);
  m.cut(120, 20, 8, 127, 23, 10);
  m.cut(120, 20, 21, 127, 23, 23);
  m.cut(88, 25, 0, 103, 30, 6); m.cut(88, 25, 25, 103, 30, 31);
  if (m.L.glassOn) {
    m.box(88, 25, 0, 103, 30, 1, m.glass);
    m.box(88, 25, 30, 103, 30, 31, m.glass);
  }
  m.cut(108, 25, 10, 111, 30, 21);                          // cab front windows
  if (m.L.glassOn) m.box(110, 25, 10, 111, 30, 21, m.glass);
  m.box(88, 16, 10, 100, 20, 21, m.cloth);                  // bench
  m.box(80, 36, 1, 111, 39, 30, Col(m.L.roof));
  m.c.cylY(36.f, 16.f, 1.8f, 1.8f, 26, 31, Col(m.L.dark));  // exhaust
  m.lampX(124, 127, 20.f, 16.f, 2.4f);
  m.box(0, 10, 14, 5, 14, 17, m.steel);                     // coupler
  m.box(82, 18, 1, 83, 34, 2, m.steel);                     // grab irons
  m.box(82, 18, 29, 83, 34, 30, m.steel);
  for (int i = 0; i < 8; i++) {
    const float cx = 4.f + i * 16.f;
    m.tyre(cx, 4.3f, 4.1f, 0, 6);
    m.tyre(cx, 4.3f, 4.1f, 25, 31);
  }
  if (m.L.wheels) {
    m.box(4, 3, 5, 116, 5, 6, m.steel);
    m.box(4, 3, 25, 116, 5, 26, m.steel);
  }
}

// ---------------------------------------------------------------- meshes, one per kind / condition / paint
VoxelMeshGL& wreckMesh(int kind, int cond, int paint) {
  static auto& cache = *new std::unordered_map<int, std::unique_ptr<VoxelMeshGL>>();
  const int key = (kind & 7) | ((cond & 3) << 3) | ((paint & 7) << 5);
  if (auto it = cache.find(key); it != cache.end()) return *it->second;
  int L, W, H;
  zone::wreckDims(kind, L, W, H);
  M m(L * V, H * V, W * V, makeLook(kind, cond, paint));
  switch (kind) {
    case zone::WK_CAR: buildCar(m); break;
    case zone::WK_TRUCK: buildJeep(m); break;
    case zone::WK_BUS: buildBus(m); break;
    case zone::WK_APC: buildApc(m); break;
    case zone::WK_HELI: buildHeli(m); break;
    case zone::WK_TRACTOR: buildTractor(m); break;
    default: buildLoco(m); break;
  }
  VoxelMeshData md;
  buildVoxelMesh(m.c.g, md);
  auto gl = std::make_unique<VoxelMeshGL>();
  gl->upload(md);
  VoxelMeshGL* p = gl.get();
  cache.emplace(key, std::move(gl));
  return *p;
}

// Lie in block space. Orthogonal lies match bWreck's integer stamp (a block centre lands in the same cell).
// Ditch is a rigid nose-down tilt (the nose drops one block, the tail stays). Jack shears the tail out by one.
mat4 lieMat(int lie, int L, int W, int H) {
  mat4 m(1.f);
  switch (lie) {
    case zone::WL_SIDE_L:             // (x, y, z) -> (x, z, H - y): the -z flank is on the ground
      m[0] = vec4(1, 0, 0, 0); m[1] = vec4(0, 0, -1, 0); m[2] = vec4(0, 1, 0, 0); m[3] = vec4(0, 0, (float)H, 1);
      break;
    case zone::WL_SIDE_R:             // (x, y, z) -> (x, W - z, y)
      m[0] = vec4(1, 0, 0, 0); m[1] = vec4(0, 0, 1, 0); m[2] = vec4(0, -1, 0, 0); m[3] = vec4(0, (float)W, 0, 1);
      break;
    case zone::WL_ROOF:               // (x, y, z) -> (x, H - y, z), belly up
      m[0] = vec4(1, 0, 0, 0); m[1] = vec4(0, -1, 0, 0); m[2] = vec4(0, 0, 1, 0); m[3] = vec4(0, (float)H, 0, 1);
      break;
    case zone::WL_DITCH: {
      const float a = -std::asin(std::min(0.95f, 1.f / (float)std::max(1, L)));
      m = glm::rotate(mat4(1.f), a, vec3(0, 0, 1));
      break;
    }
    case zone::WL_JACK:
      m[0] = vec4(1.f, 0.f, -1.f / (float)std::max(1, L), 0.f);
      m[3] = vec4(0.f, 0.f, 1.f, 1.f);
      break;
    case zone::WL_SUNK: m = glm::translate(mat4(1.f), vec3(0.f, -1.f, 0.f)); break;
    case zone::WL_STACK: m = glm::translate(mat4(1.f), vec3(0.f, 2.f, 0.f)); break;
    default: break;
  }
  return m;
}

// Local (0,0) is the corner of the footprint, not the minimum cell index: rot 2's cell 0 sits at x0+w-1,
// so the continuous edge is x0+w. p.w / p.d are the footprint (already swapped for a side lie).
mat4 wreckPose(const zone::Placed& p, int baseY, const FrameCtx& f, int L, int W, int H) {
  float ox = (float)p.x0, oz = (float)p.z0, yaw = 0.f;
  switch (p.rot & 3) {
    case 1: ox += (float)p.d; yaw = -PI * 0.5f; break;
    case 2: ox += (float)p.w; oz += (float)p.d; yaw = PI; break;
    case 3: oz += (float)p.w; yaw = PI * 0.5f; break;
    default: break;
  }
  const vec3 rel = f.cam.rel(glm::dvec3(ox, (double)dimBase(DIM_OVERWORLD) + baseY, oz));
  return glm::translate(mat4(1.f), rel) * glm::rotate(mat4(1.f), yaw, vec3(0, 1, 0)) *
         glm::translate(mat4(1.f), vec3(0.f, 0.02f, 0.f)) * lieMat(zone::wreckLieOf(p.variant), L, W, H) *
         glm::scale(mat4(1.f), vec3(1.f / (float)V));
}

int wreckBaseY(u32 seed, const zone::Placed& p) {
  const zone::Ground g = zone::groundAt(seed, p.x0 + p.W / 2, p.z0 + p.D / 2);
  if (zone::wreckLieOf(p.variant) == zone::WL_SUNK && g.water) return g.waterY;
  return (int)g.h + 1;
}

class ZoneWreckLayer : public RenderLayer {
public:
  const char* name() const override { return "zone-wrecks"; }
  int order() const override { return 90; }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!s.player || !s.level || !zone::zoneWorld(*s.level)) return;
    if (s.player->realm != REALM_MAIN || f.dim != DIM_OVERWORLD) return;
    const u32 seed = (u32)s.level->info.seed;
    const zone::Plan& plan = zone::zonePlan(seed);
    World& w = s.world();
    const auto ids = plan.near(ifloor(f.cam.pos.x), ifloor(f.cam.pos.z), 160);
    VoxelRenderer& VR = voxelRenderer();
    bool begun = false;
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);
    for (int i : ids) {
      const zone::Placed& p = plan.s[i];
      if (p.type != zone::S_WRECK) continue;
      const float dx = (float)(p.x0 + p.W * 0.5 - f.cam.pos.x);
      const float dz = (float)(p.z0 + p.D * 0.5 - f.cam.pos.z);
      if (dx * dx + dz * dz > 165.f * 165.f) continue;
      const int kind = zone::wreckKindOf(p.variant);
      const int lie = zone::wreckLieOf(p.variant);
      int L, W, H;
      zone::wreckDims(kind, L, W, H);
      const int baseY = wreckBaseY(seed, p);
      const int gy = dimBase(DIM_OVERWORLD) + baseY;
      const int mx = p.x0 + p.W / 2, mz = p.z0 + p.D / 2;
      if (!w.isLoaded(mx, gy, mz)) continue;
      const mat4 model = wreckPose(p, baseY, f, L, W, H);
      const vec4 ctr = model * vec4(L * V * 0.5f, H * V * 0.5f, W * V * 0.5f, 1.f);
      const float rad = std::sqrt(float(L * L + W * W + H * H)) * 0.5f + 2.f;
      if (!f.cam.frustum.sphere(vec3(ctr), rad)) continue;
      const int cond = lie == zone::WL_BURNT ? 1 : lie == zone::WL_STRIPPED ? 2 : 0;
      const VoxelMeshGL& mesh = wreckMesh(kind, cond, (int)(p.seed % 5));
      if (!mesh.ready()) continue;
      if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
      const u8 lit = w.getLight(mx, gy + 1, mz);
      VR.draw(mesh, model, VoxelLight{(lit >> 4) / 15.f, (lit & 15) / 15.f});
    }
    if (begun) VR.end();
  }
};

} // namespace

void registerZoneWreckLayer() {
  registerRenderLayer([](GameSession&) { return std::make_shared<ZoneWreckLayer>(); });
}

} // namespace dq
