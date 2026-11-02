// render/zone_fpgun.cpp — first person in the Zone (S.T.A.L.K.E.R. style). Gloved hands (render/zone_fp_hands.*) in the
// suit's sleeves hold whatever is in hand:
//  * a gun (render/zone_gun_models.*): the right fist on its pistol grip, the left under the handguard (a pistol: the left
//    cups the right); hip low and right, aimed with the sights on the eye, kicked by each shot, dipped to reload, lowered
//    and turned in while sprinting, raised from below when drawn, lagging the view a little (sway) and bobbing with steps;
//  * a detector: held up in the left hand, its live display drawn on its screen (ui/zone_screens.cpp, from zoneDetRect),
//    with a bolt ready in the right fist (attack throws one: sim/zone_core.cpp);
//  * the knife: in the right fist, blade forward; it slashes with the swing;
//  * the PDA (its screen open): both hands round the device's ends, its interface on the screen (zonePdaRect);
//  * anything else: in the right fist. Empty hands: nothing (no block-game arm).
// Chained onto HeldItemLayer::handsHook; in Zone worlds it always claims both hands.
#include "render/item_layers.h"
#include "render/char_layer.h"
#include "render/zone_fp_hands.h"
#include "render/zone_gun_models.h"
#include "client/app.h"
#include "client/session.h"
#include "ui/screen.h"
#include "assets/item_models.h"
#include "assets/zone_item_models.h"
#include "sim/player.h"
#include "sim/zone.h"
#include "core/item_glint.h"
#include <cmath>

namespace dq {

float zonePdaRect[4] = {0, 0, 0, 0}; bool zonePdaRectOn = false;    // the held PDA's screen, window pixels (x0 y0 x1 y1)
float zoneDetRect[4] = {0, 0, 0, 0}; bool zoneDetRectOn = false;    // the held detector's screen
std::string zoneDetId;

namespace {

struct Fp {
  float ads = 0, kick = 0, reload = 0, raise = 0, sprint = 0, pda = 0, swayX = 0, swayY = 0;
  float lastYaw = 0, lastPitch = 0; double t = 0; int lastMag = -1; std::string id; bool look = false;
};

mat4 rotX(float a) { return glm::rotate(mat4(1), a, vec3(1, 0, 0)); }
mat4 rotY(float a) { return glm::rotate(mat4(1), a, vec3(0, 1, 0)); }
mat4 rotZ(float a) { return glm::rotate(mat4(1), a, vec3(0, 0, 1)); }

// a model-space rectangle (z = its face) through M and the projection to window pixels
bool screenRect(const mat4& proj, const mat4& M, vec2 lo, vec2 hi, float z, float W, float H, float out[4]) {
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  for (int k = 0; k < 4; k++) {
    const vec4 v = proj * (M * vec4(k & 1 ? hi.x : lo.x, k & 2 ? hi.y : lo.y, z, 1));
    if (v.w <= 0) return false;
    const float sx = (v.x / v.w * 0.5f + 0.5f) * W, sy = (1 - (v.y / v.w * 0.5f + 0.5f)) * H;
    x0 = std::min(x0, sx); x1 = std::max(x1, sx); y0 = std::min(y0, sy); y1 = std::max(y1, sy);
  }
  out[0] = x0; out[1] = y0; out[2] = x1; out[3] = y1;
  return x1 - x0 > 40 && y1 - y0 > 30;
}

int drawZoneFp(HeldItemLayer& L, GameSession& s, const FrameCtx& f, const mat4& proj) {
  static Fp A;
  Player* p = s.player;
  zoneDetRectOn = false;
  if (!p || !zone::zoneWorld(*s.level)) { zonePdaRectOn = false; return 0; }
  if (p->dead) { zonePdaRectOn = false; return 3; }
  const float dt = std::clamp((float)(f.time - A.t), 0.f, 0.1f); A.t = f.time;
  const Settings& st = s.app.settings;
  const ZoneHands& H = zoneHands(p->armor.size() > 1 ? p->armor[1].id : "");
  // light on the hands: the world's at the eye, the head torch's when it's on
  World& w = s.world();
  const u8 l = w.getLight(ifloor(p->pos.x), ifloor(p->pos.y + 1.62f), ifloor(p->pos.z));
  float blk = (l & 15) / 15.f;
  if (p->stats.contains("zone") && p->stats["zone"].value("torch", false)) blk = std::max(blk, 0.85f);
  const VoxelLight Lt{std::max(0.08f, (l >> 4) / 15.f), blk};
  // sway: the hands lag the view a little
  if (!A.look) { A.lastYaw = p->yaw; A.lastPitch = p->pitch; A.look = true; }
  float dy = p->yaw - A.lastYaw; while (dy > PI) dy -= TAU; while (dy < -PI) dy += TAU;
  const float dp = p->pitch - A.lastPitch; A.lastYaw = p->yaw; A.lastPitch = p->pitch;
  A.swayX += (std::clamp(dy * 0.6f, -0.05f, 0.05f) - A.swayX) * std::min(1.f, dt * 9);
  A.swayY += (std::clamp(dp * 0.6f, -0.05f, 0.05f) - A.swayY) * std::min(1.f, dt * 9);
  const float bobK = st.viewBob ? L.bobAmt : 0, hsn = std::sin(L.bob * 1.4f);
  vec3 bob(hsn * 0.014f * bobK, -hsn * hsn * 0.018f * bobK, 0);
  bob += vec3(A.swayX, -A.swayY, 0) * 0.6f;

  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glDisable(GL_BLEND);
  VoxelRenderer& VR = voxelRenderer();

  // ---- the PDA ----
  Screen* top = s.app.topScreen();
  const bool pdaOpen = top && std::string(top->id()) == "pda";
  A.pda += ((pdaOpen ? 1.f : 0.f) - A.pda) * std::min(1.f, dt * 10);
  zonePdaRectOn = false;
  if (A.pda > 0.02f) {
    const VoxelModel& im = itemModel("pda");
    if (!im.ready()) return 3;
    const float lift = (1 - A.pda) * -0.4f;
    const mat4 M = glm::translate(mat4(1), vec3(0, -0.03f + lift, -0.305f) + bob * 0.3f) * rotX(-0.1f) * glm::scale(mat4(1), vec3(0.59f));
    VR.begin(f, proj, false);
    VR.draw(im.gl, M * im.matrix(), Lt);
    const vec3 Xd = glm::normalize(vec3(M[0])), Yd = glm::normalize(vec3(M[1])), Zd = glm::normalize(vec3(M[2]));
    for (int side = 0; side < 2; side++) {
      const float sx = side == 0 ? 1.f : -1.f;
      const vec3 at = vec3(M * vec4(0.405f * sx, -0.05f, -0.012f, 1));
      drawZoneHand(H, side, zoneFrame(at, -Zd, Yd), at + Xd * (0.12f * sx) + vec3(0.1f * sx, -0.5f, 0.32f), Lt);
    }
    VR.end();
    vec2 lo, hi; float zf;
    if (A.pda > 0.9f && zoneDeviceScreen("pda", lo, hi, zf)) zonePdaRectOn = screenRect(proj, M, lo, hi, zf, (float)s.app.width, (float)s.app.height, zonePdaRect) && zonePdaRect[2] - zonePdaRect[0] > 200;
    return 3;
  }

  const ItemStack& held = p->held();
  if (held.empty()) { A.id.clear(); return 3; }
  const zone::ZItem* z = zone::zi(held.id);
  if (A.id != held.id) { A.id = held.id; A.raise = 0; A.ads = 0; A.reload = 0; A.lastMag = -1; }
  A.raise = std::min(1.f, A.raise + dt * 3.2f);
  const float rz = 1 - A.raise, raiseEase = rz * rz;
  A.sprint += ((p->sprinting ? 1.f : 0.f) - A.sprint) * std::min(1.f, dt * 7);

  // ---- a gun ----
  if (z && z->isGun) {
    const VoxelModel* gm = zoneGunModel(held);
    const GunModelInfo* GI = gm ? zoneGunInfo(held) : nullptr;
    if (!GI) return 3;   // being built (a few ms)
    const zone::GunHud G = zone::gunHud(*p);
    if (A.lastMag < 0) A.lastMag = G.mag;
    if (G.mag < A.lastMag && !G.reloading) A.kick = 1;
    A.lastMag = G.mag;
    A.ads += ((G.ads ? 1.f : 0.f) - A.ads) * std::min(1.f, dt * 12);
    A.reload += ((G.reloading || G.jammed ? 1.f : 0.f) - A.reload) * std::min(1.f, dt * 8);
    A.kick = std::max(0.f, A.kick - dt * 9);
    if (G.scope && A.ads > 0.9f) return 3;   // the scope's own picture fills the screen (ui/zone_screens.cpp)
    const bool pistol = GI->pistol;
    const float sc = pistol ? 0.82f : std::min(0.66f, 0.82f / std::max(0.3f, GI->length));
    const vec3 hip = pistol ? vec3(0.13f, -0.19f, -0.34f) : vec3(0.16f, -0.235f, -0.39f);
    const float relief = GI->scope ? 0.24f : GI->optic ? 0.26f : 0.3f;
    const vec3 aim(0.f, -GI->sightY * sc, -relief + GI->sightX * sc);
    const float ads = A.ads * (1 - A.sprint);
    vec3 at = glm::mix(hip, aim, ads) + bob * (1 - ads * 0.85f);
    at += vec3(0, -0.2f, 0.05f) * A.reload + vec3(0, 0.01f, 0.06f) * A.kick + vec3(-0.04f, -0.07f, 0.04f) * A.sprint + vec3(0, -0.28f, 0.05f) * raiseEase;
    mat4 M = glm::translate(mat4(1), at);
    M = M * rotY((1 - ads) * 0.05f + A.sprint * 0.75f) * rotX(A.kick * 0.09f - A.reload * 0.45f - A.sprint * 0.4f - raiseEase * 0.6f) * rotZ(A.reload * 0.35f + A.sprint * 0.2f);
    mat4 B(1);   // the gun's frame (x barrel, y up, z right) -> view (barrel away from you)
    B[0] = vec4(0, 0, -1, 0) * sc; B[1] = vec4(0, 1, 0, 0) * sc; B[2] = vec4(1, 0, 0, 0) * sc;
    M = M * B;
    VR.begin(f, proj, false);
    VR.draw(gm->gl, M * gm->matrix(), Lt, vec4(0), itemGlints(held) ? 1.f : 0.f);
    // the right fist on the grip (a pistol grip rakes back), the forearm down and back to the right
    const mat4 Fr = M * glm::translate(mat4(1), GI->grip + vec3(0.002f, 0.004f, 0)) * rotZ(-0.26f);
    const vec3 elbowR = glm::mix(vec3(0.3f, -0.55f, 0.12f), vec3(0.17f, -0.5f, 0.12f), ads) + vec3(0, -0.25f * A.reload, 0);
    drawZoneHand(H, 0, Fr, elbowR, Lt);
    // the left: under the handguard (palm up, fingers round its right side) — on a pistol, cupping the right fist
    if (pistol) {
      const mat4 Fl = M * glm::translate(mat4(1), GI->grip + vec3(0.012f, -0.008f, -0.03f)) * rotZ(-0.26f) * rotX(0.35f);
      drawZoneHand(H, 1, Fl, glm::mix(vec3(-0.16f, -0.55f, 0.08f), vec3(-0.1f, -0.5f, 0.1f), ads), Lt);
    } else if (A.reload < 0.6f) {
      const float a = 0.7071f;
      const mat4 Fl = M * zoneFrame(GI->fore + vec3(0, 0.012f, 0), vec3(0, -a, a), vec3(1, 0, 0));
      drawZoneHand(H, 1, Fl, glm::mix(vec3(-0.3f, -0.5f, -0.06f), vec3(-0.2f, -0.5f, -0.08f), ads), Lt);
    }
    VR.end();
    return 3;
  }

  const vec3 rest = vec3(0, -0.3f, 0.04f) * raiseEase + bob - vec3(0, 0.04f, 0) * A.sprint;
  VR.begin(f, proj, false);
  // ---- a detector: up in the left hand, a bolt ready in the right ----
  if (z && z->cat == "detector") {
    const VoxelModel& dm = itemModel(held.id);
    const float ds = held.id == "detector_veles" ? 0.18f : 0.17f;
    const mat4 M = glm::translate(mat4(1), vec3(-0.115f, -0.105f, -0.33f) + rest) * rotY(0.32f) * rotX(-0.22f) * glm::scale(mat4(1), vec3(ds));
    if (dm.ready()) VR.draw(dm.gl, M * dm.matrix(), Lt);
    const vec3 Yd = glm::normalize(vec3(M[1])), Zd = glm::normalize(vec3(M[2]));
    const vec3 lh = vec3(M * vec4(-0.24f, -0.2f, -0.015f, 1));
    drawZoneHand(H, 1, zoneFrame(lh, -Zd, Yd), lh + vec3(-0.12f, -0.5f, 0.3f), Lt);
    vec2 lo, hi; float zf;
    if (A.raise > 0.95f && zoneDeviceScreen(held.id, lo, hi, zf)) { zoneDetRectOn = screenRect(proj, M, lo, hi, zf, (float)s.app.width, (float)s.app.height, zoneDetRect); zoneDetId = held.id; }
    // the right fist with a bolt in it (if you have any); a throw swings it forward
    bool bolts = false; for (const ItemStack& b : p->inv) if (!b.empty() && b.id == "bolt") { bolts = true; break; }
    const float sw = L.swing > 0 ? std::sin(std::min(1.f, L.swing) * PI) : 0;
    const vec3 rh = vec3(0.15f, -0.17f, -0.36f) + rest + vec3(0, 0.05f, -0.06f) * sw;
    const mat4 Fr = zoneFrame(rh, vec3(-0.8f, 0, -0.6f), vec3(-0.1f, 1, 0.2f - sw * 0.8f));
    drawZoneHand(H, 0, Fr, rh + vec3(0.14f, -0.5f, 0.32f), Lt);
    if (bolts) { const VoxelModel& bm = itemModel("bolt"); if (bm.ready()) VR.draw(bm.gl, Fr * glm::translate(mat4(1), vec3(-0.004f, 0.042f, 0)) * glm::scale(mat4(1), vec3(0.036f)) * bm.matrix(), Lt); }
    VR.end();
    return 3;
  }
  // ---- the knife: blade forward in the right fist, slashing across with the swing ----
  if (z && z->cat == "melee") {
    const VoxelModel& km = itemModel(held.id);
    const float sw = L.swing > 0 ? std::sin(std::min(1.f, L.swing) * PI) : 0;
    const vec3 o = vec3(0.15f, -0.17f, -0.34f) + rest + vec3(-0.18f, 0.06f, -0.08f) * sw;
    const vec3 b = glm::normalize(vec3(-0.3f - sw * 0.9f, 0.22f - sw * 0.3f, -1.f));
    const mat4 Fr = zoneFrame(o, vec3(-0.3f, -1.f, 0.f), b);
    drawZoneHand(H, 0, Fr, o + vec3(0.16f, -0.5f, 0.3f), Lt);
    if (km.ready()) {
      const vec3 X = vec3(Fr[0]), Y = vec3(Fr[1]);
      const float ks = 0.3f;
      mat4 K(1);   // the knife model: x -> the blade's way (the fist's Y), y -> its spine (away from the fingers), handle in the fist
      const vec3 kx = glm::normalize(Y), ky = glm::normalize(-X), kz = glm::cross(kx, ky);
      K[0] = vec4(kx * ks, 0); K[1] = vec4(ky * ks, 0); K[2] = vec4(kz * ks, 0); K[3] = vec4(o + kx * (0.304f * ks), 1);
      VR.draw(km.gl, K * km.matrix(), Lt, vec4(0), itemGlints(held) ? 1.f : 0.f);
    }
    VR.end();
    return 3;
  }
  // ---- anything else: in the right fist ----
  {
    const VoxelModel& im = itemModel(held.id);
    const float sw = L.swing > 0 ? std::sin(std::min(1.f, L.swing) * PI) : 0;
    const vec3 rh = vec3(0.15f, -0.18f, -0.36f) + rest + vec3(-0.03f, 0.04f, -0.04f) * sw;
    const mat4 Fr = zoneFrame(rh, vec3(-0.8f, 0, -0.6f), vec3(-0.1f, 1, 0.2f));
    drawZoneHand(H, 0, Fr, rh + vec3(0.14f, -0.5f, 0.32f), Lt);
    if (im.ready()) VR.draw(im.gl, Fr * glm::translate(mat4(1), vec3(-0.02f, 0.055f, -0.01f)) * rotY(0.6f) * glm::scale(mat4(1), vec3(0.11f)) * im.matrix(), Lt);
    VR.end();
  }
  return 3;
}
} // namespace

void registerZoneFpGun() {
  static bool done = false;
  if (done) return;
  done = true;
  auto prev = HeldItemLayer::handsHook;
  HeldItemLayer::handsHook = [prev](HeldItemLayer& L, GameSession& s, const FrameCtx& f, const mat4& proj) {
    int t = drawZoneFp(L, s, f, proj);
    if (!(t & 1) && prev) t |= prev(L, s, f, proj);
    return t;
  };
}

} // namespace dq
