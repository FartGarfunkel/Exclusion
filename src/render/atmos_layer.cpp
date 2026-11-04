// render/atmos_layer.cpp — screen-space light and shade over the world, before the hand is drawn (assets/shaders/atmos.glsl):
//  * after the opaque world: the depth is copied out, ambient occlusion is worked out at half size from it and multiplied
//    into the picture (High and Ultra);
//  * after the translucent world: sun shafts (High and Ultra). The open sky smeared out from the sun, and a short march
//    from the eye through the air, each step lit or dark by the sun's shadow map, so a roof, a wall or a tree cuts a beam.
//    A faint lens flare when the sun is in view and not behind something. Added onto the picture.
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
    // shafts from the sun (or a faint one from the moon): in the air the shadow map says is lit
    const SkyEnv& e = skyOf(f);
    const vec3 L = f.lightDir;
    const bool sunUp = f.sunDir.y >= 0;
    if (e.underwater || f.cam.realm != REALM_MAIN || f.dim != DIM_OVERWORLD) return;
    const float height = sunUp ? smoothstepf(-0.04f, 0.14f, L.y) : smoothstepf(0.04f, 0.22f, L.y);
    const float haze = 0.72f + 0.28f * std::min(1.f, f.mist + f.heightFog.y * 30.f);
    const float clear = std::clamp(1.f - 0.42f * std::clamp(e.cloud, 0.f, 1.f) - 0.62f * f.rain, 0.22f, 1.f);
    const float k = height * haze * clear * (sunUp ? 1.05f : 0.28f) * (e.showSun || !sunUp ? 1.f : 0.45f);
    if (k < 0.02f) return;
    const vec3 eye = vec3(f.cam.pos - f.cam.origin);
    const vec4 c = f.cam.viewProj * vec4(eye + L * 1000.f, 1.f);
    const bool ahead = c.w > 0.05f;
    const vec2 sunUV = ahead ? vec2(c.x / c.w, c.y / c.w) * 0.5f + 0.5f : vec2(0.5f, 2.f);
    const float off = ahead ? std::max(std::abs(sunUV.x - 0.5f), std::abs(sunUV.y - 0.5f)) : 2.f;
    if (!begin()) return;
    common(f);
    const bool vol = f.shadowTex != 0 && f.shadowK.z > 1.f;
    ensureLit();
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, vol ? f.shadowTex : litShadow); glActiveTexture(GL_TEXTURE0);
    sh->set("uShadow", 2);
    sh->set("uVol", vol ? 1.f : 0.f);
    sh->set("uInvView", glm::inverse(f.cam.view));
    sh->set("uLightDir", L);
    sh->set("uMaxDist", vol ? f.shadowK.z : 48.f);
    sh->set("uShadowK", vol ? f.shadowK : vec4(0));
    if (vol) {
      const mat4 bias = glm::translate(mat4(1), vec3(0.5f)) * glm::scale(mat4(1), vec3(0.5f));
      sh->set("uShadowMat", bias * f.shadowVP * glm::translate(mat4(1), vec3(f.cam.origin - f.shadowAnchor)));
    } else sh->set("uShadowMat", mat4(1));
    const int qw = std::max(1, vp[2] / 4), qh = std::max(1, vp[3] / 4);
    src.make(qw, qh, GL_RGBA8); rays.make(qw, qh, GL_RGBA8);
    sh->set("uSunUV", sunUV);
    glBindFramebuffer(GL_FRAMEBUFFER, src.fbo); glViewport(0, 0, qw, qh);
    sh->set("uStep", 2); sh->set("uTexel", vec2(1.f / qw, 1.f / qh)); tri();
    glBindFramebuffer(GL_FRAMEBUFFER, rays.fbo);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, src.tex); glActiveTexture(GL_TEXTURE0);
    sh->set("uStep", 3); sh->set("uRaySteps", f.quality >= 3 ? 32 : 20); tri();
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)outFbo); glViewport(vp[0], vp[1], vp[2], vp[3]);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, rays.tex); glActiveTexture(GL_TEXTURE0);
    const vec3 col = sunUp ? vec3(1.05f, 0.84f, 0.52f) + e.sunGlow * 0.65f : vec3(0.50f, 0.60f, 0.82f);
    sh->set("uStep", 4); sh->set("uShaftCol", col * k);
    sh->set("uFlare", vec2(sunUp && ahead ? 0.55f * clear * height : 0.f, smoothstepf(0.62f, 0.45f, off)));
    glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
    tri();
    end();
    (void)s;
  }
private:
  std::unique_ptr<Shader> sh; bool tried = false, broken = false, haveDepth = false;
  GLuint vao = 0, depth = 0, litShadow = 0; int dw = 0, dh = 0;
  Tex ao, src, rays;
  GLint vp[4] = {0, 0, 0, 0}, outFbo = 0;
  u32 frameNo = 0;

  void tri() { glBindVertexArray(vao); glDrawArrays(GL_TRIANGLES, 0, 3); }
  void ensureLit() {
    if (litShadow) return;
    glGenTextures(1, &litShadow); glBindTexture(GL_TEXTURE_2D, litShadow);
    const float one = 1.f;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 1, 1, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &one);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  }
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
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0); glActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE);
  }
};
} // namespace

void registerAtmosLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<AtmosLayer>(); }); }

} // namespace dq
