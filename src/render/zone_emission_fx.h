// render/zone_emission_fx.h — the emission's show (Call of Pripyat), worked out from its clock alone (sim/zone.h Emission:
// phase, t), so the sky (sky.cpp), the clouds (clouds.cpp), the screen (zone_emission_fx.cpp) and the sound
// (audio/ambience.cpp) agree on every lightning bolt and every pass of the wave without telling each other anything.
// Phase 1 the warning: far bolts toward the plant, more and more of them; phase 2 the wave: bolts all round and, twice,
// a front of light rolling over the sky from the north (the plant) to the south; phase 3 the aftermath: a few far bolts.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dq::emfx {

inline float h01(uint32_t a, uint32_t b) {
  uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
  h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
  return (float)(h & 0xFFFFFF) / 16777215.f;
}

// the latest bolt: id (-1 none), how long ago, which way (world angle: direction (cos az, 0, sin az); north, -z, is -pi/2),
// how near (0 far off .. 1 overhead), and the flash it makes now (0..1: a stroke, a dark gap, a restrike, then fading)
struct Bolt { int id = -1; float age = 1e9f, az = 0, near = 0, flash = 0, delay = 0; };
inline Bolt bolt(int phase, float t) {
  Bolt b;
  float slot, prob, nearK, spread;
  if (phase == 1) { slot = 6.f; prob = 0.25f + 0.65f * std::min(1.f, t / 90.f); nearK = 0.3f; spread = 1.1f; }
  else if (phase == 2) { slot = 2.2f; prob = 0.88f; nearK = 1.f; spread = 3.14159f; }
  else if (phase == 3 && t < 40) { slot = 7.f; prob = 0.5f; nearK = 0.35f; spread = 1.6f; }
  else return b;
  const int s0 = (int)std::floor(t / slot);
  for (int s = s0; s >= std::max(0, s0 - 1); s--) {
    const uint32_t k = (uint32_t)(phase * 7919 + s);
    if (h01(k, 1) > prob) continue;
    const float at = (s + 0.05f + 0.6f * h01(k, 2)) * slot;
    if (at > t) continue;
    b.id = phase * 100000 + s; b.age = t - at;
    b.az = -1.5708f + (h01(k, 3) * 2 - 1) * spread;
    b.near = nearK * (0.35f + 0.65f * h01(k, 4));
    b.delay = 0.12f + (1 - b.near) * 2.6f;   // the thunder comes after the light: farther, later
    break;
  }
  if (b.id >= 0) {
    const float a = b.age;
    float f = a < 0.06f ? 1.f : a < 0.11f ? 0.2f : a < 0.19f ? 0.85f : a < 0.24f ? 0.3f : 0.65f * std::exp(-(a - 0.24f) * 7.f);
    if (a > 1.2f) f = 0;
    b.flash = f * (0.35f + 0.65f * b.near);
  }
  return b;
}

// the wave's front: s = where it is on the sky (the dot of a direction with north: 1 the north horizon, 0 overhead, -1
// the south one), k its strength (0 none), u how far through its pass (0..1), push how hard it shoves you as it goes over
struct Sweep { int id = -1; float s = 9, k = 0, u = 0, push = 0; };
constexpr float SWEEP_AT[2] = {14.f, 40.f}, SWEEP_LEN = 18.f;
inline Sweep sweep(int phase, float t) {
  Sweep w;
  if (phase != 2) return w;
  for (int i = 0; i < 2; i++) {
    const float u = (t - SWEEP_AT[i]) / SWEEP_LEN;
    if (u < 0 || u > 1) continue;
    w.id = i; w.u = u; w.s = 1.15f - 2.3f * u;
    w.k = std::sqrt(std::sin(u * 3.14159f)) * (i == 0 ? 0.85f : 1.f);
    w.push = std::exp(-std::pow((u - 0.5f) / 0.09f, 2.f)) * (i == 0 ? 0.7f : 1.f);   // overhead
  }
  return w;
}
// when a pass of the wave is overhead (for the sound): SWEEP_AT[i] + SWEEP_LEN / 2
inline float sweepOverhead(int i) { return SWEEP_AT[i] + SWEEP_LEN * 0.5f; }

} // namespace dq::emfx
