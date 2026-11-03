// render/zone_anomaly_fx.cpp — what gives each anomaly away, on this device (sim/zone_anomaly.cpp runs them on every
// machine, so zone::anomaliesLive is the same list here as on the host): particles within FX_R of the player — a dust
// devil over an undertow, a shimmer and swirling leaves over a kickback, debris turning over a spinner, glass glinting
// in a shardfield, embers over a flare vent and its pillar of fire, the walking fireball, steam, grey moss strands,
// the arc nest's crackle and its arcs, the humming ball, the acid's green bubbles, the gas cloud, the blister, the
// mire's slow bubbles, violet motes in a psy field, pollen over the poppies and the wisps at night — and their idle
// sounds (audio/zone_sfx.cpp zone_ano_*). The screen's share (zone::anomalySense: the flash, the gas, the drowse,
// the skip, the spin, the wisps) is drawn over the HUD.
#include "client/session.h"
#include "client/app.h"
#include "audio/audio.h"
#include "render/particles.h"
#include "sim/zone.h"
#include "sim/zone_anomalies.h"
#include "sim/level.h"
#include "sim/player.h"
#include <imgui.h>
#include <unordered_map>
#include <cmath>

namespace dq {

namespace {
using namespace zone;
inline float rnd() { static u32 s = 0x51ed270bu; s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFFFF) / 16777216.f; }
inline float rs() { return rnd() * 2 - 1; }
constexpr float FX_R = 42;

struct Kit { u16 white = 0, smoke = 0, flame = 0, ember = 0, firefly = 0; };

class ZoneAnomalyFx : public RenderLayer {
public:
  const char* name() const override { return "zone_anomaly_fx"; }
  void update(GameSession& s, double dtIn) override {
    if (!s.player || !s.level || s.paused() || dtIn <= 0 || !zoneWorld(*s.level) || s.player->realm != REALM_MAIN) return;
    const float dt = (float)std::min(dtIn, 0.1);
    ParticleSystem& P = particles();
    if (P.level <= 0) return;
    if (!K.white) { K.white = (u16)P.tile("white"); K.smoke = (u16)P.tile("smoke"); K.flame = (u16)P.tile("torch_flame"); K.ember = (u16)P.tile("ember_spark"); K.firefly = (u16)P.tile("firefly"); }
    amount = P.level == 1 ? 0.45f : 1.f;
    const vec3 me = s.player->pos;
    const double now = s.level->info.time;
    AudioEngine* au = s.app.audio.get();
    for (const AnoLive& L : anomaliesLive(*s.level)) {
      const float d = glm::distance(vec2(L.c.x, L.c.z), vec2(me.x, me.z));
      if (d > FX_R + L.r || std::abs(L.c.y - me.y) > 30) continue;
      tell(P, L, dt, now, d);
      if (au && d < 26) idleSound(*au, L, dt);
    }
  }
  void drawHud(GameSession& s) override {
    if (!s.player || !s.level || !zoneWorld(*s.level)) return;
    const AnoSense S = anomalySense(*s.level, s.player->id);
    const float W = (float)s.app.width, H = (float)s.app.height;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    auto fill = [&](int r, int g, int b, float a) { if (a > 0.01f) dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(r, g, b, (int)(std::min(a, 1.f) * 255))); };
    auto edges = [&](int r, int g, int b, float a) {   // a vignette: dark at the rims
      if (a <= 0.01f) return;
      const ImU32 c = IM_COL32(r, g, b, (int)(std::min(a, 1.f) * 255)), z = IM_COL32(r, g, b, 0);
      const float e = std::min(W, H) * 0.35f;
      dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, e), c, c, z, z);
      dl->AddRectFilledMultiColor(ImVec2(0, H - e), ImVec2(W, H), z, z, c, c);
      dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(e, H), c, z, z, c);
      dl->AddRectFilledMultiColor(ImVec2(W - e, 0), ImVec2(W, H), z, c, c, z);
    };
    fill(150, 190, 60, S.gas * 0.28f);
    edges(10, 6, 14, S.drowse * 0.8f);
    if (S.asleep > 0) fill(4, 2, 8, 0.93f);
    fill(170, 90, 230, S.skip * 0.5f);
    edges(120, 10, 10, S.spin * 0.6f);
    edges(120, 90, 200, S.wisps * 0.45f);
    fill(255, 255, 250, S.flash * 0.9f);
  }

private:
  Kit K; float amount = 1;
  std::unordered_map<u32, float> sndT;

  int count(float rate, float dt) { return (int)std::floor(rate * amount * dt + rnd()); }
  Particle base(u16 tile, vec3 p, vec3 v, float life, float size) { Particle q; q.tile = tile; q.p = p; q.v = v; q.life = q.max = life; q.size = q.size0 = size; return q; }
  void tint(Particle& q, int r, int g, int b) { q.tint[0] = (u8)r; q.tint[1] = (u8)g; q.tint[2] = (u8)b; }
  void glowDot(ParticleSystem& P, vec3 p, vec3 v, float life, float size, int r, int g, int b, float blink = 0) {
    Particle q = base(K.white, p, v, life, size); q.light = 0xFF; q.glow = true; q.shrink = true; q.blink = blink; q.ph = rnd() * 6; tint(q, r, g, b); P.add(q);
  }
  void puff(ParticleSystem& P, vec3 p, vec3 v, float life, float size, int r, int g, int b, float alpha, float grow = 1) {
    Particle q = base(K.smoke, p, v, life, size); q.soft = true; q.alpha = alpha; q.grow = grow; q.light = 0xF0 | 12; tint(q, r, g, b); P.add(q);
  }
  // a jagged arc from a to b: a few short glowing strokes
  void arc(ParticleSystem& P, vec3 a, vec3 b, int r, int g, int bl) {
    const int n = 6; vec3 prev = a;
    for (int i = 1; i <= n; i++) {
      vec3 q = glm::mix(a, b, i / (float)n); if (i < n) q += vec3(rs(), rs(), rs()) * 0.35f;
      const vec3 d = q - prev; const float len = glm::length(d);
      if (len > 1e-3f) {
        Particle s = base(K.white, (prev + q) * 0.5f, vec3(0), 0.12f, 0.05f); s.sd = d / len; s.streak = len * 0.5f; s.light = 0xFF; s.glow = true; tint(s, r, g, bl);
        P.addForced(s);
      }
      prev = q;
    }
  }
  vec3 ring(const AnoLive& L, float rad, float y) { const float a = rnd() * 6.2832f, rr = rad * std::sqrt(rnd()); return L.c + vec3(std::cos(a) * rr, y, std::sin(a) * rr); }

  void tell(ParticleSystem& P, const AnoLive& L, float dt, double now, float dist) {
    const float near = dist < 18 ? 1.f : 0.6f;
    switch (L.kind) {
      case AK_UNDERTOW: {   // a dust devil: grit circling up round the core; drawn in hard while it clenches, flung out when it bursts
        const float R = L.r + 1.5f;
        for (int n = count((L.state ? 90.f : 34.f) * near, dt); n > 0; n--) {
          const float a = rnd() * 6.2832f, rr = (L.state ? 0.4f + rnd() * 1.2f : 0.6f + rnd() * R * 0.7f), h = rnd() * 2.8f;
          const vec3 p = L.c + vec3(std::cos(a) * rr, 0.1f + h, std::sin(a) * rr);
          const vec3 tang(-std::sin(a), 0, std::cos(a));
          Particle q = base(K.white, p, tang * (2.5f + 2.f * (1 - rr / R)) + vec3(-std::cos(a), 0.5f, -std::sin(a)) * (L.state ? 3.f : 0.6f), 0.9f + rnd() * 0.6f, 0.03f + rnd() * 0.03f);
          q.light = 0xF0 | 10; tint(q, 150 + (int)(rnd() * 40), 128, 96); q.shrink = true; P.add(q);
        }
        if (rnd() < dt * 1.4f * amount) puff(P, ring(L, L.r * 0.5f, 0.3f), vec3(0, 0.8f, 0), 1.6f, 0.35f, 150, 132, 104, 0.25f, 1.5f);
        if (L.fire > 0) for (int n = count(260, dt); n > 0; n--) { const float a = rnd() * 6.2832f; puff(P, L.c + vec3(0, 0.6f, 0), vec3(std::cos(a) * 7, 2 + rnd() * 3, std::sin(a) * 7), 0.8f, 0.4f, 160, 140, 110, 0.5f, 2); }
        break;
      }
      case AK_KICKBACK: {   // the air wobbles: faint clear blobs and a few dry leaves turning
        for (int n = count(10 * near, dt); n > 0; n--) { Particle q = base(K.white, ring(L, L.r, 0.3f + rnd() * 1.8f), vec3(rs() * 0.2f, 0.15f, rs() * 0.2f), 1.4f, 0.12f + rnd() * 0.1f); q.soft = true; q.alpha = 0.12f; q.grow = 0.6f; q.light = 0xFF; tint(q, 230, 236, 255); P.add(q); }
        for (int n = count(3, dt); n > 0; n--) { Particle q = base(K.white, ring(L, L.r * 0.8f, 0.2f), vec3(rs() * 1.5f, 1.2f + rnd(), rs() * 1.5f), 1.5f, 0.05f); q.flutter = 0.4f; q.ph = rnd() * 6; q.grav = 1.5f; q.light = 0xF0 | 10; tint(q, 140, 110, 50); P.add(q); }
        if (L.fire > 0) for (int n = count(160, dt); n > 0; n--) { const float a = rnd() * 6.2832f; puff(P, L.c + vec3(0, 1, 0), vec3(std::cos(a) * 9, 1, std::sin(a) * 9), 0.5f, 0.3f, 220, 220, 230, 0.35f, 2); }
        break;
      }
      case AK_SPINNER: {   // debris turning slowly in the air over it
        for (int n = count(8 * near, dt); n > 0; n--) {
          const float a = rnd() * 6.2832f, rr = 0.4f + rnd() * L.r * 0.8f;
          const vec3 p = L.c + vec3(std::cos(a) * rr, 0.6f + rnd() * 2.f, std::sin(a) * rr);
          Particle q = base(K.white, p, vec3(-std::sin(a), 0.05f, std::cos(a)) * (L.state ? 6.f : 1.2f), 2.f, 0.04f + rnd() * 0.05f); q.light = 0xF0 | 9; tint(q, 90, 84, 76); P.add(q);
        }
        if (L.state == 1) for (int n = count(120, dt); n > 0; n--) puff(P, L.c + vec3(rs() * 0.8f, 1.4f + rnd() * 2.5f, rs() * 0.8f), vec3(rs() * 3, 1, rs() * 3), 0.5f, 0.25f, 140, 120, 100, 0.35f);
        if (L.fire > 0) for (int n = count(200, dt); n > 0; n--) glowDot(P, L.c + vec3(0, 2.5f, 0), vec3(rs() * 9, rs() * 6 + 2, rs() * 9), 0.6f, 0.03f, 160, 30, 20);
        break;
      }
      case AK_SHARDS:   // glass hanging in the air, catching the light
        for (int n = count(14 * near, dt); n > 0; n--) glowDot(P, ring(L, L.r, 0.3f + rnd() * 1.9f), vec3(rs() * 0.05f, 0.02f, rs() * 0.05f), 2.5f + rnd() * 2, 0.022f, 200, 235, 255, 4 + rnd() * 3);
        break;
      case AK_SKIP:   // a fold: a violet shimmer standing in the air
        for (int n = count(16 * near, dt); n > 0; n--) { const float a = rnd() * 6.2832f; glowDot(P, L.c + vec3(std::cos(a) * 0.6f, 0.2f + rnd() * 2, std::sin(a) * 0.6f), vec3(-std::sin(a) * 0.8f, 0.3f, std::cos(a) * 0.8f), 1.f, 0.025f, 190, 120, 255); }
        if (L.fire > 0) for (int n = count(160, dt); n > 0; n--) glowDot(P, L.c + vec3(0, 1, 0), vec3(rs(), rs(), rs()) * 4.f, 0.5f, 0.04f, 210, 160, 255);
        break;
      case AK_FLARE:   // embers over the crack; the pillar when it goes off
        for (int n = count(10 * near, dt); n > 0; n--) { Particle q = base(K.ember, L.c + vec3(rs() * 0.5f, 0.05f, rs() * 0.5f), vec3(rs() * 0.3f, 0.8f + rnd() * 0.8f, rs() * 0.3f), 1.2f + rnd(), 0.018f); q.light = 0xFF; q.shrink = true; q.grav = -0.3f; P.add(q); }
        if (rnd() < dt * 0.8f * amount) puff(P, L.c + vec3(0, 0.3f, 0), vec3(0, 0.7f, 0), 2.5f, 0.25f, 60, 56, 52, 0.3f, 1.5f);
        if (L.fire > 0) for (int n = count(220, dt); n > 0; n--) {
          Particle q = base(K.flame, L.c + vec3(rs() * L.r * 0.4f, 0.1f, rs() * L.r * 0.4f), vec3(rs() * 0.6f, 6 + rnd() * 4, rs() * 0.6f), 0.55f + rnd() * 0.25f, 0.25f + rnd() * 0.2f);
          q.light = 0xFF; q.glow = true; q.shrink = true; P.add(q);
        }
        break;
      case AK_WANDERFIRE:   // the fireball itself
        for (int n = count(70, dt); n > 0; n--) { Particle q = base(K.flame, L.pos + vec3(rs(), rs(), rs()) * 0.35f, vec3(rs() * 0.4f, 0.8f + rnd(), rs() * 0.4f), 0.35f + rnd() * 0.2f, 0.18f + rnd() * 0.14f); q.light = 0xFF; q.glow = true; q.shrink = true; P.add(q); }
        for (int n = count(12, dt); n > 0; n--) { Particle q = base(K.ember, L.pos, vec3(rs(), rnd() * 2, rs()) * 1.5f, 0.8f, 0.016f); q.light = 0xFF; q.shrink = true; P.add(q); }
        break;
      case AK_STEAM:   // a wisp between gusts, a scalding plume on the beat
        for (int n = count(L.fire > 0 ? 70.f : 2.5f * near, dt); n > 0; n--)
          puff(P, L.c + vec3(rs() * 0.25f, 0.05f, rs() * 0.25f), vec3(rs() * 0.4f, L.fire > 0 ? 6.f + rnd() * 3 : 0.7f, rs() * 0.4f), L.fire > 0 ? 1.2f : 2.5f, 0.25f, 236, 238, 240, L.fire > 0 ? 0.6f : 0.25f, 2);
        break;
      case AK_ASHMOSS:   // grey strands hanging off the dead tree, shedding ash; a flare where a bolt touched one
        for (const vec4& b : L.bits) {
          if (rnd() < dt * 6 * amount * near) { Particle q = base(K.white, vec3(b.x, b.y - rnd() * b.w, b.z) + vec3(rs() * 0.06f, 0, rs() * 0.06f), vec3(0, -0.15f, 0), 1.5f, 0.05f); q.light = 0xF0 | 10; tint(q, 120, 120, 116); q.flutter = 0.1f; P.add(q); }
          if (rnd() < dt * 0.7f * amount) { Particle q = base(K.ember, vec3(b.x, b.y - rnd() * b.w, b.z), vec3(rs() * 0.1f, 0.3f, rs() * 0.1f), 0.8f, 0.016f); q.light = 0xFF; q.shrink = true; P.add(q); }
        }
        if (L.fire > 0) for (int n = count(80, dt); n > 0; n--) { Particle q = base(K.flame, L.hitAt + vec3(rs() * 0.2f, rnd() * 0.6f, rs() * 0.2f), vec3(0, 1.5f, 0), 0.4f, 0.15f); q.light = 0xFF; q.glow = true; q.shrink = true; P.add(q); }
        break;
      case AK_ARCNEST:   // the crackle as it charges; the arcs when it lets go
        for (int n = count((8 + 50 * L.charge * L.charge) * near, dt); n > 0; n--) glowDot(P, ring(L, L.r * 0.7f, 0.1f + rnd() * 1.4f), vec3(rs(), rs() + 0.5f, rs()) * 0.6f, 0.2f + rnd() * 0.2f, 0.035f, 150, 220, 255);
        if (rnd() < dt * (1.5f + 8 * L.charge) * amount) { const vec3 a = L.c + vec3(rs() * 0.4f, 0.15f, rs() * 0.4f); arc(P, a, a + vec3(rs() * 1.4f, 0.6f + rnd() * 1.6f * L.charge, rs() * 1.4f), 150, 220, 255); }
        if (rnd() < dt * 2 * amount) glowDot(P, L.c + vec3(0, 0.3f, 0), vec3(0), 0.2f, 0.12f + 0.1f * L.charge, 120, 200, 255);   // the knot it gathers in
        if (L.hitT > 0 && rnd() < 0.7f) arc(P, L.c + vec3(0, 0.4f, 0), L.hitAt, 200, 240, 255);
        break;
      case AK_BALL:   // the humming ball rolling its loop
        if (L.hidden <= 0) {
          for (int n = count(60, dt); n > 0; n--) glowDot(P, L.pos + vec3(rs(), rs(), rs()) * 0.22f, vec3(rs(), rs(), rs()) * 0.3f, 0.25f, 0.06f + rnd() * 0.05f, 170 + (int)(rnd() * 60), 220, 255);
          if (rnd() < dt * 4 * amount) arc(P, L.pos, L.pos + vec3(rs(), rs(), rs()) * 0.9f, 200, 230, 255);
        }
        if (L.hitT > 0 && rnd() < 0.7f) arc(P, L.pos, L.hitAt, 220, 240, 255);
        break;
      case AK_FLASHPOP:   // hidden in the grass: a spark now and then is the only sign
        if (L.cd <= 0 && rnd() < dt * 0.9f * amount) glowDot(P, ring(L, L.r * 0.6f, 0.15f), vec3(0, 0.6f, 0), 0.25f, 0.03f, 255, 250, 200);
        if (L.fire > 0) for (int n = count(300, dt); n > 0; n--) glowDot(P, L.c + vec3(0, 0.5f, 0), vec3(rs(), rnd(), rs()) * 6.f, 0.3f, 0.04f, 255, 255, 230);
        break;
      case AK_SPARKPOOL:   // sparks skittering over the water
        for (int n = count(12 * near, dt); n > 0; n--) glowDot(P, ring(L, L.r, -0.05f), vec3(rs() * 2, 0.4f, rs() * 2), 0.12f + rnd() * 0.1f, 0.022f, 160, 230, 255);
        if (rnd() < dt * 1.2f * amount) { const vec3 a = ring(L, L.r * 0.8f, -0.05f); arc(P, a, a + vec3(rs() * 1.5f, 0.1f, rs() * 1.5f), 150, 220, 255); }
        if (L.hitT > 0 && rnd() < 0.6f) arc(P, L.c, L.hitAt, 200, 240, 255);
        break;
      case AK_ACID:   // glowing green bubbles and a sickly haze
        for (int n = count(10 * near, dt); n > 0; n--) glowDot(P, ring(L, L.r, -0.1f), vec3(rs() * 0.05f, 0.25f + rnd() * 0.3f, rs() * 0.05f), 0.9f, 0.035f + rnd() * 0.03f, 150, 255, 60);
        if (rnd() < dt * 1.5f * amount) puff(P, ring(L, L.r, 0.1f), vec3(0, 0.3f, 0), 3, 0.4f, 150, 220, 70, 0.22f, 1.2f);
        if (L.fire > 0) for (int n = count(120, dt); n > 0; n--) {   // a bolt fizzing down: a boil of froth over the whole pool, a hiss of green
          glowDot(P, L.hitAt + vec3(rs() * 0.3f, 0.05f, rs() * 0.3f), vec3(rs() * 0.6f, 0.9f + rnd() * 0.8f, rs() * 0.6f), 0.7f, 0.04f + rnd() * 0.03f, 170, 255, 80);
          if (rnd() < 0.3f) glowDot(P, ring(L, L.r, 0.f), vec3(0, 0.4f + rnd() * 0.4f, 0), 0.9f, 0.04f, 150, 255, 60);
        }
        break;
      case AK_GAS:   // a yellow haze lying low; the cloud when it puffs
        if (rnd() < dt * 2.f * amount * near) puff(P, ring(L, L.r, 0.15f), vec3(rs() * 0.1f, 0.08f, rs() * 0.1f), 3.5f, 0.5f, 196, 204, 80, 0.16f, 0.8f);
        if (L.fire > 0) for (int n = count(40, dt); n > 0; n--) puff(P, ring(L, L.r + 1.5f, 0.2f + rnd() * 1.8f), vec3(rs() * 0.6f, 0.3f, rs() * 0.6f), 2.2f, 0.6f, 190, 200, 70, 0.45f, 1.5f);
        break;
      case AK_BLISTER:   // the drifting bubble and the small ones it burst into
        if (L.hidden <= 0) for (int n = count(40, dt); n > 0; n--) {
          const vec3 o = glm::normalize(vec3(rs(), rs(), rs()) + vec3(1e-3f)) * (0.45f + 0.05f * std::sin((float)now * 4));
          glowDot(P, L.pos + o, vec3(0), 0.15f, 0.045f, 170, 230, 90);
        }
        for (const vec4& b : L.bits) if (rnd() < 0.5f) glowDot(P, vec3(b) + vec3(rs(), rs(), rs()) * 0.15f, vec3(0), 0.12f, 0.04f, 180, 240, 100);
        break;
      case AK_MIRE:   // slow bubbles working up through the mud
        if (rnd() < dt * 2.5f * amount * near) puff(P, ring(L, L.r, -0.05f), vec3(0, 0.35f, 0), 1.2f, 0.12f, 100, 84, 56, 0.4f, 0.6f);
        if (L.fire > 0 && rnd() < dt * 30) puff(P, ring(L, L.r, 0.f), vec3(rs() * 0.4f, 0.8f, rs() * 0.4f), 1.f, 0.2f, 96, 80, 52, 0.5f, 0.8f);   // a bolt swallowed: the mud heaves
        break;
      case AK_PSYFIELD:   // violet motes and a faint shimmer
        for (int n = count(8 * near, dt); n > 0; n--) glowDot(P, ring(L, L.r, 0.3f + rnd() * 2.2f), vec3(rs() * 0.1f, 0.12f, rs() * 0.1f), 3.f, 0.025f, 180, 120, 255, 2 + rnd() * 2);
        break;
      case AK_LULL:   // pollen hanging over the poppies
        for (int n = count(6 * near, dt); n > 0; n--) { Particle q = base(K.white, ring(L, L.r, 0.4f + rnd() * 1.2f), vec3(rs() * 0.08f, 0.04f, rs() * 0.08f), 4.f, 0.02f); q.light = 0xF8; q.flutter = 0.3f; q.ph = rnd() * 6; q.glow = true; tint(q, 255, 200, 160); P.add(q); }
        break;
      case AK_WISPS:   // the lights over the ground at night
        for (const vec4& w : L.bits) {
          for (int n = count(30, dt); n > 0; n--) glowDot(P, vec3(w) + vec3(rs(), rs(), rs()) * 0.12f, vec3(0, 0.1f, 0), 0.3f, 0.05f + rnd() * 0.03f, 200, 230, 255);
        }
        break;
      default: break;
    }
  }
  void idleSound(AudioEngine& au, const AnoLive& L, float dt) {
    const char* nm = nullptr; float gap = 3;
    switch (L.kind) {
      case AK_UNDERTOW: if (L.cd <= 0) { nm = "zone_ano_gust"; gap = 2.6f; } break;
      case AK_SHARDS: nm = "zone_ano_chime"; gap = 3.5f; break;
      case AK_FLARE: nm = "zone_ano_fire"; gap = 3.2f; break;
      case AK_WANDERFIRE: nm = "zone_ano_fire"; gap = 1.3f; break;
      case AK_ARCNEST: if (L.charge > 0.5f) { nm = "zone_ano_crackle"; gap = 1.6f; } break;
      case AK_BALL: if (L.hidden <= 0) { nm = "zone_ano_hum"; gap = 1.2f; } break;
      case AK_SPARKPOOL: nm = "zone_ano_crackle"; gap = 2.8f; break;
      case AK_ACID: nm = "zone_ano_fizz"; gap = 2.6f; break;
      case AK_MIRE: nm = "zone_ano_glop"; gap = 3.5f; break;
      case AK_PSYFIELD: nm = "zone_whisper"; gap = 6; break;
      case AK_LULL: nm = "zone_ano_lull"; gap = 4.5f; break;
      case AK_WISPS: if (!L.bits.empty()) { nm = "zone_ano_wisp"; gap = 3.5f; } break;
      case AK_ASHMOSS: nm = "zone_ano_sizzle"; gap = 5; break;
      default: break;
    }
    if (!nm) return;
    float& t = sndT[L.id];
    if ((t -= dt) > 0) return;
    t = gap * (0.8f + rnd() * 0.5f);
    const vec3 at = (L.kind == AK_WANDERFIRE || L.kind == AK_BALL) ? L.pos : L.c + vec3(0, 0.6f, 0);
    au.playAt(nm, at, 22, 0.75f, 0.92f + rnd() * 0.16f);
  }
};
} // namespace

void registerZoneAnomalyFx() {
  static bool done = false; if (done) return; done = true;
  registerRenderLayer([](GameSession&) -> std::shared_ptr<RenderLayer> { return std::make_shared<ZoneAnomalyFx>(); });
}

} // namespace dq
