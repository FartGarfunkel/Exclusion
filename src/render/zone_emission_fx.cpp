// render/zone_emission_fx.cpp — the emission as you see it (Call of Pripyat), on top of what sky.cpp (the red sky, the
// bolts' light on the land) and clouds.cpp (the cloud heaps blackened and racing) already do. The schedule of bolts and of
// the wave's passes is render/zone_emission_fx.h, from the emission's own clock (sim/zone.h), so the sound
// (audio/ambience.cpp) keeps time with it. Shader: assets/shaders/zone_emission.glsl.
//   drawOpaque:  the sky behind the land (drawn at the far plane): racing black-red cloud, lightning strokes, the wave's front
//   drawOverlay: the screen of anyone caught outdoors (after the colour grade, so the red isn't drained): shimmer, smear,
//                colour split, red pulse, the flash, the shove as the wave goes over; and the camera shakes
#include "render/zone_emission_fx.h"
#include "client/session.h"
#include "client/app.h"
#include "render/gl.h"
#include "sim/zone.h"
#include "sim/level.h"
#include "sim/player.h"
#include <cmath>

namespace dq {
class PlayerController; PlayerController* localController(GameSession& s);   // client/play.h
float& controllerShake(PlayerController& c);                                   // client/play.cpp

namespace {
class ZoneEmissionFx : public RenderLayer {
public:
  const char* name() const override { return "zone-emission"; }
  int order() const override { return 5050; }   // drawOverlay after the post passes (4900) and the Zone grade (5000)
  void update(GameSession& s, double dt) override {
    em = zone::Emission{};
    if (!s.level || !s.player || !zone::zoneWorld(*s.level)) { out = 0; return; }
    em = zone::emission(*s.level);
    const bool inMain = s.player->realm == REALM_MAIN;
    if (!inMain) em.phase = 0;
    const float want = em.phase > 0 && !s.player->dead && !zone::underCover(*s.level, *s.player) ? 1.f : 0.f;
    out += (want - out) * std::min(1.f, (float)dt * 1.5f);
    if (em.phase == 0) return;
    B = emfx::bolt(em.phase, em.t); W = emfx::sweep(em.phase, em.t);
    // the ground shakes through the wave (more in the open), a jolt with each near bolt, a heave as the front goes over
    if (s.app.settings.screenShake && !s.player->dead && em.phase == 2) {
      float k = em.strength * (0.05f + 0.1f * out) + W.push * (0.18f + 0.4f * out);
      if (B.age < 0.3f) k = std::max(k, B.near * B.near * 0.25f);
      if (PlayerController* c = localController(s)) { float& sh = controllerShake(*c); sh = std::max(sh, k); }
    }
  }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (em.phase == 0 || f.portalDepth > 0 || f.cam.realm != REALM_MAIN || f.dim != DIM_OVERWORLD || f.underwater) return;
    if (!ready()) return;
    const float k = std::clamp(em.strength * (em.phase == 1 ? 1.4f : 1.f), 0.f, 1.f);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    sh->use(); sh->set("uMode", 0);
    sh->set("uInvViewProj", glm::inverse(f.cam.proj * mat4(mat3(f.cam.view))));
    sh->set("uTime", (float)std::fmod(s.app.time, 1000.0));
    sh->set("uEm", vec4(k, std::clamp(f.daylight, 0.f, 1.f), 0, 0));
    sh->set("uNorth", vec3(0, 0, -1));
    const float bw = B.id >= 0 ? B.flash * (0.5f + 0.5f * B.near) : 0.f;
    sh->set("uBolt", vec4(std::cos(B.az), 0, std::sin(B.az), bw));
    sh->set("uSweep", vec4(W.s, W.k * em.strength, (float)(B.id % 997) * 1.37f, 0));
    glBindVertexArray(vao); glDrawArrays(GL_TRIANGLES, 0, 3); glBindVertexArray(0);
    glDisable(GL_BLEND); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE);
  }
  void drawOverlay(GameSession& s, const FrameCtx& f) override {
    if (em.phase == 0 || f.portalDepth > 0 || !s.player) return;
    float k = em.phase == 1 ? em.strength * 0.2f * out : em.phase == 2 ? em.strength * (0.25f + 0.75f * out) : em.strength * 0.6f * out;
    const float flash = B.id >= 0 ? B.flash * B.near * (0.08f + 0.3f * out) : 0.f;
    const float push = W.push * em.strength * (0.25f + 0.75f * out);
    if (k < 0.01f && flash < 0.01f && push < 0.01f) return;
    if (!ready()) return;
    GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
    if (vp[2] <= 0 || vp[3] <= 0) return;
    if (!tex || tw != vp[2] || th != vp[3]) {
      if (tex) glDeleteTextures(1, &tex);
      glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, vp[2], vp[3], 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      tw = vp[2]; th = vp[3];
    }
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], vp[2], vp[3]);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    sh->use(); sh->set("uMode", 1); sh->set("uTex", 0);
    sh->set("uTime", (float)std::fmod(s.app.time, 1000.0));
    sh->set("uEm", vec4(std::min(k, 1.f), 0, std::min(flash, 1.f), std::min(push, 1.f)));
    glBindVertexArray(vao); glDrawArrays(GL_TRIANGLES, 0, 3); glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE);
  }
private:
  bool ready() {
    if (!tried) { tried = true; sh = std::make_unique<Shader>(); if (!sh->load("zone_emission.glsl", "zone_emission.glsl")) sh.reset(); glGenVertexArrays(1, &vao); }
    return sh != nullptr;
  }
  zone::Emission em; emfx::Bolt B; emfx::Sweep W; float out = 0;
  bool tried = false; std::unique_ptr<Shader> sh; GLuint vao = 0, tex = 0; int tw = 0, th = 0;
};
} // namespace

void registerZoneEmissionFx() { registerRenderLayer([](GameSession&) { return std::make_shared<ZoneEmissionFx>(); }); }

} // namespace dq
