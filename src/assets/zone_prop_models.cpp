// assets/zone_prop_models.cpp — see zone_prop_models.h. Every builder works in metres in the kit's frame (x length with the
// front at +x, y up from the ground, z across with +z its right side); PB turns that into voxels (1/8 m).
#include "assets/zone_prop_models.h"
#include "assets/zone_model_kit.h"
#include "gen/zone_details.h"
#include <cmath>

namespace dq {
namespace {
using namespace zk;
using zone::PL_OK; using zone::PL_BURNT; using zone::PL_STRIPPED;

constexpr int TYRE = 0x242321, HUB = 0x55524c, CHROME = 0x9c9c96, LAMP = 0xc4c0a4, TAIL = 0x7a2822, DARK = 0x1e1e1c, SEAT = 0x4c3c32,
              CANVAS = 0x5c5f40, WOOD = 0x6c5c46, GLASS = 0x2c383e, RUBBER = 0x2a2a28, STEEL = 0x5e5c58, RUSTC = 0x6e3e24, OLIVE = 0x4c5a3a;

struct PB {
  Canvas cv; vec3 off; int look; u32 seed;
  PB(vec3 lo, vec3 hi, int lk, u32 sd) : cv((int)std::ceil((hi.x - lo.x) * PROP_VOX) + 1, (int)std::ceil((hi.y - lo.y) * PROP_VOX) + 1, (int)std::ceil((hi.z - lo.z) * PROP_VOX) + 1),
                                          off(-lo), look(lk), seed(sd) {}
  vec3 v(vec3 m) const { return (m + off) * PROP_VOX; }
  // a box in metres (lo..hi)
  void box(vec3 a, vec3 b, const Col& c) {
    const vec3 p = v(glm::min(a, b)), q = v(glm::max(a, b));
    cv.box((int)std::floor(p.x + .01f), (int)std::floor(p.y + .01f), (int)std::floor(p.z + .01f), (int)std::ceil(q.x - .01f) - 1, (int)std::ceil(q.y - .01f) - 1, (int)std::ceil(q.z - .01f) - 1, c);
  }
  void cut(vec3 a, vec3 b) { box(a, b, Col(-1)); }
  // any shape: inside(metres) over the box lo..hi
  template <class F> void shape(vec3 lo, vec3 hi, F inside, const Col& c) { cv.fill(v(lo), v(hi), [&](vec3 pv) { return inside(pv / PROP_VOX - off); }, c); }
  void cylX(float y, float z, float r, float x0, float x1, const Col& c) { const vec3 a = v(vec3(x0, y, z)), b = v(vec3(x1, y, z)); cv.cylX(a.y, a.z, r * PROP_VOX, r * PROP_VOX, (int)a.x, (int)b.x - 1, c); }
  void cylY(float x, float z, float r, float y0, float y1, const Col& c) { const vec3 a = v(vec3(x, y0, z)), b = v(vec3(x, y1, z)); cv.cylY(a.x, a.z, r * PROP_VOX, r * PROP_VOX, (int)a.y, (int)b.y - 1, c); }
  void cylZ(float x, float y, float r, float z0, float z1, const Col& c) { const vec3 a = v(vec3(x, y, z0)), b = v(vec3(x, y, z1)); cv.cylZ(a.x, a.y, r * PROP_VOX, r * PROP_VOX, (int)a.z, (int)b.z - 1, c); }
  void ell(vec3 c0, vec3 r, const Col& c) { const vec3 a = v(c0); cv.ell(a.x, a.y, a.z, r.x * PROP_VOX, r.y * PROP_VOX, r.z * PROP_VOX, c); }
  void rod(vec3 a, vec3 b, float r, const Col& c) { cv.rod(v(a), v(b), r * PROP_VOX, r * PROP_VOX, c); }

  // ---- colours, weathered by look: blotches of rust (3-voxel cells), dirt toward the ground; burnt = black and rust, no glass
  Col paint(int base, float rust = .22f) const {
    const int lk = look; const u32 s = seed;
    return ZCF {
      const double b1 = hash3(x / 3, y / 3, z / 3, (int)s), b2 = hash3(x / 2, y / 2, z / 2, (int)s + 7), j = hash3(x >> 1, y >> 1, z >> 1, (int)s + 3);
      int c = shadeC(base, .9 + .12 * j);
      if (lk == PL_BURNT) { c = mixC(c, 0x2b2622, .84); if (b1 < .42) c = mixC(c, b2 < .5 ? 0x6a3e26 : 0x4e3426, .75); else if (b2 < .12) c = 0x6e6a62; return c; }
      const double r = rust + (lk == PL_STRIPPED ? .18 : 0);
      if (b1 < r) c = mixC(c, b2 < .5 ? RUSTC : 0x8a5230, .72);
      else if (b1 < r + .1) c = mixC(c, 0x7a5e44, .3);
      if (y < 8) c = mixC(c, 0x4a4036, (1 - y / 8.0) * .35);
      return c;
    };
  }
  Col plain(int base) const { return paint(base, .08f); }
  Col glass() const {
    if (look != PL_OK) return Col(-1);
    const u32 s = seed;
    return ZCF { const double b = hash3(x / 2, y / 2, z / 2, (int)s + 11); return b < .14 ? -1 : shadeC(GLASS, .9 + .25 * hash3(x, y / 3, z, (int)s)); };
  }
  Col wood() const { const int lk = look; const u32 s = seed; return ZCF { int c = shadeC(WOOD, .82 + .25 * hash3(x / 2, y, z / 2, (int)s + 5)); if (lk == PL_BURNT) c = mixC(c, 0x1e1a18, .85); return c; }; }
  // a road wheel on the z side: a tyre and its hub (a rusty rim only when stripped or burnt)
  void wheel(float x, float r, float z0, float z1) {
    const float yc = r;
    if (look == PL_OK) { cylZ(x, yc, r, z0, z1, Col(TYRE)); cylZ(x, yc, r * .55f, z0 - .02f, z1 + .02f, paint(HUB, .3f)); }
    else cylZ(x, yc, r * .62f, z0 + .03f, z1 - .03f, paint(look == PL_BURNT ? 0x3a2a22 : HUB, .6f));
  }
  void wheels(float x, float r, float W, float w = .22f) { wheel(x, r, -W / 2, -W / 2 + w); wheel(x, r, W / 2 - w, W / 2); }
  // a box whose front (+x) and back faces lean in by `fs` / `bs` metres per metre of height above lo.y
  void wedge(vec3 lo, vec3 hi, float fs, float bs, const Col& c) {
    shape(lo, hi, [&](vec3 m) { const float t = m.y - lo.y; return m.x <= hi.x - t * fs && m.x >= lo.x + t * bs; }, c);
  }
  // a box with its top edges rounded (radius r in the y-z plane): bus and van bodies
  void rounded(vec3 lo, vec3 hi, float r, const Col& c) {
    shape(lo, hi, [&](vec3 m) {
      const float dz = std::max(0.f, std::abs(m.z - (lo.z + hi.z) * .5f) - ((hi.z - lo.z) * .5f - r)), dy = std::max(0.f, m.y - (hi.y - r));
      return dz * dz + dy * dy <= r * r; }, c);
  }
};

int pick(int tint, std::initializer_list<int> pal) { return pal.begin()[tint % pal.size()]; }

// ---------------------------------------------------------------- cars
void car(PB& b, float L, float W, float H, int style, int tint) {
  const int body = style == 2 ? pick(tint, {0x2e2e2c, 0xb4ac9c, 0x4a5a72, 0x5a6a52}) : pick(tint, {0xb8b0a0, 0x6a7a5a, 0x4a5a78, 0x8a3a2a, 0xa4a8a0, 0x8a7a4a, 0x6a5040});
  const Col P = b.paint(body);
  const float wr = .3f, wx = L / 2 - (style == 3 ? .68f : .8f), sill = .25f, belt = .82f;
  // the lower body with arches, then the wheels in them
  b.box(vec3(-L / 2, sill, -W / 2), vec3(L / 2, belt, W / 2), P);
  b.wedge(vec3(L / 2 - .25f, sill, -W / 2), vec3(L / 2, belt, W / 2), .0f, .0f, P);
  for (float x : {-wx, wx}) { b.cylZ(x, wr, wr + .06f, -W / 2 - .01f, -W / 2 + .3f, Col(-1)); b.cylZ(x, wr, wr + .06f, W / 2 - .3f, W / 2 + .01f, Col(-1)); b.wheels(x, wr, W - .04f, .18f); }
  // the cabin: glass round, pillars and roof in paint, hollow, seats
  const float c0 = style == 3 ? -L * .3f : -L * .27f, c1 = style == 3 ? L * .12f : L * .15f, ci = .1f;
  b.wedge(vec3(c0, belt, -W / 2 + ci), vec3(c1, H, W / 2 - ci), .55f, -.35f, b.glass());
  b.wedge(vec3(c0, H - .08f, -W / 2 + ci), vec3(c1, H, W / 2 - ci), .55f, -.35f, P);
  const float mid = (c0 + c1) * .5f - .05f;
  b.wedge(vec3(c1 - .2f, belt, -W / 2 + ci), vec3(c1, H, W / 2 - ci), .55f, -10.f, P);    // A pillars (the windscreen frame)
  b.box(vec3(mid, belt, -W / 2 + ci), vec3(mid + .08f, H, W / 2 - ci), P);                 // B pillars
  b.wedge(vec3(c0, belt, -W / 2 + ci), vec3(c0 + .25f, H, W / 2 - ci), -10.f, -.35f, P);   // C pillars
  b.cut(vec3(c0 + .2f, belt - .1f, -W / 2 + ci + .08f), vec3(c1 - .25f, H - .08f, W / 2 - ci - .08f));
  b.box(vec3(c0 + .2f, belt - .1f, -W / 2 + ci + .08f), vec3(c1 - .25f, belt + .02f, W / 2 - ci - .08f), Col(DARK));
  for (float sx : {mid + .25f, c0 + .35f}) { b.box(vec3(sx, belt, -W / 2 + .25f), vec3(sx + .45f, belt + .15f, W / 2 - .25f), Col(SEAT)); b.box(vec3(sx - .06f, belt, -W / 2 + .25f), vec3(sx + .04f, belt + .5f, W / 2 - .25f), Col(SEAT)); }
  // the face: grille, lamps, bumpers; tail lamps; the Zaporozhets' air intakes
  if (style != 3) b.box(vec3(L / 2 - .03f, .5f, -W * .3f), vec3(L / 2 + .01f, .72f, W * .3f), Col(style == 2 ? CHROME : DARK));
  for (float s : {-1.f, 1.f}) {
    if (b.look != PL_BURNT) {
      b.cylX(.62f, s * (W / 2 - .25f), .085f, L / 2 - .03f, L / 2 + .02f, Col(LAMP));
      if (style == 0) b.cylX(.62f, s * (W / 2 - .45f), .075f, L / 2 - .03f, L / 2 + .02f, Col(LAMP));
      b.box(vec3(-L / 2 - .01f, .55f, s * (W / 2 - .38f) - .12f), vec3(-L / 2 + .03f, .7f, s * (W / 2 - .38f) + .12f), Col(TAIL));
    }
    if (style == 3) b.box(vec3(-L / 2 + .45f, .62f, s * W / 2 - (s > 0 ? .02f : -.02f) - .1f), vec3(-L / 2 + .9f, .95f, s * W / 2 + (s > 0 ? .08f : -.08f)), P);
  }
  if (b.look == PL_OK) for (float x : {L / 2, -L / 2}) b.box(vec3(x - .08f, .32f, -W / 2 + .02f), vec3(x + .06f, .42f, W / 2 - .02f), Col(CHROME));
}

void uaz469(PB& b, int tint, int var) {
  const float L = 4.0f, W = 1.8f;
  const Col P = b.paint(pick(tint, {OLIVE, 0x56603e, 0x4a5440}));
  b.box(vec3(-L / 2, .45f, -W / 2), vec3(L / 2, 1.18f, W / 2), P);
  for (float x : {-1.2f, 1.2f}) { b.cylZ(x, .4f, .48f, -W / 2 - .01f, -W / 2 + .35f, Col(-1)); b.cylZ(x, .4f, .48f, W / 2 - .35f, W / 2 + .01f, Col(-1)); b.wheels(x, .4f, W - .04f, .26f); }
  b.cut(vec3(-1.85f, .75f, -W / 2 + .08f), vec3(.75f, 1.2f, W / 2 - .08f));
  b.box(vec3(-1.85f, .7f, -W / 2 + .08f), vec3(.75f, .78f, W / 2 - .08f), Col(DARK));
  b.box(vec3(.75f, 1.18f, -W / 2 + .05f), vec3(.85f, 1.75f, W / 2 - .05f), b.glass());     // the windscreen in its frame
  b.box(vec3(.75f, 1.68f, -W / 2 + .05f), vec3(.85f, 1.78f, W / 2 - .05f), P);
  for (float s : {-1.f, 1.f}) b.box(vec3(.75f, 1.18f, s * (W / 2 - .1f) - .05f), vec3(.85f, 1.78f, s * (W / 2 - .1f) + .05f), P);
  if (var % 2 == 0 && b.look != PL_BURNT) {   // the canvas top
    b.box(vec3(-1.95f, 1.18f, -W / 2 + .05f), vec3(.75f, 1.95f, W / 2 - .05f), b.paint(CANVAS, .05f));
    b.cut(vec3(-1.85f, 1.18f, -W / 2 + .14f), vec3(.7f, 1.87f, W / 2 - .14f));
    b.box(vec3(-1.4f, 1.4f, -W / 2 + .04f), vec3(-.3f, 1.75f, W / 2 - .04f), b.glass());
  }
  b.box(vec3(1.0f, .55f, -W * .32f), vec3(L / 2 + .01f, 1.05f, W * .32f), b.paint(0x3a4030));   // the slotted grille
  for (float z = -W * .3f; z < W * .3f; z += .14f) b.box(vec3(L / 2 - .02f, .6f, z), vec3(L / 2 + .02f, 1.0f, z + .05f), Col(DARK));
  for (float s : {-1.f, 1.f}) if (b.look != PL_BURNT) b.cylX(.9f, s * (W / 2 - .22f), .1f, L / 2 - .02f, L / 2 + .03f, Col(LAMP));
  if (b.look == PL_OK) b.cylX(1.0f, .2f, .36f, -L / 2 - .22f, -L / 2 - .02f, Col(TYRE));   // the spare on the tailgate
}

// the loaf van (UAZ-452) and the RAF ambulance
void loafVan(PB& b, float L, float W, float H, int tint, bool raf, int var) {
  const bool amb = raf || var == 1;
  const int base = amb ? 0xb8b6aa : pick(tint, {OLIVE, 0x6a7058, 0x8a8a6a, 0x4a5a6a});
  const Col P = b.paint(base);
  const float wx = L / 2 - .95f;
  b.shape(vec3(-L / 2, .38f, -W / 2), vec3(L / 2, H, W / 2), [&](vec3 m) {
    const float dz = std::max(0.f, std::abs(m.z) - (W / 2 - .35f)), dy = std::max(0.f, m.y - (H - .35f)), dx = std::max(0.f, m.x - (L / 2 - .45f));
    return dz * dz + dy * dy <= .35f * .35f && dx * dx + std::max(0.f, m.y - 1.2f) * std::max(0.f, m.y - 1.2f) * .15f + dy * dy <= .45f * .45f; }, P);
  for (float x : {-wx, wx}) { b.cylZ(x, .38f, .45f, -W / 2 - .01f, -W / 2 + .3f, Col(-1)); b.cylZ(x, .38f, .45f, W / 2 - .3f, W / 2 + .01f, Col(-1)); b.wheels(x, .38f, W - .04f, .24f); }
  b.cut(vec3(-L / 2 + .1f, .6f, -W / 2 + .1f), vec3(L / 2 - .5f, H - .1f, W / 2 - .1f));
  b.box(vec3(-L / 2 + .1f, .55f, -W / 2 + .1f), vec3(L / 2 - .5f, .65f, W / 2 - .1f), Col(DARK));
  // the windows: the windscreen, a row along both sides
  b.shape(vec3(L / 2 - .6f, 1.3f, -W / 2 + .12f), vec3(L / 2 + .02f, H - .2f, W / 2 - .12f), [&](vec3 m) { return true; }, b.glass());
  b.cut(vec3(L / 2 - .8f, 1.3f, -W / 2 + .12f), vec3(L / 2 - .5f, H - .2f, W / 2 - .12f));
  for (float x = -L / 2 + .5f; x < L / 2 - 1.0f; x += .85f) for (float s : {-1.f, 1.f})
    b.box(vec3(x, 1.35f, s * W / 2 - (s > 0 ? .06f : -.06f) - .03f), vec3(x + .65f, 1.8f, s * W / 2 + (s > 0 ? .0f : -.0f) + .03f), b.glass());
  if (amb) {
    b.box(vec3(-L / 2 + .3f, .95f, -W / 2 - .01f), vec3(L / 2 - .1f, 1.08f, W / 2 + .01f), b.paint(0x9a2a24, .1f));
    for (float s : {-1.f, 1.f}) { b.box(vec3(-.7f, 1.3f, s * W / 2 - .02f), vec3(-.25f, 1.42f, s * W / 2 + .02f), Col(0x9a2a24)); b.box(vec3(-.53f, 1.15f, s * W / 2 - .02f), vec3(-.42f, 1.58f, s * W / 2 + .02f), Col(0x9a2a24)); }
    if (raf) b.box(vec3(L / 2 - 1.2f, H, -.15f), vec3(L / 2 - .9f, H + .14f, .15f), Col(b.look == PL_OK ? 0x3a5aa0 : DARK));
  }
  for (float s : {-1.f, 1.f}) if (b.look != PL_BURNT) b.cylX(.85f, s * (W / 2 - .3f), .1f, L / 2 - .05f, L / 2 + .03f, Col(LAMP));
}

// ---------------------------------------------------------------- lorries
// load: 0 drop-side bed, 1 canvas tilt, 2 box van, 3 cistern, 4 dump, 5 fire body, 6 logs, 7 crane
void truck(PB& b, float L, float W, float H, float cabL, float bonL, float cabH, float wr, std::initializer_list<float> axles, int load, int cabCol, int loadCol) {
  const Col P = b.paint(cabCol), D = b.paint(0x2e2c2a, .3f);
  // chassis rails, wheels
  for (float s : {-1.f, 1.f}) b.box(vec3(-L / 2 + .15f, .55f, s * .5f - .08f), vec3(L / 2 - .25f, .8f, s * .5f + .08f), D);
  for (float x : axles) b.wheels(x, wr, W - .06f, .32f);
  // bumper, the bonnet between its wings, the cab
  const float fx = L / 2, cx0 = L / 2 - cabL, cx1 = L / 2 - bonL;
  b.box(vec3(fx - .15f, .5f, -W / 2 + .1f), vec3(fx, .78f, W / 2 - .1f), D);
  b.box(vec3(cx1, .8f, -W / 2 + .45f), vec3(fx - .1f, 1.55f, W / 2 - .45f), P);
  for (float s : {-1.f, 1.f}) {
    b.box(vec3(cx1 - .2f, .85f, s > 0 ? W / 2 - .55f : -W / 2 + .05f), vec3(fx - .1f, 1.2f, s > 0 ? W / 2 - .05f : -W / 2 + .55f), P);
    for (float x : axles) if (x > cx0) b.cylZ(x, wr, wr + .08f, s > 0 ? W / 2 - .56f : -W / 2 - .01f, s > 0 ? W / 2 + .01f : -W / 2 + .56f, Col(-1));
    if (b.look != PL_BURNT) b.cylX(1.1f, s * (W / 2 - .3f), .12f, fx - .14f, fx - .05f, Col(LAMP));
  }
  for (float x : axles) if (x > cx0) b.wheels(x, wr, W - .06f, .32f);
  b.box(vec3(fx - .14f, .85f, -W / 2 + .5f), vec3(fx - .08f, 1.5f, W / 2 - .5f), Col(DARK));
  for (float y = .9f; y < 1.48f; y += .12f) b.box(vec3(fx - .12f, y, -W / 2 + .5f), vec3(fx - .06f, y + .05f, W / 2 - .5f), b.paint(CHROME, .4f));
  b.box(vec3(cx0, .8f, -W / 2 + .12f), vec3(cx1, cabH, W / 2 - .12f), P);
  b.cut(vec3(cx0 + .08f, 1.0f, -W / 2 + .2f), vec3(cx1 - .08f, cabH - .08f, W / 2 - .2f));
  b.box(vec3(cx0 + .08f, .95f, -W / 2 + .2f), vec3(cx1 - .08f, 1.02f, W / 2 - .2f), Col(DARK));
  b.box(vec3(cx0 + .1f, 1.0f, -W / 2 + .25f), vec3(cx0 + .5f, 1.45f, W / 2 - .25f), Col(SEAT));
  b.box(vec3(cx1 - .08f, cabH - .72f, -W / 2 + .2f), vec3(cx1, cabH - .12f, W / 2 - .2f), b.glass());              // windscreen
  b.box(vec3(cx1 - .08f, cabH - .72f, -.04f), vec3(cx1, cabH - .12f, .04f), P);
  for (float s : {-1.f, 1.f}) b.box(vec3(cx1 - (cx1 - cx0) * .55f, cabH - .7f, s * (W / 2 - .12f) - .04f), vec3(cx1 - .12f, cabH - .15f, s * (W / 2 - .12f) + .04f), b.glass());
  // the load behind the cab
  const float l0 = -L / 2, l1 = cx0 - .12f, fl = .95f;
  const Col LC = b.paint(loadCol, .25f);
  switch (load) {
    case 0: case 1: {
      b.box(vec3(l0, fl - .1f, -W / 2), vec3(l1, fl, W / 2), b.wood());
      const float st = load == 0 ? 1.55f : 1.6f;
      for (float s : {-1.f, 1.f}) b.box(vec3(l0, fl, s > 0 ? W / 2 - .07f : -W / 2), vec3(l1, st, s > 0 ? W / 2 : -W / 2 + .07f), b.wood());
      b.box(vec3(l1 - .07f, fl, -W / 2), vec3(l1, st + .1f, W / 2), b.wood());
      b.box(vec3(l0, fl, -W / 2), vec3(l0 + .07f, st, W / 2), b.wood());
      for (float x = l0 + .6f; x < l1; x += 1.1f) for (float s : {-1.f, 1.f}) b.box(vec3(x, fl, s * W / 2 - .04f), vec3(x + .08f, st, s * W / 2 + .04f), LC);
      if (load == 1 && b.look != PL_BURNT) {   // the tilt over its hoops, torn here and there
        const float r = W / 2 - .02f, cy = H - r * .55f;
        const u32 sd = b.seed;
        b.shape(vec3(l0, st - .1f, -W / 2), vec3(l1, H, W / 2), [&](vec3 m) { const float dy = std::max(0.f, m.y - cy) / (r * .55f), dz = m.z / r; return dy * dy + dz * dz <= 1.f; },
                Col(ZCF { const double t = hash3(x / 3, y / 3, z / 3, (int)sd + 21); return t < (b.look == PL_STRIPPED ? .3 : .06) ? -1 : shadeC(CANVAS, .85 + .2 * hash3(x / 4, 0, 0, (int)sd)); }));
        b.shape(vec3(l0 + .05f, st - .1f, -W / 2 + .06f), vec3(l1 - .05f, H - .06f, W / 2 - .06f), [&](vec3 m) { const float dy = std::max(0.f, m.y - cy) / (r * .55f - .06f), dz = m.z / (r - .06f); return dy * dy + dz * dz <= 1.f; }, Col(-1));
      }
      break;
    }
    case 2: b.box(vec3(l0, fl - .1f, -W / 2), vec3(l1, H, W / 2), LC); b.cut(vec3(l0 + .08f, fl, -W / 2 + .08f), vec3(l1 - .08f, H - .08f, W / 2 - .08f));
            b.box(vec3(l0 - .01f, fl + .1f, -.03f), vec3(l0 + .02f, H - .15f, .03f), Col(DARK)); break;
    case 3: case 5: {
      if (load == 3) {
        b.box(vec3(l0 + .3f, .8f, -.7f), vec3(l1, 1.0f, .7f), D);
        b.shape(vec3(l0, .95f, -W / 2), vec3(l1, H - .15f, W / 2), [&](vec3 m) { const float a = (m.y - (fl + H - .15f) * .5f) / ((H - .15f - fl) * .5f), c = m.z / (W / 2 - .05f); return a * a + c * c <= 1.f; }, LC);
        b.cylY((l0 + l1) * .5f, 0, .35f, H - .3f, H, LC);
        b.box(vec3(l0, (fl + H) * .5f - .12f, -W / 2 - .02f), vec3(l1, (fl + H) * .5f + .1f, W / 2 + .02f), b.paint(0x8a2a22, .2f));
      } else {
        b.box(vec3(l0, fl - .1f, -W / 2), vec3(l1, H - .3f, W / 2), LC);
        for (float x = l0 + .2f; x < l1 - .3f; x += 1.0f) for (float s : {-1.f, 1.f}) b.box(vec3(x, fl + .2f, s * W / 2 - .02f), vec3(x + .8f, H - .55f, s * W / 2 + .02f), b.paint(0x8a8a84, .3f));
        b.box(vec3(l0, H - .3f, -W / 2 + .05f), vec3(l1, H - .25f, W / 2 - .05f), b.paint(0xa4a49a, .3f));
        for (float s : {-1.f, 1.f}) b.box(vec3(l0 - .4f, H - .25f, s * .45f - .04f), vec3(cx1, H - .15f, s * .45f + .04f), b.paint(0x9a9a92, .4f));   // the ladder on the roof
        for (float x = l0 - .3f; x < cx1; x += .35f) b.box(vec3(x, H - .25f, -.45f), vec3(x + .05f, H - .17f, .45f), b.paint(0x9a9a92, .4f));
      }
      break;
    }
    case 4: b.box(vec3(l0, fl - .1f, -W / 2), vec3(l1, 2.0f, W / 2), LC); b.cut(vec3(l0 + .1f, fl, -W / 2 + .1f), vec3(l1 - .1f, 2.01f, W / 2 - .1f)); break;
    case 6: for (float s : {-.6f, 0.f, .6f}) b.cylX(1.35f + std::abs(s) * .0f, s, .28f, l0 - .8f, l1 - .2f, b.paint(0x5a4632, .0f));
            for (float x : {l0 + .5f, l1 - .7f}) for (float s : {-1.f, 1.f}) b.box(vec3(x, fl, s * (W / 2 - .05f) - .05f), vec3(x + .1f, 1.9f, s * (W / 2 - .05f) + .05f), D);
            b.cylX(1.85f, -.3f, .26f, l0 - .5f, l1 - .4f, b.paint(0x5e4a34, .0f)); b.cylX(1.85f, .3f, .26f, l0 - .7f, l1 - .3f, b.paint(0x56422e, .0f)); break;
    case 7: {   // the truck crane: outriggers, the turntable and the boom (var: stowed along the truck, or raised)
      b.box(vec3(l0, fl - .1f, -W / 2 + .1f), vec3(l1, fl + .1f, W / 2 - .1f), D);
      b.cylY(l0 + 1.4f, 0, .9f, fl + .1f, 1.4f, D);
      b.box(vec3(l0 + .4f, 1.4f, -1.0f), vec3(l0 + 2.6f, 2.5f, .2f), b.paint(0xb08a3a));
      b.box(vec3(l0 + 1.6f, 1.6f, .2f), vec3(l0 + 2.6f, 2.6f, 1.15f), b.paint(0xb08a3a));
      b.box(vec3(l0 + 2.55f, 1.85f, .3f), vec3(l0 + 2.62f, 2.45f, 1.05f), b.glass());
      break;
    }
    default: break;
  }
}

// the truck crane's boom (rides with the truck: raised or along the cab)
void craneBoom(PB& b, float L, bool raised) {
  const Col Y = b.paint(0xb08a3a, .3f);
  const vec3 a(-L / 2 + 1.4f, 2.2f, -.3f), e = raised ? vec3(L / 2 + 2.5f, 11.5f, -.3f) : vec3(L / 2 + .6f, 2.5f, -.3f);
  b.rod(a, e, .28f, Y); b.rod(a + vec3(0, .1f, 0), a + (e - a) * .45f, .36f, Y);
  b.rod(e, e - vec3(0, raised ? 4.5f : 1.0f, 0), .03f, Col(DARK));
  b.box(e - vec3(.15f, raised ? 4.9f : 1.3f, .15f), e - vec3(-.15f, raised ? 4.5f : 1.0f, -.15f), b.paint(0xb08a3a, .4f));
}

// ---------------------------------------------------------------- buses
void bus(PB& b, float L, float W, float H, int kind, int tint) {
  // kind 0 LAZ-695, 1 PAZ-672, 2 Ikarus-280 (articulated), 3 ZiU-9 trolleybus
  const int lower = kind == 2 ? pick(tint, {0xc08a3a, 0xb07a34}) : kind == 3 ? pick(tint, {0x4a6a8a, 0x8a3a2a}) : pick(tint, {0xb8ac88, 0xb0904a, 0x8a3a2a, 0x6a8a6a, 0x4a6a8a});
  const int upper = kind == 2 ? 0xc89040 : 0xc4bca4;
  const Col P = b.paint(lower), U = b.paint(upper, .15f);
  const float floorY = .65f, win0 = 1.3f, win1 = H - .55f, wr = .5f;
  b.rounded(vec3(-L / 2, .35f, -W / 2), vec3(L / 2, H, W / 2), .38f, P);
  b.rounded(vec3(-L / 2, win1, -W / 2), vec3(L / 2, H, W / 2), .38f, U);
  b.cut(vec3(-L / 2 + .1f, floorY, -W / 2 + .08f), vec3(L / 2 - .1f, H - .1f, W / 2 - .08f));
  b.box(vec3(-L / 2 + .1f, floorY - .08f, -W / 2 + .08f), vec3(L / 2 - .1f, floorY, W / 2 - .08f), Col(0x3a3632));
  // the window band with its pillars, the windscreen, the doors on the right
  for (float s : {-1.f, 1.f}) {
    const float z0 = s > 0 ? W / 2 - .08f : -W / 2, z1 = s > 0 ? W / 2 : -W / 2 + .08f;
    b.box(vec3(-L / 2 + .5f, win0, z0), vec3(L / 2 - .5f, win1, z1), b.glass());
    for (float x = -L / 2 + .5f; x < L / 2 - .4f; x += 1.12f) b.box(vec3(x, win0, z0), vec3(x + .1f, win1, z1), U);
    b.box(vec3(-L / 2 + .2f, win0 - .14f, z0 - .01f), vec3(L / 2 - .2f, win0 - .04f, z1 + .01f), b.paint(kind == 2 ? 0x2a2a28 : 0x8a2a24, .15f));
  }
  b.box(vec3(L / 2 - .1f, 1.0f, -W / 2 + .1f), vec3(L / 2, H - .45f, W / 2 - .1f), b.glass());
  b.box(vec3(L / 2 - .1f, 1.0f, -.04f), vec3(L / 2, H - .45f, .04f), U);
  b.box(vec3(-L / 2, 1.6f, -W / 2 + .3f), vec3(-L / 2 + .1f, H - .6f, W / 2 - .3f), b.glass());
  std::vector<float> doors = {L / 2 - 1.5f};
  if (kind == 0) doors.push_back(-.6f);
  if (kind == 2 || kind == 3) { doors.push_back(.2f); doors.push_back(-L / 2 + 2.0f); }
  for (float d : doors) {
    if (b.look == PL_OK) b.box(vec3(d, .45f, W / 2 - .07f), vec3(d + 1.0f, H - .6f, W / 2 + .01f), b.glass());
    else b.cut(vec3(d, .45f, W / 2 - .1f), vec3(d + 1.0f, H - .6f, W / 2 + .02f));
    b.box(vec3(d + .48f, .45f, W / 2 - .07f), vec3(d + .52f, H - .6f, W / 2 + .01f), U);
  }
  // seats in two rows
  for (float x = -L / 2 + .7f; x < L / 2 - 2.2f; x += .85f) for (float s : {-1.f, 1.f}) {
    b.box(vec3(x, floorY, s * (W / 2 - .55f) - .4f), vec3(x + .45f, floorY + .45f, s * (W / 2 - .55f) + .4f), Col(SEAT));
    b.box(vec3(x - .05f, floorY + .45f, s * (W / 2 - .55f) - .4f), vec3(x + .03f, floorY + .9f, s * (W / 2 - .55f) + .4f), Col(SEAT));
  }
  // the axles in their arches
  std::vector<float> ax = {L / 2 - 2.0f, -L / 2 + 2.4f};
  if (kind == 2) { ax = {L / 2 - 2.2f, 1.6f, -L / 2 + 2.0f}; }
  for (float x : ax) { b.cylZ(x, wr, wr + .08f, -W / 2 - .01f, -W / 2 + .4f, Col(-1)); b.cylZ(x, wr, wr + .08f, W / 2 - .4f, W / 2 + .01f, Col(-1)); b.wheels(x, wr, W - .1f, .3f); }
  // the face, the roof scoop (LAZ), the bellows (Ikarus), the poles (trolleybus)
  for (float s : {-1.f, 1.f}) if (b.look != PL_BURNT) b.cylX(.75f, s * (W / 2 - .35f), .12f, L / 2 - .03f, L / 2 + .03f, Col(LAMP));
  b.box(vec3(L / 2 - .03f, .4f, -W / 2 + .1f), vec3(L / 2 + .05f, .6f, W / 2 - .1f), b.paint(CHROME, .5f));
  if (kind == 0) b.rounded(vec3(-L / 2 + .3f, H - .1f, -.7f), vec3(-L / 2 + 2.8f, H + .28f, .7f), .3f, U);
  if (kind == 2) { b.cut(vec3(-.6f, .3f, -W / 2 - .1f), vec3(.6f, H + .1f, W / 2 + .1f)); b.box(vec3(-.6f, .4f, -W / 2 + .1f), vec3(.6f, H - .1f, W / 2 - .1f), Col(ZCF { return (x / 2) % 2 ? 0x1e1e1c : 0x2e2c2a; })); }
  if (kind == 3) for (float s : {-.3f, .3f}) { b.rod(vec3(-1.5f, H + .1f, s), vec3(-L / 2 - 2.5f, H + (b.look == PL_OK ? 1.6f : .4f), s * 1.6f), .05f, Col(DARK)); b.box(vec3(-1.8f, H, s - .1f), vec3(-1.2f, H + .2f, s + .1f), Col(DARK)); }
}

// ---------------------------------------------------------------- armour
void btr(PB& b, float L, float W, float H, int kind, int tint) {
  // kind 0 BTR-70, 1 BTR-80, 2 BRDM-2
  const Col P = b.paint(pick(tint, {0x4e5a3c, 0x55603e, 0x5a5a42}), .2f);
  const float hullB = .5f, belt = 1.25f, top = H - (kind == 2 ? .45f : .4f);
  b.shape(vec3(-L / 2, hullB, -W / 2), vec3(L / 2, top, W / 2), [&](vec3 m) {
    const float hw = m.y < 1.0f ? W / 2 - .6f : m.y < belt ? W / 2 - .05f : W / 2 - .05f - (m.y - belt) * .55f;
    const float xf = L / 2 - std::abs(m.y - 1.05f) * 1.25f, xr = -L / 2 + std::max(0.f, m.y - 1.45f) * .7f;
    return std::abs(m.z) <= hw && m.x <= xf && m.x >= xr; }, P);
  std::vector<float> ax = kind == 2 ? std::vector<float>{-1.4f, 1.4f} : std::vector<float>{-2.55f, -1.3f, .6f, 1.85f};
  for (float x : ax) b.wheels(x, .55f, W - .1f, .38f);
  // the turret and its guns, hatches, vision blocks, the exhaust
  const float tx = kind == 2 ? .3f : .9f, th = kind == 1 ? .55f : .42f;
  b.cylY(tx, 0, .58f, top - .05f, top + th - .1f, P); b.cylY(tx, 0, .42f, top + th - .1f, top + th, P);
  b.rod(vec3(tx + .3f, top + th * .55f, 0), vec3(tx + (kind == 2 ? 1.6f : 2.1f), top + th * .55f, 0), .07f, b.paint(0x3a3a34, .3f));
  b.rod(vec3(tx + .3f, top + th * .55f, .18f), vec3(tx + 1.0f, top + th * .55f, .18f), .04f, Col(DARK));
  for (float x : {L / 2 - 1.5f, -.6f, -1.8f}) for (float s : {-1.f, 1.f}) if (x < tx - .7f || x > tx + .7f) b.box(vec3(x, top - .02f, s * .5f - .3f), vec3(x + .6f, top + .06f, s * .5f + .3f), b.paint(0x434c34, .3f));
  for (float s : {-1.f, 1.f}) b.box(vec3(L / 2 - 1.25f, belt + .1f, s * (W / 2 - .1f) - .03f), vec3(L / 2 - .95f, belt + .3f, s * (W / 2 - .1f) + .03f), Col(DARK));
  if (kind == 1) for (float s : {-1.f, 1.f}) b.box(vec3(-.55f, .9f, s * (W / 2 - .05f) - .04f), vec3(.15f, 1.6f, s * (W / 2 - .05f) + .04f), b.paint(0x3e4830, .3f));
  if (b.look == PL_OK) for (float s : {-1.f, 1.f}) b.box(vec3(-L / 2 + 1.0f, belt + .35f, s * (W / 2 - .3f) - .1f), vec3(-L / 2 + 1.3f, belt + .45f, s * (W / 2 - .3f) + .1f), Col(0xb8b4a0));
}

// a track run on each side: road wheels on the outer face, the sprocket at the front
void tracks(PB& b, float x0, float x1, float W, float tw, float h, int nWheels) {
  const Col T = b.paint(0x34322e, .35f);
  for (float s : {-1.f, 1.f}) {
    const float z0 = s > 0 ? W / 2 - tw : -W / 2, z1 = s > 0 ? W / 2 : -W / 2 + tw;
    b.shape(vec3(x0, 0, z0), vec3(x1, h, z1), [&](vec3 m) {
      const float r = h * .5f, cx = std::clamp(m.x, x0 + r, x1 - r), dx = m.x - cx, dy = m.y - r;
      return dx * dx + dy * dy <= r * r || (m.x > x0 + r && m.x < x1 - r); }, Col(ZCF { return (x / 2) % 2 ? 0x2a2826 : 0x3a3632; }));
    const float zo = s > 0 ? W / 2 + .02f : -W / 2 - .02f;
    for (int k = 0; k < nWheels; k++) { const float x = x0 + h * .55f + (x1 - x0 - h * 1.1f) * k / std::max(1, nWheels - 1); b.cylZ(x, h * .42f, h * .32f, std::min(zo, zo - s * .05f), std::max(zo, zo - s * .05f) + .02f, T); }
  }
}

void bmp(PB& b, float L, float W, int tint) {
  const Col P = b.paint(pick(tint, {0x4e5a3c, 0x5a5e42}), .2f);
  tracks(b, -L / 2 + .2f, L / 2 - .5f, W, .45f, .85f, 6);
  b.shape(vec3(-L / 2, .5f, -W / 2 + .3f), vec3(L / 2, 1.9f, W / 2 - .3f), [&](vec3 m) { return m.x <= L / 2 - (m.y - .5f) * 1.9f; }, P);
  b.box(vec3(-L / 2 + .3f, .85f, -W / 2), vec3(L / 2 - .9f, .95f, W / 2), P);   // the track guards
  b.cylY(-.3f, 0, .75f, 1.85f, 2.2f, P); b.cylY(-.3f, 0, .55f, 2.2f, 2.42f, P);
  b.rod(vec3(.3f, 2.12f, 0), vec3(2.9f, 2.12f, 0), .06f, Col(DARK));
  b.cylY(-.4f, .55f, .12f, 2.4f, 2.62f, Col(DARK));   // the ATGM launcher stub
  for (float x : {-2.6f, -1.6f}) for (float s : {-1.f, 1.f}) b.box(vec3(x, 1.88f, s * .55f - .3f), vec3(x + .7f, 1.95f, s * .55f + .3f), b.paint(0x434c34, .3f));
}

void tank(PB& b, int tint, int var) {
  const Col P = b.paint(pick(tint, {0x4e5a3c, 0x5c5e44, 0x56603e}), .22f);
  tracks(b, -4.6f, 2.0f, 3.6f, .6f, .95f, 6);
  b.shape(vec3(-4.75f, .5f, -1.25f), vec3(2.15f, 1.5f, 1.25f), [&](vec3 m) { return m.x <= 2.15f - (m.y - .5f) * 1.4f; }, P);
  for (float s : {-1.f, 1.f}) b.box(vec3(-4.6f, .95f, s > 0 ? 1.2f : -1.8f), vec3(1.9f, 1.05f, s > 0 ? 1.8f : -1.2f), P);
  for (float s : {-1.f, 1.f}) if (b.look == PL_OK) b.box(vec3(-4.2f, .55f, s > 0 ? 1.78f : -1.82f), vec3(1.6f, .95f, s > 0 ? 1.82f : -1.78f), Col(RUBBER));
  b.cylX(1.25f, 0, .32f, -4.7f, -3.9f, b.paint(0x3e3a32, .4f));   // the fuel drums on the back
  if (var != 1) {
    b.ell(vec3(-1.4f, 1.55f, 0), vec3(1.55f, .62f, 1.18f), P);
    b.cut(vec3(-3.2f, .9f, -1.5f), vec3(.5f, 1.5f, 1.5f)); b.box(vec3(-3.0f, 1.45f, -1.2f), vec3(.2f, 1.55f, 1.2f), P);
    b.cylX(1.75f, 0, .2f, .1f, .7f, P);
    b.rod(vec3(.6f, 1.78f, 0), vec3(4.75f, var == 2 ? 1.25f : 1.85f, 0), .1f, b.paint(0x3e463a, .3f));
    b.cylY(-1.8f, .5f, .22f, 2.1f, 2.25f, P);
  } else {   // the turret blown off: a hole in the roof, black inside
    b.cylY(-1.4f, 0, .9f, 1.3f, 1.55f, Col(0x1c1a18));
  }
}

// ---------------------------------------------------------------- the Mi-8
void mi8(PB& b, int tint, int var) {
  const int top = pick(tint, {0x55603e, 0x5a6046, 0x6a6e5a}), belly = 0x6c767a;
  const Col P = b.paint(top, .2f), Pb = b.paint(belly, .2f);
  const Col body = Col(ZCF { (void)x; (void)z; return y < 13 ? 0 : 0; });
  (void)body;
  auto fus = [&](vec3 m) { const float a = (m.y - 1.9f) / 1.4f, c = m.z / 1.25f; return std::pow(std::abs(a), 2.6f) + std::pow(std::abs(c), 2.6f) <= 1.f; };
  b.shape(vec3(-5.6f, .5f, -1.26f), vec3(5.0f, 3.3f, 1.26f), fus, P);
  b.shape(vec3(-5.6f, .5f, -1.26f), vec3(5.0f, 1.35f, 1.26f), fus, Pb);
  b.ell(vec3(5.0f, 1.85f, 0), vec3(1.6f, 1.25f, 1.2f), P);
  b.shape(vec3(5.0f, 1.9f, -1.2f), vec3(6.6f, 3.1f, 1.2f), [&](vec3 m) { const float a = (m.x - 5.0f) / 1.6f, bb = (m.y - 1.85f) / 1.25f, c = m.z / 1.2f; return a * a + bb * bb + c * c <= 1.f && a * a + bb * bb + c * c >= .72f; }, b.glass());
  b.cut(vec3(-5.4f, .75f, -1.0f), vec3(5.4f, 2.9f, 1.0f));
  b.box(vec3(-5.4f, .7f, -1.0f), vec3(5.4f, .8f, 1.0f), Col(0x3a3832));
  for (float x = -3.6f; x < 2.0f; x += .75f) for (float s : {-1.f, 1.f}) b.cylZ(x, 2.05f, .17f, s > 0 ? 1.0f : -1.3f, s > 0 ? 1.3f : -1.0f, b.glass());
  b.box(vec3(-5.65f, .6f, -1.0f), vec3(-5.5f, 2.9f, 1.0f), b.paint(top, .3f));   // the clamshell doors
  // the engines' hump, the exhausts, the rotor head
  b.rounded(vec3(-1.6f, 3.0f, -.95f), vec3(3.6f, 3.9f, .95f), .45f, P);
  for (float s : {-1.f, 1.f}) b.cylX(3.4f, s * .75f, .2f, -2.4f, -1.5f, Col(0x2a2622));
  b.cylY(.6f, 0, .28f, 3.9f, 4.45f, b.paint(STEEL, .3f));
  // the tail boom, fin, stabilisers, tail rotor
  b.shape(vec3(-11.2f, 2.0f, -.6f), vec3(-5.5f, 3.2f, .6f), [&](vec3 m) { const float t = (m.x + 5.5f) / -5.7f, r = .58f - .32f * t, cy = 2.5f + .15f * t; return (m.y - cy) * (m.y - cy) + m.z * m.z <= r * r; }, P);
  b.shape(vec3(-11.8f, 2.4f, -.1f), vec3(-10.4f, 4.7f, .1f), [&](vec3 m) { return m.x <= -10.4f - (m.y - 2.4f) * .45f && m.x >= -11.8f + std::max(0.f, m.y - 3.4f) * .3f; }, P);
  for (float s : {-1.f, 1.f}) b.box(vec3(-9.9f, 2.45f, s > 0 ? .3f : -1.3f), vec3(-9.3f, 2.55f, s > 0 ? 1.3f : -.3f), P);
  if (var != 1) for (int k = 0; k < 3; k++) { const float a = k * 2.094f + .4f; b.rod(vec3(-11.0f, 4.0f, -.25f), vec3(-11.0f + std::cos(a) * 1.75f, 4.0f + std::sin(a) * 1.75f, -.25f), .07f, Col(DARK)); }
  // the gear
  b.cylZ(4.2f, .3f, .3f, -.45f, -.2f, Col(TYRE)); b.cylZ(4.2f, .3f, .3f, .2f, .45f, Col(TYRE)); b.rod(vec3(4.2f, .3f, 0), vec3(4.0f, 1.0f, 0), .07f, Col(STEEL));
  for (float s : {-1.f, 1.f}) { b.cylZ(-1.0f, .4f, .4f, s * 1.75f - .15f, s * 1.75f + .15f, Col(b.look == PL_OK ? TYRE : 0x3a2a22)); b.rod(vec3(-1.0f, .45f, s * 1.75f), vec3(-.6f, 1.2f, s * 1.1f), .07f, Col(STEEL)); b.rod(vec3(-1.0f, .45f, s * 1.75f), vec3(-1.6f, 1.1f, s * 1.1f), .07f, Col(STEEL)); }
  // five drooping blades (none left on var 1; stubs on the burnt)
  if (var != 1) for (int k = 0; k < 5; k++) {
    const float a = k * 1.2566f + .3f + (float)hash3(k, 1, 2, (int)b.seed) * .15f, R = b.look == PL_BURNT ? 2.5f : 10.4f;
    const vec3 hub(.6f, 4.4f, 0), tip = hub + vec3(std::cos(a) * R, -.75f * R / 10.4f, std::sin(a) * R);
    b.rod(hub, tip, .11f, b.paint(0x3a3c34, .2f));
    b.rod(hub + vec3(0, 0, 0), hub + (tip - hub) * .97f + vec3(-std::sin(a) * .18f, 0, std::cos(a) * .18f), .07f, b.paint(0x3a3c34, .2f));
  }
}

// ---------------------------------------------------------------- tractors, dozer, excavator
void tractor(PB& b, bool cab, int tint) {
  const Col R = b.paint(pick(tint, {0x8a3a26, 0x9a4a2a, 0x3a5a7a}), .3f), Wc = b.paint(0xb0aca0, .25f);
  const float L = cab ? 3.8f : 3.1f, W = cab ? 1.9f : 1.6f;
  const float rx = -L / 2 + .9f, fx = L / 2 - .7f;
  for (float s : {-1.f, 1.f}) {
    const float zc = s * (W / 2 - .22f);
    if (b.look == PL_OK) { b.cylZ(rx, .75f, .75f, zc - .2f, zc + .2f, Col(TYRE)); for (int k = 0; k < 16; k++) { const float a = k * .3927f; b.box(vec3(rx + std::cos(a) * .72f - .05f, .75f + std::sin(a) * .72f - .05f, zc - .2f), vec3(rx + std::cos(a) * .72f + .05f, .75f + std::sin(a) * .72f + .05f, zc + .2f), Col(0x2e2c2a)); } }
    b.cylZ(rx, .75f, .42f, zc - .22f, zc + .22f, b.paint(0xa08a3a, .5f));
    b.cylZ(fx, .45f, .45f, s * (W / 2 - .35f) - .13f, s * (W / 2 - .35f) + .13f, Col(b.look == PL_OK ? TYRE : 0x3a2a22));
    b.box(vec3(rx - .8f, 1.45f, zc - .25f), vec3(rx + .7f, 1.55f, zc + .25f), R);   // the mudguards
  }
  b.box(vec3(-L / 2 + .3f, .6f, -.38f), vec3(L / 2, 1.45f, .38f), R);
  b.box(vec3(L / 2 - .05f, .7f, -.34f), vec3(L / 2 + .02f, 1.35f, .34f), Col(DARK));
  b.cylY(L / 2 - .9f, -.2f, .06f, 1.45f, 2.3f, Col(DARK));
  if (cab) {
    b.box(vec3(-1.5f, 1.25f, -.72f), vec3(-.2f, 2.75f, .72f), b.glass());
    for (float x : {-1.5f, -.28f}) for (float s : {-1.f, 1.f}) b.box(vec3(x, 1.25f, s * .68f - .04f), vec3(x + .08f, 2.75f, s * .68f + .04f), Wc);
    b.box(vec3(-1.55f, 2.68f, -.78f), vec3(-.15f, 2.8f, .78f), Wc);
    b.cut(vec3(-1.42f, 1.35f, -.64f), vec3(-.3f, 2.66f, .64f));
    b.box(vec3(-1.2f, 1.25f, -.25f), vec3(-.8f, 1.65f, .25f), Col(SEAT));
  } else b.box(vec3(-1.2f, 1.25f, -.25f), vec3(-.8f, 1.7f, .25f), Col(SEAT));
}

void dozer(PB& b, int tint) {
  const Col Y = b.paint(pick(tint, {0xb08a3a, 0xa0782e, 0x8a7a3a}), .3f);
  tracks(b, -2.5f, 2.0f, 2.6f, .55f, .9f, 5);
  b.box(vec3(-2.2f, .8f, -.85f), vec3(1.7f, 1.85f, .85f), Y);
  for (float z = -.7f; z < .7f; z += .18f) b.box(vec3(1.69f, .95f, z), vec3(1.72f, 1.7f, z + .08f), Col(DARK));
  b.box(vec3(-2.3f, 1.8f, -.85f), vec3(-.7f, 3.05f, .85f), b.glass());
  for (float x : {-2.3f, -.78f}) for (float s : {-1.f, 1.f}) b.box(vec3(x, 1.8f, s * .8f - .05f), vec3(x + .08f, 3.05f, s * .8f + .05f), Y);
  b.box(vec3(-2.35f, 3.0f, -.9f), vec3(-.65f, 3.12f, .9f), Y); b.cut(vec3(-2.2f, 1.9f, -.75f), vec3(-.86f, 2.98f, .75f));
  b.cylY(1.0f, -.4f, .07f, 1.85f, 2.6f, Col(DARK));
  b.shape(vec3(2.25f, 0, -1.6f), vec3(2.8f, 1.25f, 1.6f), [&](vec3 m) { const float c = m.x - 2.25f - (1 - (m.y / 1.25f - .5f) * (m.y / 1.25f - .5f) * 4) * .3f; return c >= 0 && c <= .12f; }, Y);
  for (float s : {-1.f, 1.f}) b.rod(vec3(-1.0f, .5f, s * 1.35f), vec3(2.3f, .55f, s * 1.35f), .09f, Y);
}

void excavator(PB& b, int tint) {
  const Col Y = b.paint(pick(tint, {0xb08a3a, 0x9a7a34}), .3f);
  tracks(b, -2.2f, 2.2f, 3.0f, .6f, .9f, 6);
  b.cylY(0, 0, .8f, .9f, 1.15f, b.paint(0x3a3632));
  b.box(vec3(-2.4f, 1.15f, -1.4f), vec3(1.3f, 2.5f, 1.4f), Y);
  b.box(vec3(-2.6f, 1.15f, -1.4f), vec3(-2.3f, 2.2f, 1.4f), b.paint(0x4a4640, .3f));
  b.box(vec3(.2f, 1.15f, -1.4f), vec3(1.6f, 3.2f, -.4f), b.glass());
  b.box(vec3(.2f, 3.1f, -1.42f), vec3(1.62f, 3.25f, -.38f), Y); b.cut(vec3(.3f, 1.25f, -1.3f), vec3(1.5f, 3.08f, -.5f));
  const vec3 a(1.3f, 2.0f, .35f), k(3.6f, 4.2f, .35f), c(4.3f, .7f, .35f);
  b.rod(a, k, .26f, Y); b.rod(k, c, .18f, Y); b.rod(a + vec3(.3f, -.5f, 0), (a + k) * .5f, .1f, b.paint(CHROME, .5f));
  b.shape(vec3(3.8f, 0, -.25f), vec3(4.8f, 1.0f, .95f), [&](vec3 m) { const float dx = m.x - 4.3f, dy = m.y - .7f; return dx * dx + dy * dy <= .5f * .5f && m.y < .95f && (dx * dx + dy * dy >= .36f * .36f || m.y < .25f); }, b.paint(0x4a4640, .5f));
}

// ---------------------------------------------------------------- rolling stock
void bogie(PB& b, float x, int axles, float r, const Col& F) {
  const float sp = axles == 3 ? 1.85f : 1.85f;
  for (int k = 0; k < axles; k++) {
    const float ax = x + (k - (axles - 1) * .5f) * sp;
    for (float s : {-1.f, 1.f}) b.cylZ(ax, r, r, s * .76f - .07f, s * .76f + .07f, b.paint(0x3a3430, .4f));
    b.cylZ(ax, r, .08f, -.76f, .76f, Col(DARK));
  }
  for (float s : {-1.f, 1.f}) b.box(vec3(x - (axles - 1) * sp * .5f - .6f, r * .55f, s * 1.0f - .1f), vec3(x + (axles - 1) * sp * .5f + .6f, r + .3f, s * 1.0f + .1f), F);
}
void underframe(PB& b, float L, float y, const Col& F) {
  b.box(vec3(-L / 2 + .2f, y - .3f, -1.4f), vec3(L / 2 - .2f, y, 1.4f), F);
  for (float s : {-1.f, 1.f}) for (float e : {-1.f, 1.f}) {
    b.cylX(y - .3f, s * .87f, .2f, e > 0 ? L / 2 - .2f : -L / 2 - .3f, e > 0 ? L / 2 + .3f : -L / 2 + .2f, b.paint(0x3a3632, .4f));
    b.cylX(y - .3f, s * .87f, .27f, e > 0 ? L / 2 + .2f : -L / 2 - .3f, e > 0 ? L / 2 + .3f : -L / 2 - .2f, b.paint(0x4a4640, .5f));
  }
}

void wagon(PB& b, int kind, float L, float W, float H, int tint, int var) {
  // kind 0 boxcar, 1 gondola, 2 tank car, 3 hopper, 4 flatcar, 5 passenger carriage
  const Col F = b.paint(0x2e2c2a, .35f);
  const float fl = 1.3f;
  underframe(b, L, fl, F);
  bogie(b, L / 2 - 2.4f, 2, .48f, F); bogie(b, -L / 2 + 2.4f, 2, .48f, F);
  switch (kind) {
    case 0: {
      const Col Bc = b.paint(pick(tint, {0x6a3e2c, 0x5e3a2a, 0x704a32}), .3f);
      b.box(vec3(-L / 2 + .3f, fl, -W / 2), vec3(L / 2 - .3f, H - .35f, W / 2), Bc);
      b.cut(vec3(-L / 2 + .4f, fl + .1f, -W / 2 + .1f), vec3(L / 2 - .4f, H - .45f, W / 2 - .1f));
      for (float x = -L / 2 + .3f; x < L / 2 - .2f; x += 1.25f) for (float s : {-1.f, 1.f}) b.box(vec3(x, fl, s * W / 2 - .05f), vec3(x + .12f, H - .35f, s * W / 2 + .05f), b.paint(0x3e3630, .4f));
      b.rounded(vec3(-L / 2 + .25f, H - .45f, -W / 2 - .02f), vec3(L / 2 - .25f, H, W / 2 + .02f), .5f, b.paint(0x6e6a62, .45f));
      for (float s : {-1.f, 1.f}) {
        if (var == 1) b.cut(vec3(-1.0f, fl + .1f, s > 0 ? W / 2 - .12f : -W / 2 - .02f), vec3(1.0f, H - .6f, s > 0 ? W / 2 + .02f : -W / 2 + .12f));
        else b.box(vec3(-1.0f, fl + .1f, s * W / 2 - .07f), vec3(1.0f, H - .6f, s * W / 2 + .07f), b.paint(0x5a3426, .35f));
      }
      break;
    }
    case 1: {
      const Col G = b.paint(pick(tint, {0x5a3a2a, 0x4a4440, 0x5a4a3a}), .45f);
      b.box(vec3(-L / 2 + .3f, fl, -W / 2), vec3(L / 2 - .3f, H, W / 2), G);
      b.cut(vec3(-L / 2 + .42f, fl + .12f, -W / 2 + .1f), vec3(L / 2 - .42f, H + .1f, W / 2 - .1f));
      for (float x = -L / 2 + .3f; x < L / 2; x += 1.1f) for (float s : {-1.f, 1.f}) b.box(vec3(x, fl, s * W / 2 - .06f), vec3(x + .1f, H, s * W / 2 + .06f), b.paint(0x3a3430, .5f));
      if (var == 1 || var == 2) {   // a load of scrap / coal
        const u32 sd = b.seed;
        b.shape(vec3(-L / 2 + .45f, fl + .1f, -W / 2 + .12f), vec3(L / 2 - .45f, H + .3f, W / 2 - .12f), [&](vec3 m) { return m.y < H - .5f + .6f * (float)hash3((int)(m.x * 2), 0, (int)(m.z * 2), (int)sd); },
                var == 2 ? Col(ZCF { return hash3(x, y, z, 3) < .5 ? 0x1c1c1a : 0x2a2826; }) : Col(ZCF { const double h = hash3(x / 2, y / 2, z / 2, 9); return h < .3 ? 0x6e3e24 : h < .55 ? 0x5a5852 : h < .75 ? 0x8a5230 : 0x3a3632; }));
      }
      break;
    }
    case 2: {
      const Col T = b.paint(pick(tint, {0x262624, 0x5e5e5a, 0x2e2e2a}), .25f);
      for (float x : {-L / 2 + 1.2f, L / 2 - 1.2f}) b.box(vec3(x - .3f, fl, -1.0f), vec3(x + .3f, fl + .5f, 1.0f), F);
      b.cylX(fl + 1.45f, 0, 1.35f, -L / 2 + .5f, L / 2 - .5f, T);
      for (float e : {-1.f, 1.f}) b.ell(vec3(e * (L / 2 - .5f), fl + 1.45f, 0), vec3(.35f, 1.3f, 1.3f), T);
      b.cylY(0, 0, .5f, fl + 2.6f, fl + 3.1f, T);
      for (float s : {-1.f, 1.f}) b.box(vec3(-.08f, fl, s * 1.42f - .04f), vec3(.08f, fl + 2.7f, s * 1.42f + .04f), b.paint(STEEL, .4f));
      b.box(vec3(-L / 2 + 1.0f, fl + 1.35f, -1.4f), vec3(L / 2 - 1.0f, fl + 1.45f, 1.4f), b.paint(0x6a6a64, .4f));
      break;
    }
    case 3: {
      const Col Hc = b.paint(pick(tint, {0x5e5a52, 0x6a4a36, 0x4e4a44}), .45f);
      b.box(vec3(-L / 2 + .3f, fl + .6f, -W / 2), vec3(L / 2 - .3f, H, W / 2), Hc);
      b.cut(vec3(-L / 2 + .42f, fl + .7f, -W / 2 + .1f), vec3(L / 2 - .42f, H + .1f, W / 2 - .1f));
      for (float c : {-L / 4 + .3f, L / 4 - .3f}) b.shape(vec3(c - 2.0f, fl - .2f, -W / 2 + .3f), vec3(c + 2.0f, fl + .7f, W / 2 - .3f), [&](vec3 m) { return std::abs(m.x - c) <= .6f + (m.y - fl + .2f) * 1.6f; }, Hc);
      for (float x = -L / 2 + .3f; x < L / 2; x += 1.4f) for (float s : {-1.f, 1.f}) b.box(vec3(x, fl + .6f, s * W / 2 - .06f), vec3(x + .12f, H, s * W / 2 + .06f), b.paint(0x3a3430, .5f));
      break;
    }
    case 4: {
      b.box(vec3(-L / 2 + .2f, fl - .05f, -W / 2), vec3(L / 2 - .2f, fl + .12f, W / 2), b.wood());
      for (float x = -L / 2 + .8f; x < L / 2; x += 1.6f) for (float s : {-1.f, 1.f}) b.box(vec3(x, fl - .3f, s * W / 2 - .06f), vec3(x + .12f, fl + .12f, s * W / 2 + .06f), F);
      if (var == 1) { b.box(vec3(-1.2f, fl + .12f, -1.0f), vec3(1.2f, fl + 2.0f, 1.0f), b.wood()); for (float x : {-1.2f, 1.08f}) b.box(vec3(x, fl + .12f, -1.02f), vec3(x + .12f, fl + 2.02f, 1.02f), b.paint(0x4a3e30, .2f)); }
      if (var == 2) { b.box(vec3(-3.5f, fl + .12f, -.8f), vec3(-2.0f, fl + .9f, .8f), b.paint(0x4a4640, .5f)); b.box(vec3(2.2f, fl + .12f, -1.2f), vec3(5.0f, fl + .4f, 1.2f), b.paint(0x5a4a3a, .2f)); }
      break;
    }
    default: {   // the passenger carriage
      const Col Gc = b.paint(pick(tint, {0x3e5a46, 0x46604a, 0x5a3a2e}), .2f);
      b.rounded(vec3(-L / 2 + .3f, fl, -W / 2), vec3(L / 2 - .3f, H, W / 2), .6f, Gc);
      b.cut(vec3(-L / 2 + .4f, fl + .15f, -W / 2 + .1f), vec3(L / 2 - .4f, H - .15f, W / 2 - .1f));
      b.box(vec3(-L / 2 + .4f, fl + .05f, -W / 2 + .1f), vec3(L / 2 - .4f, fl + .15f, W / 2 - .1f), Col(0x3a3430));
      for (float x = -L / 2 + 2.2f; x < L / 2 - 2.6f; x += 1.0f) for (float s : {-1.f, 1.f}) b.box(vec3(x, fl + 1.0f, s * W / 2 - .06f), vec3(x + .75f, fl + 1.8f, s * W / 2 + .06f), b.glass());
      for (float s : {-1.f, 1.f}) b.box(vec3(-L / 2 + .3f, fl + .75f, s * W / 2 - .07f), vec3(L / 2 - .3f, fl + .85f, s * W / 2 + .07f), b.paint(0xb0943e, .2f));
      for (float e : {-1.f, 1.f}) for (float s : {-1.f, 1.f}) b.box(vec3(e * (L / 2 - 1.2f) - .45f, fl + .15f, s * W / 2 - .07f), vec3(e * (L / 2 - 1.2f) + .45f, H - .6f, s * W / 2 + .07f), b.paint(0x2e3e32, .3f));
      break;
    }
  }
}

// the diesel shunters: TEM2 (end cab, long hood ahead) and ChME3 (centre cab)
void loco(PB& b, bool chme3, int tint) {
  const float L = chme3 ? 17.2f : 16.9f, fl = 1.4f;
  const int main = chme3 ? pick(tint, {0x8a3428, 0x6a2e28}) : pick(tint, {0x3e5a3e, 0x46603e, 0x5a3a2c});
  const Col P = b.paint(main, .25f), S = b.paint(0xb09a48, .2f), F = b.paint(0x2e2c2a, .35f), R = b.paint(0x7a2a24, .25f);
  underframe(b, L, fl, F);
  bogie(b, L / 2 - 4.0f, 3, .53f, F); bogie(b, -L / 2 + 4.0f, 3, .53f, F);
  b.box(vec3(-L / 2 + .3f, fl - .3f, -1.55f), vec3(L / 2 - .3f, fl, 1.55f), R);                            // the running board's red edge
  b.box(vec3(-L / 2 + .3f, fl - .15f, -1.56f), vec3(L / 2 - .3f, fl - .05f, 1.56f), S);
  auto hood = [&](float x0, float x1, float h) {
    b.rounded(vec3(x0, fl, -1.1f), vec3(x1, h, 1.1f), .35f, P);
    for (float x = x0 + .3f; x < x1 - .5f; x += .9f) for (float s : {-1.f, 1.f}) b.box(vec3(x, fl + .5f, s * 1.1f - .03f), vec3(x + .6f, h - .5f, s * 1.1f + .03f), b.paint(b.look == PL_STRIPPED ? 0x1c1c1a : main, .35f));
    b.box(vec3(x0 + .2f, h - .02f, -.6f), vec3(x0 + 2.2f, h + .05f, .6f), Col(DARK));
    for (float s : {-1.f, 1.f}) b.box(vec3(x0, fl + .65f, s * 1.1f - .04f), vec3(x1, fl + .8f, s * 1.1f + .04f), S);
  };
  auto cab = [&](float x0, float x1) {
    b.box(vec3(x0, fl, -1.55f), vec3(x1, 4.35f, 1.55f), P);
    b.rounded(vec3(x0 - .1f, 4.3f, -1.6f), vec3(x1 + .1f, 4.6f, 1.6f), .3f, b.paint(0x5a5a54, .3f));
    b.cut(vec3(x0 + .1f, fl + .1f, -1.45f), vec3(x1 - .1f, 4.25f, 1.45f));
    for (float x : {x0, x1 - .1f}) b.box(vec3(x, 3.0f, -1.3f), vec3(x + .1f, 3.9f, 1.3f), b.glass());
    for (float s : {-1.f, 1.f}) { b.box(vec3(x0 + .4f, 2.9f, s * 1.55f - .06f), vec3(x1 - .4f, 3.8f, s * 1.55f + .06f), b.glass()); b.box(vec3(x0, fl + .65f, s * 1.55f - .05f), vec3(x1, fl + .8f, s * 1.55f + .05f), S); }
  };
  if (chme3) { hood(1.6f, L / 2 - .4f, 3.6f); cab(-1.2f, 1.6f); hood(-L / 2 + .4f, -1.2f, 3.3f); }
  else { hood(-L / 2 + 2.8f, L / 2 - .4f, 3.9f); cab(-L / 2 + .3f, -L / 2 + 2.8f); }
  for (float e : {-1.f, 1.f}) if (b.look != PL_BURNT) b.cylX(chme3 ? 3.2f : (e > 0 ? 3.4f : 4.0f), 0, .18f, e > 0 ? L / 2 - .45f : -L / 2 + .2f, e > 0 ? L / 2 - .3f : -L / 2 + .35f, Col(LAMP));
  for (float s : {-1.f, 1.f}) b.rod(vec3(-L / 2 + .4f, fl + 1.0f, s * 1.5f), vec3(L / 2 - .4f, fl + 1.0f, s * 1.5f), .03f, b.paint(0xb09a48, .3f));   // the handrails
}

} // namespace

bool buildPropModel(int kit, int var, int look, int tint, VoxelMeshData& out, vec3& origin, ivec3& size) {
  const auto& K = zone::propKits();
  if (kit < 0 || kit >= (int)K.size()) return false;
  const zone::PropKit& k = K[kit];
  const std::string id = k.id;
  // the model's room: the kit box plus what sticks out (rotors, booms, guns, buffers)
  vec3 lo(-k.L / 2 - .5f, -.1f, -k.W / 2 - .5f), hi(k.L / 2 + .5f, k.H + .6f, k.W / 2 + .5f);
  if (id == "mi_8") { lo = vec3(-12.2f, -.1f, -11.0f); hi = vec3(11.4f, 5.0f, 11.0f); if (var == 1 || look == PL_BURNT) { lo.z = -2.4f; hi.z = 2.4f; hi.x = 7.0f; } }
  if (id == "crane_truck" && var == 1) { hi.x = k.L / 2 + 3.2f; hi.y = 12.2f; }
  if (id == "crane_truck" && var != 1) hi.x = k.L / 2 + 1.2f;
  if (id == "trolleybus") lo.x = -k.L / 2 - 3.0f, hi.y = k.H + 1.8f;
  if (id == "excavator") { lo.x = -3.0f; hi.x = 5.0f; hi.y = 4.6f; }
  if (id == "t_72") { lo.x = -5.0f; hi.x = 5.0f; }
  PB b(lo, hi, look, (u32)(tint * 7919 + kit * 104729 + var * 31));
  if (id.rfind("car_", 0) == 0) car(b, k.L, k.W, k.H, id == "car_zhiguli" ? 0 : id == "car_moskvich" ? 1 : id == "car_volga" ? 2 : 3, tint);
  else if (id == "uaz_469") uaz469(b, tint, var);
  else if (id == "uaz_452") loafVan(b, k.L, k.W, k.H, tint, false, var);
  else if (id == "raf_2203") loafVan(b, k.L, k.W, k.H, tint, true, var);
  else if (id == "zil_130") truck(b, k.L, k.W, k.H, 2.4f, 1.25f, 2.3f, .48f, {-2.2f, 2.0f}, var == 1 ? 2 : var == 2 ? 3 : var == 3 ? 4 : 0, pick(tint, {0x3c5070, 0x4a6040, 0x8a6a3a, 0x6a6a64}), pick(tint, {0x7a7a74, 0xa4a098, 0x5a6a4a}));
  else if (id == "zil_131") truck(b, k.L, k.W, k.H, 2.5f, 1.2f, 2.5f, .55f, {-2.6f, -1.25f, 2.1f}, var == 1 ? 2 : 1, pick(tint, {OLIVE, 0x56603e}), pick(tint, {0x56603e, OLIVE}));
  else if (id == "ural_4320") truck(b, k.L, k.W, k.H, 2.9f, 1.45f, 2.6f, .58f, {-2.9f, -1.5f, 2.2f}, 1, pick(tint, {OLIVE, 0x55603e, 0x5a5e44}), OLIVE);
  else if (id == "kraz_255") truck(b, k.L, k.W, k.H, 3.4f, 1.8f, 2.9f, .66f, {-3.3f, -1.9f, 2.5f}, var == 1 ? 4 : 6, pick(tint, {0x4a5a3a, 0x3c5070, 0x6a6a5a}), 0x5a5a52);
  else if (id == "gaz_53") truck(b, k.L, k.W, k.H, 2.3f, 1.1f, 2.2f, .46f, {-2.1f, 1.9f}, var == 1 ? 4 : 0, pick(tint, {0x4a6a48, 0x3c5070, 0x6a5a3a}), 0x6a6a64);
  else if (id == "fire_zil") truck(b, k.L, k.W, k.H, 2.5f, 1.2f, 2.6f, .55f, {-2.7f, -1.35f, 2.2f}, 5, 0x8e2a22, 0x8e2a22);
  else if (id == "tanker_zil") truck(b, k.L, k.W, k.H, 2.4f, 1.25f, 2.4f, .5f, {-2.3f, 2.1f}, 3, pick(tint, {0x3c5070, 0x4a6040, 0x6a6a64}), pick(tint, {0xa4a29a, 0x8a5a2a, 0x6a6a64}));
  else if (id == "crane_truck") { truck(b, k.L, k.W, k.H, 2.6f, 1.3f, 2.5f, .55f, {-3.6f, -2.2f, 3.0f}, 7, pick(tint, {0x8a7a3a, 0x4a5a3a}), 0xb08a3a); craneBoom(b, k.L, var == 1); }
  else if (id == "bus_laz695") bus(b, k.L, k.W, k.H, 0, tint);
  else if (id == "bus_paz672") bus(b, k.L, k.W, k.H, 1, tint);
  else if (id == "bus_ikarus") bus(b, k.L, k.W, k.H, 2, tint);
  else if (id == "trolleybus") bus(b, k.L, k.W, 3.1f, 3, tint);
  else if (id == "btr_70") btr(b, k.L, k.W, k.H, 0, tint);
  else if (id == "btr_80") btr(b, k.L, k.W, k.H, 1, tint);
  else if (id == "brdm_2") btr(b, k.L, k.W, k.H, 2, tint);
  else if (id == "bmp_2") bmp(b, k.L, k.W, tint);
  else if (id == "t_72") tank(b, tint, var);
  else if (id == "mi_8") mi8(b, tint, var);
  else if (id == "tractor_mtz") tractor(b, true, tint);
  else if (id == "tractor_t40") tractor(b, false, tint);
  else if (id == "dozer_t130") dozer(b, tint);
  else if (id == "excavator") excavator(b, tint);
  else if (id == "loco_tem2") loco(b, false, tint);
  else if (id == "loco_chme3") loco(b, true, tint);
  else if (id == "boxcar") wagon(b, 0, k.L, k.W, k.H, tint, var);
  else if (id == "gondola") wagon(b, 1, k.L, k.W, k.H, tint, var);
  else if (id == "tankcar") wagon(b, 2, k.L, k.W, k.H, tint, var);
  else if (id == "hopper") wagon(b, 3, k.L, k.W, k.H, tint, var);
  else if (id == "flatcar") wagon(b, 4, k.L, k.W, k.H, tint, var);
  else if (id == "passcar") wagon(b, 5, k.L, k.W, k.H, tint, var);
  else if (id == "boat_row") {
    b.shape(vec3(-2, 0, -.7f), vec3(2, .6f, .7f), [&](vec3 m) { const float t = 1 - std::pow(std::abs(m.x) / 2.f, 3.f); return std::abs(m.z) <= .7f * t * (.4f + m.y) && (std::abs(m.z) >= .7f * t * (.4f + m.y) - .08f || m.y < .1f); }, b.wood());
  } else if (id == "bus_wreck_shell") bus(b, k.L, k.W, 2.6f, 0, tint);
  else return false;
  out.verts.clear();
  buildVoxelMesh(b.cv.g, out);
  origin = lo; size = b.cv.g.size;
  return true;
}

} // namespace dq
