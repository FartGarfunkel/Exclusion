// sim/zone_guns.cpp — guns. The list, the magazines and the attachments stay the catalogue's
// (data/zone_items.json, the voxel models). The shot itself follows OpenXRay's actor cone, weapon
// dispersion, camera kick, condition and bullet trace, with the numbers from Anomaly 1.5.3's
// published weapon and ammunition sections (the stock section, not an upgrade). No meshes or sounds
// are taken from there. A round is still a hit-scan along raycastBlocks: crouch and movement change
// the cone, condition opens the weapon's share of it and wears the gun toward a misfire, and a block
// in the way keeps, slows or passes the round by the same shoot-factor test.
// The player's own machine fires (client-authoritative like the body); a creature owned by the host takes its hit
// through LevelNet::hitEntity, as melee does.
#include "sim/zone.h"
#include "sim/controller.h"
#include "sim/combat.h"
#include "sim/creature.h"
#include "sim/creature_entities.h"
#include "sim/physics.h"
#include "core/registry.h"
#include "gen/zone_plan.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <random>

namespace dq::zone {

namespace {
constexpr float D2R = 0.01745329252f;
constexpr float PI = 3.1415926536f;
constexpr float TAU = 6.2831853072f;
// actor.ltx (the player, not the difficulty globals). disp_jump_factor is not read by GetWeaponAccuracy.
constexpr float ACTOR_BASE = 1.5f, ACTOR_AIM = 0.05f, ACTOR_VEL = 2.5f, ACTOR_ACCEL = 0.f;
constexpr float ACTOR_CROUCH = -0.65f, ACTOR_CROUCH_STILL = -0.75f, VEL_CAP = 10.f;
// single-player bullet_manager: air_resistance_k, not the cartridge's k_air_resistance.
constexpr float AIR_K = 1.f, MIN_BULLET = 3.f, RICO_LOSS = 0.8f, RICO_CAP = 0.5f;
constexpr float TRACE = 64.f;   // raycastBlocks stops after 256 cells; short steps stay inside that

// Stock Anomaly 1.5.3 section, degrees except the probabilities and the per-shot condition loss.
// hit dist speed rpm disp condFactor | PDM base vel accel crouch crouchStill |
// cam disp frac inc maxV maxH step | the same aimed | misfire start cond/prob, end cond/prob, shot, queue.
struct Row {
  const char* id;
  float hit, dist, spd, rpm, disp, cfac;
  float pdmB, pdmV, pdmA, pdmC, pdmN;
  float cD, cF, cI, cMV, cMH, cS;
  float zD, zF, zI, zMV, zMH, zS;
  float m0, mp0, m1, mp1, dec, qdec;
};
const Row kGuns[] = {
  {"pm", 0.45f, 300.0f, 290.0f, 315.0f, 0.62f, 0.001f, 0.6f, 1.05f, 1.055f, 1.0f, 1.0f, 2.0f, 1.1f, 0.0f, 50.0f, 50.0f, 0.52f, 1.5f, 1.1f, 0.0f, 50.0f, 50.0f, 0.52f, 0.7f, 0.007f, 0.1f, 0.11f, 0.0016f, 0.0016f},
  {"fort12", 0.45f, 300.0f, 290.0f, 240.0f, 0.55f, 0.001f, 0.65f, 1.05f, 1.055f, 1.0f, 1.0f, 2.0f, 0.85f, 0.0f, 50.0f, 50.0f, 0.54f, 1.5f, 0.85f, 0.0f, 50.0f, 50.0f, 0.54f, 0.7f, 0.007f, 0.1f, 0.11f, 0.0019f, 0.0019f},
  {"mp443", 0.46f, 400.0f, 370.0f, 293.0f, 0.38f, 0.001f, 0.65f, 1.05f, 1.055f, 1.0f, 1.0f, 2.0f, 1.1f, 0.0f, 50.0f, 50.0f, 0.42f, 1.5f, 1.1f, 0.0f, 50.0f, 50.0f, 0.42f, 0.7f, 0.007f, 0.12f, 0.11f, 0.0015f, 0.0015f},
  {"g17", 0.46f, 400.0f, 360.0f, 270.0f, 0.34f, 0.001f, 0.75f, 1.35f, 1.355f, 1.0f, 1.0f, 2.0f, 1.0f, 0.0f, 50.0f, 50.0f, 0.69f, 1.5f, 1.0f, 0.0f, 50.0f, 50.0f, 0.69f, 0.7f, 0.007f, 0.09f, 0.11f, 0.0016f, 0.0016f},
  {"m1911", 0.55f, 300.0f, 270.0f, 230.0f, 0.6f, 0.001f, 0.7f, 1.05f, 1.055f, 1.0f, 1.0f, 2.5f, 1.1f, 0.0f, 50.0f, 50.0f, 0.76f, 2.0f, 1.1f, 0.0f, 50.0f, 50.0f, 0.76f, 0.7f, 0.007f, 0.11f, 0.11f, 0.0018f, 0.0018f},
  {"toz34", 0.8f, 400.0f, 370.0f, 1000.0f, 0.35f, 0.001f, 1.85f, 1.95f, 1.955f, 1.0f, 1.0f, 5.84f, 0.9f, 4.48f, 50.0f, 50.0f, 1.0f, 5.04f, 0.7f, 4.98f, 50.0f, 50.0f, 1.0f, 0.7f, 0.007f, 0.12f, 0.11f, 0.0025f, 0.0019f},
  {"sawnoff", 0.77f, 200.0f, 300.0f, 1000.0f, 0.85f, 0.001f, 1.35f, 1.35f, 1.355f, 1.0f, 1.0f, 9.92f, 0.5f, 4.78f, 50.0f, 50.0f, 5.98f, 9.13f, 0.5f, 3.98f, 50.0f, 50.0f, 3.93f, 0.7f, 0.007f, 0.1f, 0.11f, 0.0025f, 0.0018f},
  {"m870", 0.79f, 300.0f, 330.0f, 45.0f, 0.454f, 0.001f, 0.5f, 2.0f, 2.0f, 1.0f, 1.0f, 6.0f, 0.7f, 2.0f, 15.0f, 15.0f, 3.0f, 5.0f, 0.7f, 2.5f, 15.0f, 15.0f, 3.0f, 0.7f, 0.0f, 0.1f, 0.0f, 0.003f, 0.0035f},
  {"spas12", 0.79f, 300.0f, 340.0f, 115.0f, 0.48f, 0.001f, 1.7f, 1.75f, 1.755f, 1.0f, 1.0f, 7.88f, 1.1f, 0.0f, 50.0f, 50.0f, 2.77f, 6.03f, 0.7f, 0.0f, 50.0f, 50.0f, 2.61f, 0.7f, 0.007f, 0.1f, 0.11f, 0.0022f, 0.0022f},
  {"saiga", 0.78f, 300.0f, 325.0f, 220.0f, 0.46f, 0.001f, 0.96f, 2.4f, 2.4f, 1.0f, 1.0f, 6.6f, 1.0f, 0.0f, 50.0f, 50.0f, 1.24f, 5.3f, 0.8f, 0.0f, 50.0f, 50.0f, 1.12f, 0.7f, 0.007f, 0.12f, 0.11f, 0.002f, 0.002f},
  {"pp19", 0.46f, 300.0f, 320.0f, 680.0f, 0.57f, 0.001f, 0.55f, 1.145f, 1.15f, 1.0f, 1.0f, 0.53f, 1.0f, 0.0425f, 43.0f, 43.0f, 1.54f, 0.32f, 1.0f, 0.0325f, 43.0f, 43.0f, 1.52f, 0.7f, 0.007f, 0.16f, 0.11f, 0.0012f, 0.0012f},
  {"mp5", 0.47f, 400.0f, 402.0f, 800.0f, 0.57f, 0.001f, 0.65f, 1.15f, 1.155f, 1.0f, 1.0f, 0.44f, 1.1f, 0.0475f, 39.0f, 39.0f, 1.22f, 0.37f, 1.0f, 0.0375f, 39.0f, 39.0f, 1.17f, 0.7f, 0.007f, 0.16f, 0.11f, 0.0009f, 0.0009f},
  {"ump45", 0.56f, 300.0f, 273.0f, 600.0f, 0.54f, 0.001f, 0.65f, 1.15f, 1.155f, 1.0f, 1.0f, 0.91f, 1.0f, 0.06f, 36.0f, 36.0f, 1.19f, 0.67f, 1.0f, 0.05f, 36.0f, 36.0f, 1.09f, 0.7f, 0.007f, 0.17f, 0.11f, 0.0009f, 0.0009f},
  {"aks74u", 0.485f, 900.0f, 735.0f, 700.0f, 0.56f, 0.001f, 0.7f, 1.65f, 1.655f, 1.0f, 1.0f, 0.646f, 1.1f, 0.0625f, 41.0f, 41.0f, 1.18f, 0.616f, 1.0f, 0.0525f, 41.0f, 41.0f, 1.08f, 0.7f, 0.007f, 0.1f, 0.11f, 0.0009f, 0.0009f},
  {"ak74m", 0.5f, 1000.0f, 880.0f, 600.0f, 0.32f, 0.001f, 1.0f, 2.5f, 2.5f, 1.0f, 1.0f, 0.545f, 1.1f, 0.0625f, 50.0f, 50.0f, 1.15f, 0.515f, 0.7f, 0.0525f, 50.0f, 50.0f, 1.05f, 0.7f, 0.007f, 0.1f, 0.11f, 0.0008f, 0.0008f},
  {"akm", 0.58f, 900.0f, 715.0f, 600.0f, 0.45f, 0.001f, 1.15f, 2.5f, 2.5f, 1.0f, 1.0f, 0.762f, 1.0f, 0.0725f, 50.0f, 50.0f, 1.38f, 0.732f, 0.7f, 0.0625f, 50.0f, 50.0f, 1.28f, 0.7f, 0.007f, 0.05f, 0.11f, 0.0008f, 0.0008f},
  {"an94", 0.5f, 1000.0f, 900.0f, 600.0f, 0.265f, 0.001f, 1.2f, 2.75f, 2.75f, 1.0f, 1.0f, 0.545f, 1.1f, 0.0625f, 50.0f, 50.0f, 1.1f, 0.515f, 1.0f, 0.0525f, 50.0f, 50.0f, 0.95f, 0.7f, 0.007f, 0.05f, 0.11f, 0.0008f, 0.0008f},
  {"groza", 0.62f, 400.0f, 300.0f, 700.0f, 0.37f, 0.001f, 0.75f, 1.05f, 1.055f, 1.0f, 1.0f, 0.5f, 1.0f, 0.05f, 41.0f, 41.0f, 1.65f, 0.4f, 1.0f, 0.04f, 41.0f, 41.0f, 1.55f, 0.7f, 0.007f, 0.1f, 0.11f, 0.0009f, 0.0009f},
  {"asval", 0.62f, 600.0f, 500.0f, 900.0f, 0.33f, 0.001f, 0.73f, 1.745f, 1.745f, 1.0f, 1.0f, 0.5f, 0.9f, 0.05f, 43.0f, 43.0f, 0.59f, 0.4f, 0.9f, 0.04f, 43.0f, 43.0f, 0.41f, 0.7f, 0.007f, 0.11f, 0.11f, 0.001f, 0.001f},
  {"vss", 0.62f, 600.0f, 500.0f, 800.0f, 0.2f, 0.001f, 0.73f, 1.745f, 1.745f, 1.0f, 1.0f, 0.5f, 0.9f, 0.05f, 46.0f, 46.0f, 0.62f, 0.4f, 0.9f, 0.04f, 46.0f, 46.0f, 0.44f, 0.7f, 0.007f, 0.13f, 0.11f, 0.0015f, 0.002f},
  {"m416", 0.49f, 1000.0f, 880.0f, 850.0f, 0.24f, 0.001f, 0.8f, 2.3f, 2.3f, 1.0f, 1.0f, 0.556f, 1.0f, 0.0675f, 50.0f, 50.0f, 0.9f, 0.466f, 1.0f, 0.0525f, 50.0f, 50.0f, 0.85f, 0.7f, 0.007f, 0.11f, 0.11f, 0.0008f, 0.0008f},
  {"g36", 0.5f, 1000.0f, 915.0f, 750.0f, 0.29f, 0.001f, 1.18f, 2.5f, 2.5f, 1.0f, 1.0f, 0.556f, 1.0f, 0.0675f, 50.0f, 50.0f, 0.95f, 0.515f, 1.0f, 0.0525f, 50.0f, 50.0f, 0.9f, 0.7f, 0.007f, 0.09f, 0.11f, 0.0007f, 0.0009f},
  {"l85a2", 0.51f, 1000.0f, 930.0f, 680.0f, 0.215f, 0.001f, 0.85f, 2.5f, 2.5f, 1.0f, 1.0f, 0.556f, 1.1f, 0.0675f, 50.0f, 50.0f, 1.65f, 0.466f, 1.0f, 0.0525f, 50.0f, 50.0f, 1.5f, 0.7f, 0.007f, 0.11f, 0.1f, 0.0008f, 0.0008f},
  {"scarl", 0.75f, 1000.0f, 802.0f, 650.0f, 0.18f, 0.001f, 1.15f, 2.75f, 2.75f, 1.0f, 1.0f, 1.5f, 1.0f, 0.0775f, 50.0f, 50.0f, 1.1f, 1.2f, 0.7f, 0.0675f, 50.0f, 50.0f, 1.0f, 0.7f, 0.007f, 0.05f, 0.11f, 0.00095f, 0.00095f},
  {"sks", 0.585f, 900.0f, 735.0f, 200.0f, 0.204f, 0.001f, 1.45f, 2.75f, 2.75f, 1.0f, 1.0f, 1.6f, 1.0f, 1.4f, 50.0f, 50.0f, 1.15f, 1.3f, 0.7f, 1.1f, 50.0f, 50.0f, 0.95f, 0.7f, 0.01f, 0.17f, 0.05f, 0.0013f, 0.0013f},
  {"svd", 1.1f, 1100.0f, 820.0f, 60.0f, 0.092f, 0.001f, 1.45f, 3.05f, 3.05f, 1.0f, 1.0f, 2.78f, 1.1f, 2.78f, 50.0f, 50.0f, 0.8f, 0.75f, 1.0f, 1.97f, 50.0f, 50.0f, 1.65f, 0.7f, 0.007f, 0.11f, 0.11f, 0.004f, 0.004f},
  {"mk14", 0.76f, 1000.0f, 845.0f, 725.0f, 0.21f, 0.001f, 0.5f, 2.0f, 2.0f, 1.0f, 1.0f, 1.5f, 0.7f, 0.0775f, 60.0f, 10.0f, 3.0f, 1.2f, 1.0f, 0.0675f, 50.0f, 10.0f, 2.0f, 0.7f, 0.006f, 0.1f, 0.072f, 0.0009f, 0.0009f},
  {"sv98", 1.1f, 1200.0f, 860.0f, 30.0f, 0.08f, 0.001f, 1.45f, 3.105f, 3.105f, 1.0f, 1.0f, 2.81f, 1.1f, 2.81f, 50.0f, 50.0f, 0.9f, 1.0f, 1.0f, 1.87f, 50.0f, 50.0f, 0.68f, 0.7f, 0.007f, 0.16f, 0.11f, 0.0048f, 0.0048f},
  {"m82", 3.0f, 1800.0f, 853.0f, 100.0f, 0.1f, 0.001f, 1.3f, 3.0f, 3.0f, 1.0f, 1.0f, 4.5f, 1.0f, 4.5f, 50.0f, 50.0f, 0.25f, 1.25f, 1.0f, 2.5f, 50.0f, 50.0f, 1.8f, 0.7f, 0.006f, 0.1f, 0.08f, 0.0025f, 0.0025f},
  {"rpk", 0.585f, 900.0f, 740.0f, 600.0f, 0.33f, 0.001f, 1.15f, 2.5f, 2.5f, 1.0f, 1.0f, 0.762f, 1.0f, 0.0725f, 50.0f, 50.0f, 1.38f, 0.732f, 0.7f, 0.0625f, 50.0f, 50.0f, 1.28f, 0.7f, 0.007f, 0.14f, 0.11f, 0.0008f, 0.0008f},
  {"pkm", 0.77f, 1000.0f, 825.0f, 650.0f, 0.34f, 0.001f, 1.25f, 2.4f, 2.4f, 1.0f, 1.0f, 0.762f, 1.1f, 0.0825f, 50.0f, 50.0f, 2.1f, 0.732f, 1.0f, 0.0725f, 50.0f, 50.0f, 1.93f, 0.7f, 0.007f, 0.16f, 0.11f, 0.0008f, 0.0008f},
  {"rpg7", 3.0f, 1000.0f, 121.0f, 200.0f, 0.33f, 0.001f, 1.25f, 2.95f, 2.95f, 1.0f, 1.0f, 7.05f, 1.0f, 0.0f, 50.0f, 50.0f, 0.5f, 7.05f, 1.0f, 0.0f, 50.0f, 50.0f, 0.5f, 0.7f, 0.007f, 0.11f, 0.11f, 0.009f, 0.009f},
};
struct AmmoRow { const char* id; float kHit, kDisp, kAp, kDist, impair, kCam, kSpd, buck; };
const AmmoRow kAmmo[] = {
  {"ammo_9x18", 1.0f, 0.66f, 0.1f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {"ammo_9x18_pbp", 1.0f, 0.66f, 0.2f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {"ammo_9x19", 1.0f, 0.66f, 0.15f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {"ammo_45acp", 1.0f, 0.66f, 0.1f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {"ammo_545", 1.0f, 0.66f, 0.4f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {"ammo_545_ap", 1.0f, 0.66f, 0.6f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {"ammo_556", 1.0f, 0.66f, 0.33f, 1.0f, 1.0f, 1.0f, 1.03f, 1.0f},
  {"ammo_556_ap", 1.0f, 0.66f, 0.55f, 1.0f, 1.05f, 1.0f, 1.05f, 1.0f},
  {"ammo_762x39", 1.0f, 0.75f, 0.45f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {"ammo_762x39_ap", 1.0f, 0.75f, 0.55f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {"ammo_762x51", 1.0f, 0.5f, 0.4f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {"ammo_762x54", 1.0f, 0.66f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {"ammo_762x54_ap", 1.0f, 0.9f, 0.6f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {"ammo_9x39", 1.0f, 0.5f, 0.35f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {"ammo_9x39_ap", 1.0f, 0.66f, 0.45f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {"ammo_12g_buck", 0.4f, 4.6f, 0.03f, 0.75f, 1.0f, 1.0f, 1.0f, 9.0f},
  {"ammo_12g_slug", 2.0f, 0.5f, 0.2f, 0.9f, 1.05f, 1.0f, 1.15f, 1.0f},
  {"ammo_12g_dart", 1.75f, 0.33f, 0.3f, 1.0f, 1.05f, 1.0f, 1.2f, 1.0f},
  {"ammo_50bmg", 1.0f, 0.9f, 0.6f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
};
const Row* findRow(const std::string& id) {
  for (const Row& r : kGuns) if (id == r.id) return &r;
  return nullptr;
}
const AmmoRow* findAmmo(const std::string& id) {
  for (const AmmoRow& a : kAmmo) if (id == a.id) return &a;
  return nullptr;
}
float zoomOf(const std::string& c) {
  if (c == "sniper" || c == "dmr") return 1.4f;
  if (c == "shotgun" || c == "pistol") return 1.15f;
  if (c == "lmg") return 1.2f;
  if (c == "launcher") return 1.3f;
  return 1.25f;
}
Row rowFor(const ZItem& z) {
  if (const Row* r = findRow(z.id)) return *r;
  Row r = *findRow("ak74m");
  r.hit = z.gun.dmg / 100.f; r.rpm = z.gun.rpm; r.disp *= std::max(0.2f, z.gun.spreadMul);
  r.cD *= z.gun.kick; r.zD *= z.gun.kick; r.cS *= z.gun.kickH; r.zS *= z.gun.kickH;
  if (z.gun.cls == "pistol") { r.disp = 0.5f * z.gun.spreadMul; r.dist = 300; r.spd = 300; }
  else if (z.gun.cls == "shotgun") { r.disp = 0.5f * z.gun.spreadMul; r.dist = 300; r.spd = 340; }
  else if (z.gun.cls == "smg") { r.disp = 0.55f * z.gun.spreadMul; r.dist = 400; r.spd = 400; }
  else if (z.gun.cls == "sniper") { r.disp = 0.09f * z.gun.spreadMul; r.dist = 1100; r.spd = 830; }
  else if (z.gun.cls == "dmr") { r.disp = 0.2f * z.gun.spreadMul; r.dist = 900; r.spd = 800; }
  else if (z.gun.cls == "carbine") { r.disp = 0.5f * z.gun.spreadMul; r.dist = 700; r.spd = 720; }
  else if (z.gun.cls == "lmg") { r.disp = 0.34f * z.gun.spreadMul; r.dist = 900; r.spd = 780; }
  return r;
}

struct Stats {
  float delay, reload, adsZoom = 1.2f, loud = 1;
  int clip, pellets;
  bool slowReload, autoFire, suppressed = false, laser = false, light = false, bipod = false, scope = false;
  std::string reticle = "iron", launcher;
  float hit, dist, spd, disp, cfac;
  float pdmB, pdmV, pdmA, pdmC, pdmN;
  float cD, cF, cI, cMV, cMH, cS, zD, zF, zI, zMV, zMH, zS;
  float m0, mp0, m1, mp1, dec, qdec;
  float kHit = 1, kDisp = 1, kAp = 0.4f, kDist = 1, impair = 1, kCam = 1, kSpd = 1;
  float hipMul = 1, adsMul = 1, recoilV = 1, recoilH = 1, dmgMul = 1;
};

Stats statsFor(const ZItem& z, const ItemStack& s) {
  const GunDef& G = z.gun; const Row R = rowFor(z);
  Stats S;
  S.delay = 60.f / std::max(1.f, R.rpm); S.clip = G.mag; S.pellets = std::max(1, G.pellets); S.reload = G.reload;
  S.slowReload = G.slowReload; S.autoFire = G.autoFire; S.adsZoom = zoomOf(G.cls);
  S.hit = R.hit; S.dist = R.dist; S.spd = R.spd; S.disp = R.disp; S.cfac = R.cfac;
  S.pdmB = R.pdmB; S.pdmV = R.pdmV; S.pdmA = R.pdmA; S.pdmC = R.pdmC; S.pdmN = R.pdmN;
  S.cD = R.cD; S.cF = R.cF; S.cI = R.cI; S.cMV = R.cMV; S.cMH = R.cMH; S.cS = R.cS;
  S.zD = R.zD; S.zF = R.zF; S.zI = R.zI; S.zMV = R.zMV; S.zMH = R.zMH; S.zS = R.zS;
  S.m0 = R.m0; S.mp0 = R.mp0; S.m1 = R.m1; S.mp1 = R.mp1; S.dec = R.dec; S.qdec = R.qdec;
  json att = gunAttachments(s);
  if (!att.contains("optic") && !G.defaultOptic.empty() && !s.tag.is_object()) att["optic"] = G.defaultOptic;
  float rangeMul = 1;
  for (auto& [slot, v] : att.items()) {
    const ZItem* a = v.is_string() ? zi(v.get<std::string>()) : nullptr;
    if (!a || !a->raw.contains("att")) continue;
    const json& A = a->raw["att"]; const json& m = A.value("mods", json::object());
    if (m.contains("adsZoom")) S.adsZoom = m["adsZoom"];
    if (m.contains("adsSpreadMul")) S.adsMul *= m["adsSpreadMul"].get<float>();
    if (m.contains("hipSpreadMul")) S.hipMul *= m["hipSpreadMul"].get<float>();
    if (m.contains("rangeMul")) rangeMul *= m["rangeMul"].get<float>();
    if (m.contains("minDmgMul")) S.dmgMul *= m["minDmgMul"].get<float>();
    if (m.contains("recoilV")) S.recoilV *= m["recoilV"].get<float>();
    if (m.contains("recoilH")) S.recoilH *= m["recoilH"].get<float>();
    if (m.contains("clipMul")) S.clip = (int)std::round(S.clip * m["clipMul"].get<float>());
    if (m.contains("reloadMul")) S.reload *= m["reloadMul"].get<float>();
    if (m.contains("loud")) S.loud = m["loud"];
    if (A.value("hidden", false)) S.suppressed = true;
    if (A.value("laser", false)) S.laser = true;
    if (A.value("light", false)) S.light = true;
    if (A.value("bipod", false)) S.bipod = true;
    if (A.value("scope", false)) S.scope = true;
    if (A.contains("reticle")) S.reticle = A["reticle"];
    if (A.contains("launcher")) S.launcher = A["launcher"];
  }
  if (G.suppressedBuiltIn) { S.suppressed = true; S.loud = 0.3f; }
  if (G.cls == "lmg") S.bipod = true;
  S.dist *= rangeMul;
  const std::string at = s.tag.is_object() ? s.tag.value("at", std::string()) : std::string();
  if (const AmmoRow* a = findAmmo(at)) {
    S.kHit = a->kHit; S.kDisp = a->kDisp; S.kAp = a->kAp; S.kDist = a->kDist;
    S.impair = a->impair; S.kCam = a->kCam; S.kSpd = a->kSpd;
    S.pellets = std::max(1, (int)std::lround(a->buck));
  } else if (const ZItem* am = zi(at)) {
    const json& A = am->raw.value("ammo", json::object());
    S.kHit = A.value("dmg", 1.f); S.impair = A.value("wear", 1.f);
    if (A.contains("spread")) S.kDisp = A["spread"].get<float>();
    if (A.contains("pellets")) S.pellets = std::max(1, A["pellets"].get<int>());
  }
  return S;
}

struct GunState {
  double next = 0, lastShot = -10; float reloadLeft = 0;
  bool reloading = false, jammed = false, ads = false, aimInit = false;
  float jamClear = 0, cone = 0, angVel = 0, yaw0 = 0, pitch0 = 0, camV = 0, camH = 0;
  std::string heldKey; int heldSlot = -1, shots = 0; float sprintRec = 0;
};
std::unordered_map<u32, GunState>& states() { static std::unordered_map<u32, GunState> s; return s; }
std::mt19937& rng() { static thread_local std::mt19937 g{std::random_device{}()}; return g; }
float frand() { return std::uniform_real_distribution<float>(0, 1)(rng()); }
float wrapPi(float a) {
  a = std::fmod(a, TAU);
  if (a > PI) a -= TAU;
  if (a < -PI) a += TAU;
  return a;
}

int durOf(const ItemStack& s) { const ItemDef* d = s.def(); return d && d->hasTool ? d->tool.durability : 1000; }
float condOf(const ItemStack& s) { return std::clamp(1.f - (float)s.d / std::max(1, durOf(s)), 0.f, 1.f); }
float itemCond(const ItemStack& s) {
  const ItemDef* d = s.def(); if (!d) return 1;
  int dur = d->hasTool ? d->tool.durability : d->props.contains("armor") ? d->props["armor"].value("durability", 0) : 0;
  return dur > 0 ? std::clamp(1.f - (float)s.d / dur, 0.f, 1.f) : 1.f;
}

// the rounds in your bags that fit this calibre (the loaded type first)
int reserveOf(const Player& p, const ZItem& z, const std::string& type) {
  int n = 0;
  for (auto& s : p.inv) if (!s.empty()) if (const ZItem* a = zi(s.id); a && a->cat == "ammo" && a->raw["ammo"].value("cal", "") == z.gun.cal && (type.empty() || s.id == type)) n += s.n;
  return n;
}
std::string firstAmmo(const Player& p, const ZItem& z) {
  for (auto& s : p.inv) if (!s.empty()) if (const ZItem* a = zi(s.id); a && a->cat == "ammo" && a->raw["ammo"].value("cal", "") == z.gun.cal) return s.id;
  return "";
}
int takeAmmo(Player& p, const std::string& id, int want) {
  int got = 0;
  for (auto& s : p.inv) if (!s.empty() && s.id == id && got < want) { const int k = std::min(want - got, s.n); s.n -= k; got += k; if (s.n <= 0) s.clear(); }
  return got;
}
std::string launcherAmmo(const std::string& l) { return l == "vog" ? "vog25" : l == "m203" ? "m203_he" : ""; }

// CActor::GetWeaponAccuracy (radians) plus the weapon's own GetBaseDispersion, added, not multiplied.
float coneRad(const Player& p, const Stats& S, const GunState& gs, float cond) {
  const bool ads = gs.ads;
  float parent;
  if (ads) parent = D2R * ACTOR_AIM;
  else {
    parent = D2R * ACTOR_BASE * S.pdmB;
    const float lin = glm::length(vec3(p.vel.x, 0, p.vel.z));
    parent *= 1.f + (gs.angVel / VEL_CAP) * ACTOR_VEL * S.pdmV;
    parent *= 1.f + (lin / VEL_CAP) * ACTOR_VEL * S.pdmV;
    const bool crouch = p.sneaking || p.crawling;
    const bool accel = p.sprinting;
    if (accel || !crouch) parent *= 1.f + ACTOR_ACCEL * S.pdmA;
    if (crouch) {
      parent *= 1.f + ACTOR_CROUCH * S.pdmC;
      if (!accel) parent *= 1.f + ACTOR_CROUCH_STILL * S.pdmN;
    }
    parent = std::max(0.f, parent);
  }
  const float weapon = D2R * S.disp * S.kDisp * (1.f + S.cfac * (1.f - cond));
  float cone = (weapon + parent) * (ads ? S.adsMul : S.hipMul);
  if (zget(p, "drunk") > 0) cone *= 1.5f;
  return std::clamp(cone, 0.f, 1.2f);
}

// random_dir: gaussian angle, sigma = cone/3, clamped, then tan into the aim plane.
float nrand(float sigma) {
  if (sigma <= 0) return 0;
  for (int i = 0; i < 16; i++) {
    const float y = -std::log(std::max(1e-6f, frand()));
    if (frand() <= std::exp(-(y - 1.f) * (y - 1.f) * 0.5f))
      return (frand() < 0.5f ? -1.f : 1.f) * y * sigma * (1.f / 0.7975f);
  }
  return 0;
}
vec3 randomDir(vec3 src, float dispersion) {
  if (glm::dot(src, src) < 1e-8f) src = vec3(0, 0, -1);
  src = glm::normalize(src);
  if (dispersion <= 1e-6f) return src;
  const float alpha = std::clamp(nrand(dispersion / 3.f), -dispersion, dispersion);
  const float theta = frand() * TAU;
  const float r = std::tan(alpha);
  const vec3 up = std::abs(src.y) < 0.99f ? vec3(0, 1, 0) : vec3(1, 0, 0);
  const vec3 U = glm::normalize(glm::cross(src, up));
  const vec3 V = glm::cross(U, src);
  return glm::normalize(src + U * (r * std::sin(theta)) + V * (r * std::cos(theta)));
}

// CWeaponShotEffector::Shot2. cam_return is 0 on these guns, so the kick stays on the view.
void camKick(Player& p, const Stats& S, GunState& gs) {
  const bool ads = gs.ads;
  const float bip = (S.bipod && (p.sneaking || p.crawling)) ? 0.55f : 1.f;
  const float recV = S.recoilV * S.kCam * bip;
  const float D = D2R * (ads ? S.zD : S.cD) * recV;
  const float inc = D2R * (ads ? S.zI : S.cI) * recV;
  const float frac = ads ? S.zF : S.cF;
  const float maxV = std::max(0.05f, D2R * (ads ? S.zMV : S.cMV));
  const float maxH = std::max(0.05f, D2R * (ads ? S.zMH : S.cMH));
  const float step = D2R * (ads ? S.zS : S.cS) * S.recoilH;
  const float angle = D + inc * (float)gs.shots;
  const float prevV = gs.camV, prevH = gs.camH;
  gs.camV += angle * (frac + (frand() * 2.f - 1.f) * (1.f - frac));
  gs.camV = std::clamp(gs.camV, -maxV, maxV);
  if (std::abs(gs.camV - maxV) < 1e-4f) gs.camV *= 0.96f + frand() * 0.08f;
  gs.camH += (gs.camV / maxV) * (frand() * 2.f - 1.f) * step;
  gs.camH = std::clamp(gs.camH, -maxH, maxH);
  p.pitch = std::clamp(p.pitch + (gs.camV - prevV), -1.55f, 1.55f);
  p.yaw -= gs.camH - prevH;
}

// Peacemaker's line from misfire_start_* to misfire_end_*. Above the start, a gun does not jam.
float misfireP(float cond, const Stats& S) {
  float mis;
  if (cond > S.m0) return 0;
  if (cond < S.m1) mis = S.mp1;
  else {
    float span = S.m0 - S.m1;
    if (std::abs(span) < 1e-5f) span = std::max(S.m0, 1e-5f);
    mis = S.mp0 + (S.m0 - cond) * (S.mp1 - S.mp0) / span;
  }
  return std::clamp(mis, 0.f, 0.99f);
}
// speed / muzzle after air_resistance_k = 1. The round is done at v0/2 metres, or under 3 m/s.
float speedFrac(float dist, float muzzle) {
  if (muzzle < 1.f || dist <= 0) return dist <= 0 && muzzle >= 1.f ? 1.f : 0.f;
  const float x = 2.f * AIR_K * dist / muzzle;
  if (x >= 1.f) return 0;
  const float s = std::sqrt(1.f - x);
  return (s * muzzle < MIN_BULLET) ? 0.f : s;
}

// HitThroughArmor, the outfit branch: bone <= 0 keeps the whole hit. Otherwise the fraction, or
// (ap - bone) / (ap * apScale) clamped up to that fraction. Bone numbers are the suit's own
// "bullet" (the same 0..1 scale as damages.ltx). Limbs take a fifth of the jacket. A stalker's
// tier picks a body section: rookie cloth, then class II, III, III+.
void outfitFrac(float bone, float& hf, float& aps) {
  if (bone < 0.1f) { hf = 0.85f; aps = 0.9f; }
  else if (bone < 0.18f) { hf = 0.6f; aps = 0.85f; }
  else if (bone < 0.28f) { hf = 0.45f; aps = 0.8f; }
  else if (bone < 0.4f) { hf = 0.35f; aps = 0.75f; }
  else if (bone < 0.55f) { hf = 0.22f; aps = 0.7f; }
  else { hf = 0.2f; aps = 0.65f; }
}
float throughArmor(float ap, float bone, float cond, float hf, float aps) {
  if (bone <= 0.f || ap < 0.f) return bone <= 0.f ? 1.f : hf;
  const float ba = bone * std::clamp(cond, 0.f, 1.f);
  if (ba <= 0.f) return 1.f;
  if (ap <= ba) return hf;
  if (aps < 0.05f) aps = 1.f;
  return std::clamp((ap - ba) / (ap * aps), hf, 1.f);
}
float armorMul(Entity& E, int loc, float ap) {
  if (auto* pl = dynamic_cast<Player*>(&E)) {
    float torso = 0, head = 0, tc = 1, hc = 1, art = 0;
    for (int slot = 0; slot < (int)pl->armor.size() && slot < 2; slot++) {
      const ItemStack& s = pl->armor[slot];
      const ZItem* z = s.empty() ? nullptr : zi(s.id);
      if (!z) continue;
      if (z->raw.contains("suit")) { torso = z->raw["suit"].value("bullet", 0.f); tc = itemCond(s); }
      if (z->raw.contains("helmet")) { head = z->raw["helmet"].value("bullet", 0.f); hc = itemCond(s); }
    }
    for (const ItemStack* a : belt(*pl)) if (const ZItem* z = zi(a->id)) if (z->raw.contains("artifact"))
      art += z->raw["artifact"].value("fx", json::object()).value("bullet", 0.f);
    float bone, cond, hf, aps;
    if (loc == 0) { bone = std::max(0.f, head + art); cond = hc; outfitFrac(head, hf, aps); }
    else if (loc == 2) { bone = std::max(0.f, torso * 0.2f + art); cond = tc; outfitFrac(torso, hf, aps); }
    else { bone = std::max(0.f, torso + art); cond = tc; outfitFrac(torso, hf, aps); }
    return throughArmor(ap, bone, cond, hf, aps);
  }
  if (auto* c = dynamic_cast<Creature*>(&E); c && c->type.rfind("z_npc_", 0) == 0) {
    int tier = c->data.is_object() ? c->data.value("tier", 1) : 1;
    tier = std::clamp(tier, 1, 4);
    static const float body[] = {0, 0.011f, 0.15f, 0.40f, 0.47f};
    static const float limb[] = {0, 0.f, 0.03f, 0.08f, 0.10f};
    static const float head[] = {0, 0.f, 0.011f, 0.30f, 0.30f};
    static const float hfA[] = {0, 0.9f, 0.45f, 0.2f, 0.15f};
    static const float apA[] = {0, 0.9f, 0.8f, 0.75f, 0.7f};
    const float bone = loc == 0 ? head[tier] : loc == 2 ? limb[tier] : body[tier];
    return throughArmor(ap, bone, 1.f, hfA[tier], apA[tier]);
  }
  return 1.f;   // a mutant's hide is not a suit; bone armor 0 keeps the hit
}

// fShootFactor, from the material groups a block is closest to. 0 passes (bush, chain fence).
// A door or crate is a board (0.05); a solid timber block is a trunk (0.5); stone and metal stop a rifle.
struct Mtl { float ap = 1; bool rico = true, glass = false, crate = false; };
bool keyHas(const std::string& k, const char* s) { return k.find(s) != std::string::npos; }
Mtl mtlOf(const BlockDef& B) {
  Mtl m;
  const std::string& k = B.key;
  m.glass = keyHas(k, "glass");
  m.crate = B.has("zoneBreak");
  if (B.render == Render::Cross || keyHas(k, "leaves") || keyHas(k, "leaf") || keyHas(k, "flower") || keyHas(k, "sapling")
      || keyHas(k, "vine") || keyHas(k, "fern") || keyHas(k, "bush") || keyHas(k, "cobweb") || keyHas(k, "dead_bush")
      || keyHas(k, "tall_grass") || keyHas(k, "seagrass") || keyHas(k, "kelp")) {
    m.ap = 0; m.rico = false; return m;
  }
  if (keyHas(k, "bars") || keyHas(k, "chain") || keyHas(k, "barbed")) { m.ap = 0; m.rico = false; return m; }
  if (m.glass) { m.ap = 0.1f; m.rico = false; return m; }
  const bool thin = B.shapeKind == ShapeKind::Fence || B.shapeKind == ShapeKind::Gate || B.shapeKind == ShapeKind::Door
      || B.shapeKind == ShapeKind::Trapdoor || B.shapeKind == ShapeKind::Pane || B.shapeKind == ShapeKind::Ladder
      || B.shapeKind == ShapeKind::Wall || B.shapeKind == ShapeKind::Rod;
  const bool wood = B.sound == "wood" || keyHas(k, "plank") || keyHas(k, "wood") || keyHas(k, "log");
  if (m.crate || (thin && wood)) { m.ap = 0.05f; return m; }
  if (thin && (B.sound == "metal" || keyHas(k, "iron") || keyHas(k, "metal"))) { m.ap = 0.7f; return m; }
  if (wood) { m.ap = 0.5f; return m; }
  if (B.sound == "cloth" || keyHas(k, "wool") || keyHas(k, "carpet") || B.shapeKind == ShapeKind::Carpet) { m.ap = 0.1f; m.rico = false; return m; }
  if (keyHas(k, "brick")) { m.ap = 0.8f; return m; }
  return m;
}
void chipBlock(Level& lv, World& w, const RayHit& wall, const Mtl& m) {
  if (lv.isRemote()) return;
  if (m.glass) { w.setBlock(wall.block.x, wall.block.y, wall.block.z, AIR); lv.fx->sound("glass_break", wall.point, 16); }
  else if (m.crate) breakCrate(lv, w, wall.block.x, wall.block.y, wall.block.z);
}
double leaveCell(glm::dvec3 o, vec3 d, ivec3 cell) {
  auto plane = [](double o, double d, int c) {
    if (std::abs(d) < 1e-8) return 1e9;
    const double face = d > 0 ? c + 1.0 : (double)c;
    const double t = (face - o) / d;
    return t > 1e-4 ? t : 1e9;
  };
  double t = std::min(plane(o.x, d.x, cell.x), std::min(plane(o.y, d.y, cell.y), plane(o.z, d.z, cell.z)));
  if (t > 4) t = 0.25;
  return t + 0.02;
}

struct BodyHit { Entity* e = nullptr; double t = 1e9; int loc = 1; bool head = false; };
BodyHit nearestBody(Level& lv, Player& p, glm::dvec3 o, vec3 d, double maxT) {
  BodyHit best;
  for (auto& e : lv.entities()) {
    Entity* E = e.get();
    if (E->removed || E == &p || E->realm != p.realm || E->type == "item" || E->type == "projectile") continue;
    if (E->isPlayer()) { if (!lv.info.rules.value("pvp", false)) continue; }
    else if (!dynamic_cast<Creature*>(E)) continue;
    if (auto* c = dynamic_cast<Creature*>(E); c && !c->alive()) continue;
    const glm::dvec3 lo(E->pos.x - E->w, E->pos.y, E->pos.z - E->w), hi(E->pos.x + E->w, E->pos.y + E->h, E->pos.z + E->w);
    const double t = rayAABB(o, glm::dvec3(d), lo, hi);
    if (t < 0 || t >= maxT || t >= best.t) continue;
    const float hy = (float)(o.y + d.y * t);
    const float rel = (hy - E->pos.y) / std::max(0.2f, E->h);
    int loc = 1;
    if (rel > 0.8f) loc = 0;
    else if (rel < 0.35f) loc = 2;
    else if (std::hypot((float)(o.x + d.x * t - E->pos.x), (float)(o.z + d.z * t - E->pos.z)) > E->w * 0.7f) loc = 2;
    best = {E, t, loc, loc == 0};
  }
  return best;
}
} // namespace

json gunAttachments(const ItemStack& gun) {
  if (gun.tag.is_object() && gun.tag.contains("att") && gun.tag["att"].is_object()) return gun.tag["att"];
  return json::object();
}
std::vector<std::string> gunSlots(const std::string& id) {
  std::vector<std::string> out;
  if (const ZItem* z = zi(id); z && z->isGun) for (const char* s : {"optic", "muzzle", "under", "mag"}) if (z->gun.fits.contains(s)) out.push_back(s);
  return out;
}

bool attachTo(Player& p, int gi, int ai, std::string& why) {
  if (gi < 0 || ai < 0 || gi >= (int)p.inv.size() || ai >= (int)p.inv.size()) { why = "Nothing there"; return false; }
  ItemStack& g = p.inv[gi]; ItemStack& a = p.inv[ai];
  const ZItem* gz = zi(g.id); const ZItem* az = zi(a.id);
  if (!gz || !gz->isGun) { why = "That's not a gun"; return false; }
  if (!az || !az->raw.contains("att")) { why = "That's not an attachment"; return false; }
  const std::string slot = az->raw["att"].value("slot", "");
  const json& fits = gz->gun.fits.value(slot, json::array());
  if (std::find(fits.begin(), fits.end(), json(a.id)) == fits.end()) { why = az->name + " doesn't fit the " + gz->name; return false; }
  if (!g.tag.is_object()) { g.tag = json::object(); if (!gz->gun.defaultOptic.empty()) g.tag["att"] = json{{"optic", gz->gun.defaultOptic}}; }
  json& att = g.tag["att"]; if (!att.is_object()) att = json::object();
  // what was there comes off into the bag
  if (att.contains(slot) && att[slot].is_string()) { ItemStack old(att[slot].get<std::string>(), 1); att.erase(slot); if (p.give(old) > 0) { why = "No room for the old " + slot; return false; } }
  att[slot] = a.id;
  if (--a.n <= 0) a.clear();
  return true;
}
bool detachFrom(Player& p, int gi, const std::string& slot, std::string& why) {
  if (gi < 0 || gi >= (int)p.inv.size()) { why = "Nothing there"; return false; }
  ItemStack& g = p.inv[gi];
  const ZItem* gz = zi(g.id);
  if (!gz || !gz->isGun) { why = "That's not a gun"; return false; }
  if (!g.tag.is_object()) { g.tag = json::object(); if (!gz->gun.defaultOptic.empty()) g.tag["att"] = json{{"optic", gz->gun.defaultOptic}}; }
  json& att = g.tag["att"];
  if (!att.is_object() || !att.contains(slot)) { why = "Nothing fitted there"; return false; }
  ItemStack old(att[slot].get<std::string>(), 1);
  att.erase(slot);
  if (slot == "under") g.tag.erase("gl");
  if (p.give(old) > 0) { att[slot] = old.id; why = "No room in your bags"; return false; }
  // a smaller magazine spills the extra rounds back
  if (slot == "mag") { const Stats S = statsFor(*gz, g); const int am = g.tag.value("am", 0); if (am > S.clip) { p.give(ItemStack(g.tag.value("at", std::string()), am - S.clip)); g.tag["am"] = S.clip; } }
  return true;
}
bool unloadGun(Player& p, int gi) {
  if (gi < 0 || gi >= (int)p.inv.size()) return false;
  ItemStack& g = p.inv[gi];
  if (!g.tag.is_object() || g.tag.value("am", 0) <= 0) return false;
  const std::string at = g.tag.value("at", std::string()); const int am = g.tag.value("am", 0);
  g.tag["am"] = 0;
  if (!at.empty()) p.give(ItemStack(at, am));
  return true;
}
void cycleAmmo(Player& p, int gi) {
  if (gi < 0 || gi >= (int)p.inv.size()) return;
  ItemStack& g = p.inv[gi]; const ZItem* z = zi(g.id); if (!z || !z->isGun) return;
  std::vector<std::string> types;
  for (auto& s : p.inv) if (!s.empty()) if (const ZItem* a = zi(s.id); a && a->cat == "ammo" && a->raw["ammo"].value("cal", "") == z->gun.cal && std::find(types.begin(), types.end(), s.id) == types.end()) types.push_back(s.id);
  if (types.empty()) return;
  const std::string cur = g.tag.is_object() ? g.tag.value("at", std::string()) : std::string();
  auto it = std::find(types.begin(), types.end(), cur);
  const std::string next = (it == types.end() || ++it == types.end()) ? types[0] : *it;
  if (next == cur) return;
  unloadGun(p, gi);
  if (!g.tag.is_object()) g.tag = json::object();
  g.tag["at"] = next; g.tag["am"] = 0;
}

float gunFovScale(const Player& p) {
  auto it = states().find(p.id); if (it == states().end() || !it->second.ads) return 1;
  const ItemStack& h = p.inv[p.selected];
  const ZItem* z = h.empty() ? nullptr : zi(h.id);
  if (!z || !z->isGun) return 1;
  return 1.f / statsFor(*z, h).adsZoom;
}

GunHud gunHud(const Player& p) {
  GunHud H;
  const ItemStack& h = p.inv[p.selected];
  const ZItem* z = h.empty() ? nullptr : zi(h.id);
  if (!z || !z->isGun) return H;
  const Stats S = statsFor(*z, h);
  H.has = true; H.name = z->name; H.clip = S.clip; H.cond = (int)std::round(condOf(h) * 100);
  const bool gl = h.tag.is_object() && h.tag.value("gl", false) && !S.launcher.empty();
  const std::string at = gl ? launcherAmmo(S.launcher) : h.tag.is_object() ? h.tag.value("at", std::string()) : std::string();
  H.mag = gl ? (h.tag.value("gla", 0)) : h.tag.is_object() ? h.tag.value("am", 0) : 0;
  if (gl) H.clip = 1;
  H.ammo = zi(at) ? zi(at)->name : at.empty() ? "(no rounds)" : at;
  H.reserve = gl ? reserveOf(p, *zi(h.id), "") * 0 + [&] { int n = 0; for (auto& s : p.inv) if (!s.empty() && s.id == at) n += s.n; return n; }() : reserveOf(p, *z, at);
  auto it = states().find(p.id);
  if (it != states().end()) { H.reloading = it->second.reloading; H.reloadT = it->second.reloadLeft; H.ads = it->second.ads; H.jammed = it->second.jammed; H.spread = it->second.cone; }
  H.scope = S.scope; H.reticle = S.reticle;
  return H;
}

// ---------------------------------------------------------------- firing
static void shoot(PlayerController& pc, ItemStack& h, const ZItem& z, const Stats& S, GunState& gs) {
  Level& lv = pc.lv; Player& p = pc.p;
  World& w = lv.world(p.realm);
  const glm::dvec3 eye(p.pos.x, p.pos.y + p.eye, p.pos.z);
  const vec3 L = lookVector(p.yaw, p.pitch);
  const float cone = coneRad(p, S, gs, condOf(h));
  const float muzzle = std::max(1.f, S.spd * S.kSpd);
  const float maxDist = std::max(1.f, S.dist * S.kDist);
  const float power = S.hit * S.kHit * S.dmgMul;
  for (int k = 0; k < S.pellets; k++) {
    vec3 dir = randomDir(L, cone);
    glm::dvec3 o = eye;
    float traveled = 0, pen = 1;
    bool rico = true;
    ivec3 prev{1 << 30, 0, 0};
    for (int step = 0; step < 40 && traveled < maxDist && pen > 1e-3f; step++) {
      const float remain = maxDist - traveled;
      const float span = std::min(remain, TRACE);
      const RayHit wall = raycastBlocks(w, o, dir, span);
      const double limit = wall.hit ? wall.dist : span;
      const BodyHit body = nearestBody(lv, p, o, dir, limit);
      if (body.e) {
        const float dist = traveled + (float)body.t;
        const float sp = speedFrac(dist, muzzle) * pen;
        if (sp > 0 && dist <= maxDist) {
          const float dmg = power * sp * armorMul(*body.e, body.loc, S.kAp) * 100.f * DMG_SCALE;
          DamageSource src; src.type = "bullet"; src.attacker = p.id; src.weapon = h.id; src.tool = "gun";
          src.knock = S.pellets > 1 ? 0.35f : 1.5f;
          src.hasRay = true; src.rayO = vec3(eye); src.rayD = dir;
          if (body.e->ghost && lv.net && lv.net->isClient()) lv.net->hitEntity(p, body.e->id, dmg, {{"tool", "gun"}, {"weapon", h.id}, {"knock", src.knock}, {"crit", body.head}});
          else damageEntity(lv, *body.e, dmg, src);
          lv.fx->particles(body.head ? "crit" : "burst", vec3(o + glm::dvec3(dir) * body.t), body.head ? 6 : 3);
        }
        break;
      }
      if (!wall.hit) { o += glm::dvec3(dir) * (double)span; traveled += span; continue; }
      if (wall.block == prev) { o += glm::dvec3(dir) * 0.5; traveled += 0.5f; continue; }
      prev = wall.block;
      const Mtl m = mtlOf(reg().block(wall.id));
      if (m.ap <= 1e-3f) {
        const double hop = leaveCell(glm::dvec3(wall.point), dir, wall.block);
        o = glm::dvec3(wall.point) + glm::dvec3(dir) * hop;
        traveled += wall.dist + (float)hop;
        continue;
      }
      const float shootF = (S.kAp > 1e-4f && S.kAp >= m.ap) ? (S.kAp - m.ap) / S.kAp : 0.f;
      vec3 n(wall.normal.x, wall.normal.y, wall.normal.z);
      if (glm::dot(n, n) < 0.5f) n = -dir; else n = glm::normalize(n);
      const vec3 bounced = randomDir(dir - 2.f * glm::dot(dir, n) * n, D2R * 10.f);
      const float glance = glm::dot(dir, bounced);
      if (rico && m.rico && (0.5f + frand() * 0.3f) < glance) {
        float scale = 1.f - std::abs(glm::dot(dir, n)) * RICO_LOSS;
        scale = std::clamp(scale, 0.f, RICO_CAP);
        pen *= scale; rico = false;
        dir = bounced;
        o = glm::dvec3(wall.point) + glm::dvec3(dir) * 0.08;
        traveled += wall.dist;
        chipBlock(lv, w, wall, m);
        lv.fx->particles("smoke", wall.point, 1);
        continue;
      }
      chipBlock(lv, w, wall, m);
      if (shootF < 1e-3f) { lv.fx->particles("smoke", wall.point, 2); break; }
      pen *= shootF;
      const vec3 onward = dir;
      const double hop = leaveCell(glm::dvec3(wall.point), onward, wall.block);
      dir = randomDir(onward, D2R * 2.f);
      o = glm::dvec3(wall.point) + glm::dvec3(onward) * hop;
      traveled += wall.dist + (float)hop;
      lv.fx->particles("smoke", wall.point, 1);
    }
  }
  const vec3 muzzleP = vec3(eye) + L * 0.8f;
  if (S.suppressed) lv.fx->sound("bow", muzzleP, 14, 0.7f, 0.55f);
  else {
    const std::string cls = z.gun.cls.empty() ? std::string("pistol") : z.gun.cls;
    lv.fx->sound(std::string("zone_gun_") + cls, muzzleP, 90 * S.loud, 0.9f, 0.97f + 0.06f * frand());
  }
  if (!S.suppressed) lv.fx->particles("smoke", muzzleP, 1);
}

bool gunTick(PlayerController& pc, float dt, const PlayerInput& in) {
  Player& p = pc.p; Level& lv = pc.lv;
  if (!zoneWorld(lv)) return false;
  ItemStack& h = p.inv[p.selected];
  const ZItem* z = h.empty() ? nullptr : zi(h.id);
  GunState& gs = states()[p.id];
  const double tnow = pc.t;
  if (!z || !z->isGun) { gs.ads = false; gs.reloading = false; gs.heldKey.clear(); gs.aimInit = false; return false; }
  if (!gs.aimInit) { gs.yaw0 = p.yaw; gs.pitch0 = p.pitch; gs.aimInit = true; gs.angVel = 0; }
  else if (dt > 1e-4f) gs.angVel = std::min(30.f, std::sqrt(wrapPi(p.yaw - gs.yaw0) * wrapPi(p.yaw - gs.yaw0) + (p.pitch - gs.pitch0) * (p.pitch - gs.pitch0)) / dt);
  gs.yaw0 = p.yaw; gs.pitch0 = p.pitch;
  // a new gun in the hands: it takes a moment to bring up
  if (gs.heldKey != h.id || gs.heldSlot != p.selected) {
    gs.heldKey = h.id; gs.heldSlot = p.selected; gs.reloading = false; gs.jammed = false; gs.next = tnow + 0.35;
    gs.shots = 0; gs.camV = 0; gs.camH = 0; gs.lastShot = -10;
  }
  if (!h.tag.is_object()) { h.tag = json::object(); if (!z->gun.defaultOptic.empty()) h.tag["att"] = json{{"optic", z->gun.defaultOptic}}; h.tag["am"] = 0; h.tag["at"] = firstAmmo(p, *z); }
  const Stats S = statsFor(*z, h);
  const bool gl = h.tag.value("gl", false) && !S.launcher.empty();
  // Use on loot (furniture, a crate's pile, a shelf, a pack) searches / takes it with a gun up too (STALKER's F), instead of aiming
  if (in.usePressed && !gs.ads && pc.target.hit) {
    const BlockDef& tb = reg().block(pc.target.id);
    if (tb.has("container") || tb.has("pile") || tb.has("shelf") || tb.has("furnTop") || tb.has("placedBackpack")) return false;
  }
  gs.ads = in.use && !p.sprinting;
  gs.cone = coneRad(p, S, gs, condOf(h));
  // out of a sprint the gun takes a moment to come back up (render/zone_fpgun.cpp blends the pose out in the same time)
  if (p.sprinting) gs.sprintRec = 0.3f; else gs.sprintRec = std::max(0.f, gs.sprintRec - dt);
  // the fire-mode key raises the underbarrel launcher (or lowers it)
  if (in.fireMode && !S.launcher.empty()) { h.tag["gl"] = !h.tag.value("gl", false); pc.toastMsg(h.tag["gl"].get<bool>() ? "Grenade launcher" : "Rifle", ""); lv.fx->sound("click", p.pos, 6); }
  // clearing a jam
  if (gs.jammed) {
    if ((in.reload || in.attackPressed) && gs.jamClear <= 0) { gs.jamClear = condOf(h) < 0.2f ? 2.5f : 1.2f; lv.fx->sound("door_iron_open", p.pos, 8, 0.5f, 1.8f); }
    if (gs.jamClear > 0 && (gs.jamClear -= dt) <= 0) { gs.jammed = false; pc.toastMsg("Jam cleared", ""); }
    return true;
  }
  // reloading
  if (gs.reloading) {
    gs.reloadLeft -= dt;
    if (in.attackPressed && S.slowReload && h.tag.value("am", 0) > 0) gs.reloading = false;   // a pump gun: shoot what's in
    if (gs.reloadLeft <= 0) {
      if (gl) {
        const std::string ga = launcherAmmo(S.launcher);
        if (takeAmmo(p, ga, 1) == 1) h.tag["gla"] = 1;
        gs.reloading = false;
      } else {
        std::string at = h.tag.value("at", std::string());
        if (at.empty() || reserveOf(p, *z, at) == 0) { at = firstAmmo(p, *z); h.tag["at"] = at; }
        const int am = h.tag.value("am", 0);
        const int want = S.slowReload ? 1 : S.clip - am;
        const int got = at.empty() ? 0 : takeAmmo(p, at, want);
        h.tag["am"] = am + got;
        lv.fx->sound("click_off", p.pos, 6, 0.8f, 0.8f);
        if (S.slowReload && h.tag.value("am", 0) < S.clip && reserveOf(p, *z, at) > 0) gs.reloadLeft = S.reload;
        else gs.reloading = false;
      }
    }
    return true;
  }
  const int am = gl ? h.tag.value("gla", 0) : h.tag.value("am", 0);
  if (in.reload || (in.attackPressed && am == 0)) {
    const std::string at = gl ? launcherAmmo(S.launcher) : h.tag.value("at", std::string());
    const int have = gl ? [&] { int n = 0; for (auto& s : p.inv) if (!s.empty() && s.id == at) n += s.n; return n; }() : (reserveOf(p, *z, at) + reserveOf(p, *z, ""));
    if (am < (gl ? 1 : S.clip) && have > 0) {
      gs.reloading = true; gs.reloadLeft = gl ? 2.4f : S.reload * (am == 0 && !S.slowReload ? 1.15f : 1.f);
      lv.fx->sound("door_iron_close", p.pos, 8, 0.6f, 2.f);
      return true;
    }
    if (in.attackPressed) { lv.fx->sound("click", p.pos, 6); if (have == 0) pc.toastMsg("No " + z->gun.cal + " rounds", "warn"); }
    return true;
  }
  // firing
  const bool trigger = (S.autoFire && !gl) ? in.attack : in.attackPressed;
  if (trigger && tnow >= gs.next && am > 0 && !p.sprinting && gs.sprintRec <= 0) {
    if (gl) {
      const vec3 L = lookVector(p.yaw, p.pitch);
      spawnProjectile(lv, p.realm, p.pos + vec3(0, p.eye - 0.1f, 0) + L * 0.8f, L * 30.f + vec3(0, 2.f, 0), "cannonball", &p);
      h.tag["gla"] = 0; gs.next = tnow + 0.6;
      lv.fx->sound("firework_launch", p.pos, 40, 0.9f, 0.7f);
      p.pitch = std::clamp(p.pitch + 0.06f, -1.55f, 1.55f);
      return true;
    }
    const float c = condOf(h);
    if (frand() < misfireP(c, S)) { gs.jammed = true; gs.jamClear = 0; pc.toastMsg("Jammed! Press reload to clear it", "warn"); lv.fx->sound("click", p.pos, 8, 1, 0.7f); return true; }
    h.tag["am"] = am - 1;
    gs.next = std::max(gs.next, tnow - dt) + S.delay;
    if (!S.autoFire || tnow - gs.lastShot > (double)S.delay * 1.6 + 0.05) { gs.shots = 0; gs.camV = 0; gs.camH = 0; }
    if (z->gun.launcher) {
      const vec3 L = lookVector(p.yaw, p.pitch);
      const vec3 d = randomDir(L, coneRad(p, S, gs, c));
      spawnProjectile(lv, p.realm, p.pos + vec3(0, p.eye - 0.1f, 0) + d * 0.9f, d * 26.f, "rocket", &p);
      lv.fx->sound("firework_launch", p.pos, 60, 1, 0.6f);
    } else shoot(pc, h, *z, S, gs);
    camKick(p, S, gs);
    {
      const float shot = (S.autoFire && gs.shots > 0) ? S.qdec : S.dec;
      if (!p.creative()) {
        const int dur = durOf(h);
        float acc = h.tag.value("wd", 0.f) + shot * S.impair * (float)dur;
        const int add = (int)std::floor(acc);
        h.tag["wd"] = acc - (float)add;
        if (add > 0) h.d = std::min(dur, h.d + add);
      }
    }
    gs.shots += 1;
    gs.lastShot = tnow;
    pc.swingArm();
    return true;
  }
  if (in.attackPressed && am == 0) lv.fx->sound("click", p.pos, 6);
  return true;   // a gun in the hands never digs or punches
}

} // namespace dq::zone
