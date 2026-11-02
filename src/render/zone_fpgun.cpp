// render/zone_fpgun.cpp — a Zone gun held in first person: pointed down the view at the hip (right of centre, a
// little low), brought up to the middle of the screen when aiming (the sights in line with the eye), kicked back
// and up by each shot, dipped while reloading, swaying with your steps. Both arms hold it: the right hand on the
// grip, the left under the barrel. The gun is its item model (sculpted from its sprite, barrel to the right),
// turned so the barrel points away from you. Chained onto HeldItemLayer::handsHook like the bow (render/fp_bow.cpp).
#include "render/item_layers.h"
#include "render/char_layer.h"
#include "client/app.h"
#include "client/session.h"
#include "assets/item_models.h"
#include "sim/player.h"
#include "sim/zone.h"
#include "core/item_glint.h"
#include <cmath>

namespace dq {
namespace {

mat4 armTo(vec3 sh, vec3 fist) {
  vec3 d = fist - sh; float len = glm::length(d); if (len < 1e-4f) len = 1;
  d /= len;
  vec3 yv = -d, xv(yv.z, 0, -yv.x);
  float xl = glm::length(xv); xv /= xl > 1e-4f ? xl : 1.f;
  vec3 zv = glm::cross(xv, yv);
  const float sl = 0.72f;
  mat4 m(1);
  m[0] = vec4(xv * sl, 0); m[1] = vec4(yv, 0); m[2] = vec4(zv * sl, 0);
  m[3] = vec4(fist - (yv * -0.3f + zv * -0.02f), 1);
  return m;
}

struct GunAnim { float ads = 0, kick = 0, reload = 0, lastPitch = 0; double t = 0; int lastMag = -1; std::string id; };

int drawFpGun(HeldItemLayer& L, GameSession& s, const FrameCtx& f, const mat4& proj) {
  static GunAnim A;
  Player* p = s.player;
  if (!p || p->dead || !zone::zoneWorld(*s.level)) return 0;
  const ItemStack& held = p->held();
  const zone::ZItem* z = held.empty() ? nullptr : zone::zi(held.id);
  if (!z || !z->isGun) { A.id.clear(); return 0; }
  const VoxelModel& im = itemModel(held.id);
  if (!im.ready()) return 0;
  const float dt = clampv((float)(f.time - A.t), 0.f, 0.1f); A.t = f.time;
  const zone::GunHud H = zone::gunHud(*p);
  if (A.id != held.id) { A.id = held.id; A.lastMag = H.mag; A.ads = 0; A.reload = 0; }
  if (H.mag < A.lastMag && !H.reloading) A.kick = 1;   // a round went
  A.lastMag = H.mag;
  A.ads += ((H.ads ? 1.f : 0.f) - A.ads) * std::min(1.f, dt * 12);
  A.reload += ((H.reloading || H.jammed ? 1.f : 0.f) - A.reload) * std::min(1.f, dt * 8);
  A.kick = std::max(0.f, A.kick - dt * 9);
  // a scope fills the screen with its own picture (ui/zone_screens.cpp): the gun keeps out of it
  if (H.scope && A.ads > 0.9f) return 3;
  const Settings& st = s.app.settings;
  const float bobK = st.viewBob ? L.bobAmt * (1 - A.ads * 0.8f) : 0, hsn = std::sin(L.bob * 1.4f);
  const float bx = hsn * 0.02f * bobK, by = -hsn * hsn * 0.025f * bobK;
  const float len = z->gun.cls == "pistol" ? 0.24f : z->gun.cls == "smg" ? 0.4f : z->gun.cls == "sniper" || z->gun.cls == "lmg" || z->gun.cls == "dmr" ? 0.62f : 0.52f;
  // the gun's frame: model x (barrel) -> away from you, y -> up, z -> right
  const vec3 hip(0.2f, -0.19f, -0.5f), aim(0.0f, -0.118f, -0.56f);
  vec3 at = glm::mix(hip, aim, A.ads) + vec3(bx, by, 0);
  at += vec3(0, -0.22f, 0.05f) * A.reload;                 // down and in to reload
  at += vec3(0, 0.012f, 0.07f) * A.kick;                    // back into the shoulder
  const float pitchUp = A.kick * 0.09f - A.reload * 0.5f, yawIn = (1 - A.ads) * 0.06f;
  mat4 M(1);
  M = glm::translate(M, at);
  M = glm::rotate(M, yawIn, vec3(0, 1, 0));
  M = glm::rotate(M, pitchUp, vec3(1, 0, 0));
  mat4 B(1);
  B[0] = vec4(0, 0, -1, 0) * len; B[1] = vec4(0, 1, 0, 0) * len; B[2] = vec4(1, 0, 0, 0) * len;
  M = M * B;
  auto pt = [&](float x, float y) { return vec3(M * vec4(x, y, 0, 1)); };
  World& w = s.world();
  u8 l = w.getLight(ifloor(p->pos.x), ifloor(p->pos.y + 1.62f), ifloor(p->pos.z));
  VoxelLight Lt{(l >> 4) / 15.f, (l & 15) / 15.f};
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glDisable(GL_BLEND);
  VoxelRenderer& VR = voxelRenderer();
  VR.begin(f, proj, false);
  VR.draw(im.gl, M * im.matrix(), Lt, vec4(0), itemGlints(held) ? 1.f : 0.f);
  const VoxelModel& arm = firstPersonArm();
  auto drawArm = [&](const mat4& m) { VR.draw(arm.gl, m * arm.matrix(), Lt); drawFirstPersonSleeve(m, Lt); };
  drawArm(armTo(vec3(0.6f, -0.75f, 0.1f), pt(-0.2f, -0.18f)));                       // the grip
  if (z->gun.cls != "pistol") drawArm(armTo(vec3(-0.55f, -0.85f, 0.1f), pt(0.16f, -0.1f)));   // under the barrel
  else drawArm(armTo(vec3(-0.45f, -0.85f, 0.2f), pt(-0.18f, -0.2f)));
  VR.end();
  return 3;
}
} // namespace

void registerZoneFpGun() {
  static bool done = false;
  if (done) return;
  done = true;
  auto prev = HeldItemLayer::handsHook;
  HeldItemLayer::handsHook = [prev](HeldItemLayer& L, GameSession& s, const FrameCtx& f, const mat4& proj) {
    int t = drawFpGun(L, s, f, proj);
    if (!(t & 1) && prev) t |= prev(L, s, f, proj);
    return t;
  };
}

} // namespace dq
