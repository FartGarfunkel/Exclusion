// render/atmos_layer.cpp — screen-space light and shade over the world, before the hand is drawn (assets/shaders/atmos.glsl):
//  * after the opaque world: the depth is copied out, ambient occlusion is worked out at half size from it and multiplied
//    into the picture (High and Ultra);
//  * after the translucent world: light shafts — the open sky and far fog round the sun (or the moon) smeared out from it,
//    so trees, buildings and towers cut dark bars through the haze (High and Ultra) — and a faint lens flare when the sun
//    is in view and not behind something (High and Ultra). Added onto the picture.
// Settings::gfxQuality picks what runs (Low and Medium: neither). Nothing runs in portal views or where the depth can't be
// copied out of the frame.
#include "client/session.h"
#include "client/app.h"
#include "render/gl.h"
#include "render/sky.h"
#include "sim/level.h"

namespace dq {

namespace {
struct Tex {
  GLuint tex = 0, fbo = 0; int w = 0, h = 0;
  void make(int W, int H, GLenum fmt) {
    if (tex && w == W && h == H) return;
    drop(); w = W; h = H;
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  }
  void drop() { if (fbo) glDeleteFramebuffers(1, &fbo); if (tex) glDeleteTextures(1, &tex); fbo = tex = 0; w = h = 0; }
};

class AtmosLayer : public RenderLayer {
public:
  const char* name() const override { return "atmos"; }
  int order() const override { return 3000; }   // after every opaque / translucent layer, before the hand (drawOverlay)
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    haveDepth = false;
    const int q = f.quality;
    if (f.portalDepth > 0 || q < 2 || broken) return;
    if (!begin()) return;
    // the depth, as it is now (the hand clears it later)
    if (!depth || dw != vp[2] || dh != vp[3]) {
      if (depth) glDeleteTextures(1, &depth);
      glGenTextures(1, &depth); glBindTexture(GL_TEXTURE_2D, depth);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, vp[2], vp[3], 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      dw = vp[2]; dh = vp[3];
    }
    while (glGetError() != GL_NO_ERROR) {}
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)outFbo);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, depth);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], vp[2], vp[3]);
    if (glGetError() != GL_NO_ERROR) { broken = true; DQ_WARN("atmos: the depth can't be copied out of the frame; AO and light shafts off"); end(); return; }
    haveDepth = true;
    common(f);
    // ambient occlusion at half size, multiplied in
    const int hw = std::max(1, vp[2] / 2), hh = std::max(1, vp[3] / 2);
    ao.make(hw, hh, GL_RGBA8);
    glBindFramebuffer(GL_FRAMEBUFFER, ao.fbo); glViewport(0, 0, hw, hh);
    sh->set("uStep", 0); sh->set("uTexel", vec2(1.f / hw, 1.f / hh)); sh->set("uSamples", q >= 3 ? 12 : 6);
    tri();
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)outFbo); glViewport(vp[0], vp[1], vp[2], vp[3]);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, ao.tex); glActiveTexture(GL_TEXTURE0);
    sh->set("uStep", 1); sh->set("uTexel", vec2(1.f / hw, 1.f / hh)); sh->set("uAoK", 0.75f);
    glEnable(GL_BLEND); glBlendFunc(GL_ZERO, GL_SRC_COLOR);
    tri();
    end();
    (void)s;
  }
  void drawTranslucent(GameSession& s, const FrameCtx& f) override {
    if (!haveDepth || f.portalDepth > 0) return;
    // where the sun (or the moon) is on the screen, and how strong its shafts are now
    const SkyEnv& e = skyOf(f);
    const vec3 L = f.lightDir;
    const bool sunUp = f.sunDir.y >= 0;
    if (!e.showSun || e.underwater || f.cam.realm != REALM_MAIN) return;
    const vec3 eye = vec3(f.cam.pos - f.cam.origin);
    const vec4 c = f.cam.viewProj * vec4(eye + L * 1000.f, 1.f);
    if (c.w <= 0) return;
    const vec2 sunUV = vec2(c.x / c.w, c.y / c.w) * 0.5f + 0.5f;
    const float off = std::max(std::abs(sunUV.x - 0.5f), std::abs(sunUV.y - 0.5f));
    const float onScreen = smoothstepf(1.3f, 0.55f, off);
    const float height = sunUp ? smoothstepf(-0.06f, 0.1f, L.y) : smoothstepf(0.04f, 0.2f, L.y);
    const float haze = 0.45f + 0.55f * std::min(1.f, f.mist + f.heightFog.y * 40.f);
    const float clear = std::clamp(1 - 0.55f * e.cloud - 0.8f * f.rain, 0.f, 1.f);
    const float k = onScreen * height * haze * clear * (sunUp ? 0.35f : 0.1f);
    if (k < 0.01f) return;
    if (!begin()) return;
    common(f);
    const int qw = std::max(1, vp[2] / 4), qh = std::max(1, vp[3] / 4);
    src.make(qw, qh, GL_RGBA8); rays.make(qw, qh, GL_RGBA8);
    sh->set("uSunUV", sunUV);
    glBindFramebuffer(GL_FRAMEBUFFER, src.fbo); glViewport(0, 0, qw, qh);
    sh->set("uStep", 2); sh->set("uTexel", vec2(1.f / qw, 1.f / qh)); tri();
    glBindFramebuffer(GL_FRAMEBUFFER, rays.fbo);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, src.tex); glActiveTexture(GL_TEXTURE0);
    sh->set("uStep", 3); sh->set("uRaySteps", f.quality >= 3 ? 40 : 24); tri();
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)outFbo); glViewport(vp[0], vp[1], vp[2], vp[3]);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, rays.tex); glActiveTexture(GL_TEXTURE0);
    const vec3 col = sunUp ? vec3(1.0f, 0.86f, 0.64f) + e.sunGlow * 0.5f : vec3(0.55f, 0.65f, 0.85f);
    sh->set("uStep", 4); sh->set("uShaftCol", col * k);
    sh->set("uFlare", vec2(sunUp ? 0.9f * clear * height : 0.f, smoothstepf(0.62f, 0.45f, off)));
    glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
    tri();
    end();
    (void)s;
  }
private:
  std::unique_ptr<Shader> sh; bool tried = false, broken = false, haveDepth = false;
  GLuint vao = 0, depth = 0; int dw = 0, dh = 0;
  Tex ao, src, rays;
  GLint vp[4] = {0, 0, 0, 0}, outFbo = 0;
  u32 frameNo = 0;

  void tri() { glBindVertexArray(vao); glDrawArrays(GL_TRIANGLES, 0, 3); }
  bool begin() {
    if (!tried) { tried = true; sh = std::make_unique<Shader>(); if (!sh->load("atmos.glsl", "atmos.glsl")) sh.reset(); glGenVertexArrays(1, &vao); }
    if (!sh) return false;
    glGetIntegerv(GL_VIEWPORT, vp);
    if (vp[2] <= 16 || vp[3] <= 16) return false;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &outFbo);
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    sh->use();
    return true;
  }
  void common(const FrameCtx& f) {
    sh->set("uDepth", 0); sh->set("uSrc", 1);
    sh->set("uNearFar", vec2(f.cam.nearZ, f.cam.farZ));
    sh->set("uProjK", vec2(f.cam.proj[0][0], f.cam.proj[1][1]));
    sh->set("uAspect", (float)vp[2] / std::max(1, vp[3]));
    sh->set("uFog", vec2(f.fogStart, f.fogEnd));
    sh->set("uFrame", (float)(frameNo++ % 64));
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, depth);
  }
  void end() {
    glDisable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)outFbo);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glBindVertexArray(0); glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0); glActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE);
  }
};
} // namespace

void registerAtmosLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<AtmosLayer>(); }); }

} // namespace dq
