// render/zone_anomaly_fx.cpp — what gives each anomaly away, on this device (sim/zone_anomaly.cpp runs them on every
// machine, so zone::anomaliesLive is the same list here as on the host). The three gravities do not share a picture:
// a kickback is a red stain and jumping leaves (the screen brightens), a spinner is a tall pale dust column, an
// undertow is a dark inward drag. A flare vent is a heat shimmer until its pillar; a steam vent is a white column on
// a beat; a wanderfire is a moving ball. An arc nest is sitting lightning; ball lightning is the ball. An acid pool
// is wet and low, a gas pocket is an ankle-high bank, a blister is a thick wandering mass, ash moss hangs.
// Idle sounds are audio/zone_sfx.cpp zone_ano_*. The screen's share (zone::anomalySense) is drawn over the HUD.
#include "client/session.h"
#include "client/app.h"
#include "audio/audio.h"
#include "render/particles.h"
#include "sim/zone.h"
#include "sim/zone_anomalies.h"
#include "sim/steam.h"
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
    fill(255, 228, 196, S.bright * 0.5f);                 // kickback: the air brightens as you close
    edges(6, 5, 8, S.crush * 0.9f);                       // undertow: a dark vignette closing in
    edges(168, 140, 90, S.spin * 0.62f);                  // spinner: a pale lift at the edges, not the crush and not the shock
    edges(150, 170, 50, S.gas * 0.55f);                   // the drifting bank, around you
    fill(150, 230, 60, S.acid * 0.34f);                   // the puddle: a wet splash while you stand in it
    edges(10, 6, 14, S.drowse * 0.8f);
    if (S.asleep > 0) fill(4, 2, 8, 0.93f);
    fill(170, 90, 230, S.skip * 0.5f);
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
      case AK_UNDERTOW: {   // crush: grit and leaves dragged inward along the ground. Flat and dark, never a rising column.
        if (L.cd > 0 && L.fire <= 0) break;
        const float pull = L.state == 1 ? 7.f : L.state == 2 ? 0.3f : 2.4f;
        for (int n = count((L.state == 1 ? 70.f : 28.f) * near, dt); n > 0; n--) {
          const float a = rnd() * 6.2832f, rr = 0.4f + rnd() * (L.r + 1.2f), h = 0.05f + rnd() * (L.state == 1 ? 1.1f : 0.55f);
          const vec3 p = L.c + vec3(std::cos(a) * rr, h, std::sin(a) * rr);
          const vec3 inward(-std::cos(a) * pull, L.state == 2 ? 0.4f : 0.05f, -std::sin(a) * pull);
          Particle q = base(K.white, p, inward + vec3(rs(), 0, rs()) * 0.3f, 0.7f + rnd() * 0.4f, 0.03f + rnd() * 0.025f);
          q.light = 0xF0 | 6; tint(q, 36 + (int)(rnd() * 18), 32, 30); q.shrink = true; P.add(q);
        }
        if (rnd() < dt * 2.2f * amount) {
          Particle q = base(K.white, ring(L, L.r, 0.15f), vec3(0, 0.2f, 0), 1.3f, 0.05f);
          q.flutter = 0.5f; q.ph = rnd() * 6; q.grav = 0.4f; q.light = 0xF0 | 8; tint(q, 70, 78, 40); P.add(q);
        }
        if (L.fire > 0) for (int n = count(220, dt); n > 0; n--) {
          const float a = rnd() * 6.2832f;
          puff(P, L.c + vec3(0, 0.3f, 0), vec3(std::cos(a) * 8, 0.6f + rnd() * 1.2f, std::sin(a) * 8), 0.7f, 0.4f, 40, 36, 34, 0.55f, 1.6f);
        }
        break;
      }
      case AK_KICKBACK: {   // shock: a low red stain, leaves jumping straight up, a flat shockwave when it coughs
        if (L.cd > 0 && L.fire <= 0) break;
        for (int n = count(8 * near, dt); n > 0; n--) puff(P, ring(L, L.r * 0.85f, 0.08f), vec3(rs() * 0.15f, 0.05f, rs() * 0.15f), 1.6f, 0.45f, 150, 42, 28, 0.22f, 0.4f);
        for (int n = count(5 * near, dt); n > 0; n--) {
          Particle q = base(K.white, ring(L, L.r * 0.9f, 0.05f), vec3(rs() * 0.4f, 2.2f + rnd() * 1.6f, rs() * 0.4f), 1.1f, 0.05f);
          q.flutter = 0.7f; q.ph = rnd() * 6; q.grav = 2.2f; q.light = 0xF0 | 10; tint(q, 120, 90, 36); P.add(q);
        }
        if (L.fire > 0) for (int n = count(180, dt); n > 0; n--) {
          const float a = rnd() * 6.2832f;
          puff(P, L.c + vec3(0, 0.4f, 0), vec3(std::cos(a) * (8 + rnd() * 4), 0.15f, std::sin(a) * (8 + rnd() * 4)), 0.45f, 0.28f, 255, 220, 190, 0.4f, 1.8f);
        }
        break;
      }
      case AK_SPINNER: {   // lift: a tall pale column, debris orbiting high. Not the dark inward stain.
        if (L.cd > 0 && L.state == 0 && L.fire <= 0) break;
        const float spin = L.state == 1 ? 5.5f : 1.6f;
        for (int n = count((L.state == 1 ? 50.f : 22.f) * near, dt); n > 0; n--) {
          const float a = rnd() * 6.2832f, rr = 0.25f + rnd() * 0.85f, h = rnd() * (L.state == 1 ? 5.5f : 4.2f);
          const vec3 p = L.c + vec3(std::cos(a) * rr, 0.15f + h, std::sin(a) * rr);
          Particle q = base(K.white, p, vec3(-std::sin(a) * spin, 1.4f + rnd() * 0.8f, std::cos(a) * spin), 1.1f, 0.035f + rnd() * 0.02f);
          q.light = 0xF0 | 12; tint(q, 214, 204, 180); q.shrink = true; P.add(q);
        }
        for (int n = count(L.state == 1 ? 16.f : 4.f, dt); n > 0; n--) {
          const float a = rnd() * 6.2832f, rr = 0.5f + rnd() * L.r * 0.5f;
          Particle q = base(K.white, L.c + vec3(std::cos(a) * rr, 2.2f + rnd() * 2.2f, std::sin(a) * rr), vec3(-std::sin(a), 0.3f, std::cos(a)) * spin, 1.6f, 0.045f);
          q.light = 0xF0 | 8; tint(q, 90, 78, 64); P.add(q);
        }
        if (L.fire > 0) for (int n = count(160, dt); n > 0; n--) puff(P, L.c + vec3(0, 3.2f, 0), vec3(rs() * 6, 2 + rnd() * 3, rs() * 6), 0.6f, 0.3f, 210, 196, 160, 0.4f, 1.5f);
        break;
      }
      case AK_SHARDS:   // glass hanging in the air, catching the light
        for (int n = count(14 * near, dt); n > 0; n--) glowDot(P, ring(L, L.r, 0.3f + rnd() * 1.9f), vec3(rs() * 0.05f, 0.02f, rs() * 0.05f), 2.5f + rnd() * 2, 0.022f, 200, 235, 255, 4 + rnd() * 3);
        break;
      case AK_SKIP:   // a fold: a violet shimmer standing in the air
        for (int n = count(16 * near, dt); n > 0; n--) { const float a = rnd() * 6.2832f; glowDot(P, L.c + vec3(std::cos(a) * 0.6f, 0.2f + rnd() * 2, std::sin(a) * 0.6f), vec3(-std::sin(a) * 0.8f, 0.3f, std::cos(a) * 0.8f), 1.f, 0.025f, 190, 120, 255); }
        if (L.fire > 0) for (int n = count(160, dt); n > 0; n--) glowDot(P, L.c + vec3(0, 1, 0), vec3(rs(), rs(), rs()) * 4.f, 0.5f, 0.04f, 210, 160, 255);
        break;
      case AK_FLARE:   // armed: a heat shimmer only. The pillar is the trip. Quiet while it sleeps.
        if (L.fire > 0) for (int n = count(220, dt); n > 0; n--) {
          Particle q = base(K.flame, L.c + vec3(rs() * L.r * 0.35f, 0.1f, rs() * L.r * 0.35f), vec3(rs() * 0.5f, 7 + rnd() * 4, rs() * 0.5f), 0.55f + rnd() * 0.25f, 0.28f + rnd() * 0.2f);
          q.light = 0xFF; q.glow = true; q.shrink = true; P.add(q);
        } else if (L.cd <= 0) for (int n = count(8 * near, dt); n > 0; n--) {
          Particle q = base(K.white, L.c + vec3(rs() * 0.45f, 0.15f + rnd() * 1.1f, rs() * 0.45f), vec3(rs() * 0.15f, 0.25f, rs() * 0.15f), 0.8f, 0.1f);
          q.soft = true; q.alpha = 0.1f; q.grow = 0.4f; q.light = 0xFF; tint(q, 255, 170, 90); P.add(q);
        }
        break;
      case AK_WANDERFIRE:   // the fireball itself
        for (int n = count(70, dt); n > 0; n--) { Particle q = base(K.flame, L.pos + vec3(rs(), rs(), rs()) * 0.35f, vec3(rs() * 0.4f, 0.8f + rnd(), rs() * 0.4f), 0.35f + rnd() * 0.2f, 0.18f + rnd() * 0.14f); q.light = 0xFF; q.glow = true; q.shrink = true; P.add(q); }
        for (int n = count(12, dt); n > 0; n--) { Particle q = base(K.ember, L.pos, vec3(rs(), rnd() * 2, rs()) * 1.5f, 0.8f, 0.016f); q.light = 0xFF; q.shrink = true; P.add(q); }
        break;
      case AK_STEAM: {   // a white column on a clock: it gathers, then jets. Never a flame.
        const float ph = steamPhase(now, ifloor(L.c.x), ifloor(L.c.y) - 1, ifloor(L.c.z));
        const bool warn = ph > 0.82f, jet = L.fire > 0;
        const float rise = jet ? 7.f + rnd() * 2.5f : warn ? 1.4f + (ph - 0.82f) / 0.18f * 3.5f : 0.55f;
        for (int n = count(jet ? 80.f : warn ? 16.f : 2.f * near, dt); n > 0; n--)
          puff(P, L.c + vec3(rs() * (jet ? 0.35f : 0.15f), 0.05f, rs() * (jet ? 0.35f : 0.15f)), vec3(rs() * 0.25f, rise, rs() * 0.25f), jet ? 1.1f : 1.8f, jet ? 0.32f : 0.18f, 236, 240, 244, jet ? 0.7f : 0.28f, 1.6f);
        break;
      }
      case AK_ASHMOSS:   // grey strands hanging off the dead tree, shedding ash; a flare where a bolt touched one
        for (const vec4& b : L.bits) {
          if (rnd() < dt * 6 * amount * near) { Particle q = base(K.white, vec3(b.x, b.y - rnd() * b.w, b.z) + vec3(rs() * 0.06f, 0, rs() * 0.06f), vec3(0, -0.15f, 0), 1.5f, 0.05f); q.light = 0xF0 | 10; tint(q, 120, 120, 116); q.flutter = 0.1f; P.add(q); }
          if (rnd() < dt * 0.7f * amount) { Particle q = base(K.ember, vec3(b.x, b.y - rnd() * b.w, b.z), vec3(rs() * 0.1f, 0.3f, rs() * 0.1f), 0.8f, 0.016f); q.light = 0xFF; q.shrink = true; P.add(q); }
        }
        if (L.fire > 0) for (int n = count(80, dt); n > 0; n--) { Particle q = base(K.flame, L.hitAt + vec3(rs() * 0.2f, rnd() * 0.6f, rs() * 0.2f), vec3(0, 1.5f, 0), 0.4f, 0.15f); q.light = 0xFF; q.glow = true; q.shrink = true; P.add(q); }
        break;
      case AK_ARCNEST:   // sitting lightning: a blue fog on the ground and arcs across the patch. No ball.
        if (L.cd > 0 && L.charge < 0.2f) break;   // spent
        for (int n = count((6 + 28 * L.charge) * near, dt); n > 0; n--) puff(P, ring(L, L.r * 0.9f, 0.15f + rnd() * 0.8f), vec3(rs() * 0.2f, 0.15f, rs() * 0.2f), 1.4f, 0.4f, 70, 110, 180, 0.16f + 0.12f * L.charge, 0.8f);
        for (int n = count((10 + 40 * L.charge * L.charge) * near, dt); n > 0; n--) glowDot(P, ring(L, L.r * 0.85f, 0.05f + rnd() * 0.5f), vec3(rs() * 1.5f, 0.3f, rs() * 1.5f), 0.16f, 0.03f, 140, 200, 255);
        if (rnd() < dt * (2.f + 10 * L.charge) * amount) { const vec3 a = ring(L, L.r * 0.7f, 0.1f); arc(P, a, a + vec3(rs() * L.r, 0.15f + rnd() * 0.5f, rs() * L.r), 160, 210, 255); }
        if (L.hitT > 0 && rnd() < 0.7f) arc(P, L.c + vec3(0, 0.2f, 0), L.hitAt, 210, 235, 255);
        break;
      case AK_BALL:   // a tight moving ball, not a patch of ground lightning
        if (L.hidden <= 0) {
          glowDot(P, L.pos, vec3(0), 0.12f, 0.22f, 230, 245, 255);
          for (int n = count(40, dt); n > 0; n--) glowDot(P, L.pos + vec3(rs(), rs(), rs()) * 0.18f, vec3(rs(), rs(), rs()) * 0.2f, 0.2f, 0.045f, 180, 220, 255);
          if (rnd() < dt * 5 * amount) arc(P, L.pos, L.pos + vec3(rs(), rs(), rs()) * 0.55f, 220, 240, 255);
        }
        if (L.hitT > 0 && rnd() < 0.7f) arc(P, L.pos, L.hitAt, 230, 245, 255);
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
      case AK_ACID:   // a wet pool: bubbles on the surface, no bank of cloud
        for (int n = count(12 * near, dt); n > 0; n--) glowDot(P, ring(L, L.r, -0.05f), vec3(rs() * 0.04f, 0.2f + rnd() * 0.25f, rs() * 0.04f), 0.8f, 0.04f + rnd() * 0.03f, 140, 255, 50);
        if (L.fire > 0) for (int n = count(90, dt); n > 0; n--) glowDot(P, L.hitAt + vec3(rs() * 0.35f, 0.02f, rs() * 0.35f), vec3(rs() * 0.4f, 0.5f + rnd() * 0.4f, rs() * 0.4f), 0.55f, 0.04f, 170, 255, 80);
        break;
      case AK_GAS: {   // an ankle-high bank that drifts; it thickens when something touches it
        const vec3 at = L.pos;
        const bool thick = L.fire > 0;
        for (int n = count((thick ? 36.f : 10.f) * near, dt); n > 0; n--) {
          const float a = rnd() * 6.2832f, rr = rnd() * (L.r * 0.7f + (thick ? 1.3f : 0.4f));
          puff(P, at + vec3(std::cos(a) * rr, 0.15f + rnd() * (thick ? 1.3f : 0.55f), std::sin(a) * rr), vec3(rs() * 0.35f, 0.08f, rs() * 0.35f), thick ? 2.4f : 3.2f, thick ? 0.7f : 0.45f, thick ? 150 : 186, thick ? 170 : 200, 50, thick ? 0.5f : 0.2f, 1.1f);
        }
        break;
      }
      case AK_BLISTER:   // a thick toxic mass, not a bubble and not a low bank
        if (L.hidden <= 0) for (int n = count(26, dt); n > 0; n--)
          puff(P, L.pos + vec3(rs(), rs() * 0.45f, rs()) * 0.75f, vec3(rs() * 0.12f, 0.06f, rs() * 0.12f), 1.5f, 0.62f, 62, 96, 38, 0.62f, 1.15f);
        for (const vec4& b : L.bits) if (rnd() < 0.45f) puff(P, vec3(b), vec3(0, 0.08f, 0), 0.9f, 0.32f, 80, 120, 48, 0.5f, 1.f);
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
    float vol = 0.75f, pitch = 0.92f + rnd() * 0.16f;
    switch (L.kind) {
      case AK_UNDERTOW: if (L.cd <= 0 && L.state != 2) { nm = "zone_ano_gust"; gap = 2.6f; pitch = 0.78f; } break;
      case AK_KICKBACK: if (L.cd <= 0) { nm = "zone_ano_shove"; gap = 2.8f; vol = 0.45f; } break;   // new cue: a thin shove of air, not the crush's gust
      case AK_SHARDS: nm = "zone_ano_chime"; gap = 3.5f; break;
      case AK_WANDERFIRE: nm = "zone_ano_fire"; gap = 1.3f; break;
      case AK_ARCNEST: if (L.cd <= 0 && L.charge > 0.35f) { nm = "zone_ano_crackle"; gap = 1.6f; } break;
      case AK_BALL: if (L.hidden <= 0) { nm = "zone_ano_hum"; gap = 1.2f; } break;
      case AK_SPARKPOOL: nm = "zone_ano_crackle"; gap = 2.8f; break;
      case AK_ACID: nm = "zone_ano_fizz"; gap = 2.6f; break;
      case AK_GAS: if (L.fire <= 0) { nm = "zone_ano_hiss"; gap = 2.6f; vol = 0.4f; } break;   // new cue: the bank's hiss, not the puff and not the acid's fizz
      case AK_BLISTER: if (L.hidden <= 0) { nm = "zone_ano_blister"; gap = 3.4f; vol = 0.28f; pitch = 0.55f; } break;
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
    const vec3 at = (L.kind == AK_WANDERFIRE || L.kind == AK_BALL || L.kind == AK_GAS || L.kind == AK_BLISTER) ? L.pos + vec3(0, 0.3f, 0) : L.c + vec3(0, 0.6f, 0);
    au.playAt(nm, at, 22, vol, pitch);
  }
};
} // namespace

void registerZoneAnomalyFx() {
  static bool done = false; if (done) return; done = true;
  registerRenderLayer([](GameSession&) -> std::shared_ptr<RenderLayer> { return std::make_shared<ZoneAnomalyFx>(); });
}

} // namespace dq
