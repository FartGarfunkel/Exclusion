// render/zone_fp_hands.cpp — the Zone's first-person hands (see zone_fp_hands.h): a gloved fist modelled round a grip
// (a palm and the back of the hand, a knuckle pad, four curled fingers, the thumb crossing over, a glove cuff) and a
// sleeved forearm with folds, in the colours of the suit worn. Voxels of 3.4 mm.
#include "render/zone_fp_hands.h"
#include "assets/zone_model_kit.h"
#include "assets/voxel.h"
#include <cmath>
#include <unordered_map>
#include <memory>

namespace dq {
using namespace zk;

namespace {
constexpr float V = 0.0034f;   // metres a voxel

struct Look {
  int sleeve, sleeve2, cuff, glove, pad, skin;   // sleeve2: camo / stripes (-1 none)
  bool fingerless = true, armoured = false, stripes = false;
};
Look lookFor(const std::string& suit) {
  Look L{0x46362A, -1, 0x2E241C, 0x2B2622, 0x3C352E, 0xC0927A};
  if (suit == "suit_bandit") { L.sleeve = 0x222022; L.cuff = 0x161516; }
  else if (suit == "suit_drifter") { L.sleeve = 0x5E6446; L.cuff = 0x4A5036; }
  else if (suit == "suit_army") { L.sleeve = 0x4E563A; L.sleeve2 = 0x3A402C; L.cuff = 0x42482F; L.fingerless = false; L.glove = 0x2A2826; }
  else if (suit == "suit_merc") { L.sleeve = 0x54627A; L.sleeve2 = 0x7A8490; L.cuff = 0x3C4656; L.fingerless = false; L.glove = 0x2C2E32; }
  else if (suit == "suit_opensky") { L.sleeve = 0x7E8640; L.sleeve2 = 0xCCCCB0; L.stripes = true; L.cuff = 0x46542E; }
  else if (suit == "suit_bastion") { L.sleeve = 0x2C2624; L.sleeve2 = 0x682E28; L.stripes = true; L.cuff = 0x42201C; L.fingerless = false; L.glove = 0x221E1C; }
  else if (suit == "suit_choir") { L.sleeve = 0x9A9A90; L.cuff = 0x808078; L.fingerless = false; L.glove = 0xA8A498; L.pad = 0x8C8A80; }
  else if (suit == "suit_hazmat") { L.sleeve = 0xBC962C; L.cuff = 0x2A2A2A; L.glove = 0x1E1E1E; L.pad = 0x2C2C2C; L.fingerless = false; }
  else if (suit == "suit_exo") { L.sleeve = 0x4C5048; L.sleeve2 = 0x5E625A; L.cuff = 0x3A3C38; L.glove = 0x4A4E48; L.pad = 0x60645E; L.fingerless = false; L.armoured = true; }
  return L;
}

int grain(int c, int x, int y, int z, double amt, int seed) { return shadeC(c, 1 + (hash3(x, y, z, seed) - 0.5) * 2 * amt); }

// ---- the fist (right; `mirror` makes the left) ----
void buildFist(VoxelModel& out, const Look& L, bool mirror, vec3& wrist) {
  const ivec3 N(46, 42, 34); const vec3 O(28, 22, 17);
  Canvas c(N.x, N.y, N.z);
  const float m = mirror ? -1.f : 1.f;
  auto P = [&](float x, float y, float z) { return O + vec3(x, y, z * m); };
  auto rodP = [&](vec3 a, vec3 b, float r0, float r1, const Col& col) { c.rod(P(a.x, a.y, a.z), P(b.x, b.y, b.z), r0, r1, col); };
  const Col glove = Col(ZCF { return grain(L.glove, x, y, z, 0.07, 3); });
  const Col pad = Col(ZCF { return grain(L.pad, x, y, z, 0.06, 4); });
  const Col skin = Col(ZCF { return grain(L.skin, x, y, z, 0.05, 5); });
  const Col cuff = Col(ZCF { return grain(L.cuff, x, y, z, 0.06, 6); });
  // the hand: the back of it and the palm behind the grip
  c.ell(P(-4, 0, 6).x, P(-4, 0, 6).y, P(-4, 0, 6).z, 8, 12.5f, 4.6f, glove);
  c.ell(P(-6.5f, -1, 2).x, P(-6.5f, -1, 2).y, P(-6.5f, -1, 2).z, 5, 11, 4, glove);
  // the knuckles' pad (armoured: a plate)
  rodP(vec3(1.5f, -8, 8.6f), vec3(1.5f, 8, 8.6f), L.armoured ? 3.f : 2.3f, L.armoured ? 3.f : 2.3f, pad);
  if (L.armoured) rodP(vec3(-6, -6, 9), vec3(-6, 6, 9), 3.f, 3.f, pad);
  // four fingers curled round the grip from the knuckles (back side) over the front to the far side
  const float ys[4] = {7.6f, 2.6f, -2.4f, -7.f}, rs[4] = {2.7f, 2.8f, 2.7f, 2.3f}, ends[4] = {2.88f, 2.92f, 2.86f, 2.62f};
  for (int i = 0; i < 4; i++) {
    const float R = 4.6f + rs[i] + 0.3f, y = ys[i];
    auto at = [&](float th) { return vec3(R * std::sin(th), y, R * std::cos(th)); };
    const vec3 K(0.5f, y, 8.4f), A = at(0.62f), B = at(1.57f), C = at(2.35f), D = at(ends[i]);
    const Col& tip = L.fingerless ? skin : glove;
    rodP(K, A, rs[i] + 0.2f, rs[i], glove); rodP(A, B, rs[i], rs[i], glove);
    rodP(B, C, rs[i], rs[i] - 0.15f, tip); rodP(C, D, rs[i] - 0.15f, rs[i] - 0.4f, tip);
  }
  // the thumb, from the heel of the hand across the near side
  rodP(vec3(-8, -5, 2.5f), vec3(-7.5f, 1.5f, -3.5f), 3.3f, 3.f, glove);
  rodP(vec3(-7.5f, 1.5f, -3.5f), vec3(-4, 5.5f, -7.2f), 3.f, 2.8f, L.fingerless ? skin : glove);
  rodP(vec3(-4, 5.5f, -7.2f), vec3(-0.8f, 7, -8), 2.8f, 2.4f, L.fingerless ? skin : glove);
  // the glove's cuff and its strap
  rodP(vec3(-9, -7, 5), vec3(-17, -12, 4.5f), 6.f, 6.6f, cuff);
  rodP(vec3(-14.5f, -10.4f, 4.6f), vec3(-16.2f, -11.5f, 4.5f), 7.f, 7.f, Col(ZCF { return grain(shadeC(L.cuff, 1.35), x, y, z, 0.05, 7); }));
  wrist = vec3(-17, -12, 4.5f * m) * V;
  out.frame.voxel = V; out.frame.origin = -O * V;
  out.size = N; out.kind = VoxelModel::Flat; out.built = true;
  buildVoxelMesh(c.g, out.data);
}

// ---- the forearm: a sleeve along +Y, wider toward the elbow, folds every so often, a rim at the wrist ----
void buildArm(VoxelModel& out, const Look& L) {
  const int len = (int)std::round(ARM_LEN / V);
  const ivec3 N(28, len + 2, 28); const vec3 O(14, 0, 14);
  Canvas c(N.x, N.y, N.z);
  auto sleeve = [&](int x, int y, int z) -> int {
    int col = L.sleeve;
    if (L.sleeve2 >= 0) {
      if (L.stripes) { if ((y / 9) % 5 == 2) col = L.sleeve2; }
      else if (hash3(x / 3, y / 4, z / 3, 11) < 0.38) col = L.sleeve2;
    }
    const int fold = y % 26; if (fold < 3) col = shadeC(col, 0.78); else if (fold < 6) col = shadeC(col, 1.1);
    if (L.armoured && (y / 30) % 2 == 0) col = shadeC(col, 1.18);
    return grain(col, x, y, z, 0.07, 9);
  };
  c.coneY(O.x, O.z, 6.9f, 9.6f, 0, len, Col(sleeve));
  c.coneY(O.x, O.z, 7.5f, 7.5f, 0, 6, Col(ZCF { return grain(shadeC(L.sleeve, 0.72), x, y, z, 0.05, 10); }));   // the cuff of the sleeve
  out.frame.voxel = V; out.frame.origin = -O * V;
  out.size = N; out.kind = VoxelModel::Flat; out.built = true;
  buildVoxelMesh(c.g, out.data);
}
} // namespace

const ZoneHands& zoneHands(const std::string& suitId) {
  static auto* cache = new std::unordered_map<std::string, std::unique_ptr<ZoneHands>>();   // GL objects: never destroyed
  auto it = cache->find(suitId);
  if (it != cache->end()) return *it->second;
  auto H = std::make_unique<ZoneHands>();
  const Look L = lookFor(suitId);
  buildFist(H->fist[0], L, false, H->wrist[0]);
  buildFist(H->fist[1], L, true, H->wrist[1]);
  buildArm(H->arm, L);
  for (VoxelModel* m : {&H->fist[0], &H->fist[1], &H->arm}) { m->gl.upload(m->data); m->data.verts.clear(); m->data.verts.shrink_to_fit(); }
  H->ok = H->fist[0].ready() && H->arm.ready();
  auto& slot = (*cache)[suitId];
  slot = std::move(H);
  return *slot;
}

mat4 zoneFrame(vec3 o, vec3 x, vec3 y, float s) {
  y = glm::normalize(y);
  x = glm::normalize(x - y * glm::dot(x, y));
  const vec3 z = glm::cross(x, y);
  mat4 m(1);
  m[0] = vec4(x * s, 0); m[1] = vec4(y * s, 0); m[2] = vec4(z * s, 0); m[3] = vec4(o, 1);
  return m;
}

void drawZoneHand(const ZoneHands& H, int side, const mat4& F, vec3 elbow, VoxelLight Lt, float armScale) {
  if (!H.ok) return;
  VoxelRenderer& VR = voxelRenderer();
  VR.draw(H.fist[side].gl, F * H.fist[side].matrix(), Lt);
  const float s = glm::length(vec3(F[0]));
  const vec3 w = vec3(F * vec4(H.wrist[side], 1));
  vec3 d = elbow - w; const float dl = glm::length(d); if (dl < 1e-4f) return;
  d /= dl;
  const vec3 side0 = std::abs(d.y) < 0.95f ? vec3(0, 1, 0) : vec3(1, 0, 0);
  vec3 xv = glm::normalize(glm::cross(side0, d)), zv = glm::cross(xv, d);
  mat4 A(1);
  A[0] = vec4(xv * s, 0); A[1] = vec4(d * armScale, 0); A[2] = vec4(zv * s, 0); A[3] = vec4(w - d * (0.01f * s), 1);
  VR.draw(H.arm.gl, A * H.arm.matrix(), Lt);
}

} // namespace dq
