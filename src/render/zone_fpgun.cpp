// render/zone_fpgun.cpp — first person in the Zone (S.T.A.L.K.E.R. / Misery / STALCRAFT): your own skin's two arms holding
// things at their REAL size (1 block = 1 m: a pistol ~0.2 m, an AK ~0.9 m, the knife ~0.3 m, the detector and the PDA
// hand-sized; render/zone_creature_draw.cpp zoneHeldSize), moved procedurally and time-based (springs sub-stepped, so it is
// the same at 5 fps and 240):
//  * always: breathing (deeper when winded), the hands trailing the view (look sway on springs), the step bob as a figure 8
//    on the footstep phase (bigger running), a lean into strafes, crouch lowers, a jump lifts and the landing dips (the view
//    dips too: zoneFpCam, added to the camera by client/play.cpp);
//  * a gun: hip low and right; aiming eases it onto the sight line (zoneGunInfo) with a dip on the way, little sway; sprinting
//    lowers it across the chest (a pistol: lowered), blended out over the 0.3 s the sim keeps it from firing (zone_guns.cpp
//    sprintRec); each shot kicks it back and up on springs per gun class, the view takes a share; pump / bolt cycling; the
//    reload as a sequence over the sim's reload time (the left hand yanks the mag, brings a fresh one up and seats it, then the
//    charging handle / slide if it ran dry; a shotgun shell by shell into the port); a jam racked clear; switching: the old
//    one goes down, the new one comes up from below;
//  * a detector up in the left hand (its display: zoneDetRect), a bolt ready in the right (the throw winds back, lets go);
//  * the knife: a diagonal slash with a wind-up and a follow-through;
//  * the PDA: what's in hand goes down, then the PDA comes up in both hands (its interface on the screen: zonePdaRect);
//  * a med / food used (sim zoneLastUse, Misery-style): what's in hand goes down, the item comes up: eaten, drunk, a bandage
//    wound round the left forearm, an injector into it, pills; then the hands come back;
//  * anything else: in the right fist at its real size.
// Test hook: Player::data["fpTest"] = {"a": reload|reloadEmpty|shell|jam|sprint|ads|slash|bolt|draw|pda|cycle|use, "u": 0..1,
// "id": the item for "use"} freezes that action at that phase (smoke scripts: {"do":"data","key":"fpTest","value":{...}}).
// Chained onto HeldItemLayer::handsHook; in Zone worlds it always claims both hands.
#include "render/item_layers.h"
#include "render/char_layer.h"
#include "render/zone_gun_models.h"
#include "render/zone_creature_draw.h"
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
float zoneFpCam[4] = {0, 0, 0, 0};   // the hands' share of the view: pitch, yaw, roll (radians), dip (blocks) — client/play.cpp

namespace {

float sat(float x) { return std::clamp(x, 0.f, 1.f); }
float smooth(float x) { x = sat(x); return x * x * (3 - 2 * x); }
float seg(float u, float a, float b) { return smooth((u - a) / (b - a)); }
float bump(float u, float a, float b) { return std::sin(sat((u - a) / (b - a)) * PI); }
float backOut(float x) { x = sat(x); const float c = 1.4f, y = x - 1; return 1 + y * y * ((c + 1) * y + c); }
vec3 path(float u, std::initializer_list<std::pair<float, vec3>> k) {
  const std::pair<float, vec3>* a = k.begin();
  if (u <= a->first) return a->second;
  for (const auto* b = a + 1; b != k.end(); a = b, ++b) if (u <= b->first) return glm::mix(a->second, b->second, seg(u, a->first, b->first));
  return (k.end() - 1)->second;
}
float rnd() { static u32 s = 0x2545F491u; s = s * 1664525u + 1013904223u; return (float)(s >> 8) / 8388608.f - 1.f; }

// a damped spring, sub-stepped (stable and frame-rate independent)
struct Spring {
  float x = 0, v = 0;
  void step(float target, float k, float damp, float dt) {
    const int n = std::max(1, (int)std::ceil(dt / 0.006f)); const float h = dt / n, c = 2 * std::sqrt(k) * damp;
    for (int i = 0; i < n; i++) { v += ((target - x) * k - v * c) * h; x += v * h; }
  }
  void hit(float amount, float k) { v += amount * std::sqrt(k); }   // peaks near 0.55 * amount (damp 0.5)
};

struct Fp {
  double t = 0;
  Spring swX, swY, swYaw, swPitch, tilt, air, dip, kZ, kY, kP, kR, kYaw, camP, camY, camR;
  float lastYaw = 0, lastPitch = 0, fallV = 0, crouch = 0, sprintL = 0, adsL = 0, breath = 0;
  bool look = false, wasAir = false;
  int lastMag = -1;
  float relTotal = 0, relLast = 0, relW = 0, relU = 0; bool relOn = false, relEmpty = false, relFirst = true;
  float jamW = 0, jamT = 0, cycle = 9;
  std::string key; ItemStack shown; bool lowering = false; float lowT = 0, upT = 1;
  float pdaK = 0; int useSerial = -1; std::string useId; float useT = -1, useDur = 1;
};

mat4 rotX(float a) { return glm::rotate(mat4(1), a, vec3(1, 0, 0)); }
mat4 rotY(float a) { return glm::rotate(mat4(1), a, vec3(0, 1, 0)); }
mat4 rotZ(float a) { return glm::rotate(mat4(1), a, vec3(0, 0, 1)); }
mat4 scl(float s) { return glm::scale(mat4(1), vec3(s)); }

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

// a frame from two axes (made orthonormal: y kept, x fixed against it), an origin and a scale
mat4 zoneFrame(vec3 o, vec3 x, vec3 y, float s = 1.f) {
  y = glm::normalize(y); x = glm::normalize(x - y * glm::dot(x, y));
  mat4 m(1); m[0] = vec4(x * s, 0); m[1] = vec4(y * s, 0); m[2] = vec4(glm::cross(x, y) * s, 0); m[3] = vec4(o, 1);
  return m;
}
// one of your own arms (the skin's arm box, the left from the skin's left arm, 0.75 m long, 0.12 m thick): its hand end at
// `hand`, the arm running down and out below the view (Minecraft / STALCRAFT style), never straight at the eye
void skinHand(int side, vec3 hand, VoxelLight Lt) {
  const VoxelModel* m = side == 0 ? localPlayerArm() : localPlayerArmLeft();
  if (!m || !m->ready()) return;
  const float out = side == 0 ? 1.f : -1.f;
  vec3 yv = glm::normalize(vec3(out * 0.22f - hand.x * 0.15f, -1.f, 0.28f));
  vec3 xv(yv.z, 0, -yv.x); const float xl = glm::length(xv); xv = xl > 1e-4f ? xv / xl : vec3(1, 0, 0);
  const vec3 zv = glm::cross(xv, yv);
  const float sl = 0.48f, ln = 1.0f;
  mat4 A(1); A[0] = vec4(xv * sl, 0); A[1] = vec4(yv * ln, 0); A[2] = vec4(zv * sl, 0); A[3] = vec4(hand + yv * (0.34375f * ln), 1);
  voxelRenderer().draw(m->gl, A * m->matrix(), Lt);
}

int drawZoneFp(HeldItemLayer& L, GameSession& s, const FrameCtx& f, const mat4& proj) {
  static Fp A;
  Player* p = s.player;
  zoneDetRectOn = false;
  auto clearCam = [] { for (float& c : zoneFpCam) c = 0; };
  if (!p || !zone::zoneWorld(*s.level)) { zonePdaRectOn = false; clearCam(); return 0; }
  if (p->dead) { zonePdaRectOn = false; clearCam(); return 3; }
  const float dt = std::clamp((float)(f.time - A.t), 0.f, 0.1f); A.t = f.time;
  const Settings& st = s.app.settings;
  // light on the hands: the world's at the eye, the head torch's when it's on
  World& w = s.world();
  const u8 l = w.getLight(ifloor(p->pos.x), ifloor(p->pos.y + 1.62f), ifloor(p->pos.z));
  float blk = (l & 15) / 15.f;
  if (p->stats.contains("zone") && p->stats["zone"].value("torch", false)) blk = std::max(blk, 0.85f);
  const VoxelLight Lt{std::max(0.08f, (l >> 4) / 15.f), blk};
  // the test hook: one action frozen at one phase
  std::string tA, tId; float tU = 0;
  if (p->data.contains("fpTest") && p->data["fpTest"].is_object()) { const json& T = p->data["fpTest"]; tA = T.value("a", ""); tU = T.value("u", 0.f); tId = T.value("id", ""); }

  // ---- the body under the hands: look sway, a lean into strafes, crouch, air and landing, breathing ----
  if (!A.look) { A.lastYaw = p->yaw; A.lastPitch = p->pitch; A.look = true; }
  float dy = p->yaw - A.lastYaw; while (dy > PI) dy -= TAU; while (dy < -PI) dy += TAU;
  const float dp = p->pitch - A.lastPitch; A.lastYaw = p->yaw; A.lastPitch = p->pitch;
  const float yr = dt > 0 ? std::clamp(dy / dt, -6.f, 6.f) : 0.f, pr = dt > 0 ? std::clamp(dp / dt, -6.f, 6.f) : 0.f;
  A.swX.step(std::clamp(yr * 0.006f, -0.03f, 0.03f), 90, 0.6f, dt);   // turning, the hands trail behind the view
  A.swY.step(std::clamp(-pr * 0.006f, -0.03f, 0.03f), 90, 0.6f, dt);
  A.swYaw.step(std::clamp(-yr * 0.03f, -0.12f, 0.12f), 90, 0.6f, dt);
  A.swPitch.step(std::clamp(-pr * 0.03f, -0.12f, 0.12f), 90, 0.6f, dt);
  const float side = p->vel.x * std::cos(p->yaw) - p->vel.z * std::sin(p->yaw);
  A.tilt.step(st.viewBob ? std::clamp(-side * 0.025f, -0.1f, 0.1f) : 0.f, 60, 0.8f, dt);
  A.crouch += ((p->sneaking ? 1.f : 0.f) - A.crouch) * std::min(1.f, dt * 9);
  const bool air = !p->onGround && !p->flying && !p->inWater;
  if (air) A.fallV = std::max(A.fallV, -p->vel.y);
  if (A.wasAir && !air && A.fallV > 2) { const float k = std::min(1.f, (A.fallV - 2) / 10); A.air.hit(-0.11f * k, 150); A.dip.hit(-0.12f * k, 120); }
  if (!air) A.fallV = 0;
  A.wasAir = air;
  A.air.step(air ? std::clamp(p->vel.y * -0.004f, -0.025f, 0.03f) : 0.f, 150, 0.55f, dt);
  A.dip.step(0, 120, 0.6f, dt);
  const float stam = std::clamp(zone::zget(*p, "stamina", 100) / 100.f, 0.f, 1.f), br = 1 + (1 - stam) * 2.5f;
  A.breath += dt * (1.6f + (1 - stam) * 1.6f);
  A.sprintL = std::clamp(A.sprintL + (p->sprinting ? dt : -dt) / 0.28f, 0.f, 1.f);
  float sprint = smooth(A.sprintL);
  if (tA == "sprint") sprint = smooth(tU > 0 ? tU : 1);
  // recoil and the view's share settle
  A.kZ.step(0, 420, 0.5f, dt); A.kY.step(0, 420, 0.5f, dt); A.kP.step(0, 300, 0.5f, dt); A.kR.step(0, 300, 0.5f, dt); A.kYaw.step(0, 300, 0.5f, dt);
  A.camP.step(0, 160, 0.65f, dt); A.camY.step(0, 160, 0.65f, dt); A.camR.step(0, 160, 0.65f, dt);
  zoneFpCam[0] = A.camP.x; zoneFpCam[1] = A.camY.x; zoneFpCam[2] = A.camR.x; zoneFpCam[3] = A.dip.x;
  const float bobK = st.viewBob ? L.bobAmt : 0, ph = L.bob * 1.4f;
  auto motion = [&](float ads, float spr, vec3& pos, vec3& rot) {   // rot = pitch (muzzle up), yaw (muzzle left), roll
    const float calm = 1 - ads * 0.8f, a = bobK * (1 + spr * 1.4f) * (1 - ads * 0.75f);
    const float s1 = std::sin(ph - 0.35f), c2 = std::cos(2 * (ph - 0.35f));
    pos += vec3(s1 * 0.011f * a, (c2 - 1) * 0.0055f * a, 0);       // the figure 8 on the stride, a little behind the footfall
    rot += vec3((c2 - 1) * 0.012f * a, s1 * 0.02f * a, -s1 * 0.025f * a);
    pos += vec3(A.swX.x, A.swY.x, 0) * calm; rot += vec3(A.swPitch.x, A.swYaw.x, 0) * calm;
    rot.z += A.tilt.x * (1 - ads * 0.6f); pos.x += A.tilt.x * -0.08f * (1 - ads);
    pos.y += A.air.x;
    pos += vec3(0.004f, -0.018f, 0.012f) * A.crouch * (1 - ads); rot.z += 0.04f * A.crouch * (1 - ads);
    const float b = std::sin(A.breath) * br, q = 1 - ads * 0.65f;
    pos += vec3(std::sin(A.breath * 0.5f) * 0.0012f, b * 0.0022f, 0) * q;
    rot += vec3(std::sin(A.breath + 0.6f) * 0.005f * br, std::sin(A.breath * 0.5f + 1) * 0.003f * br, 0) * q;
  };

  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glDisable(GL_BLEND);
  VoxelRenderer& VR = voxelRenderer();

  // ---- what's in hand: switching lowers the old one first, then the new one comes up from below ----
  const ItemStack& held = p->held();
  const std::string key = held.empty() ? std::string() : held.id + "#" + std::to_string(p->selected);
  auto fresh = [&] { A.key = key; A.upT = 0; A.lastMag = -1; A.relOn = false; A.relW = 0; A.adsL = 0; A.jamW = 0; A.cycle = 9; };
  if (key != A.key && !A.lowering) { if (!A.key.empty() && A.upT > 0.3f) { A.lowering = true; A.lowT = 0; } else fresh(); }
  if (A.lowering) { A.lowT += dt / 0.13f; if (A.lowT >= 1) { A.lowering = false; fresh(); } }
  if (!A.lowering) { A.upT = std::min(1.f, A.upT + dt / 0.27f); A.shown = held; }
  if (tA == "draw") A.upT = tU;
  float down = A.lowering ? smooth(A.lowT) : 1 - backOut(A.upT);
  // the PDA: the hands go down, then it comes up
  Screen* top = s.app.topScreen();
  const bool pdaOpen = (top && std::string(top->id()) == "pda") || tA == "pda";
  A.pdaK = std::clamp(A.pdaK + (pdaOpen ? dt : -dt) / 0.42f, 0.f, 1.f);
  if (tA == "pda") A.pdaK = tU;
  float handsDown = smooth(A.pdaK * 2);
  const float pdaUp = smooth(A.pdaK * 2 - 1);
  // a med / food used: the hands go down, the item comes up to you
  if (zone::zoneLastUse.serial != A.useSerial) {
    if (A.useSerial >= 0 && zone::zoneLastUse.by == p->id) { A.useId = zone::zoneLastUse.id; A.useT = 0; A.useDur = std::clamp(zone::zoneLastUse.time, 1.2f, 3.f); }
    A.useSerial = zone::zoneLastUse.serial;
  }
  if (tA == "use") { A.useId = tId; A.useDur = 2.f; A.useT = tU * A.useDur; }
  else if (A.useT >= 0 && (A.useT += dt) > A.useDur) A.useT = -1;
  const float useU = A.useT >= 0 ? sat(A.useT / A.useDur) : -1.f;
  if (useU >= 0) handsDown = std::max(handsDown, seg(useU, 0, 0.12f) * (1 - seg(useU, 0.9f, 1)));
  const float dn = std::max(down, handsDown);

  VR.begin(f, proj, false);
  // ---- the PDA in both hands ----
  zonePdaRectOn = false;
  if (pdaUp > 0.001f) {
    const VoxelModel& im = itemModel("pda");
    if (im.ready()) {
      vec3 pos(0), rot(0); motion(0.7f, 0, pos, rot);
      const mat4 M = glm::translate(mat4(1), vec3(0, -0.035f - (1 - pdaUp) * 0.3f, -0.19f) + pos * 0.5f) * rotX(-0.08f - (1 - pdaUp) * 0.7f + rot.x * 0.5f) * rotZ(rot.z * 0.5f) * scl(zoneHeldSize("pda") * 1.1f);
      VR.draw(im.gl, M * im.matrix(), Lt);
      for (int sd = 0; sd < 2; sd++) skinHand(sd, vec3(M * vec4(0.47f * (sd == 0 ? 1.f : -1.f), -0.16f, -0.012f, 1)), Lt);   // (the lower corners: the key column stays clear)
      vec2 lo, hi; float zf;
      if (pdaUp > 0.9f && zoneDeviceScreen("pda", lo, hi, zf)) zonePdaRectOn = screenRect(proj, M, lo, hi, zf, (float)s.app.width, (float)s.app.height, zonePdaRect) && zonePdaRect[2] - zonePdaRect[0] > 200;
    }
    VR.end();
    return 3;
  }

  const ItemStack& S = A.shown;
  const zone::ZItem* z = S.empty() ? nullptr : zone::zi(S.id);

  // ---- a gun ----
  if (z && z->isGun && dn < 0.999f) {
    const VoxelModel* gm = zoneGunModel(S);
    const GunModelInfo* GI = gm ? zoneGunInfo(S) : nullptr;
    if (GI) {
      const bool live = !A.lowering;
      zone::GunHud G = live ? zone::gunHud(*p) : zone::GunHud{};
      if (tA == "ads") G.ads = true;
      const bool pistol = GI->pistol;
      const std::string& cls = z->gun.cls;
      // aiming: eased in (long guns and scopes a touch slower)
      const float adsTime = pistol ? 0.16f : GI->scope ? 0.24f : (cls == "lmg" || cls == "sniper") ? 0.26f : 0.2f;
      A.adsL = std::clamp(A.adsL + ((G.ads && sprint < 0.5f) ? dt : -dt) / adsTime, 0.f, 1.f);
      if (tA == "ads") A.adsL = tU > 0 ? tU : 1;
      const float ads = smooth(A.adsL);
      if (G.scope && ads > 0.9f && tA.empty()) { VR.end(); return 3; }   // the scope's own picture fills the screen (ui/zone_screens.cpp)
      // a shot: the kick (per class, the catalogue's recoil), the view's share; a pump / bolt gun works its action
      const bool pump = cls == "shotgun" && !z->gun.autoFire, bolt = cls == "sniper" && z->gun.rpm <= 70;
      if (live) {
        if (A.lastMag < 0) A.lastMag = G.mag;
        if (G.mag < A.lastMag && !G.reloading) {
          struct Rk { float back, up, side, cam; };
          const Rk k = cls == "pistol" ? Rk{0.03f, 0.16f, 0.05f, 0.012f} : cls == "smg" ? Rk{0.018f, 0.06f, 0.03f, 0.005f} : cls == "shotgun" ? Rk{0.06f, 0.22f, 0.06f, 0.03f}
                     : cls == "sniper" ? Rk{0.055f, 0.2f, 0.05f, 0.028f} : cls == "dmr" ? Rk{0.04f, 0.14f, 0.04f, 0.018f} : cls == "lmg" ? Rk{0.024f, 0.07f, 0.04f, 0.008f}
                     : cls == "launcher" ? Rk{0.05f, 0.18f, 0.03f, 0.02f} : Rk{0.026f, 0.09f, 0.035f, 0.009f};
          const float km = std::clamp(z->gun.kick, 0.5f, 2.f) * (G.ads ? 0.6f : 1.f) * (p->sneaking ? 0.85f : 1.f) * 1.8f;
          A.kZ.hit(k.back * km, 420); A.kY.hit(k.back * 0.3f * km, 420); A.kP.hit(k.up * km, 300); A.kYaw.hit(rnd() * k.side * km, 300); A.kR.hit(rnd() * k.side * 1.2f * km, 300);
          A.camP.hit(k.cam * 1.8f, 160); A.camR.hit(rnd() * k.cam * 0.6f, 160); A.camY.hit(rnd() * k.cam * 0.3f, 160);
          if (pump || bolt) A.cycle = 0;
        }
        A.lastMag = G.mag;
      }
      A.cycle += dt;
      float cyc = (pump || bolt) ? sat((A.cycle - 0.12f) / 0.5f) : 1.f;
      if (tA == "cycle") cyc = tU;
      const float cw = cyc < 1 ? std::sin(cyc * PI) : 0.f;
      // the reload: its phase over the sim's time (a shotgun: each shell is a cycle of its own)
      const bool tRel = tA == "reload" || tA == "reloadEmpty" || tA == "shell";
      if (live && (G.reloading || tRel)) {
        if (!A.relOn) { A.relOn = true; A.relTotal = std::max(0.3f, G.reloadT); A.relLast = G.reloadT; A.relEmpty = G.mag == 0; A.relFirst = true; }
        if (G.reloadT > A.relLast + 0.05f) { A.relTotal = std::max(0.3f, G.reloadT); A.relFirst = false; }
        A.relLast = G.reloadT;
        A.relU = sat(1 - G.reloadT / A.relTotal);
      } else A.relOn = false;
      if (tRel) { A.relU = tU; A.relEmpty = tA == "reloadEmpty"; A.relW = 1; }
      else A.relW += ((A.relOn ? 1.f : 0.f) - A.relW) * std::min(1.f, dt * 9);
      const bool shells = z->gun.slowReload || tA == "shell";
      const bool jam = (live && G.jammed) || tA == "jam";
      A.jamW += ((jam ? 1.f : 0.f) - A.jamW) * std::min(1.f, dt * 8);
      if (jam) A.jamT += dt; else A.jamT = 0;
      if (tA == "jam") { A.jamW = 1; A.jamT = tU * 0.8f; }

      // ---- the pose: hip, aimed, the body's motion, sprint, recoil, reload / jam / cycling, lowered ----
      const vec3 hip = pistol ? vec3(0.1f, -0.13f, -0.4f) : vec3(0.12f, -0.19f, -0.33f);   // low and right, the hands in view
      const float relief = GI->scope ? 0.12f : GI->optic ? 0.2f : pistol ? 0.45f : 0.26f;   // (a pistol out at arm's length)
      const vec3 aim(0.f, -GI->sightY, -relief + GI->sightX);
      vec3 pos = glm::mix(hip, aim, ads), rot(0, (1 - ads) * 0.05f, 0);
      const float mid = std::sin(ads * PI);
      pos.y -= mid * 0.012f; rot.z += mid * 0.06f;   // a little dip and cant on the way up to the eye
      motion(ads, sprint, pos, rot);
      if (sprint > 0) {
        const float sw = sprint * (1 - ads), s1 = std::sin(ph);
        if (pistol) { pos += vec3(-0.01f, -0.07f, 0.06f) * sw; rot += vec3(-0.85f, 0.25f, 0.1f) * sw; }
        else { pos += vec3(-0.07f, -0.05f, 0.08f) * sw; rot += vec3(-0.3f, 0.8f, 0.45f) * sw; }   // across the chest
        rot.z += s1 * 0.06f * sw * bobK; rot.y += s1 * 0.05f * sw * bobK; pos.y += std::fabs(std::cos(ph)) * 0.012f * sw * bobK;
      }
      pos += vec3(A.kYaw.x * -0.05f, A.kY.x, A.kZ.x); rot += vec3(A.kP.x, A.kYaw.x, A.kR.x);
      const float u = A.relU, rw = A.relW;
      if (rw > 0.001f) {
        if (shells) {   // rolled over to show the loading port, a shove as each shell goes home
          pos += vec3(-0.04f, 0.03f, 0.03f) * rw; rot += vec3(0.12f, 0.15f, -0.6f) * rw;
          const float push = bump(u, 0.72f, 0.92f); pos.z += 0.01f * push * rw; rot.x += 0.03f * push * rw;
        } else if (pistol) {
          pos += vec3(-0.03f, 0.04f, 0.04f) * rw; rot += vec3(0.2f, 0.3f, -0.4f) * rw;
          const float ins = bump(u, 0.66f, 0.78f); pos.y += 0.012f * ins * rw; rot.x += 0.06f * ins * rw;
          if (A.relEmpty) { const float r = bump(u, 0.8f, 0.97f); rot.z += 0.5f * r * rw; rot.x += 0.1f * r * rw; }
        } else {        // canted toward the left hand; yanking the old mag drags it down, seating the new one shoves it up
          pos += vec3(-0.06f, 0.035f, 0.03f) * rw; rot += vec3(0.16f, 0.28f, -0.5f) * rw;
          const float pull = bump(u, 0.15f, 0.3f), ins = bump(u, 0.66f, 0.8f);
          pos.y += (0.016f * ins - 0.02f * pull) * rw; rot.x += (0.08f * ins - 0.05f * pull) * rw;
          if (A.relEmpty) {   // rolled the other way for the charging handle, a jolt as it snaps forward
            const float h = seg(u, 0.77f, 0.83f) * (1 - seg(u, 0.95f, 1));
            rot.z += 0.9f * h * rw; rot.y -= 0.15f * h * rw; pos.y += 0.02f * h * rw;
            pos.z += 0.012f * bump(u, 0.89f, 0.95f) * rw;
          }
        }
      }
      if (A.jamW > 0.001f) {   // racking it clear, again and again
        const float jp = std::fmod(A.jamT, 0.8f) / 0.8f, jolt = bump(jp, 0.45f, 0.62f);
        pos += vec3(-0.03f, 0.02f, 0.02f + 0.01f * jolt) * A.jamW; rot += vec3(0.1f + 0.05f * jolt, 0.2f, 0.4f) * A.jamW;
      }
      if (cw > 0) { if (pump) { rot.x += 0.04f * cw; rot.z -= 0.06f * cw; } else { rot.z += 0.3f * cw; pos.y -= 0.01f * cw; } }
      pos += vec3(0.02f, -0.3f, 0.06f) * dn; rot += vec3(-0.75f, -0.15f, 0.2f) * dn;   // lowered: switching, the PDA, a med
      mat4 M = glm::translate(mat4(1), pos) * rotY(rot.y) * rotX(rot.x) * rotZ(rot.z);
      mat4 B(1);   // the gun's frame (x barrel, y up, z right; metres) -> view (barrel away from you), real size
      B[0] = vec4(0, 0, -1, 0); B[1] = vec4(0, 1, 0, 0); B[2] = vec4(1, 0, 0, 0);
      M = M * B;
      auto gv = [&](vec3 g) { return vec3(M * vec4(g, 1)); };
      VR.draw(gm->gl, M * gm->matrix(), Lt, vec4(0), itemGlints(S) ? 1.f : 0.f);
      // the right fist on the grip (off it to work a bolt)
      vec3 rh = GI->grip + vec3(0.002f, 0.004f, 0);
      if (bolt && cw > 0) { const vec3 bh = GI->grip + vec3(0.08f, 0.075f, 0.045f); rh = path(cyc, {{0.f, rh}, {0.2f, bh}, {0.32f, bh + vec3(0, 0.02f, 0.02f)}, {0.5f, bh + vec3(-0.08f, 0.02f, 0.02f)}, {0.65f, bh + vec3(0, 0.02f, 0.02f)}, {0.75f, bh}, {1.f, rh}}); }
      skinHand(0, gv(rh), Lt);
      // the left: under the handguard (on a pistol cupping the right fist), pumping, or busy with the reload / the jam
      const vec3 F = pistol ? gv(GI->grip + vec3(0.012f, -0.008f, -0.03f)) : gv(GI->fore + vec3(pump ? -0.1f * bump(cyc, 0.12f, 0.88f) : 0.f, 0.012f, 0));
      vec3 lh = F; bool magOn = false, shellOn = false;
      const vec3 Dv(-0.1f, -0.45f, -0.25f);   // down out of view: the pouch on the chest rig
      if (rw > 0.001f) {
        vec3 r;
        if (shells) {
          const vec3 P = gv(vec3(0.07f, -0.045f, 0)), Pd = gv(vec3(0.07f, -0.09f, 0)), Bt(-0.05f, -0.38f, -0.2f);
          r = path(u, {{0.f, A.relFirst ? F : P}, {0.35f, Bt}, {0.62f, Pd}, {0.82f, P}, {0.9f, gv(vec3(0.1f, -0.035f, 0))}, {1.f, P}});
          shellOn = u > 0.38f && u < 0.86f;
        } else {
          const vec3 well = pistol ? GI->grip + vec3(-0.012f, -0.075f, 0) : vec3(GI->grip.x + (GI->fore.x - GI->grip.x) * 0.42f, GI->grip.y - 0.02f, -0.01f);
          const vec3 W = gv(well), Wd = gv(well + vec3(0, -0.08f, 0));
          const vec3 C = pistol ? gv(vec3(-0.05f, 0.035f, -0.01f)) : gv(vec3(GI->grip.x + (GI->fore.x - GI->grip.x) * 0.3f, 0.03f, 0.045f));
          const vec3 Cb = pistol ? gv(vec3(-0.095f, 0.035f, -0.01f)) : gv(vec3(GI->grip.x + (GI->fore.x - GI->grip.x) * 0.3f - 0.08f, 0.03f, 0.045f));
          if (A.relEmpty) r = path(u, {{0.f, F}, {0.12f, W}, {0.2f, W}, {0.34f, Dv}, {0.48f, Dv}, {0.64f, Wd}, {0.72f, W}, {0.78f, W}, {0.84f, C}, {0.89f, Cb}, {0.92f, C}, {1.f, F}});
          else r = path(u, {{0.f, F}, {0.12f, W}, {0.2f, W}, {0.34f, Dv}, {0.48f, Dv}, {0.64f, Wd}, {0.72f, W}, {0.8f, W}, {1.f, F}});
          magOn = u > 0.46f && u < 0.72f;
        }
        lh = glm::mix(F, r, rw);
      }
      if (A.jamW > 0.001f && !pistol) {
        const float jp = std::fmod(A.jamT, 0.8f) / 0.8f;
        const vec3 C = gv(vec3(GI->grip.x + (GI->fore.x - GI->grip.x) * 0.3f, 0.03f, 0.045f)), Cb = C + vec3(M * vec4(-0.08f, 0, 0, 0));
        lh = glm::mix(lh, path(jp, {{0.f, C}, {0.4f, C}, {0.52f, Cb}, {0.6f, C}, {1.f, C}}), A.jamW);
      }
      skinHand(1, lh, Lt);
      if (magOn || shellOn) {   // the fresh mag (or shell) in the left hand, turned like the gun
        const int k = shellOn ? 2 : pistol ? 1 : 0;
        mat4 R = M; R[3] = vec4(lh + vec3(M * vec4(0, shellOn ? 0.012f : 0.035f, 0, 0)), 1);
        VR.draw(zonePropMesh(k), R * zonePropLocal(k), Lt);
      }
    }
  } else if (z && z->cat == "detector" && dn < 0.999f) {
    // ---- a detector up in the left hand, a bolt ready in the right: the throw winds back and lets go ----
    vec3 pos(0), rot(0); motion(0, sprint, pos, rot);
    pos += vec3(0, -0.3f, 0.05f) * dn - vec3(0, 0.04f, 0) * sprint;
    float sw = L.swing > 0 ? std::min(1.f, L.swing) : 0.f;
    if (tA == "bolt") sw = tU;
    pos.x -= 0.02f * bump(sw, 0.2f, 0.6f);
    const VoxelModel& dm = itemModel(S.id);
    const mat4 M = glm::translate(mat4(1), vec3(-0.11f, -0.12f, -0.3f) + pos) * rotY(0.32f + rot.y) * rotX(-0.22f + rot.x) * rotZ(rot.z) * scl(zoneHeldSize(S.id));
    if (dm.ready()) VR.draw(dm.gl, M * dm.matrix(), Lt);
    skinHand(1, vec3(M * vec4(-0.24f, -0.2f, -0.015f, 1)), Lt);
    vec2 lo, hi; float zf;
    if (dn < 0.05f && zoneDeviceScreen(S.id, lo, hi, zf)) { zoneDetRectOn = screenRect(proj, M, lo, hi, zf, (float)s.app.width, (float)s.app.height, zoneDetRect); zoneDetId = S.id; }
    bool bolts = false; for (const ItemStack& b : p->inv) if (!b.empty() && b.id == "bolt") { bolts = true; break; }
    const vec3 R0 = vec3(0.14f, -0.17f, -0.34f) + pos;
    const vec3 rh = path(sw, {{0.f, R0}, {0.28f, vec3(0.3f, -0.03f, -0.46f) + pos}, {0.45f, vec3(0.03f, -0.06f, -0.6f)}, {0.6f, vec3(-0.02f, -0.3f, -0.45f)}, {0.75f, R0 + vec3(0, -0.25f, 0)}, {1.f, R0}});
    const mat4 Fr = zoneFrame(rh, vec3(-0.8f, 0, -0.6f), vec3(-0.1f, 1, 0.2f - bump(sw, 0.2f, 0.6f) * 0.8f));
    skinHand(0, rh, Lt);
    if (bolts && (sw < 0.44f || sw > 0.72f)) { const VoxelModel& bm = itemModel("bolt"); if (bm.ready()) VR.draw(bm.gl, Fr * glm::translate(mat4(1), vec3(-0.004f, 0.045f, 0)) * scl(zoneHeldSize("bolt")) * bm.matrix(), Lt); }
  } else if (z && z->cat == "melee" && dn < 0.999f) {
    // ---- the knife: the handle in the right fist, the blade up and forward; the slash winds up, cuts down across, comes back ----
    vec3 pos(0), rot(0); motion(0, sprint, pos, rot);
    pos += vec3(0, -0.3f, 0.05f) * dn - vec3(0, 0.04f, 0) * sprint;
    float sw = L.swing > 0 ? std::min(1.f, L.swing) : 0.f;
    if (tA == "slash") sw = tU;
    const vec3 o = pos + path(sw, {{0.f, vec3(0.17f, -0.2f, -0.36f)}, {0.18f, vec3(0.25f, -0.08f, -0.3f)}, {0.5f, vec3(-0.1f, -0.3f, -0.45f)}, {1.f, vec3(0.17f, -0.2f, -0.36f)}});
    const vec3 d = glm::normalize(path(sw, {{0.f, vec3(-0.16f, 0.6f, -1.f)}, {0.18f, vec3(0.15f, 1.f, -0.3f)}, {0.5f, vec3(-1.f, -0.45f, -0.5f)}, {1.f, vec3(-0.16f, 0.6f, -1.f)}}));
    skinHand(0, o, Lt);
    const VoxelModel& km = itemModel(S.id);
    if (km.ready()) {
      const vec3 cam = glm::normalize(-o);
      const vec3 kx = d, kz = -glm::normalize(cam - d * glm::dot(cam, d)), ky = glm::cross(kz, kx);   // x along the knife, y its spine, z the flat
      const float ks = zoneHeldSize(S.id);
      mat4 K(1);
      K[0] = vec4(kx * ks, 0); K[1] = vec4(ky * ks, 0); K[2] = vec4(kz * ks, 0); K[3] = vec4(o + kx * ((0.304f - 0.06f) * ks), 1);
      VR.draw(km.gl, K * km.matrix(), Lt, vec4(0), itemGlints(S) ? 1.f : 0.f);
    }
  } else if (!S.empty() && dn < 0.999f) {
    // ---- anything else: in the right fist at its real size ----
    vec3 pos(0), rot(0); motion(0, sprint, pos, rot);
    pos += vec3(0, -0.3f, 0.05f) * dn - vec3(0, 0.04f, 0) * sprint;
    const VoxelModel& im = itemModel(S.id);
    const vec3 rh = vec3(0.15f, -0.18f, -0.36f) + pos + vec3(-0.03f, 0.04f, -0.04f) * (L.swing > 0 ? std::sin(std::min(1.f, L.swing) * PI) : 0.f);
    const mat4 Fr = zoneFrame(rh, vec3(-0.8f, 0, -0.6f), vec3(-0.1f, 1, 0.2f));
    skinHand(0, rh, Lt);
    const float sz = zoneHeldSize(S.id);
    if (im.ready()) VR.draw(im.gl, Fr * glm::translate(mat4(1), vec3(-0.02f, 0.015f + sz * 0.4f, -0.01f)) * rotY(0.6f) * scl(sz) * im.matrix(), Lt);
  }

  // ---- using a med / food: the item comes up to you (Misery) ----
  if (useU >= 0 && !A.useId.empty()) {
    const std::string& id = A.useId;
    enum { EAT, DRINK, BANDAGE, INJECT, PILLS } kind = EAT;
    if (id == "bandage") kind = BANDAGE;
    else if (id == "antirad" || id == "atlas" || id.rfind("medkit", 0) == 0) kind = INJECT;
    else if (id == "psyblock") kind = PILLS;
    else if (id == "vodka" || id == "energy" || id == "water") kind = DRINK;
    const std::string shown = kind == INJECT && id.rfind("medkit", 0) == 0 ? "antirad" : id;   // (a medkit's injector)
    const VoxelModel& im = itemModel(shown);
    const float sz = zoneHeldSize(shown), u = useU;
    vec3 pos(0), rot(0); motion(0, 0, pos, rot);
    const vec3 low(0.15f, -0.5f, -0.3f), lowL(-0.15f, -0.5f, -0.3f);
    vec3 rh; float tilt = 0;
    if (kind == BANDAGE || kind == INJECT) {   // the left forearm held up across; the right works on it just below the wrist
      const vec3 Lf = vec3(-0.05f, -0.08f, -0.32f) + pos, C = Lf + vec3(-0.02f, -0.06f, 0.02f);
      skinHand(1, path(u, {{0.f, lowL}, {0.15f, Lf}, {0.85f, Lf}, {1.f, lowL}}), Lt);
      if (kind == BANDAGE) {
        const float a = sat((u - 0.22f) / 0.56f) * 3 * TAU;
        const vec3 ring = C + vec3(std::cos(a) * 0.065f, -0.03f * sat((u - 0.22f) / 0.56f), std::sin(a) * 0.065f);
        rh = path(u, {{0.f, low}, {0.2f, C + vec3(0.065f, 0, 0)}, {0.78f, ring}, {0.8f, ring}, {1.f, low}});
        if (u > 0.2f && u < 0.78f) rh = ring;
      } else {
        rh = path(u, {{0.f, low}, {0.35f, C + vec3(0.1f, 0.02f, 0)}, {0.5f, C + vec3(0.035f, 0, 0)}, {0.7f, C + vec3(0.035f, 0, 0)}, {0.82f, C + vec3(0.12f, 0.02f, 0)}, {1.f, low}});
        tilt = -1.2f;
      }
    } else {   // up to the mouth: bites, a long drink with the bottle tipped up, or pills knocked back
      const vec3 Mo = (kind == DRINK ? vec3(0.05f, -0.2f, -0.36f) : vec3(0.05f, -0.17f, -0.38f)) + pos;
      rh = path(u, {{0.f, low}, {0.25f, Mo}, {0.8f, Mo}, {1.f, low}});
      if (kind == EAT) rh.z += 0.07f * (bump(u, 0.3f, 0.42f) + bump(u, 0.46f, 0.58f) + bump(u, 0.62f, 0.74f));
      if (kind == DRINK || kind == PILLS) { tilt = (kind == DRINK ? 0.6f : 0.7f) * seg(u, 0.28f, 0.42f) * (1 - seg(u, 0.72f, 0.82f)); zoneFpCam[0] += tilt * 0.05f; }
    }
    skinHand(0, rh, Lt);
    if (im.ready()) {
      const mat4 Fr = zoneFrame(rh, vec3(-0.8f, 0, -0.6f), vec3(-0.1f, 1, 0.2f));
      VR.draw(im.gl, Fr * glm::translate(mat4(1), vec3(-0.02f, 0.015f + sz * 0.4f, -0.01f)) * rotY(0.6f) * rotX(tilt) * scl(sz) * im.matrix(), Lt);
    }
  }
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
    int t = drawZoneFp(L, s, f, proj);
    if (!(t & 1) && prev) t |= prev(L, s, f, proj);
    return t;
  };
}

} // namespace dq
