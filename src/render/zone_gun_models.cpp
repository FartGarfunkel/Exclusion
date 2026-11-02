// render/zone_gun_models.cpp — the Zone's guns as 3D voxel models. A port of Battle of Spades' render/guns.js (the
// per-gun box models, fine voxels = 19 mm) and render/models.js gunGrid (attachments). BoS axes are x forward, y right,
// z DOWN, the bore on z = -2, the right hand's grip at the origin; ours (see the header) are x forward, y up, z right
// -- the box lists are kept in BoS coordinates and flipped when baked into the grid.
#include "render/zone_gun_models.h"
#include "assets/zone_model_kit.h"
#include "sim/zone.h"
#include <climits>
#include <unordered_map>
#include <map>

namespace dq {
namespace {
using namespace zk;

constexpr float FINE = 0.019f;                       // one fine voxel in blocks
constexpr int NONE = INT_MIN;
namespace GK {   // finishes
constexpr int blk = 0x3A3C40, blk2 = 0x1C1D1F, poly = 0x2C2D30, park = 0x45474A, steel = 0x55575C, light = 0x6A6D72, stain = 0xA2A5AA,
  tan = 0xA68E62, tan2 = 0x8C774F, fde = 0x7E6A48, od = 0x4C5436, wood = 0x8A4A22, wood2 = 0x6E3A1A, plum = 0x6E3226, rub = 0x161617, white = 0xD8D8D0;
}
constexpr int GLASS = 0x4A7A9A;

// colour patterns
Col gkRail(int c) { return Col(ZCF { return (x & 1) ? c : shadeC(c, 0.66); }); }
Col gkWood(int c) { return Col(ZCF { return shadeC(c, 0.86 + 0.22 * hash3(x >> 2, y, z, 41)); }); }
Col gkQuad(int c, int z0, int z1, int w = 4) { return Col(ZCF { return ((z == z0 || z == z1 || y == -(w >> 1) || y == (w >> 1) - 1) && !(x & 1)) ? shadeC(c, 0.62) : c; }); }

// ---- the box list (BoS coordinates; inclusive voxel ranges) ----
struct Box { int x0, y0, z0, x1, y1, z1; Col c; };
struct Val {   // a number, [at x0, at x1] (straight between) or a function of x
  int kind = 0; double a = 0, b = 0; std::function<double(int)> f;
  Val(double v) : a(v) {}
  Val(double a_, double b_) : kind(1), a(a_), b(b_) {}
  template <class F, class = std::enable_if_t<!std::is_arithmetic_v<std::decay_t<F>> && std::is_invocable_v<F, int>>> Val(F fn) : kind(2), f(std::move(fn)) {}
  double at(int x, int x0, int x1) const { return kind == 2 ? f(x) : kind == 1 ? a + (b - a) * (x - x0) / std::max(1, x1 - x0) : a; }
};
struct G {
  std::vector<Box> b;
  G& add(int x0, int y0, int z0, int x1, int y1, int z1, Col c) { b.push_back({x0, y0, z0, x1, y1, z1, std::move(c)}); return *this; }
  // a box centred across the gun, w wide
  G& c(int x0, int x1, int z0, int z1, int w, Col col) { int h = w >> 1; return add(std::min(x0, x1), -h, std::min(z0, z1), std::max(x0, x1), w - h - 1, std::max(z0, z1), std::move(col)); }
  G& y(int x0, int x1, int y0, int y1, int z0, int z1, Col col) { return add(std::min(x0, x1), std::min(y0, y1), std::min(z0, z1), std::max(x0, x1), std::max(y0, y1), std::max(z0, z1), std::move(col)); }
  G& cut(int x0, int x1, int z0, int z1, int w = 16) { return c(x0, x1, z0, z1, w, Col(-1)); }
  G& rod(int x0, int x1, int zc, int d, Col col, int y0 = 0) {
    const double r = d / 2.0;
    for (int k = 0; k < d; k++) {
      int z = (int)std::floor(zc - r) + k; double dz = z + 0.5 - zc;
      double half = std::sqrt(std::max(0.0, r * r + 0.05 - dz * dz)); int m = std::max(1, (int)std::floor(half + 0.5));
      add(std::min(x0, x1), y0 - m, z, std::max(x0, x1), y0 + m - 1, z, col);
    }
    return *this;
  }
  G& prof(int x0, int x1, Val top, Val bot, int w, Col col) {
    const int h = w >> 1;
    for (int x = x0; x <= x1; x++) { int t = jr(top.at(x, x0, x1)), u = jr(bot.at(x, x0, x1)); if (u >= t) add(x, -h, t, x, w - h - 1, u, col); }
    return *this;
  }
  G& bend(int x0, int x1, int z0, int z1, std::function<int(int)> shift, int w, Col col) {
    const int h = w >> 1;
    for (int z = z0; z <= z1; z++) { int s = shift(z - z0); add(x0 + s, -h, z, x1 + s, w - h - 1, z, col); }
    return *this;
  }
};
struct O { bool optic = false; std::string muzzle, mag; bool under = false; };   // what is fitted

// ---- parts shared by many guns ----
void gpGrip(G& g, Col col, int x0 = -4, int rows = 9, double rake = 0.4, int z0 = 2, int w = 4, int len = 3) { g.bend(x0, x0 + len - 1, z0, z0 + rows - 1, [=](int k) { return -jr(k * rake); }, w, col); }
void gpGuard(G& g, Col col, int x0, int x1, int z = 4, Col trig = GK::blk2) { g.c(x0, x1, z, z, 2, col).c(x1, x1, 2, z, 2, col).c(x0 + 2, x0 + 2, 2, z - 2, 2, trig); }
void gpBarrel(G& g, int x0, int x1, Col col = GK::park, int d = 2, int zc = -2) { g.rod(x0, x1, zc, d, col); }
void gpMuzzle(G& g, const char* kind, int x, int col = GK::blk2, int zc = -2) {
  const std::string k = kind;
  if (k == "a2") { g.rod(x, x + 2, zc, 2, Col([=](int xx, int, int) -> int { return (xx & 1) ? col : shadeC(col, 1.5); })); g.c(x, x, zc - 2, zc - 2, 2, col); }
  else if (k == "hk") { g.rod(x, x + 2, zc, 2, col); g.y(x + 1, x + 2, -2, -2, zc - 1, zc, col).y(x + 1, x + 2, 1, 1, zc - 1, zc, col); }
  else if (k == "prong") { g.rod(x, x, zc, 2, col); g.y(x + 1, x + 3, -2, -2, zc - 2, zc - 2, col).y(x + 1, x + 3, 1, 1, zc - 2, zc - 2, col).y(x + 1, x + 3, -2, -2, zc + 1, zc + 1, col).y(x + 1, x + 3, 1, 1, zc + 1, zc + 1, col); }
  else if (k == "ak74") { g.rod(x, x + 4, zc, 4, col); g.y(x + 2, x + 3, -2, -2, zc - 1, zc, -1).y(x + 2, x + 3, 1, 1, zc - 1, zc, -1).c(x + 4, x + 4, zc - 1, zc, 2, -1); }
  else if (k == "akm") { g.rod(x, x + 2, zc, 2, col); g.y(x + 1, x + 2, -1, 0, zc - 1, zc - 1, shadeC(col, 1.4)); }
  else if (k == "brake") { g.rod(x, x + 3, zc, 4, col); g.y(x + 1, x + 1, -2, 1, zc - 1, zc, shadeC(col, 0.5)).y(x + 3, x + 3, -2, 1, zc - 1, zc, shadeC(col, 0.5)); }
  else if (k == "bigbrake") { g.rod(x, x + 5, zc, 6, col); g.y(x + 1, x + 1, -3, 2, zc - 2, zc + 1, -1).y(x + 3, x + 3, -3, 2, zc - 2, zc + 1, -1); g.rod(x + 1, x + 4, zc, 2, col); }
  else if (k == "cyl") { g.rod(x, x + 5, zc, 4, col); g.y(x + 1, x + 4, -2, -2, zc - 1, zc, Col([=](int xx, int, int) -> int { return (xx & 1) ? col : shadeC(col, 0.5); })); }
}
void gpStanag(G& g, int x, const O& o, int col = GK::blk, int rows = 10, int w = 2) {
  const int n = rows + (o.mag == "ext" ? 4 : 0);
  auto sh = [](int k) { return k < 3 ? 0 : k < 6 ? 1 : k < 9 ? 2 : k < 12 ? 3 : 4; };
  g.bend(x, x + 3, 2, 2 + n, sh, w, col);
  g.c(x - 1 + sh(n), x + 3 + sh(n), 2 + n, 2 + n, w + 2 > 4 ? 4 : w + 2, shadeC(col, 0.8));
}
void gpBoxMag(G& g, int x, const O& o, int col, int rows, int len = 4, int w = 2, double rake = 0) {
  const int n = rows + (o.mag == "ext" ? (int)std::ceil(rows * 0.45) : 0);
  auto sh = [=](int k) { return jr(k * rake); };
  g.bend(x, x + len - 1, 2, 2 + n, sh, w, col);
  g.c(x - 1 + sh(n), x + len - 1 + sh(n), 2 + n, 2 + n, w + 2, shadeC(col, 0.75));
}
void gpAkMag(G& g, int x, const O& o, int col, int rows = 11, double curve = 1, int len = 4, int w = 2) {
  const int n = rows + (o.mag == "ext" ? 6 : 0);
  auto sh = [=](int k) { return jr((0.2 * k + 0.028 * k * k) * curve); };
  for (int k = 0; k <= n; k++) g.c(x + sh(k), x + len - 1 + sh(k) + (k > n * 0.5 ? 1 : 0), 2 + k, 2 + k, w, (k % 3 == 1) ? shadeC(col, 0.82) : col);
}
void gpRail(G& g, int x0, int x1, int z, int col = GK::blk, int w = 4) { g.c(x0, x1, z, z, w, gkRail(col)); }
void gpBuisRear(G& g, const O& o, int x, int rz, int col = GK::blk, int line = NONE) {
  if (line == NONE) line = rz - 4;
  g.c(x, x + 2, rz - 1, rz - 1, 4, col);
  if (o.optic) g.c(x, x + 3, rz - 2, rz - 2, 4, col);
  else g.c(x + 1, x + 1, line, rz - 2, 4, col).c(x + 1, x + 1, line, line, 2, GK::blk2);
}
void gpBuisFront(G& g, const O& o, int x, int rz, int col = GK::blk, int line = NONE) {
  if (line == NONE) line = rz - 4;
  g.c(x, x + 1, rz - 1, rz - 1, 4, col);
  if (o.optic) g.c(x - 2, x + 1, rz - 2, rz - 2, 2, col);
  else g.y(x, x + 1, -2, -2, line, rz - 2, col).y(x, x + 1, 1, 1, line, rz - 2, col).c(x, x, line, rz - 2, 2, GK::blk2);
}
void hkFront(G& g, const O& o, int x, int rz, int col = GK::blk) {
  g.c(x, x + 1, rz - 1, rz - 1, 4, col);
  if (o.optic) { g.c(x - 2, x + 1, rz - 2, rz - 2, 2, col); return; }
  g.y(x, x + 1, -2, -2, rz - 5, rz - 2, col).y(x, x + 1, 1, 1, rz - 5, rz - 2, col).c(x, x + 1, rz - 5, rz - 5, 4, col).c(x, x, rz - 4, rz - 4, 2, GK::blk2);
}
void gpDisc(G& g, int cx, int cz, int r, int w, Col col, int y0 = NONE) {
  for (int dz = -r; dz <= r; dz++) {
    int hw = jr(std::sqrt(r * r - dz * dz + 0.3));
    if (y0 == NONE) g.c(cx - hw, cx + hw, cz + dz, cz + dz, w, col); else g.y(cx - hw, cx + hw, y0, y0 + w - 1, cz + dz, cz + dz, col);
  }
}
void gpBox(G& g, int x0, int x1, int z1, int side, int col, int wide = 5) {
  int y0 = side < 0 ? -wide - 1 : side > 0 ? 0 : -(wide >> 1) - 1;
  g.y(x0, x1, y0, y0 + wide, 2, z1, col); g.y(x0 + 1, x1 - 1, y0, y0 + wide, z1 + 1, z1 + 1, shadeC(col, 0.8));
}
void gpBipod(G& g, int x, int z, int len, int col) {   // folded back along the gun
  g.c(x, x + 1, z, z, 4, col);
  g.y(x - len, x, -2, -2, z + 1, z + 1, col).y(x - len, x, 1, 1, z + 1, z + 1, col);
}

// ---- the AR-15 family ----
void arLower(G& g, int col, Col grip, bool big = false) {
  g.c(-5, 8 + (big ? 1 : 0), 0, 1, 4, col); g.c(3, 8 + (big ? 1 : 0), 2, 3, 4, col);
  gpGuard(g, col, -1, 3, 4); gpGrip(g, grip, -4, 8, 0.4); g.c(-5, -4, 2, 2, 4, col);
}
void arUpper(G& g, int col, int x1 = 9) {
  g.c(-5, x1, -3, -1, 4, col); gpRail(g, -5, x1 - 1, -4, col);
  g.c(-7, -6, -4, -3, 4, col).y(-6, -6, -3, -3, -4, -4, col);
  g.y(-3, -2, 2, 2, -3, -2, col).y(-1, -1, 2, 2, -3, -2, col);
}
void arStock(G& g, const char* kind, int col, int x0 = -6, int tube = GK::blk) {
  const std::string k = kind;
  if (k == "m4") {
    g.rod(-12, x0, -2, 2, tube); g.c(x0, x0, -4, 0, 4, tube);
    g.prof(-17, -11, -4, Val(3, 0), 4, col); g.c(-18, -18, -4, 3, 4, GK::rub);
    g.c(-11, -9, 0, 0, 2, col); g.cut(-15, -14, 1, 1);
  } else if (k == "hk") {
    g.rod(-11, x0, -2, 2, tube); g.c(x0, x0, -4, 0, 4, tube);
    g.prof(-18, -10, Val(-5, -4), Val(4, 0), 4, col); g.c(-19, -19, -5, 4, 4, GK::rub);
    g.cut(-16, -13, 1, 2); g.c(-12, -10, -5, -5, 2, col);
  }
}
void arGuardQuad(G& g, int x0, int x1, int col = GK::blk) { g.c(x0, x1, -4, 1, 4, gkQuad(col, -4, 1)); g.c(x1, x1, -3, 0, 4, shadeC(col, 0.8)); }

// ---- the Kalashnikovs ----
struct AkP { Col M, H, Hu, G; bool hasHu = false, hasG = false, hood = false, lowRib = false, noLeaf = false, noGrip = false; int hi = 20, gas = 21, fs = 27, bx = 30; };
void akBody(G& g, const O& o, const AkP& p) {
  const int M = p.M.c; const Col& H = p.H; const Col Hu = p.hasHu ? p.Hu : p.H;
  const int hi = p.hi, gas = p.gas, fs = p.fs;
  g.c(-7, 9, -1, 1, 4, M); g.c(-8, -7, -2, 1, 4, M);
  g.c(-6, 8, -3, -2, 4, Col(ZCF { return (x % 4) == 0 ? shadeC(M, 0.75) : M; })).c(-6, 8, -4, -4, 2, M);
  g.c(-7, -7, -3, -3, 2, M);
  g.y(1, 4, 2, 2, -2, -2, M).y(1, 1, 2, 2, -1, 0, M);
  g.c(9, 13, -4, -1, 4, M);
  if (!p.noLeaf) { g.c(10, 13, -5, -5, 2, M).c(11, 11, -5, -5, 4, M); if (!o.optic) g.c(11, 11, -6, -6, 4, M).c(11, 11, -6, -6, 2, -1); }
  g.c(10, 10, -3, 1, 4, M).c(hi + 1, hi + 1, -3, 1, 4, M);
  const bool lowRib = p.lowRib;
  g.c(11, hi, -3, 1, 4, Col([=](int x, int y, int z) -> int { int h = H.at(x, y, z); return z == -3 ? shadeC(h, 0.7) : H.f ? h : (lowRib && (z & 1)) ? shadeC(h, 0.85) : h; }));
  g.rod(13, gas, -4, 2, M); g.c(14, hi, -5, -4, 4, Hu);
  g.c(gas, gas + 2, -5, -1, 4, M);
  gpBarrel(g, 10, p.bx, M);
  g.c(fs, fs + 2, -4, -1, 4, M).c(fs + 1, fs + 2, -5, -5, 4, M).c(fs, fs + 1, 0, 1, 2, M);
  if (p.hood) g.y(fs + 1, fs + 2, -2, -2, -7, -6, M).y(fs + 1, fs + 2, 1, 1, -7, -6, M).c(fs + 1, fs + 2, -7, -7, 4, M);
  else g.y(fs + 1, fs + 2, -2, -2, -6, -6, M).y(fs + 1, fs + 2, 1, 1, -6, -6, M);
  g.c(fs + 1, fs + 1, -6, -6, 2, GK::blk2);
  gpGuard(g, M, -1, 3, 3);
  if (!p.noGrip) gpGrip(g, p.hasG ? p.G : H, -4, 8, 0.35);
  g.c(3, 3, 2, 2, 2, M);
  if (o.optic) g.y(0, 5, -3, -3, -4, -1, M).c(-1, 6, -5, -5, 4, M);
}
void akStock(G& g, const char* kind, Col col) {
  const std::string k = kind; const int c = col.c;
  if (k == "ak74m") { g.c(-9, -8, -2, 2, 4, col); g.prof(-21, -10, Val(-1, -2), Val(5, 2), 4, Col(ZCF { return (z == 1 && x < -11) ? shadeC(c, 0.7) : c; })); g.c(-22, -22, -1, 5, 4, GK::rub); }
  else if (k == "wood") { g.prof(-21, -9, Val(0, -2), Val(6, 2), 4, gkWood(c)); g.c(-22, -22, 0, 6, 4, GK::steel); }
  else if (k == "rpk") { g.prof(-21, -9, Val(-1, -2), Val([](int x) { return x < -17 ? 7.0 : x < -13 ? 5.0 + jr((-13 - x) * 0.5) : (double)jr(2 + (-9 - x) * 0.6); }), 4, gkWood(c)); g.c(-22, -22, -1, 7, 4, GK::steel); }
  else if (k == "fold") { g.c(-8, -8, -2, 1, 4, col); g.c(-19, -9, -2, -2, 2, col); g.prof(-19, -9, Val(4, 1), Val(5, 1), 2, col); g.c(-20, -20, -2, 5, 4, col); }
  else if (k == "skel") { g.c(-9, -8, -2, 1, 4, col); g.c(-20, -9, -2, -1, 2, col); g.c(-19, -9, 3, 3, 2, col); g.c(-10, -10, -1, 3, 2, col); g.c(-21, -21, -3, 4, 4, GK::rub); }
}
// a side-rail optic mount over the dust cover (AK family, SVD, VSS)
void akMount(G& g, const O& o, int M) { if (o.optic) g.y(0, 5, -3, -3, -4, -1, M).c(-1, 6, -5, -5, 4, M); }

// ---- bullpup / battle rifle parts ----
void l85(G& g, const O& o, int bx, int B, int H) {
  g.c(-17, 6, -3, 1, 4, B).c(-17, -12, 2, 4, 4, B).c(-18, -18, -3, 4, 4, GK::rub);
  g.c(-17, -12, -4, -4, 4, B);
  gpRail(g, -12, 5, -4, B);
  g.c(6, 14, -3, 1, 4, Col(ZCF { return (y == 1 && z == -1 && x % 2 == 0) ? shadeC(H, 0.6) : H; }));
  g.c(13, 15, -4, -4, 4, B);
  g.y(-6, -3, 2, 2, -2, -2, GK::steel);
  gpGrip(g, H, -4, 8, 0.3); gpGuard(g, B, -1, 3, 4);
  gpBarrel(g, 14, bx, GK::park);
  gpBuisRear(g, o, -11, -4, B, -7); gpBuisFront(g, o, 13, -4, B, -7);
}
void scar(G& g, const O& o, int bx, int T, int L) {
  g.c(-7, -6, -4, 1, 4, GK::blk);
  g.prof(-20, -8, Val([](int x) { return (x > -17 && x < -11) ? -5.0 : -4.0; }), Val(3, 1), 4, L); g.c(-21, -21, -4, 3, 4, GK::rub); g.cut(-16, -12, 0, 1);
  g.c(-6, 18, -4, -2, 4, Col(ZCF { return (x > 9 && (y == -2 || y == 1) && !(x & 1)) ? shadeC(T, 0.65) : T; }));
  gpRail(g, -6, 18, -5, T);
  g.c(-6, 10, -1, 1, 4, L); g.c(10, 18, -1, 1, 4, Col(ZCF { return z == 1 && !(x & 1) ? shadeC(L, 0.65) : L; }));
  g.c(3, 9, 2, 3, 4, L); g.y(5, 7, -3, -3, -3, -3, GK::blk);
  gpGrip(g, L, -4, 8, 0.38); gpGuard(g, L, -1, 3, 4);
  gpBarrel(g, 18, bx, GK::blk);
  gpBuisRear(g, o, -5, -5, GK::blk, -9); gpBuisFront(g, o, 16, -5, GK::blk, -9);
}
void g36body(G& g, const O& o, int hx1, int P) {
  const int M = 0x2E3033;
  g.c(-7, -7, -3, 1, 4, GK::blk);
  g.c(-18, -8, -3, -2, 4, P); g.prof(-18, -8, Val(3, 1), Val(3, 1), 4, P); g.c(-19, -19, -3, 3, 4, GK::rub);
  g.c(-6, 12, -3, 1, 4, P); g.c(3, 8, 2, 3, 4, P);
  g.c(-5, -3, -6, -4, 4, P).c(hx1 - 2, hx1, -6, -4, 4, P).c(-5, hx1, -7, -7, 4, gkRail(P));
  g.c(2, 3, -4, -4, 2, M);
  if (!o.optic) g.c(-5, -4, -8, -8, 4, P).c(-5, -4, -8, -8, 2, -1).c(hx1 - 1, hx1 - 1, -8, -8, 2, GK::blk2);
  gpGrip(g, P, -4, 8, 0.35); gpGuard(g, P, -1, 3, 5);
  gpBarrel(g, 12, 30, M);
}
void m14(G& g, const O& o, int bx, int C, int S) {
  g.rod(-12, -7, -2, 2, GK::blk); g.prof(-18, -12, Val([](int x) { return (x > -17 && x < -12) ? -5.0 : -4.0; }), Val(3, 0), 4, S); g.c(-19, -19, -4, 3, 4, GK::rub);
  g.c(-7, 20, -3, 1, 4, Col(ZCF { return (x > 8 && (y == -2 || y == 1 || z == 1) && !(x & 1)) ? shadeC(C, 0.65) : C; }));
  gpRail(g, -7, 20, -4, C);
  g.y(-1, 12, 2, 2, -2, -2, GK::park).y(1, 2, 3, 3, -2, -1, GK::park);
  g.c(3, 9, 2, 2, 4, C);
  gpGrip(g, S, -4, 8, 0.38); gpGuard(g, C, -1, 3, 4);
  gpBarrel(g, 20, bx, GK::park); g.c(20, 25, 0, 1, 2, GK::park);
  gpBoxMag(g, 3, o, 0x2C2D30, 7, 5, 2, 0.1); g.c(3, 3, 1, 2, 2, 0x2C2D30);
  gpBuisRear(g, o, -6, -4, GK::blk, -8);
}
void mp5body(G& g, const O& o, int x1, int B) {
  g.c(-6, x1, -2, 0, 4, B).c(-6, x1, -3, -3, 2, B);
  gpDisc(g, -4, -5, 1, 4, B); g.c(-4, -4, -5, -5, 2, GK::blk2);
  gpGrip(g, 0x26272A, -4, 8, 0.3); g.c(-2, 3, 4, 4, 2, 0x26272A).c(3, 3, 1, 4, 2, 0x26272A).c(0, 0, 1, 2, 2, GK::blk2);
  g.c(3, 7, 1, 1, 4, B);
  gpAkMag(g, 3, o, 0x2A2B2E, 9, 0.6);
  if (o.optic) g.c(-4, 6, -4, -4, 4, gkRail(B)).c(-2, -1, -3, -3, 4, B).c(4, 5, -3, -3, 4, B);
}
void bolt(G& g, const O& o, int M, int step, int bx, int mag) {
  g.rod(-6, 8, -2, 4, M); gpRail(g, -7, 8, -4, M); g.c(-8, -7, -3, -1, 4, M);
  g.y(-3, -2, 2, 2, -2, -2, M).y(-3, -2, 3, 3, -1, 0, GK::blk2);
  gpBarrel(g, 8, step, M, 4); gpBarrel(g, step, bx, M, 2);
  gpGuard(g, M, -1, 3, 4);
  if (mag) gpBoxMag(g, 1, o, M, mag, 4, 2, 0);
  if (!o.optic) g.c(-6, -5, -5, -5, 4, M).c(-6, -5, -5, -5, 2, -1).c(bx - 2, bx - 2, -4, -3, 2, M);
}
void thumbhole(G& g, Col col, int b, bool cheek) {
  g.c(b + 1, -8, -4, -2, 4, col); g.c(b, b + 1, -4, 7, 4, col); g.c(b - 1, b - 1, -4, 7, 4, GK::rub);
  g.c(b + 2, -5, 6, 7, 4, col); g.bend(-5, -2, 0, 7, [](int k) { return -jr(k * 0.15); }, 4, col); g.c(-8, -2, -1, 0, 4, col);
  if (cheek) g.c(b + 5, b + 10, -6, -5, 4, col).c(b + 6, b + 6, -5, -5, 2, GK::steel).c(b + 9, b + 9, -5, -5, 2, GK::steel);
}

// ---- pistols (drawn half as big again) ----
void gpPistolSights(G& g, const O& o, int rx, int fx, int col = GK::blk2) {
  if (!o.optic) { g.c(rx, rx + 1, -4, -4, 4, col).c(rx, rx + 1, -4, -4, 2, -1); g.c(fx, fx, -4, -4, 2, col); }
  else g.c(fx, fx, -4, -4, 2, col);
}
void gpPistolMag(G& g, const O& o, int x, int z, int col = GK::blk2, int extra = 0) {
  const int n = extra + (o.mag == "ext" ? 3 : 0);
  if (n > 0) g.bend(x + 1, x + 3, z, z + n - 1, [](int k) { return -jr((k + 9) * 0.3) + 3; }, 2, col);
  const int s = n > 0 ? -jr((n - 1 + 9) * 0.3) + 3 : 0;
  g.c(x + s, x + 4 + s, z + n, z + n, 4, col);
}

// ---- the guns ----
// what each gun tells the attachments: where the rail / muzzle / handguard are (fine voxels, BoS rows: negative z is up)
struct Anchors {
  bool rail = true; int r0 = -4, r1 = 8, rz = -4;
  int mx = 30, dev = 3, bore = -2;
  int ux = NONE, uz = 2, hw = 2, ax = NONE, side = NONE, bip = NONE;
  bool bipod = false;     // has its own
  bool ugl = false; int u0 = 0, u1 = 0, utop = 2;
  int rear = -3, line = -8;   // iron sights: the rear sight's x, the sight line's row
  bool pistol = false;
};
struct GunDefn { Anchors a; std::function<void(G&, const O&)> build; };

const std::map<std::string, GunDefn>& table() {
  static const std::map<std::string, GunDefn> T = [] {
    std::map<std::string, GunDefn> t;
    auto A = [](int r0, int r1, int rz, int mx, int dev, int ux, int uz, int rear, int line) { Anchors a; a.r0 = r0; a.r1 = r1; a.rz = rz; a.mx = mx; a.dev = dev; a.ux = ux; a.uz = uz; a.rear = rear; a.line = line; return a; };
    auto U = [](Anchors a, int u0, int u1, int top) { a.ugl = true; a.u0 = u0; a.u1 = u1; a.utop = top; return a; };
    auto PIS = [](int mx, int line) { Anchors a; a.r0 = -3; a.r1 = 6; a.rz = -3; a.bore = -1; a.ux = 4; a.uz = 2; a.hw = -1; a.side = 2; a.ax = 1; a.mx = mx; a.dev = 0; a.rear = -3; a.line = line; a.pistol = true; return a; };

    // ---- AR family
    t["m416"] = {U(A(-4, 18, -4, 30, 3, 14, 2, -3, -8), 10, 25, 2), [](G& g, const O& o) {
      const int B = GK::blk;
      arStock(g, "hk", GK::poly); arLower(g, B, GK::poly); arUpper(g, B);
      g.c(9, 9, -4, 1, 4, B); arGuardQuad(g, 10, 22); gpRail(g, 10, 22, -4);
      gpBuisRear(g, o, -4, -4); hkFront(g, o, 20, -4);
      gpBarrel(g, 22, 27); if (o.muzzle.empty()) gpMuzzle(g, "hk", 27);
      gpStanag(g, 4, o, 0x303134);
    }};
    t["scarl"] = {U(A(-4, 14, -5, 29, 3, 14, 2, -4, -9), 10, 25, 2), [](G& g, const O& o) {
      scar(g, o, 26, GK::tan, GK::tan2); if (o.muzzle.empty()) gpMuzzle(g, "prong", 26, GK::blk); gpStanag(g, 4, o, 0x3A342A);
    }};
    t["g36"] = {U(A(-4, 7, -7, 26, 3, 14, 2, -5, -8), 10, 23, 2), [](G& g, const O& o) {
      const int P = 0x40463A, M = 0x2E3033;
      g36body(g, o, 9, P);
      g.c(12, 17, -3, 1, 4, Col(ZCF { return (z == -1 && (x & 1)) ? shadeC(P, 0.6) : P; }));
      g.cut(18, 31, -4, 1, 4); gpBarrel(g, 18, 22, M); if (o.muzzle.empty()) gpMuzzle(g, "prong", 22, M);
      gpStanag(g, 4, o, 0x5E5A4C);
    }};
    t["l85a2"] = {U(A(-11, 4, -4, 25, 2, 10, 2, -10, -7), 5, 22, 2), [](G& g, const O& o) {
      l85(g, o, 22, 0x34383A, 0x26272A);
      if (o.muzzle.empty()) gpMuzzle(g, "a2", 22, GK::blk2);
      gpStanag(g, -11, o, 0x2C2D30);
    }};
    // the OTs-14 Groza: the L85's bullpup layout in dark furniture with an AK flash hider and a curved mag behind the grip
    t["groza"] = {U(A(-11, 4, -4, 22, 2, 10, 2, -10, -7), 5, 19, 2), [](G& g, const O& o) {
      l85(g, o, 19, 0x2C2E2A, 0x3A3A32);
      g.c(8, 14, 2, 2, 4, 0x2C2E2A).c(9, 12, 3, 7, 2, 0x3A3A32);                      // the vertical fore grip
      if (o.muzzle.empty()) gpMuzzle(g, "akm", 19, 0x2C2E2A);
      gpAkMag(g, -11, o, 0x2C2E2A, 9, 0.6);
    }};

    // ---- AK family
    t["ak74m"] = {U(A(-2, 6, -5, 35, 5, 16, 2, 11, -6), 12, 27, 2), [](G& g, const O& o) {
      const int M = 0x3E4045, H = 0x242527;
      akStock(g, "ak74m", H); AkP p; p.M = M; p.H = H; p.hi = 20; p.gas = 21; p.fs = 27; p.bx = 30; p.hood = true; p.lowRib = true; akBody(g, o, p);
      if (o.muzzle.empty()) gpMuzzle(g, "ak74", 30, M);
      gpAkMag(g, 4, o, GK::plum);
    }};
    t["akm"] = {U(A(-2, 6, -5, 32, 2, 16, 2, 11, -6), 14, 28, 2), [](G& g, const O& o) {
      const int M = 0x37393D, W = GK::wood;
      akStock(g, "wood", W); AkP p; p.M = M; p.H = gkWood(W); p.Hu = gkWood(W); p.hasHu = true; p.G = gkWood(GK::wood2); p.hasG = true; p.hi = 20; p.gas = 21; p.fs = 27; p.bx = 30; akBody(g, o, p);
      g.y(4, 8, 2, 2, -1, 0, shadeC(M, 0.7)).y(4, 8, -3, -3, -1, 0, shadeC(M, 0.7));
      if (o.muzzle.empty()) gpMuzzle(g, "akm", 30, M);
      gpAkMag(g, 4, o, 0x404246, 11, 1.35);
    }};
    t["aks74u"] = {U(A(-2, 6, -5, 27, 5, 12, 2, 4, -6), 12, 22, 2), [](G& g, const O& o) {
      const int M = 0x34363A, W = 0x7A4020;
      akStock(g, "fold", M); AkP p; p.M = M; p.H = gkWood(W); p.Hu = gkWood(W); p.hasHu = true; p.G = 0x2A2B2E; p.hasG = true; p.hi = 14; p.gas = 15; p.fs = 16; p.bx = 22; p.hood = true; p.noLeaf = true; akBody(g, o, p);
      g.c(3, 5, -5, -5, 4, M); if (!o.optic) g.c(4, 4, -6, -6, 4, M).c(4, 4, -6, -6, 2, -1);
      if (o.muzzle.empty()) { g.rod(22, 24, -2, 4, M); g.rod(25, 26, -2, 6, M); g.c(26, 26, -3, -2, 2, -1); }
      gpAkMag(g, 4, o, GK::plum);
    }};
    t["an94"] = {U(A(-4, 6, -5, 36, 4, 16, 2, -4, -7), 13, 28, 3), [](G& g, const O& o) {
      const int M = 0x323438, H = 0x26272A;
      akStock(g, "skel", H);
      g.c(-7, 11, -3, 1, 4, M); g.c(-6, 10, -4, -4, 4, Col(ZCF { return (x & 1) ? M : shadeC(M, 0.75); }));
      g.c(-5, -3, -5, -5, 4, M); if (!o.optic) g.rod(-5, -3, -6, 4, M).c(-4, -4, -7, -6, 2, GK::blk2);
      g.c(11, 25, -4, 2, 4, Col(ZCF { return (y == 1 && x % 3 == 0) ? shadeC(H, 0.75) : H; }));
      gpBarrel(g, 25, 32, M);
      g.c(29, 31, -4, -1, 4, M).c(30, 31, -5, -5, 4, M);
      g.y(30, 31, -2, -2, -8, -6, M).y(30, 31, 1, 1, -8, -6, M).c(30, 31, -8, -8, 4, M).c(30, 30, -7, -7, 2, GK::blk2);
      if (o.muzzle.empty()) { g.rod(32, 35, -2, 4, M); g.y(33, 33, -2, 1, -2, -2, -1); }
      gpGuard(g, M, -1, 3, 3); gpGrip(g, H, -4, 8, 0.35);
      const int n = 11 + (o.mag == "ext" ? 6 : 0);
      for (int k = 0; k <= n; k++) { int sh = jr(0.2 * k + 0.028 * k * k), dy = jr(k * 0.27); g.y(5 + sh, 8 + sh + (k > n / 2.0 ? 1 : 0), -1 + dy, dy, 2 + k, 2 + k, k % 3 == 1 ? shadeC(GK::plum, 0.8) : GK::plum); }
      akMount(g, o, M);
    }};
    t["rpk"] = {[&] { Anchors a = A(-2, 6, -5, 46, 3, 16, 2, 11, -6); a.bip = 36; a.bipod = true; return a; }(), [](G& g, const O& o) {
      const int M = 0x3E4045, H = 0x242527;
      akStock(g, "rpk", H); AkP p; p.M = M; p.H = H; p.hi = 20; p.gas = 26; p.fs = 39; p.bx = 43; p.hood = true; p.lowRib = true; akBody(g, o, p);
      gpBarrel(g, 20, 43, M, 2); g.rod(10, 26, -2, 2, M);
      if (o.muzzle.empty()) gpMuzzle(g, "brake", 43, M);
      g.c(36, 37, -3, 0, 4, M);
      g.y(25, 36, -2, -2, 0, 0, M).y(25, 36, 1, 1, 0, 0, M);
      gpAkMag(g, 4, o, GK::plum, 15, 0.8);
    }};
    t["saiga"] = {A(-2, 6, -5, 31, 0, 15, 2, 11, -6), [](G& g, const O& o) {
      const int M = 0x303236, H = 0x26272A;
      akStock(g, "ak74m", H); AkP p; p.M = M; p.H = H; p.hi = 19; p.gas = 20; p.fs = 26; p.bx = 30; akBody(g, o, p);
      g.rod(20, 30, -2, 4, M); g.rod(30, 30, -2, 2, GK::blk2);
      if (o.mag == "drum") { gpDisc(g, 7, 12, 6, 6, H); g.c(4, 9, 2, 6, 4, H); g.c(6, 8, 12, 12, 2, shadeC(H, 1.6)); }
      else {
        g.bend(3, 8, 2, 9, [](int k) { return jr(k * 0.25); }, 4, H); g.c(4, 10, 10, 10, 4, shadeC(H, 0.7));
        if (o.mag == "ext") g.bend(5, 9, 11, 15, [](int k) { return jr(k * 0.3); }, 4, H);
      }
    }};
    t["pp19"] = {A(-2, 6, -5, 26, 3, 12, 4, 11, -6), [](G& g, const O& o) {
      const int M = 0x34363A, P = 0x252629;
      akStock(g, "fold", M);
      g.c(-7, 9, -1, 1, 4, M); g.c(-8, -7, -2, 1, 4, M);
      g.c(-6, 8, -3, -2, 4, Col(ZCF { return (x % 4) == 0 ? shadeC(M, 0.75) : M; })).c(-6, 8, -4, -4, 2, M);
      g.c(9, 13, -4, -1, 4, M); g.c(10, 13, -5, -5, 2, M); if (!o.optic) g.c(11, 11, -6, -6, 4, M).c(11, 11, -6, -6, 2, -1);
      gpBarrel(g, 10, 23, M); g.c(19, 21, -4, -1, 4, M).c(20, 21, -5, -5, 4, M);
      g.y(20, 21, -2, -2, -7, -6, M).y(20, 21, 1, 1, -7, -6, M).c(20, 21, -7, -7, 4, M).c(20, 20, -6, -6, 2, GK::blk2);
      if (o.muzzle.empty()) gpMuzzle(g, "brake", 23, M);
      const int n = o.mag == "ext" ? 22 : 19;
      g.rod(4, n, 2, 6, Col(ZCF { return (x % 3 == 0 && z > 0) ? shadeC(P, 0.7) : P; })); g.rod(n + 1, n + 1, 2, 4, P);
      gpGuard(g, M, -1, 3, 3); gpGrip(g, P, -4, 8, 0.35);
      akMount(g, o, M);
    }};
    // AS VAL / VSS: the integral suppressor (a fat tube to the muzzle) -- the VSS has the wooden skeleton stock
    auto val = [](bool vss) {
      return [vss](G& g, const O& o) {
        const int M = 0x35373B, P = 0x262729;
        if (vss) { Col W = gkWood(0x8C4C24);
          g.c(-21, -8, -2, -1, 4, W); g.c(-22, -21, -2, 7, 4, W); g.c(-23, -23, -2, 7, 4, GK::rub); g.c(-20, -6, 6, 7, 4, W);
          g.bend(-4, -2, 2, 6, [](int k) { return -jr(k * 0.5); }, 4, W); g.c(-7, -1, 0, 1, 4, W); }
        else akStock(g, "skel", M);
        g.c(-7, 9, -1, 1, 4, M); g.c(-8, -7, -2, 1, 4, M);
        g.c(-6, 8, -3, -2, 4, Col(ZCF { return (x % 4) == 0 ? shadeC(M, 0.75) : M; })).c(-6, 8, -4, -4, 2, M);
        g.c(7, 9, -5, -5, 2, M); if (!o.optic) g.c(8, 8, -6, -6, 4, M).c(8, 8, -6, -6, 2, -1);
        const int e = vss ? 35 : 29;
        g.rod(10, e, -2, 6, Col(ZCF { return (x % 5) == 0 ? shadeC(M, 0.8) : M; }));
        g.rod(e, e, -2, 2, GK::blk2);
        g.c(10, 17, -2, 2, 4, vss ? Col(gkWood(0x8C4C24)) : Col(P)).c(10, 17, 3, 3, 2, vss ? Col(gkWood(0x8C4C24)) : Col(P));
        g.c(17, 19, -5, -5, 2, M).c(18, 19, -5, -5, 4, M); g.y(18, 19, -2, -2, -6, -6, M).y(18, 19, 1, 1, -6, -6, M).c(18, 18, -6, -6, 2, GK::blk2);
        gpGuard(g, M, -1, 3, 3); gpGrip(g, vss ? Col(0x3A2010) : Col(P), -4, 8, 0.35);
        if (vss) gpBoxMag(g, 4, o, 0x2C2D30, 6, 4, 2, 0.2); else gpAkMag(g, 4, o, P, 8, 0.6);
        akMount(g, o, M);
      };
    };
    t["asval"] = {A(-2, 6, -5, 30, 0, 13, 2, 8, -6), val(false)};
    t["vss"] = {A(-2, 6, -5, 36, 0, 13, 2, 8, -6), val(true)};

    // ---- battle rifles, designated marksman and sniper rifles
    t["sks"] = {A(-3, 6, -5, 35, 0, 15, 2, 11, -6), [](G& g, const O& o) {
      const int S = 0x3A3C40; Col W = gkWood(0x8E4C24);
      g.prof(-20, -5, Val([](int x) { return x < -9 ? (double)jr(-3 + (x + 20) * 0.18) : -1.0; }), Val([](int x) { return x < -9 ? (double)jr(5 - (x + 20) * 0.18) : 3.0; }), 4, W);
      g.c(-21, -21, -3, 5, 4, S);
      g.c(-5, 20, -1, 1, 4, W);
      g.c(-6, 10, -4, -2, 4, S); g.y(4, 5, 2, 3, -2, -2, S);
      g.c(10, 12, -5, -4, 4, S); if (!o.optic) g.c(11, 11, -6, -6, 4, S).c(11, 11, -6, -6, 2, -1);
      g.rod(12, 22, -4, 2, S); g.c(13, 20, -5, -4, 4, W);
      gpBarrel(g, 10, 34, S); g.c(31, 33, -4, -1, 4, S).c(32, 33, -5, -5, 4, S);
      g.y(32, 33, -2, -2, -7, -6, S).y(32, 33, 1, 1, -7, -6, S).c(32, 33, -7, -7, 4, S).c(32, 32, -6, -6, 2, GK::blk2);
      g.c(21, 31, 0, 0, 2, 0x7A7C80); g.c(30, 31, -1, 0, 4, S);
      g.c(3, 8, 2, 4, 4, S); gpGuard(g, S, -2, 2, 4);
      akMount(g, o, S);
    }};
    t["svd"] = {A(-3, 7, -5, 47, 5, 15, 2, 11, -6), [](G& g, const O& o) {
      const int M = 0x34363A; Col W = gkWood(0x8C4C24);
      g.c(-21, -8, -2, -1, 4, W); g.c(-17, -12, -3, -3, 4, W);
      g.c(-22, -21, -2, 7, 4, W); g.c(-23, -23, -2, 7, 4, GK::rub);
      g.c(-20, -5, 6, 7, 4, W);
      g.bend(-4, -2, 2, 6, [](int k) { return -jr(k * 0.5); }, 4, W); g.c(-7, -1, 0, 1, 4, W);
      AkP p; p.M = M; p.H = Col(ZCF { return (z == -1 && x % 3 == 0) ? 0x3A2010 : W.at(x, y, z); }); p.Hu = W; p.hasHu = true; p.hi = 21; p.gas = 27; p.fs = 37; p.bx = 42; p.hood = true; p.noGrip = true;
      akBody(g, o, p);
      g.rod(21, 27, -4, 2, M);
      if (o.muzzle.empty()) g.rod(42, 46, -2, 4, Col(ZCF { return ((x == 43 || x == 45) && (y == -2 || y == 1)) ? GK::blk2 : M; }));
      gpBoxMag(g, 4, o, 0x2C2D30, 6, 4, 2, 0.2);
    }};
    t["mk14"] = {A(-6, 18, -4, 37, 3, 15, 2, -5, -8), [](G& g, const O& o) {
      m14(g, o, 34, GK::tan, GK::tan2);
      if (o.muzzle.empty()) { gpMuzzle(g, "brake", 34, GK::blk2); g.c(34, 35, -5, -4, 2, GK::blk2).c(34, 34, -8, -6, 2, GK::blk2); }
    }};
    t["sv98"] = {A(-6, 7, -4, 40, 5, 14, 3, 6, -6), [](G& g, const O& o) {
      const int M = 0x2C2E30; Col W = gkWood(0x7A4422);
      thumbhole(g, W, -22, true);
      g.c(-2, 16, -1, 2, 4, W);
      bolt(g, o, M, 16, 35, 4);
      g.c(6, 7, -5, -5, 4, M);
      if (o.muzzle.empty()) { g.rod(35, 39, -2, 4, M); g.c(38, 39, -5, -4, 4, M).c(38, 38, -5, -5, 2, -1); }
    }};
    t["m82"] = {[&] { Anchors a = A(-8, 18, -6, 48, 6, 14, 2, -7, -8); a.bip = 17; a.bipod = true; return a; }(), [](G& g, const O& o) {
      const int T = 0x9C8A64, M = 0x2E3033;
      g.prof(-26, -9, -4, Val(5, 1), 4, T); g.c(-28, -27, -5, 5, 4, GK::rub); g.c(-23, -22, 4, 8, 2, M);
      g.c(-8, 20, -5, 0, 4, Col(ZCF { return (x > 9 && (y == -2 || y == 1) && (x % 3 == 0) && (z == -3 || z == -2)) ? 0x1A1A1C : T; }));
      gpRail(g, -9, 20, -6, M);
      g.c(-6, 8, 1, 1, 4, T); g.c(3, 9, 1, 1, 4, T);
      if (!o.optic) g.c(4, 5, -8, -7, 2, M).c(9, 10, -8, -7, 2, M).c(4, 10, -9, -9, 2, M);
      gpBarrel(g, 20, 42, M, 4); if (o.muzzle.empty()) gpMuzzle(g, "bigbrake", 42, M);
      gpGrip(g, M, -4, 8, 0.3); gpGuard(g, M, -1, 3, 4);
      gpBoxMag(g, 3, o, M, 4, 6, 4, 0);
      gpBipod(g, 17, 1, 9, M);
      if (!o.optic) g.c(-7, -6, -7, -7, 4, M).c(-7, -6, -7, -7, 2, -1).c(18, 18, -8, -7, 2, M);
    }};
    t["pkm"] = {[&] { Anchors a = A(-4, 6, -6, 38, 3, 15, 3, 8, -8); a.bip = 32; a.bipod = true; return a; }(), [](G& g, const O& o) {
      const int M = 0x33353A, P = 0x27282B;
      g.c(-25, -24, -3, 5, 4, P); g.c(-24, -9, -3, -2, 4, P); g.c(-23, -9, 4, 5, 4, P); g.c(-9, -8, -3, 3, 4, P);
      g.c(-7, 10, -3, 1, 4, M); g.c(-6, 9, -5, -4, 4, M); g.c(7, 9, -6, -6, 4, M);
      if (!o.optic) g.c(8, 8, -7, -7, 4, M).c(8, 8, -7, -7, 2, -1);
      if (o.optic) g.y(-2, 4, -3, -3, -5, -1, M).c(-4, 5, -6, -6, 4, M);
      g.rod(10, 34, -2, 6, Col(ZCF { return (z & 1) ? shadeC(M, 0.7) : M; }));
      g.c(15, 15, -6, -6, 2, M).c(20, 20, -6, -6, 2, M).c(14, 21, -8, -7, 2, P);
      g.c(32, 34, -6, -6, 2, M).c(33, 34, -6, -6, 4, M); g.y(33, 34, -2, -2, -8, -7, M).y(33, 34, 1, 1, -8, -7, M).c(33, 33, -8, -7, 2, GK::blk2);
      g.c(10, 27, 1, 2, 2, M);
      if (o.muzzle.empty()) gpMuzzle(g, "prong", 35, M);
      gpBarrel(g, 34, 35, M);
      gpBipod(g, 32, 1, 11, M);
      gpGrip(g, P, -4, 8, 0.35); gpGuard(g, M, -1, 3, 4);
      gpBox(g, -1, 7, 8, 1, 0x4A5236, 4);
    }};

    // ---- SMGs
    t["mp5"] = {A(-4, 5, -4, 20, 0, 12, 2, -4, -6), [](G& g, const O& o) {
      const int B = 0x34363A, P = 0x26272A;
      g.prof(-19, -7, -3, Val(4, 1), 4, P); g.c(-20, -20, -3, 4, 4, GK::rub);
      mp5body(g, o, 9, B);
      g.rod(9, 16, -3, 2, B); g.y(12, 13, -3, -3, -3, -3, B);
      g.c(9, 15, -2, 1, 4, Col(ZCF { return (z == 1 && (x & 1)) ? shadeC(P, 0.7) : P; }));
      g.c(15, 16, -4, -1, 4, B); g.y(15, 16, -2, -2, -7, -5, B).y(15, 16, 1, 1, -7, -5, B).c(15, 16, -7, -7, 4, B).c(15, 15, -6, -6, 2, GK::blk2);
      gpBarrel(g, 16, 17, B); if (o.muzzle.empty()) g.rod(18, 19, -2, 2, B).y(18, 18, -2, 1, -2, -2, B);
    }};
    t["ump45"] = {A(-5, 10, -4, 18, 0, 11, 2, -5, -8), [](G& g, const O& o) {
      const int B = 0x2A2B2E;
      g.c(-7, -7, -3, 1, 4, B); g.c(-18, -8, -3, -2, 4, B); g.c(-18, -8, 2, 3, 4, B); g.c(-19, -19, -3, 3, 4, GK::rub);
      g.c(-6, 14, -3, 1, 4, Col(ZCF { return (x > 8 && (y == -2 || y == 1) && !(x & 1)) ? shadeC(B, 0.65) : B; }));
      gpRail(g, -6, 12, -4, B);
      gpBarrel(g, 15, 17, GK::park);
      gpDisc(g, -5, -6, 1, 4, B); g.c(-5, -5, -6, -6, 2, GK::blk2);
      hkFront(g, o, 10, -4, B);
      gpGrip(g, B, -4, 8, 0.3); g.c(-1, 4, 5, 5, 2, B).c(4, 4, 2, 5, 2, B).c(1, 1, 2, 3, 2, GK::blk2);
      gpBoxMag(g, 5, o, 0x222326, 9, 4, 2, 0.1);
    }};

    // ---- shotguns
    t["m870"] = {A(-3, 5, -4, 23, 0, 11, 2, -2, -5), [](G& g, const O& o) {
      const int M = 0x2E3033, P = 0x26272A;
      arStock(g, "m4", P, -5, M);
      g.c(-4, 6, -3, 1, 4, M); g.y(0, 3, 2, 2, -2, -1, GK::blk2);
      gpBarrel(g, 6, 22, M); g.rod(6, 18, 0, 2, M); g.c(18, 19, -3, 1, 2, M);
      g.c(8, 14, -2, 1, 4, Col(ZCF { return (x & 1) ? P : shadeC(P, 0.7); }));
      gpGrip(g, P, -4, 8, 0.38); gpGuard(g, M, -1, 3, 4);
      g.c(-3, -1, -4, -4, 4, M); if (!o.optic) g.c(-2, -2, -5, -5, 4, M).c(-2, -2, -5, -5, 2, -1);
      g.c(21, 21, -3, -3, 2, GK::white);
      if (o.optic) gpRail(g, -3, 5, -4, M);
    }};
    t["spas12"] = {A(-3, 5, -4, 26, 0, 11, 3, -2, -5), [](G& g, const O& o) {
      const int M = 0x2E3033, P = 0x26272A;
      g.y(-19, -5, -2, -2, -3, -3, M).y(-19, -5, 1, 1, -3, -3, M); g.c(-20, -19, -3, 3, 4, M); g.c(-19, -16, 3, 4, 2, M);
      g.c(-4, 6, -3, 1, 4, M);
      g.rod(6, 24, -2, 4, Col(ZCF { return ((z == -4 || y == -2 || y == 1) && (x & 1) && x < 23) ? 0x111113 : M; }));
      gpBarrel(g, 24, 25, M); g.rod(6, 20, 1, 2, M); g.c(20, 21, -1, 2, 4, M);
      g.c(8, 15, -1, 2, 4, Col(ZCF { return (x & 1) ? P : shadeC(P, 0.7); }));
      gpGrip(g, P, -4, 8, 0.38); gpGuard(g, M, -1, 3, 4);
      g.c(-3, -1, -4, -4, 4, M); if (!o.optic) g.c(-2, -2, -5, -5, 4, M).c(-2, -2, -5, -5, 2, -1);
      g.c(22, 22, -5, -5, 2, M);
      if (o.optic) gpRail(g, -3, 5, -4, M);
    }};
    // TOZ-34: a double-barrelled shotgun -- two barrels side by side on a wooden stock, a rib between, no pump
    auto toz = [](bool sawn) {
      return [sawn](G& g, const O&) {
        const int S = 0x2E3033; Col W = gkWood(0x7A4422);
        const int e = sawn ? 17 : 32;
        if (sawn) { g.bend(-6, -2, 0, 8, [](int k) { return -jr(k * 0.3); }, 4, W); g.c(-7, -3, 8, 8, 4, GK::rub); }
        else { g.prof(-22, -5, Val(-1, -3), Val(7, 3), 4, W); g.c(-23, -23, -1, 7, 4, GK::rub); g.bend(-6, -3, 3, 6, [](int k) { return -jr(k * 0.2); }, 4, W); }
        g.c(-6, 4, -4, 0, 6, Col(ZCF { return (z == -4 || z == 0) ? shadeC(S, 1.25) : S; }));
        g.y(-7, -5, -1, 0, -5, -4, S); g.y(-7, -5, -3, -2, -5, -4, S);      // the two hammers
        gpGuard(g, S, -1, 3, 3);
        g.rod(4, e, -2, 2, S, -1); g.rod(4, e, -2, 2, S, 1);
        g.c(4, e, -4, -4, 2, shadeC(S, 1.4));
        g.c(5, sawn ? 11 : 17, -1, 0, 4, W);
        g.c(e - 1, e - 1, -5, -5, 2, GK::white);
        g.c(-3, -2, -5, -5, 2, S);
      };
    };
    { Anchors a = A(0, 0, -4, 33, 0, 12, 2, -2, -5); a.rail = false; t["toz34"] = {a, toz(false)}; Anchors b = A(0, 0, -4, 18, 0, 8, 2, -2, -5); b.rail = false; t["sawnoff"] = {b, toz(true)}; }
    // RPG-7: the tube with its flared rear, wooden heat shield, grips and a PG-7 warhead in the muzzle
    { Anchors a = A(-2, 6, -4, 43, 0, 9, 3, 0, -7); a.line = -7; t["rpg7"] = {a, [](G& g, const O& o) {
      const int S = 0x3A3C40, OD = 0x4A5236; Col W = gkWood(0x7A4422);
      g.rod(-14, -11, -2, 6, S); g.rod(-10, 24, -2, 4, S);
      g.rod(2, 14, -2, 6, W);
      g.c(-6, 4, 0, 1, 4, S); gpGuard(g, S, -1, 3, 3); gpGrip(g, W, -4, 8, 0.3);
      g.c(10, 12, 0, 1, 4, S); g.c(10, 12, 2, 7, 4, W);
      if (!o.optic) { g.c(0, 1, -5, -5, 4, S).c(0, 1, -5, -5, 2, -1); }
      g.c(20, 21, -5, -5, 2, S); g.c(20, 20, -7, -6, 2, S);
      g.rod(24, 24, -2, 6, S);
      g.rod(25, 33, -2, 8, OD); g.rod(34, 37, -2, 6, OD); g.rod(38, 40, -2, 4, shadeC(OD, 0.8)); g.rod(41, 42, -2, 2, S);
      g.rod(26, 26, -2, 8, shadeC(OD, 0.6));
    }}; }

    // ---- pistols
    auto m9 = [](int E, int B, int grips) {   // slide runs to E (11 for the M9)
      return [=](G& g, const O& o) {
        g.c(-4, E, -3, -1, 4, Col([=](int x, int, int) -> int { return x <= -2 && (x & 1) ? shadeC(B, 0.7) : B; }));
        g.c(1, E - 2, -3, -3, 4, -1); g.c(2, E - 3, -2, -2, 4, -1);
        g.c(0, E, -3, -1, 2, Col([=](int, int, int z) -> int { return z == -3 ? 0x7A7D82 : 0x5C5F64; }));
        g.y(-3, -2, -3, -3, -2, -2, B).y(-3, -2, 2, 2, -2, -2, B);
        g.c(-5, -5, -3, -2, 2, B);
        gpPistolSights(g, o, -3, E - 1);
        g.c(-4, E - 2, 0, 0, 4, B);
        g.c(1, 6, 4, 4, 2, B).c(7, 7, 1, 3, 2, B).c(6, 6, 3, 3, 2, B).c(6, 6, 0, 1, 2, B);
        g.c(2, 3, 1, 2, 2, GK::blk2);
        g.bend(-4, 0, 1, 7, [](int k) { return -jr(k * 0.22); }, 4, Col([=](int, int y, int) -> int { return (y == -2 || y == 1) ? grips : B; }));
        g.c(-5, -4, 1, 2, 4, B);
        gpPistolMag(g, o, -6, 8);
      };
    };
    t["pm"] = {PIS(10, -4), m9(9, 0x2A2C30, 0x6A3A1E)};
    t["fort12"] = {PIS(12, -4), m9(11, 0x2E2F33, 0x1C1C1E)};
    t["m1911"] = {PIS(12, -4), [](G& g, const O& o) {
      const int S = 0x4A4B4C, W = 0x7A4420;
      g.c(-4, 11, -3, -1, 4, Col(ZCF { return x <= -2 && (x & 1) ? shadeC(S, 0.72) : S; }));
      g.c(11, 11, -2, -1, 2, GK::light);
      g.y(-1, 1, 2, 2, -2, -2, shadeC(S, 0.6));
      g.c(-5, -5, -3, -2, 2, S).c(-6, -5, 0, 0, 4, S).c(-5, -5, -1, -1, 4, S);
      g.y(2, 5, -3, -3, 0, 0, S);
      gpPistolSights(g, o, -3, 10);
      g.c(-4, 9, 0, 0, 4, S);
      g.c(1, 5, 3, 3, 2, S).c(5, 5, 1, 2, 2, S).c(2, 3, 1, 2, 2, GK::blk2);
      g.bend(-4, -1, 1, 7, [](int k) { return -jr(k * 0.18); }, 4, Col(ZCF { return (y == -2 || y == 1) ? ((x + z) % 3 == 0 ? shadeC(W, 0.7) : W) : S; }));
      gpPistolMag(g, o, -6, 8, S);
    }};
    t["g17"] = {PIS(12, -4), [](G& g, const O& o) {
      const int B = 0x2A2B2D, F = 0x202122;
      g.c(-4, 11, -3, -1, 4, Col(ZCF { return x <= -2 && (x & 1) ? shadeC(B, 0.72) : B; }));
      g.c(11, 11, -3, -3, 4, -1);
      g.c(11, 11, -2, -1, 2, GK::blk2);
      gpPistolSights(g, o, -3, 9);
      g.c(-4, 9, 0, 0, 4, F); g.c(6, 9, 1, 1, 4, Col(ZCF { return (x & 1) ? F : shadeC(F, 0.7); }));
      g.c(1, 6, 4, 4, 2, F).c(6, 6, 1, 4, 2, F).c(2, 2, 1, 2, 2, GK::blk2);
      g.bend(-4, -1, 1, 8, [](int k) { return -jr(k * 0.3); }, 4, F);
      g.c(-1, -1, 4, 4, 4, F).c(-2, -2, 6, 6, 4, F);
      gpPistolMag(g, o, -7, 9, F, 0);
    }};
    t["mp443"] = {PIS(12, -4), [](G& g, const O& o) {
      const int B = 0x28292B;
      g.c(-4, 11, -3, -1, 4, Col(ZCF { return x <= -2 && (x & 1) ? shadeC(B, 0.72) : B; }));
      g.c(10, 11, -3, -3, 4, -1).c(11, 11, -2, -2, 4, -1).c(11, 11, -2, -1, 2, GK::blk2);
      g.c(-5, -5, -3, -2, 2, B).c(-6, -6, -3, -3, 2, B);
      g.y(-3, -2, -3, -3, -1, -1, B);
      gpPistolSights(g, o, -3, 9);
      g.c(-4, 9, 0, 0, 4, B);
      g.c(1, 7, 4, 4, 2, B).c(7, 7, 1, 4, 2, B).c(8, 8, 3, 4, 2, B).c(2, 2, 1, 2, 2, GK::blk2);
      g.bend(-4, -1, 1, 8, [](int k) { return -jr(k * 0.34); }, 4, Col(ZCF { return (y == -2 || y == 1) && (z & 1) ? 0x1E1F21 : B; }));
      gpPistolMag(g, o, -7, 9, B);
    }};
    return t;
  }();
  return T;
}

// ---- attachments (BoS gunGrid / gunOpticBoxes) ----
void opticBoxes(G& g, const std::string& optic, int x0, int rz) {
  const int D = 0x1E1F21, D2 = 0x2A2B2E;
  if (optic == "reddot") {
    g.add(x0 + 3, -1, rz - 1, x0 + 6, 0, rz - 1, D); g.rod(x0 + 2, x0 + 7, rz - 3, 4, D2);
    g.add(x0 + 4, -1, rz - 6, x0 + 5, 0, rz - 6, D2); g.add(x0 + 4, 2, rz - 4, x0 + 5, 2, rz - 3, D2);
    g.add(x0 + 2, -1, rz - 4, x0 + 7, 0, rz - 3, -1);          // the window, open all the way through
  } else if (optic == "holo") {
    g.add(x0, -2, rz - 1, x0 + 6, 1, rz - 1, D); g.add(x0 + 3, -2, rz - 3, x0 + 6, 1, rz - 2, D2);
    g.add(x0, -2, rz - 5, x0 + 2, -2, rz - 2, D2); g.add(x0, 1, rz - 5, x0 + 2, 1, rz - 2, D2); g.add(x0, -2, rz - 5, x0 + 3, 1, rz - 5, D2);
    g.add(x0 + 2, -1, rz - 4, x0 + 2, 0, rz - 2, -1);
  } else if (optic == "acog") {
    g.add(x0 + 2, -1, rz - 1, x0 + 6, 0, rz - 1, D); g.rod(x0, x0 + 1, rz - 3, 4, D2); g.add(x0 + 2, -2, rz - 5, x0 + 6, 1, rz - 2, D2);
    g.rod(x0 + 7, x0 + 8, rz - 3, 4, D2); g.add(x0 + 9, -1, rz - 4, x0 + 9, 0, rz - 3, 0x3A6A5A);
    g.add(x0 + 3, -1, rz - 6, x0 + 4, 0, rz - 6, D); g.add(x0 + 3, -1, rz - 7, x0 + 3, 0, rz - 7, 0x7AA0B0);
  } else {   // pso1, scope8: a rifle scope in two rings
    const int L = optic == "scope8" ? 18 : 14, big = 4, zc = rz - 3;
    g.rod(x0 - 2, x0 + 1, zc, 4, D); g.rod(x0 + 2, x0 + L - 4, zc, 2, D2); g.rod(x0 + L - 3, x0 + L, zc, big, D);
    const int tx = x0 + jr(L * 0.42);
    g.add(tx, -1, zc - 3, tx + 1, 0, zc - 2, D); g.add(tx, 1, zc - 1, tx + 1, 2, zc, D);
    g.add(x0 + 3, -1, zc + 1, x0 + 4, 0, rz - 1, D); g.add(x0 + L - 6, -1, zc + 1, x0 + L - 5, 0, rz - 1, D);
    g.add(x0 + L + 1, -1, zc - 1, x0 + L + 1, 0, zc, GLASS); g.add(x0 - 3, -1, zc - 1, x0 - 3, 0, zc, 0x101012);
    if (optic == "pso1") { g.add(x0 + L + 1, -2, zc - 3, x0 + L + 4, 1, zc - 3, D); g.add(x0 + 5, -3, rz - 2, x0 + 9, -3, rz, D); }
  }
}
void uglBoxes(G& g, int x0, int x1, int top, int rz) {   // the M320 / GP-25 hung under the handguard
  const int T = 0x2D2E30, T2 = 0x38393C, D = 0x161617, zc = top + 3;
  g.add(x0 + 2, -1, top, x1 - 3, 0, top, T2);
  g.rod(x0 + 5, x1 - 1, zc, 4, Col([=](int xx, int, int) -> int { return (xx - x0) % 4 == 1 ? shadeC(T, 0.78) : T; }));
  g.rod(x1, x1, zc, 4, T2); g.add(x1, -1, zc - 1, x1, 0, zc, D);
  g.add(x0, -2, top + 1, x0 + 4, 1, top + 5, T2); g.add(x0 + 1, -3, top + 2, x0 + 3, -3, top + 3, D);
  g.add(x0 + 1, -1, top + 6, x0 + 3, 0, top + 9, T); g.add(x0 + 1, -1, top + 10, x0 + 3, 0, top + 10, D);
  g.add(x0 + 4, -1, top + 9, x0 + 7, 0, top + 9, T); g.add(x0 + 7, -1, top + 6, x0 + 7, 0, top + 8, T); g.add(x0 + 5, -1, top + 6, x0 + 5, 0, top + 7, D);
  const int lx = std::max(x0 + 3, std::min(x0 + 6, x1 - 8));
  g.add(lx, -3, top - 2, std::min(lx + 5, x1 - 2), -3, top - 1, D);
}
void suppressorBoxes(G& g, int x, int zc, bool pistol) {
  const int L = pistol ? 8 : 10;
  g.rod(x, x + L, zc, 4, Col([=](int xx, int, int) -> int { return xx == x ? 0x1E1E20 : 0x2A2A2D; })); g.rod(x + L + 1, x + L + 1, zc, 2, 0x1A1A1C);
}
void compensatorBoxes(G& g, int x, int zc) {
  g.rod(x, x + 4, zc, 4, 0x2E2F32); g.add(x + 1, -2, zc - 1, x + 1, 1, zc, 0x151517); g.add(x + 3, -2, zc - 1, x + 3, 1, zc, 0x151517);
}

// ---- which attachments are fitted ----
struct Fit { std::string optic, muzzle, under, mag; };
Fit resolve(const ItemStack& s) {
  Fit f;
  const zone::ZItem* z = zone::zi(s.id);
  json att = zone::gunAttachments(s);
  if (z && !att.contains("optic") && !z->gun.defaultOptic.empty() && !s.tag.is_object()) att["optic"] = z->gun.defaultOptic;
  auto get = [&](const char* k) { return att.contains(k) && att[k].is_string() ? att[k].get<std::string>() : std::string(); };
  f.optic = get("optic"); f.muzzle = get("muzzle"); f.under = get("under"); f.mag = get("mag");
  return f;
}
std::string opticKind(const std::string& id) { return id == "kobra" ? "reddot" : id; }   // holo, acog, pso1, scope8 keep their names
bool isLauncher(const std::string& id) { const zone::ZItem* a = zone::zi(id); return a && a->raw.contains("att") && a->raw["att"].contains("launcher"); }

// ---- baking ----
struct Baked { VoxelModel* m; GunModelInfo info; };
bool bake(const std::vector<Box>& boxes, VoxelModel& out) {
  int mnx = INT_MAX, mxx = INT_MIN, mny = INT_MAX, mxy = INT_MIN, mnz = INT_MAX, mxz = INT_MIN;
  for (const Box& b : boxes) { mnx = std::min(mnx, b.x0); mxx = std::max(mxx, b.x1); mny = std::min(mny, b.y0); mxy = std::max(mxy, b.y1); mnz = std::min(mnz, b.z0); mxz = std::max(mxz, b.z1); }
  if (mnx > mxx) return false;
  VoxelGrid g(ivec3(mxx - mnx + 1, mxz - mnz + 1, mxy - mny + 1));
  for (const Box& b : boxes)
    for (int bz = b.z0; bz <= b.z1; bz++) for (int by = b.y0; by <= b.y1; by++) for (int bx = b.x0; bx <= b.x1; bx++)
      g.set(bx - mnx, mxz - bz, by - mny, toVox(b.c.at(bx, by, bz)));
  ivec3 lo, hi;
  if (!g.bounds(lo, hi)) return false;
  VoxelGrid c(hi - lo);
  for (int z = lo.z; z < hi.z; z++) for (int y = lo.y; y < hi.y; y++) for (int x = lo.x; x < hi.x; x++) c.set(x - lo.x, y - lo.y, z - lo.z, g.at(x, y, z));
  out.frame.voxel = FINE;
  out.frame.origin = vec3(mnx + lo.x, -mxz - 1 + lo.y, mny + lo.z) * FINE;   // grid y = (mxz - bz), so model y of its floor = (-mxz - 1 + y)
  out.size = c.size; out.kind = VoxelModel::Flat; out.built = true;
  out.depth = std::max(0.02f, c.size.z * FINE * 0.5f);
  buildVoxelMesh(c, out.data);
  return true;
}

// build the gun for a stack (CPU only). A gun of the catalogue we have no model for: by its class.
bool buildGun(const ItemStack& s, VoxelModel& out, GunModelInfo& info) {
  const zone::ZItem* z = zone::zi(s.id);
  if (!z || !z->isGun) return false;
  const auto& T = table();
  auto it = T.find(s.id);
  if (it == T.end()) {
    const std::string& c = z->gun.cls;
    const char* alt = c == "pistol" ? "g17" : c == "smg" ? "mp5" : c == "shotgun" ? "m870" : c == "carbine" ? "g36" : c == "dmr" ? "svd" : c == "sniper" ? "sv98" : c == "lmg" ? "pkm" : c == "launcher" ? "rpg7" : "m416";
    it = T.find(alt);
    if (it == T.end()) return false;
  }
  const Anchors& a = it->second.a;
  const Fit fit = resolve(s);
  O o;
  const std::string look = opticKind(fit.optic);
  o.optic = !fit.optic.empty() && a.rail;
  if (!fit.muzzle.empty()) o.muzzle = fit.muzzle == "brake" ? "compensator" : "suppressor";
  const bool ugl = !fit.under.empty() && isLauncher(fit.under) && a.ugl;
  o.under = !fit.under.empty() && (ugl || !isLauncher(fit.under));
  o.mag = fit.mag == "mag_ext" ? "ext" : fit.mag == "drum12" ? "drum" : "";
  G g;
  it->second.build(g, o);
  // ---- attachments
  double sightLine = a.line; int sightX = a.rear; bool scope = false;
  if (o.optic) {
    opticBoxes(g, look, a.r0, a.rz);
    sightLine = look == "holo" ? a.rz - 3.5 : a.rz - 3; sightX = a.r0 - 3;
    scope = look == "pso1" || look == "acog" || look == "scope8";
  }
  const int zc = a.bore;
  if (!fit.muzzle.empty()) {
    const int x = a.mx - a.dev;
    if (fit.muzzle == "brake") compensatorBoxes(g, x, zc); else suppressorBoxes(g, x, zc, a.pistol);
  }
  const int G_ = 0x222326;
  int foreX = a.ux == NONE ? 8 : a.ux + 1, foreZ = a.ux == NONE ? 4 : a.uz + 3;
  if (a.ux != NONE && !fit.under.empty()) {
    const int ux = a.ux, uz = a.uz, hw = a.hw, ax = a.ax == NONE ? ux : a.ax, zz = a.side == NONE ? a.bore : a.side;
    if (ugl) {
      uglBoxes(g, a.u0, a.u1, a.utop, a.rz);
      foreX = std::min(a.u1 - 3, std::max(a.u0 + 7, ux)); foreZ = a.utop + 5;
    } else if (fit.under == "vgrip") {
      g.add(ux - 1, -2, uz, ux + 3, 1, uz, G_); g.add(ux, -1, uz + 1, ux + 2, 0, uz + 7, G_); g.add(ux - 1, -1, uz + 8, ux + 3, 0, uz + 8, G_);
      foreX = ux + 1; foreZ = uz + 5;
    } else if (fit.under == "light") {
      g.rod(ax + 2, ax + 8, zz, 2, G_, hw + 1); g.add(ax + 3, hw, zz - 1, ax + 5, hw, zz, G_); g.add(ax + 9, hw, zz - 1, ax + 9, hw + 1, zz, 0xF2EFD8 | GLOW);
    } else if (fit.under == "laser") {
      g.add(ax + 2, hw, zz - 1, ax + 6, hw + 1, zz, G_); g.add(ax + 7, hw, zz - 1, ax + 7, hw, zz - 1, 0xE02020 | GLOW); g.add(ax + 7, hw + 1, zz, ax + 7, hw + 1, zz, 0x40A040);
    } else if (fit.under == "bipod" && !a.bipod) {
      const int bx = a.bip == NONE ? ux + 8 : a.bip, bz = uz, L = 0x2A2B2E;
      g.add(bx - 1, -2, bz, bx + 2, 1, bz + 1, G_);
      g.add(bx - 10, -2, bz + 2, bx + 1, -2, bz + 2, L); g.add(bx - 10, 1, bz + 2, bx + 1, 1, bz + 2, L);
    }
  }
  // ---- bake
  if (!bake(g.b, out)) return false;
  const vec3 org = out.frame.origin;
  const float back = org.x, front = org.x + out.size.x * FINE;
  info.back = back; info.front = front; info.length = front - back;
  info.sightY = (float)(-sightLine * FINE); info.sightX = sightX * FINE;
  info.grip = vec3(-3 * FINE, a.pistol ? -4 * FINE : -5 * FINE, 0);
  info.pistol = a.pistol;
  info.fore = a.pistol ? vec3(-2 * FINE, -7 * FINE, -0.02f) : vec3(foreX * FINE, -foreZ * FINE, 0);
  info.scope = scope; info.optic = o.optic;
  return true;
}

// ---- the cache (GL thread) ----
struct Entry { std::unique_ptr<VoxelModel> m; GunModelInfo info; u64 used = 0; };
std::unordered_map<std::string, Entry>& cache() { static auto* c = new std::unordered_map<std::string, Entry>(); return *c; }   // never destroyed: GL objects must not outlive the context
const Entry* fetch(const ItemStack& s) {
  static u64 tick = 0; tick++;
  if (s.empty()) return nullptr;
  const Fit f = resolve(s);
  const std::string key = s.id + "|" + f.optic + "|" + f.muzzle + "|" + f.under + "|" + f.mag;
  auto& C = cache();
  auto it = C.find(key);
  if (it != C.end()) { it->second.used = tick; return it->second.m ? &it->second : nullptr; }
  if (C.size() > 160) {   // forget the ones not used for a while
    for (auto i = C.begin(); i != C.end();) { if (i->second.used + 3000 < tick) i = C.erase(i); else ++i; }
  }
  Entry e; e.used = tick;
  auto m = std::make_unique<VoxelModel>();
  if (buildGun(s, *m, e.info)) {
    m->key = key;
    m->gl.upload(m->data);
    m->data.verts.clear(); m->data.verts.shrink_to_fit();
    e.m = std::move(m);
  }
  auto& slot = C[key]; slot = std::move(e);
  return slot.m ? &slot : nullptr;
}
} // namespace

const VoxelModel* zoneGunModel(const ItemStack& gun) { const Entry* e = fetch(gun); return e && e->m->ready() ? e->m.get() : nullptr; }
const GunModelInfo* zoneGunInfo(const ItemStack& gun) { const Entry* e = fetch(gun); return e ? &e->info : nullptr; }

bool buildZoneGunModel(const ItemStack& gun, VoxelModel& out, GunModelInfo& info) { return buildGun(gun, out, info); }

std::string zoneGunItemKey(const ItemStack& s) {
  const zone::ZItem* z = zone::zi(s.id);
  if (!z || !z->isGun) return "";
  const Fit f = resolve(s);
  if (f.optic == z->gun.defaultOptic && f.muzzle.empty() && f.under.empty() && f.mag.empty()) return "";
  return "zg:" + s.id + "|" + f.optic + "|" + f.muzzle + "|" + f.under + "|" + f.mag;
}

bool buildZoneGunItem(const std::string& id, VoxelModel& out) {
  if (id.rfind("zg:", 0) == 0) {   // a gun with attachments
    std::vector<std::string> v; size_t a = 3;
    for (size_t b; (b = id.find('|', a)) != std::string::npos; a = b + 1) v.push_back(id.substr(a, b - a));
    v.push_back(id.substr(a));
    if (v.size() < 5) return false;
    ItemStack g(v[0], 1); json att = json::object();
    const char* slots[4] = {"optic", "muzzle", "under", "mag"};
    for (int i = 0; i < 4; i++) if (!v[1 + i].empty()) att[slots[i]] = v[1 + i];
    g.tag = json{{"att", att}};
    GunModelInfo info;
    if (!buildGun(g, out, info)) return false;
    zk::normalizeModel(out, 1.0f);
    return true;
  }
  const zone::ZItem* z = zone::zi(id);
  if (!z) return false;
  if (z->isGun) {
    GunModelInfo info;
    if (!buildGun(ItemStack(id, 1), out, info)) return false;
    zk::normalizeModel(out, 1.0f);
    return true;
  }
  if (z->cat != "attach") return false;
  // an attachment on its own: its boxes, drawn the way they hang on a gun
  G g; const std::string look = opticKind(id);
  if (id == "kobra" || id == "holo" || id == "acog" || id == "pso1" || id == "scope8") opticBoxes(g, look, 0, -4);
  else if (id.rfind("supp_", 0) == 0) { suppressorBoxes(g, 0, -2, id == "supp_pistol"); g.rod(-3, -1, -2, 2, GK::park); }
  else if (id == "brake") compensatorBoxes(g, 0, -2);
  else if (id == "vgrip") { g.add(-4, -3, 0, 4, 2, 0, 0x222326); g.add(-3, -2, 1, 3, 1, 8, 0x2A2B2E); g.add(-4, -2, 9, 4, 2, 9, 0x222326); g.add(-4, -4, -1, 4, 4, -1, GK::blk); }
  else if (id == "laser") { g.add(0, -2, -2, 8, 2, 2, 0x222326); g.add(9, -1, -1, 9, 0, 0, 0xE02020 | GLOW); g.add(2, -3, -1, 3, 3, 1, 0x3A3C40); g.add(5, 2, -1, 6, 2, 1, 0x40A040); }
  else if (id == "light") { g.rod(0, 8, 0, 4, 0x222326); g.add(9, -1, -1, 9, 0, 0, 0xF2EFD8 | GLOW); g.rod(9, 10, 0, 6, 0x2E2F32); g.add(3, -2, 2, 5, 1, 3, 0x3A3C40); }
  else if (id == "bipod") { g.add(0, -2, 0, 4, 1, 2, 0x222326); for (int k = 0; k < 11; k++) { int s = k / 4; g.add(1, -2 - s, 3 + k, 2, -2 - s, 3 + k, 0x2A2B2E); g.add(1, 1 + s, 3 + k, 2, 1 + s, 3 + k, 0x2A2B2E); } g.add(0, -4, 14, 3, -3, 14, 0x161617); g.add(0, 2, 14, 3, 3, 14, 0x161617); }
  else if (id == "gp25" || id == "m203") { uglBoxes(g, 0, 24, 0, -4); }
  else if (id == "mag_ext") { O o; o.mag = "ext"; gpStanag(g, 0, o, 0x303134, 10); g.c(-1, 4, 2, 2, 4, 0x303134); g.c(1, 2, 0, 1, 2, 0x303134); }
  else if (id == "drum12") { gpDisc(g, 0, 0, 7, 6, 0x26272A); gpDisc(g, 0, 0, 6, 8, 0x2E2F31); g.c(-2, 2, -9, -7, 4, 0x26272A); g.c(-1, 1, 0, 0, 2, 0x55575C); }
  else return false;
  if (!bake(g.b, out)) return false;
  zk::normalizeModel(out, id == "bipod" || id == "gp25" || id == "m203" ? 0.95f : 0.8f);
  return true;
}

} // namespace dq
