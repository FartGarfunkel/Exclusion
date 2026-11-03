// render/zone_creature_draw.cpp — the Zone's people and mutants beyond their rigs (see zone_creature_draw.h).
// Everything here is procedural and time-based: per-stalker blend state (sitting, a knee, the aim) lives in a small map keyed
// by entity id, eased with the real frame time, so it looks the same at 5 fps and 240.
#include "render/zone_creature_draw.h"
#include "render/zone_gun_models.h"
#include "render/char_layer.h"
#include "render/creature_layer.h"
#include "render/be_scan.h"
#include "assets/item_models.h"
#include "client/session.h"
#include "sim/creature.h"
#include "sim/creature_rig.h"
#include "sim/hallow.h"
#include "sim/player.h"
#include "sim/zone.h"
#include "common/fs.h"
#include <chrono>
#include <unordered_map>

namespace dq {

namespace {
float sat(float x) { return clampv(x, 0.f, 1.f); }
float smooth(float x) { x = sat(x); return x * x * (3 - 2 * x); }
float seg(float u, float a, float b) { return smooth((u - a) / (b - a)); }
// eased keyframes: the value at u from (time, value) pairs in order
vec3 path(float u, std::initializer_list<std::pair<float, vec3>> k) {
  const std::pair<float, vec3>* a = k.begin();
  if (u <= a->first) return a->second;
  for (const auto* b = a + 1; b != k.end(); a = b, ++b) if (u <= b->first) return glm::mix(a->second, b->second, seg(u, a->first, b->first));
  return (k.end() - 1)->second;
}
// a value of data.hw (sent to guests): numbers and flags
float hwf(const Creature& m, const char* k) {
  if (!m.data.is_object()) return 0;
  auto h = m.data.find("hw"); if (h == m.data.end() || !h->is_object()) return 0;
  auto v = h->find(k); if (v == h->end()) return 0;
  return v->is_boolean() ? (v->get<bool>() ? 1.f : 0.f) : v->is_number() ? v->get<float>() : 0.f;
}
double nowS() { using namespace std::chrono; return duration<double>(steady_clock::now().time_since_epoch()).count(); }
struct Mem { float sit = 0, kneel = 0, aim = 0; double seen = 0; bool init = false; };
Mem& memOf(const Creature& m, float& dt) {
  static std::unordered_map<u32, Mem> M; static int n = 0;
  const double t = nowS();
  if (++n % 1024 == 0) for (auto it = M.begin(); it != M.end();) { if (t - it->second.seen > 10) it = M.erase(it); else ++it; }
  Mem& e = M[m.id];
  dt = e.init ? (float)clampv(t - e.seen, 0.0, 0.25) : 10.f;
  e.seen = t;
  return e;
}

// the turns (x, y; rot() order y then x) that point a limb hanging along -y down the unit direction d
vec3 limbAngles(vec3 d) {
  const float ax = std::acos(clampv(-d.y, -1.f, 1.f)), s = std::sin(ax);
  return vec3(ax, s > 1e-4f ? std::atan2(-d.x, -d.z) : 0.f, 0);
}
mat4 rotYX(const mat4& m, vec3 r) { mat4 o = m; if (r.y) o = glm::rotate(o, r.y, vec3(0, 1, 0)); if (r.x) o = glm::rotate(o, r.x, vec3(1, 0, 0)); return o; }
// two bones from the shoulder S to the target T (body px): upper a, lower c, the elbow bent toward `pole`
void armIK(vec3 S, vec3 T, float a, float c, vec3 pole, vec3& upA, vec3& loA) {
  vec3 to = T - S; float L = glm::length(to);
  const vec3 dir = L > 1e-4f ? to / L : vec3(0, -1, 0);
  L = clampv(L, std::fabs(a - c) + 0.05f, a + c - 0.02f);
  const float A = std::acos(clampv((a * a + L * L - c * c) / (2 * a * L), -1.f, 1.f));
  vec3 pp = pole - dir * glm::dot(pole, dir);
  pp = glm::length(pp) > 1e-4f ? glm::normalize(pp) : vec3(0, -1, 0);
  const vec3 u = dir * std::cos(A) + pp * std::sin(A);
  const vec3 E = S + u * a, f = glm::normalize(S + dir * L - E);
  upA = limbAngles(u);
  const mat3 Mu = mat3(rotYX(mat4(1), upA));
  loA = limbAngles(glm::transpose(Mu) * f);
}
// one arm (0 right, 1 left) of the posed character reaching T (body px)
void ikArm(const CharModel& M, CharRig& R, int side, vec3 T, vec3 pole) {
  const int up = side ? CP_ARML_U : CP_ARMR_U, dn = up + 1;
  const vec3 pu = M.pivot[up], S(pu.x * M.body.build, pu.y - 12, pu.z);
  const float a = std::max(1.f, pu.y - M.pivot[dn].y);
  vec3 ua, la;
  armIK(S, T, a, 6.2f, pole, ua, la);
  R.part[up] = rotYX(glm::translate(R.bodyRaw, S), ua);
  R.part[dn] = rotYX(glm::translate(R.part[up], M.pivot[dn] - pu), la);
}
const VoxelMeshGL& flashMesh() {
  static VoxelMeshGL m; static bool built = false;
  if (!built) {
    built = true;
    VoxelGrid g(ivec3(5, 5, 9));
    for (int z = 0; z < 9; z++) for (int y = 0; y < 5; y++) for (int x = 0; x < 5; x++) {
      const float r = std::hypot(x - 2.f, y - 2.f), lim = z < 3 ? 2.4f : z < 6 ? 1.6f : 0.9f;
      if (r <= lim && ((x + y + z) % 3 != 0 || r < 1)) g.set(x, y, z, voxRGBA(255, r < 1 ? 240 : 190, r < 1 ? 160 : 70, VOXEL_GLOW));
    }
    VoxelMeshData md; buildVoxelMesh(g, md); m.upload(md);
  }
  return m;
}
const zone::ZItem* gunItem(const Creature& m) {
  if (m.heldItem.empty()) return nullptr;
  const zone::ZItem* z = zone::zi(m.heldItem);
  return z && z->isGun ? z : nullptr;
}

// ---- the gun in a person's hands (stalkers and co-op players) ----
struct HumanGun { const GunModelInfo* GI = nullptr; float aim = 0, shot = 0, reload = -1, pitch = 0, walk = 0, walkAmt = 0, flinch = 0, sprint = 0; };
void humanGunPose(const HumanGun& H, const CharModel& M, CharRig& R, mat4& gunFrame, bool* magOn, mat4* magFrame, int* magKind) {
  const GunModelInfo& GI = *H.GI;
  const bool pistol = GI.pistol;
  const float e = smooth(H.aim), shot = smooth(H.shot), wa = std::min(1.f, H.walkAmt);
  // the low ready (model px: x right, y up from the hips, z back): barrel down and across, bobbing with the stride
  vec3 gl = pistol ? vec3(3.4f, 3.6f, -2.6f) : vec3(2.6f, 5.4f, -3.4f);
  float pl = pistol ? -1.05f : -0.6f, yl = pistol ? 0.1f : 0.5f, rl = pistol ? 0.f : -0.25f;
  gl.y += (std::cos(H.walk * 2) * 0.35f - 0.1f) * wa; yl += std::sin(H.walk) * 0.07f * wa; pl += std::cos(H.walk * 2) * 0.04f * wa;
  if (H.sprint > 0 && !pistol) { const float s = smooth(H.sprint); gl = glm::mix(gl, vec3(1.4f, 6.8f, -3.8f), s); pl += (-0.05f - pl) * s; yl += (1.2f - yl) * s; rl += (-1.0f - rl) * s; }
  // shouldered and aimed (the pitch turns it about the shoulders), kicked back and up by each shot
  const vec3 ga = pistol ? vec3(1.0f, 8.8f, -8.0f + shot * 1.2f) : vec3(2.1f, 8.4f, -3.8f + shot * 0.9f);
  const float pa = clampv(H.pitch, -0.7f, 0.7f) + shot * 0.14f;
  vec3 grip = glm::mix(gl, ga, e);
  float pitch = pl + (pa - pl) * e, yaw = yl * (1 - e), roll = rl * (1 - e);
  // the reload: brought in and canted, the mag well toward the left hand
  float rw = 0;
  if (H.reload >= 0) {
    rw = seg(H.reload, 0, 0.12f) * (1 - seg(H.reload, 0.86f, 1));
    grip = glm::mix(grip, pistol ? vec3(1.2f, 6.4f, -5.0f) : vec3(1.4f, 6.0f, -4.8f), rw);
    pitch += (-0.3f - pitch) * rw; yaw += (0.45f - yaw) * rw; roll += (0.75f - roll) * rw;
  }
  pitch += H.flinch * 0.3f; grip.z += H.flinch * 0.8f;
  const float sh = 9.5f;
  mat4 G = glm::translate(mat4(1), vec3(0, sh, 0)) * glm::rotate(mat4(1), pitch, vec3(1, 0, 0)) * glm::translate(mat4(1), vec3(0, -sh, 0));
  G = glm::translate(G, grip);
  G = glm::rotate(G, yaw, vec3(0, 1, 0)); G = glm::rotate(G, roll, vec3(0, 0, 1));
  G = glm::rotate(G, PI / 2, vec3(0, 1, 0));    // the gun's +X (muzzle) to the body's front (-z), its +Z to the right
  G = glm::scale(G, vec3(1.f / CHAR_PX));        // gun blocks (metres) -> model px: real size
  gunFrame = R.bodyRaw * G;
  const vec3 hr = vec3(G * vec4(GI.grip, 1));
  vec3 hl = pistol ? vec3(G * vec4(GI.grip + vec3(0.01f, -0.03f, -0.05f), 1)) : vec3(G * vec4(GI.fore, 1));
  if (H.reload >= 0) {   // the left hand: to the mag, yank it, down to the belt pouch, a fresh one up and in, back
    const vec3 well = pistol ? GI.grip + vec3(-0.012f, -0.075f, 0) : vec3(GI.grip.x + (GI.fore.x - GI.grip.x) * 0.45f, GI.grip.y - 0.04f, 0);
    const vec3 W = vec3(G * vec4(well, 1)), Bt(-2.8f, 1.2f, -3.2f), Wd = W + vec3(0, -2.2f, 0.3f);
    const float u = H.reload;
    hl = path(u, {{0.f, hl}, {0.14f, W}, {0.22f, W}, {0.38f, Bt}, {0.5f, Bt}, {0.68f, Wd}, {0.78f, W}, {0.84f, W}, {1.f, hl}});
    if (magOn && u > 0.42f && u < 0.77f) {
      mat4 F = glm::translate(mat4(1), hl + vec3(0, 0.6f, 0)); F = F * mat4(mat3(G));
      *magOn = true; *magFrame = R.bodyRaw * F; *magKind = pistol ? 1 : 0;
    }
  }
  ikArm(M, R, 0, hr, vec3(0.7f, -1, 0.25f));
  ikArm(M, R, 1, hl, vec3(-0.7f, -1, 0.25f));
}

// the guitar across the lap (body px: the sound hole over the right thigh, the neck up to the left, its top facing out)
mat4 guitarFrame() {
  const vec3 X = glm::normalize(vec3(-1, 0.42f, -0.28f)), Zf0(0.1f, 0.2f, -1);
  const vec3 Zf = glm::normalize(Zf0 - X * glm::dot(Zf0, X)), Y = glm::cross(Zf, X);
  mat4 F(1); F[0] = vec4(X, 0); F[1] = vec4(Y, 0); F[2] = vec4(Zf, 0); F[3] = vec4(1.6f, 3.2f, -5.2f, 1);
  return F;
}

// the last word on a stalker's angles (CharPose::adjust): on the ground at the fire, a knee, a flinch, the fall
void npcAdjust(CharAngles& A, const CharPose& ps, const void* ctx) {
  const ZoneNpcPose& Z = *(const ZoneNpcPose*)ctx;
  const float t = Z.time;
  auto to = [](vec3& a, vec3 b, float k) { a += (b - a) * k; };
  if (Z.sit > 0.001f) {   // sitting on the ground, knees up (the body is lowered in zoneNpcCharPose)
    const float k = smooth(Z.sit);
    to(A.legR, vec3(2.0f, 0, 0.16f), k); to(A.legL, vec3(2.0f, 0, -0.16f), k);
    to(A.shinR, vec3(-2.25f, 0, 0), k); to(A.shinL, vec3(-2.25f, 0, 0), k);
    A.body.x += -0.12f * k; A.lift *= 1 - k;
    vec3 aR(0.8f, -0.12f, 0.1f), aL(0.8f, 0.12f, -0.1f), fR(0.65f, 0, 0), fL(0.65f, 0, 0);   // resting: forearms on the knees
    if (Z.act == 1) {   // hands out to the fire, rubbed now and then
      const float r = std::sin(t * 4.2f) * 0.05f * std::max(0.f, std::sin(t * 0.7f));
      aR = vec3(1.3f + r, -0.32f, 0.06f); aL = vec3(1.3f - r, 0.32f, -0.06f); fR = fL = vec3(0.3f, 0, 0); A.body.x += -0.1f * k;
    } else if (Z.act == 2) { aR = glm::mix(aR, vec3(1.75f, -0.5f, 0.05f), Z.actK); fR = glm::mix(fR, vec3(1.3f, 0, 0), Z.actK); }
    to(A.armR, aR, k); to(A.armL, aL, k); to(A.foreR, fR, k); to(A.foreL, fL, k);
    A.head.x += (Z.act == 0 ? -0.14f + std::sin(t * 0.35f) * 0.06f : Z.act == 3 ? -0.3f : -0.1f) * k;
    A.head.y += std::sin(t * 0.21f) * 0.25f * k;
  }
  if (Z.kneel > 0.001f) {   // a knee in cover: the left foot forward, the right knee down
    const float k = smooth(Z.kneel);
    to(A.legL, vec3(1.45f, 0, -0.06f), k); to(A.shinL, vec3(-1.45f, 0, 0), k);
    to(A.legR, vec3(-0.12f, 0, 0.06f), k); to(A.shinR, vec3(-1.55f, 0, 0), k);
    A.body.x *= 1 - k; A.lift *= 1 - k;
  }
  if (Z.flinch > 0) { A.body.x += 0.22f * Z.flinch * Z.hitDir; A.head.x += 0.3f * Z.flinch * Z.hitDir; A.body.y += 0.18f * Z.flinch; }
  if (Z.fallT > 0) {   // the knees go, then he goes over (zoneNpcCharPose turns the body), limbs flung out
    const float c = seg(Z.fallT, 0, 0.22f), k = seg(Z.fallT, 0.15f, 0.8f), d = Z.hitDir, s = (ps.seed & 1) ? 1.f : -1.f;
    A.legR = vec3(0.65f * c * (1 - k) + 0.12f * k, 0, 0.12f * k); A.legL = vec3(0.55f * c * (1 - k) - 0.05f * k, 0, -0.2f * k);
    A.shinR = vec3(-1.2f * c * (1 - k) - 0.3f * k, 0, 0); A.shinL = vec3(-1.0f * c * (1 - k) - 0.12f * k, 0, 0);
    A.armR = vec3(0.4f * c + (d > 0 ? 1.0f : 0.15f) * k, 0, 0.3f + 1.0f * k); A.armL = vec3(0.3f * c + (d > 0 ? 1.9f : 0.25f) * k, 0, -0.3f - 1.1f * k);
    A.foreR = vec3(0.4f * k, 0, 0); A.foreL = vec3(0.8f * k, 0, 0);
    A.body = vec3(-0.25f * c * (1 - k), 0, 0);
    A.head = vec3(-0.2f * c * (1 - k) + (d > 0 ? 0.25f : -0.2f) * k, 0.9f * k * s, 0);
    A.lift = 0;
  }
}

VoxelGrid propGrid(int kind) {
  if (kind == 0) {   // a rifle mag: 1 cm voxels, curved forward toward its foot, brass at the lips
    VoxelGrid g(ivec3(10, 20, 3));
    for (int y = 0; y < 20; y++) { const int sh = (int)std::lround((19 - y) * (19 - y) / 90.f); for (int x = 0; x < 6; x++) for (int z = 0; z < 3; z++) g.set(x + sh, y, z, y == 19 ? voxRGBA(176, 140, 64) : y == 0 ? voxRGBA(28, 29, 31) : voxRGBA(44, 46, 50)); }
    return g;
  }
  if (kind == 1) {   // a pistol mag
    VoxelGrid g(ivec3(3, 11, 2));
    for (int y = 0; y < 11; y++) for (int x = 0; x < 3; x++) for (int z = 0; z < 2; z++) g.set(x, y, z, y == 10 ? voxRGBA(176, 140, 64) : voxRGBA(36, 36, 40));
    return g;
  }
  if (kind == 2) {   // a 12 gauge shell: red hull, brass head
    VoxelGrid g(ivec3(7, 2, 2));
    for (int x = 0; x < 7; x++) for (int y = 0; y < 2; y++) for (int z = 0; z < 2; z++) g.set(x, y, z, x < 2 ? voxRGBA(196, 156, 66) : voxRGBA(168, 32, 26));
    return g;
  }
  // the guitar: 2.5 cm voxels, a metre long; two bouts and a waist, the neck along +x, its top at z = 3
  VoxelGrid g(ivec3(40, 16, 4));
  for (int x = 0; x < 20; x++) for (int y = 0; y < 16; y++) {
    const float r1 = std::hypot(x - 6.f, y - 7.5f), r2 = std::hypot(x - 14.5f, y - 7.5f);
    if (r1 > 6.6f && r2 > 5.2f) continue;
    for (int z = 0; z < 4; z++) {
      u32 c = z == 3 ? voxRGBA(198, 142, 74) : voxRGBA(104, 58, 30);
      if (z == 3 && std::hypot(x - 11.f, y - 7.5f) < 1.7f) c = voxRGBA(22, 15, 10);   // the sound hole
      if (z == 3 && x == 4 && y >= 5 && y <= 10) c = voxRGBA(42, 26, 16);            // the bridge
      if (z == 3 && (r1 > 5.9f && r2 > 4.5f)) c = voxRGBA(120, 70, 36);              // binding
      g.set(x, y, z, c);
    }
  }
  for (int x = 19; x < 36; x++) for (int y = 6; y <= 8; y++) { g.set(x, y, 1, voxRGBA(112, 68, 36)); g.set(x, y, 2, voxRGBA(46, 33, 25)); }
  for (int x = 36; x < 40; x++) for (int y = 5; y <= 9; y++) for (int z = 1; z < 3; z++) g.set(x, y, z, (y == 5 || y == 9) && (x & 1) ? voxRGBA(205, 205, 195) : voxRGBA(62, 40, 24));
  for (int x = 5; x < 37; x++) if (x < 9 || x > 12) g.set(x, 7, 3, voxRGBA(214, 214, 204));   // the strings
  return g;
}
} // namespace

std::string zoneNpcSkin(const Creature& m) {
  const std::string& base = m.T->skin;
  if (base.size() < 5 || base.compare(base.size() - 4, 4, ".png") != 0) return base;
  // how many looks the faction has (z_npc_x.png, z_npc_x_1.png ...: tools/zone/install_skins.py), counted once
  static std::unordered_map<std::string, int> count;
  auto ci = count.find(base);
  if (ci == count.end()) {
    int k = 1; std::error_code ec;
    while (k < 16) { const std::string q = base.substr(0, base.size() - 4) + "_" + std::to_string(k) + ".png"; if (!fs::exists(dataDir() / q, ec) && !inDataArchive(dataDir() / q)) break; k++; }   // (the archive is keyed by the path under data/)
    ci = count.emplace(base, k).first;
  }
  const int n = (int)std::lround(hwGet(m, "sk", 0)) % ci->second;
  if (n <= 0) return base;
  const std::string v = base.substr(0, base.size() - 4) + "_" + std::to_string(n) + ".png";
  static std::unordered_map<std::string, bool> have;
  auto it = have.find(v);
  if (it == have.end()) { std::error_code ec; it = have.emplace(v, fs::exists(dataDir() / v, ec) || inDataArchive(dataDir() / v)).first; }
  return it->second ? v : base;
}

bool zoneNpcCharPose(const Creature& m, CharPose& ps, mat4& base, vec4& tint, ZoneNpcPose& Z) {
  if (m.type.rfind("z_npc_", 0) != 0) return false;
  Z.zone = true;
  // (test hook: data.animTest = {sit, act, kneel, aim, reload, fall, hd} freezes a pose for screenshots)
  const json* T = m.data.is_object() && m.data.contains("animTest") && m.data["animTest"].is_object() ? &m.data["animTest"] : nullptr;
  const float fallT = T ? T->value("fall", m.deathTime) : m.deathTime;
  const bool corpse = m.data.is_object() && m.data.value("corpse", false), dead = fallT > 0;
  float dt = 10; Mem tmp; Mem& e = corpse ? tmp : memOf(m, dt);
  Z.time = (float)m.age + (float)(m.id % 97) * 3.1f;
  const bool aimNow = !dead && (m.aiming || m.state == "aim");
  const float cr = hwf(m, "cr"), rl = hwf(m, "rl");
  const bool kneelNow = !dead && cr > 0.5f && m.walkAmt < 0.2f, sneakNow = !dead && cr > 0.5f && !kneelNow;
  auto toward = [&](float& v, float want, float rate) { v += (want - v) * std::min(1.f, dt * rate); if (std::fabs(v - want) < 0.002f) v = want; };
  toward(e.sit, m.sitting && !dead ? 1.f : 0.f, m.sitting ? 2.2f : 6.f);   // down slowly; up fast when alarmed
  toward(e.kneel, kneelNow ? 1.f : 0.f, 6.f);
  toward(e.aim, aimNow ? 1.f : 0.f, 7.f);
  e.init = true;
  Z.sit = e.sit; Z.kneel = e.kneel; Z.aim = e.aim;
  Z.reload = rl > 0 ? clampv(1 - rl / 2.4f, 0.f, 1.f) : -1.f;
  Z.hitDir = hwf(m, "hd") < 0 ? -1.f : 1.f;
  Z.flinch = !dead && m.hurtTime > 0 ? std::sin(clampv(m.hurtTime / 0.35f, 0.f, 1.f) * PI) : 0.f;
  if (T) { Z.sit = T->value("sit", Z.sit); Z.kneel = T->value("kneel", Z.kneel); Z.aim = T->value("aim", Z.aim); Z.reload = T->value("reload", Z.reload); if (T->contains("hd")) Z.hitDir = T->value("hd", 1.f) < 0 ? -1.f : 1.f; }
  ps.sneak = sneakNow; ps.seed = m.id; ps.sit = false; ps.sleep = m.sleeping && !dead;
  if (!aimNow && Z.sit < 0.05f && !ps.sleep && !dead) {   // standing about: the stock idles, a slow look round now and then
    ps.idleT = std::fmod(Z.time * 0.9f, 26.f);
    const float cyc = std::fmod(Z.time * 0.13f, 1.f), g = cyc < 0.3f ? std::sin(cyc / 0.3f * PI) : 0.f;
    ps.headYaw += ((m.id >> 2) & 1 ? 0.8f : -0.8f) * g * g * (1 - std::min(1.f, m.walkAmt * 3));
  }
  if (Z.sit > 0.3f) {   // at the fire: the guitar (the brain says who), else each his own round of warming, eating, resting
    if (hwf(m, "gt") > 0.5f || (T && T->value("act", 0) == 3)) Z.act = 3;
    else {
      float c = std::fmod(Z.time + (float)(m.id % 13) * 5.7f, 44.f);
      Z.act = c < 18 ? 1 : c < 27 ? 2 : 0;
      if (T && T->contains("act")) { Z.act = T->value("act", 0); c = 18.9f; }
      if (Z.act == 2) { const float b = std::fmod(c - 18, 3.f); Z.actK = smooth(std::min(1.f, (b < 1.8f ? std::sin(b / 1.8f * PI) : 0.f) * 1.5f)); }
    }
  }
  float drop = 0.5f * smooth(Z.sit) + 0.33f * smooth(Z.kneel);
  if (ps.sleep) drop = 0.28f;   // (the stock sleep lies on a bed)
  mat4 F(1);
  if (dead) {   // over backward or onto the face, away from the shot, about the heels / toes; a small bounce as he lands
    Z.fallT = fallT; ps.deathT = 0;
    const float c = seg(Z.fallT, 0, 0.22f), k0 = sat((Z.fallT - 0.12f) / 0.6f), k = k0 * k0;
    const float bounce = Z.fallT > 0.72f ? std::sin(sat((Z.fallT - 0.72f) / 0.18f) * PI) * 0.07f : 0.f;
    const float d = Z.hitDir, ang = (k * (PI / 2 - 0.04f) - bounce) * d, pz = d > 0 ? 0.16f : -0.2f;
    const float yaw = ps.hasBodyYaw ? ps.bodyYaw : ps.yaw;
    drop += 0.22f * c * (1 - k);
    F = glm::rotate(mat4(1), yaw, vec3(0, 1, 0));
    F = glm::translate(F, vec3(0, 0, pz)); F = glm::rotate(F, ang, vec3(1, 0, 0)); F = glm::translate(F, vec3(0, 0, -pz));
    F = F * glm::rotate(mat4(1), -yaw, vec3(0, 1, 0));
  }
  base = glm::translate(base, vec3(0, -drop, 0)) * F;
  if (fallT > 0.3f || T) tint = vec4(0);   // the hurt flash, not a red corpse
  ps.adjust = npcAdjust; ps.adjustCtx = &Z;
  return true;
}

bool zoneNpcHands(const Creature& m, const CharModel& M, CharRig& R, ZoneNpcPose& Z, mat4& gunFrame, float& flash) {
  flash = 0;
  if (!Z.zone) return false;
  if (Z.sit > 0.5f && Z.act == 3 && m.deathTime <= 0) {   // the guitar: the right hand strums over the hole, the left on the neck
    const mat4 Gt = guitarFrame();
    const vec3 X(Gt[0]), Y(Gt[1]), Zf(Gt[2]), O(Gt[3]);
    const float t = Z.time, chord = (float)((int)(t / 2.2f) % 3);
    ikArm(M, R, 0, O - X * 1.6f + Zf * 1.3f + Y * (std::sin(t * 9.f) * 0.7f), vec3(0.8f, -1, 0.4f));
    ikArm(M, R, 1, O + X * (9.5f + chord * 0.8f) + Zf * 0.6f - Y * 0.6f, vec3(-0.5f, -1, 0.1f));
    return false;
  }
  if (!gunItem(m) || m.deathTime > 0.4f || Z.fallT > 0.4f || m.sleeping || Z.sit > 0.3f) return false;   // (slung while he sits at the fire or sleeps)
  const ItemStack st(m.heldItem, 1);
  HumanGun H; H.GI = zoneGunInfo(st);
  if (!H.GI || !zoneGunModel(st)) return false;
  H.aim = Z.aim; H.shot = clampv(m.swingT / 0.12f, 0.f, 1.f); H.reload = Z.reload; H.pitch = m.headPitch;
  H.walk = m.walk * 1.4f; H.walkAmt = m.walkAmt; H.flinch = Z.flinch; H.sprint = sat((m.speedNow - 3.6f) / 1.5f) * (1 - Z.aim);
  flash = Z.aim > 0.5f && H.shot > 0.45f ? 1.f : 0.f;
  humanGunPose(H, M, R, gunFrame, &Z.magInHand, &Z.magFrame, &Z.magKind);
  return true;
}

void drawZoneNpcHands(const Creature& m, const CharRig& R, const ZoneNpcPose& Z, bool gun, const mat4& gunFrame, float flash, VoxelLight light, vec4 tint) {
  VoxelRenderer& VR = voxelRenderer();
  if (gun) {
    const ItemStack st(m.heldItem, 1);
    const VoxelModel* gm = zoneGunModel(st);
    const GunModelInfo* GI = zoneGunInfo(st);
    if (!gm || !GI) return;
    VR.draw(gm->gl, gunFrame * gm->matrix(), light, tint);
    if (Z.magInHand) VR.draw(zonePropMesh(Z.magKind), Z.magFrame * zonePropLocal(Z.magKind), light, tint);
    if (flash > 0) {
      mat4 F = glm::translate(gunFrame, vec3(GI->front + 0.02f, GI->sightY * 0.35f, 0));
      F = glm::rotate(F, PI / 2, vec3(0, 1, 0));   // the mesh runs along +z: turn it out of the muzzle (+X)
      F = glm::rotate(F, (float)(m.id % 7) * 0.9f + m.age * 30.f, vec3(0, 0, 1));
      F = glm::scale(F, vec3(0.05f));
      F = glm::translate(F, vec3(-2.5f, -2.5f, 0));
      VR.draw(flashMesh(), F, VoxelLight{1, 1});
    }
    return;
  }
  if (Z.sit <= 0.5f || m.deathTime > 0) return;
  if (Z.act == 3) VR.draw(zonePropMesh(3), R.bodyRaw * guitarFrame() * glm::scale(mat4(1), vec3(1.f / CHAR_PX)) * zonePropLocal(3), light, tint);
  if (Z.act == 2) {   // his supper, at its real size in the right fist
    const std::string id = (m.id & 1) ? "bread" : "canned_stew";
    const VoxelModel& im = itemModel(id);
    if (im.ready()) VR.draw(im.gl, R.part[CP_ARMR_D] * glm::translate(mat4(1), vec3(0, -6.3f, -0.6f)) * glm::rotate(mat4(1), -PI / 2, vec3(1, 0, 0)) * glm::scale(mat4(1), vec3(zoneHeldSize(id) / CHAR_PX)) * im.matrix(), light, tint);
  }
}

void zonePlayerHands(const Player& p, const CharPose& ps, CharWear& wear, CharDrawOpts& o) {
  if (p.selected < 0 || p.selected >= (int)p.inv.size() || wear.held.empty()) return;
  const ItemStack& h = p.inv[p.selected];
  const zone::ZItem* z = h.empty() ? nullptr : zone::zi(h.id);
  if (!z) return;
  const ItemStack st = h; const VoxelLight L = o.light; const vec4 tint = o.tint;
  if (z->isGun) {
    const zone::GunHud G = zone::gunHud(p);
    HumanGun H; H.aim = G.ads ? 1.f : 0.f; H.pitch = p.pitch; H.walk = ps.walk; H.walkAmt = ps.walkAmt; H.sprint = p.sprinting ? 1.f : 0.f;
    if (G.reloading && z->gun.reload > 0) H.reload = sat(1 - G.reloadT / (z->gun.reload * (z->gun.slowReload ? 1.f : 1.15f)));
    if (!zoneGunInfo(st) || !zoneGunModel(st)) return;   // (built on first use: the stock held item this frame)
    wear.held.clear(); wear.heldModel.clear();
    o.rigHook = [st, H, L, tint](const CharModel& M, CharRig& R) {
      const GunModelInfo* GI = zoneGunInfo(st); const VoxelModel* gm = zoneGunModel(st);
      if (!GI || !gm) return;
      HumanGun h2 = H; h2.GI = GI; mat4 gf(1), mf(1); bool mo = false; int mk = 0;
      humanGunPose(h2, M, R, gf, &mo, &mf, &mk);
      VoxelRenderer& VR = voxelRenderer();
      VR.draw(gm->gl, gf * gm->matrix(), L, tint);
      if (mo) VR.draw(zonePropMesh(mk), mf * zonePropLocal(mk), L, tint);
    };
    return;
  }
  wear.held.clear(); wear.heldModel.clear();
  o.rigHook = [st, L, tint](const CharModel&, CharRig& R) {   // anything else: in the fist at its real size, pointing out of it
    const VoxelModel& im = itemModel(st.id);
    if (!im.ready()) return;
    voxelRenderer().draw(im.gl, R.part[CP_ARMR_D] * glm::translate(mat4(1), vec3(0, -6.3f, -0.6f)) * glm::rotate(mat4(1), -PI / 2, vec3(1, 0, 0)) * glm::scale(mat4(1), vec3(zoneHeldSize(st.id) / CHAR_PX)) * im.matrix(), L, tint);
  };
}

bool zoneRigGunFrame(const Creature& m, const RigModel& RM, const mat4& T0, mat4& gunFrame) {
  if (!gunItem(m) || m.deathTime > 0.4f) return false;
  const int bi = RM.find("body");
  if (bi < 0 || !rigPartPosed(m, bi)) return false;
  const ItemStack st(m.heldItem, 1);
  const GunModelInfo* GI = zoneGunInfo(st);
  if (!GI || !zoneGunModel(st)) return false;
  const bool aim = m.aiming || m.state == "aim";
  const float shot = clampv(m.swingT / 0.12f, 0.f, 1.f);
  // a husk holds it as he did alive, only worse: low and drooping, swung up roughly when he fires
  const vec3 grip = aim ? vec3(2.2f, 8.2f, -4.6f + shot) / 16.f : vec3(2.6f, 5.2f, -4.0f) / 16.f;
  mat4 G = glm::translate(mat4(1), grip);
  G = glm::rotate(G, aim ? 0.05f + shot * 0.15f : -0.7f, vec3(1, 0, 0));
  G = glm::rotate(G, aim ? 0.f : 0.45f, vec3(0, 1, 0));
  G = glm::rotate(G, PI / 2, vec3(0, 1, 0));
  gunFrame = T0 * m.mats[bi] * G;
  return true;
}

float zoneCloakAlpha(const Creature& m) {
  if (m.type != "z_shroud") return 1.f;
  const float k = clampv(hwGet(m, "cl", 0), 0.f, 1.f);
  return 1.f - 0.93f * k;
}

float zoneHeldSize(const std::string& id) {
  static const std::unordered_map<std::string, float> S = {
    {"bandage", 0.09f}, {"medkit", 0.2f}, {"medkit_army", 0.2f}, {"medkit_sci", 0.2f}, {"antirad", 0.14f}, {"psyblock", 0.08f}, {"atlas", 0.14f},
    {"vodka", 0.3f}, {"energy", 0.14f}, {"bread", 0.24f}, {"sausage", 0.22f}, {"canned_stew", 0.1f}, {"canned_fish", 0.1f}, {"crackers", 0.13f},
    {"water", 0.26f}, {"mutant_meat", 0.2f}, {"mutant_meat_cooked", 0.2f}, {"bolt", 0.05f}, {"pda", 0.2f}, {"pda_stalker", 0.2f}};
  if (auto it = S.find(id); it != S.end()) return it->second;
  const zone::ZItem* z = zone::zi(id);
  const std::string c = z ? z->cat : "";
  if (c == "melee") return 0.3f;
  if (c == "detector") return 0.14f;
  if (c == "ammo") return 0.09f;
  if (c == "grenade") return 0.12f;
  if (c == "throw") return 0.06f;
  if (c == "artifact" || c == "part" || c == "attach") return 0.12f;
  if (c == "repair" || c == "toolkit") return 0.3f;
  if (c == "trophy") return 0.2f;
  if (c == "suit" || c == "helmet" || c == "gear") return 0.35f;
  return 0.15f;
}

const VoxelMeshGL& zonePropMesh(int kind) {
  static VoxelMeshGL m[4]; static bool built[4] = {};
  kind = clampv(kind, 0, 3);
  if (!built[kind]) { built[kind] = true; VoxelGrid g = propGrid(kind); VoxelMeshData md; buildVoxelMesh(g, md); m[kind].upload(md); }
  return m[kind];
}
mat4 zonePropLocal(int kind) {
  switch (kind) {
    case 0: return glm::translate(glm::scale(mat4(1), vec3(0.01f)), vec3(-3, -20, -1.5f));
    case 1: return glm::translate(glm::scale(mat4(1), vec3(0.01f)), vec3(-1.5f, -11, -1));
    case 2: return glm::translate(glm::scale(mat4(1), vec3(0.01f)), vec3(-3.5f, -1, -1));
    default: return glm::translate(glm::scale(mat4(1), vec3(0.025f)), vec3(-11, -7.5f, -3));
  }
}

// ---- the dead: each body lies by its pack (zone_body_pack, the block entity says who, how he fell and where) ----
namespace {
class ZoneBodyLayer : public RenderLayer {
public:
  const char* name() const override { return "zone_bodies"; }
  int order() const override { return 114; }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!s.player || !zone::zoneWorld(*s.level)) return;
    World& w = s.world();
    std::vector<vec3> dying;   // (still falling: the creature itself is drawn until it's gone)
    for (auto& up : s.level->entities()) if (auto* c = dynamic_cast<Creature*>(up.get()); c && !c->removed && c->deathTime > 0 && c->realm == s.player->realm) dying.push_back(c->pos);
    forBlockEntities(w, f, 48.f, [&](const Chunk&, json& e, int x, int y, int z) {
      if (e.value("zone", "") != "body" || !e.contains("mob")) return;
      const vec3 at(e.value("bx", x + 0.5f), e.value("by", (float)y), e.value("bz", z + 0.5f));
      for (const vec3& d : dying) if (glm::distance(d, at) < 2.5f) return;
      const vec3 rel = f.cam.rel(glm::dvec3(at));
      if (!f.cam.frustum.sphere(rel + vec3(0, 0.3f, 0), 1.8f)) return;
      const std::string mob = e.value("mob", "");
      auto& c = dummies[mob];
      if (!c) { if (!isCreatureType(mob)) return; c = std::make_unique<Creature>(mob); }
      Creature& M = *c;
      M.data["corpse"] = true; M.data["hw"] = json{{"sk", e.value("sk", 0)}, {"hd", e.value("hd", 1)}};
      M.deathTime = 5; M.id = (u32)(x * 73856093) ^ (u32)(z * 19349663); M.age = 0; M.walk = 0; M.walkAmt = 0; M.heldItem.clear(); M.hurtTime = 0;
      drawSkinFigure(M, at, e.value("yaw", 0.f), w, f);
    });
  }
  std::unordered_map<std::string, std::unique_ptr<Creature>> dummies;
};
}
void registerZoneBodyLayer() {
  static bool done = false;
  if (done) return;
  done = true;
  registerRenderLayer([](GameSession&) { return std::make_shared<ZoneBodyLayer>(); });
}

} // namespace dq
