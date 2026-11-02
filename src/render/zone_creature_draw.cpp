// render/zone_creature_draw.cpp — the Zone's people and mutants beyond their rigs (see zone_creature_draw.h).
#include "render/zone_creature_draw.h"
#include "render/zone_gun_models.h"
#include "sim/creature.h"
#include "sim/creature_rig.h"
#include "sim/hallow.h"
#include "sim/zone.h"
#include "common/fs.h"
#include <unordered_map>

namespace dq {

namespace {
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
}

std::string zoneNpcSkin(const Creature& m) {
  const std::string& base = m.T->skin;
  if (base.size() < 5 || base.compare(base.size() - 4, 4, ".png") != 0) return base;
  // how many looks the faction has (z_npc_x.png, z_npc_x_1.png ...: tools/zone/install_skins.py), counted once
  static std::unordered_map<std::string, int> count;
  auto ci = count.find(base);
  if (ci == count.end()) {
    int k = 1; std::error_code ec;
    while (k < 16) { const std::string q = base.substr(0, base.size() - 4) + "_" + std::to_string(k) + ".png"; if (!fs::exists(dataDir() / q, ec) && !inDataArchive(fs::path(q))) break; k++; }
    ci = count.emplace(base, k).first;
  }
  const int n = (int)std::lround(hwGet(m, "sk", 0)) % ci->second;
  if (n <= 0) return base;
  const std::string v = base.substr(0, base.size() - 4) + "_" + std::to_string(n) + ".png";
  static std::unordered_map<std::string, bool> have;
  auto it = have.find(v);
  if (it == have.end()) { std::error_code ec; it = have.emplace(v, fs::exists(dataDir() / v, ec) || inDataArchive(fs::path(v))).first; }
  return it->second ? v : base;
}

bool zoneNpcGunPose(const Creature& m, const CharModel& M, CharRig& R, float headPitch, mat4& gunFrame, float& flash) {
  if (!gunItem(m) || m.deathTime > 0.4f || m.sleeping || m.sitting) return false;   // (slung while he sits at the fire or sleeps)
  const ItemStack st(m.heldItem, 1);
  const GunModelInfo* GI = zoneGunInfo(st);
  if (!GI || !zoneGunModel(st)) return false;
  const bool aim = m.aiming || m.state == "aim", pistol = GI->pistol;
  const float shot = clampv(m.swingT / 0.12f, 0.f, 1.f);
  flash = aim && shot > 0.45f ? 1.f : 0.f;
  // where the gun is in the body's frame (model px: x right, y up from the hips, z back) and how it is turned
  vec3 grip; float pitch, yaw = 0, roll = 0;
  if (aim) {
    pitch = clampv(headPitch, -0.7f, 0.7f) + shot * 0.12f;
    grip = pistol ? vec3(1.0f, 8.8f, -8.0f + shot * 1.2f) : vec3(2.1f, 8.4f, -3.8f + shot * 0.9f);
  } else {   // the low ready: barrel down and across the body
    pitch = pistol ? -1.05f : -0.6f; yaw = pistol ? 0.1f : 0.5f; roll = pistol ? 0 : -0.25f;
    grip = pistol ? vec3(3.4f, 3.6f, -2.6f) : vec3(2.6f, 5.4f, -3.4f);
  }
  const float sh = 9.5f;   // the aim turns about the shoulders
  mat4 G = glm::translate(mat4(1), vec3(0, sh, 0)) * glm::rotate(mat4(1), pitch, vec3(1, 0, 0)) * glm::translate(mat4(1), vec3(0, -sh, 0));
  G = glm::translate(G, grip);
  G = glm::rotate(G, yaw, vec3(0, 1, 0)); G = glm::rotate(G, roll, vec3(0, 0, 1));
  G = glm::rotate(G, PI / 2, vec3(0, 1, 0));    // the gun's +X (muzzle) to the body's front (-z), its +Z to the right
  G = glm::scale(G, vec3(1.f / CHAR_PX));        // gun blocks -> model px
  gunFrame = R.bodyRaw * G;
  // the hands on it: the right on the pistol grip, the left under the handguard (or cupping the right on a pistol)
  const vec3 hr = vec3(G * vec4(GI->grip, 1));
  const vec3 hl = pistol ? vec3(G * vec4(GI->grip + vec3(0.01f, -0.03f, -0.05f), 1)) : vec3(G * vec4(GI->fore, 1));
  const float b = M.body.build;
  for (int side = 0; side < 2; side++) {
    const int up = side ? CP_ARML_U : CP_ARMR_U, dn = up + 1;
    const vec3 pu = M.pivot[up], S(pu.x * b, pu.y - 12, pu.z);
    const float a = std::max(1.f, pu.y - M.pivot[dn].y), c = 6.2f;
    vec3 ua, la;
    armIK(S, side ? hl : hr, a, c, vec3(side ? -0.7f : 0.7f, -1, 0.25f), ua, la);
    R.part[up] = rotYX(glm::translate(R.bodyRaw, S), ua);
    R.part[dn] = rotYX(glm::translate(R.part[up], M.pivot[dn] - pu), la);
  }
  return true;
}

void drawZoneNpcGun(const Creature& m, const mat4& gunFrame, float flash, VoxelLight light, vec4 tint) {
  const ItemStack st(m.heldItem, 1);
  const VoxelModel* gm = zoneGunModel(st);
  const GunModelInfo* GI = zoneGunInfo(st);
  if (!gm || !GI) return;
  VoxelRenderer& VR = voxelRenderer();
  VR.draw(gm->gl, gunFrame * gm->matrix(), light, tint);
  if (flash > 0) {
    mat4 F = glm::translate(gunFrame, vec3(GI->front + 0.02f, GI->sightY * 0.35f, 0));
    F = glm::rotate(F, PI / 2, vec3(0, 1, 0));   // the mesh runs along +z: turn it out of the muzzle (+X)
    F = glm::rotate(F, (float)(m.id % 7) * 0.9f + m.age * 30.f, vec3(0, 0, 1));
    F = glm::scale(F, vec3(0.05f));
    F = glm::translate(F, vec3(-2.5f, -2.5f, 0));
    VR.draw(flashMesh(), F, VoxelLight{1, 1});
  }
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

} // namespace dq
