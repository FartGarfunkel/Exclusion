// render/shadow_map.cpp — sun and moon shadows. A depth map of the land round the player, seen from the sun by day and
// the moon by night (FrameCtx::lightDir), is drawn from the meshed sections (WorldRenderer::drawShadowCasters, chunk.glsl
// with SHADOW_PASS) and every world shader reads it through common.glsl's sunShadow(): soft (PCF) shade under trees and
// walls, fading out toward the edge of its reach, weaker under cloud and rain. The map is anchored to the world and drawn
// again only when the player has moved a few blocks, the light has turned, or now and then (new blocks), so it costs
// little per frame. Size, reach and softness follow Settings::gfxQuality (Low: none); Settings::shadows switches it off.
// Entities don't cast (they receive); see the report in docs for what's left.
#include "client/session.h"
#include "client/app.h"
#include "render/gl.h"
#include "render/sky.h"
#include "sim/level.h"

namespace dq {

namespace {
struct Tier { int size; float radius; int taps; double every; };
const Tier TIERS[4] = {{0, 0, 0, 0}, {1024, 40, 4, 1.0}, {2048, 64, 8, 0.6}, {2048, 96, 12, 0.3}};

class ShadowMapLayer : public RenderLayer {
public:
  const char* name() const override { return "shadow-map"; }
  int order() const override { return 1100; }   // after the sky and the dynamic lights have filled the frame
  void update(GameSession& s, double) override {
    FrameCtx& f = s.frame;
    f.shadowTex = 0; f.shadowK = vec4(0);
    const Settings& st = s.app.settings;
    const int q = std::clamp(st.gfxQuality, 0, 3);
    const SkyEnv& e = skyOf(f);
    if (!st.shadows || q == 0 || f.cam.realm != REALM_MAIN || f.dim != DIM_OVERWORLD || e.underwater) return;
    // how dark the shade is: the sun's (or the moon's) height, cloud and rain soften it
    const float sunK = smoothstepf(0.03f, 0.16f, f.sunDir.y), moonK = smoothstepf(0.03f, 0.16f, -f.sunDir.y);
    const float soft = std::clamp(1 - 0.62f * std::clamp(e.cloud, 0.f, 1.f) - 0.6f * f.rain - 0.3f * std::max(0.f, f.mist - 0.3f), 0.15f, 1.f);
    const float k = (0.5f * sunK + 0.42f * moonK) * soft;
    if (k < 0.01f) return;
    if (!shader) { shader = std::make_unique<Shader>(); if (!shader->load("chunk.glsl", "chunk.glsl", {"MICRO_VOXELS 0", "SHADOW_PASS 1"})) shader.reset(); }
    if (!shader || !shader->ok()) return;
    const Tier& T = TIERS[q];
    if (!make(T.size)) return;
    const vec3 L = glm::normalize(f.lightDir);
    const glm::dvec3 anchor = glm::floor(f.cam.pos / 4.0) * 4.0;
    const double now = nowSeconds();
    const bool redraw = anchor != lastAnchor || glm::dot(L, lastL) < 0.99998f || now - lastT > T.every || T.radius != lastR || &s.world() != lastWorld;
    if (redraw) draw(s, f, L, anchor, T.radius);
    f.shadowTex = depth;
    f.shadowVP = vp; f.shadowAnchor = lastAnchor;
    f.shadowK = vec4(k, T.radius * 0.72f, T.radius * 0.95f, 2.f * T.radius / T.size * 1.6f);
    f.shadowTexel = 1.f / T.size; f.shadowTaps = T.taps;
  }
  ~ShadowMapLayer() override { if (fbo) glDeleteFramebuffers(1, &fbo); if (depth) glDeleteTextures(1, &depth); }
private:
  std::unique_ptr<Shader> shader;
  GLuint fbo = 0, depth = 0; int size = 0;
  mat4 vp{1}; glm::dvec3 lastAnchor{1e18}; vec3 lastL{0}; double lastT = 0; float lastR = 0; const World* lastWorld = nullptr;

  bool make(int n) {
    if (depth && size == n) return true;
    if (fbo) glDeleteFramebuffers(1, &fbo);
    if (depth) glDeleteTextures(1, &depth);
    size = n; lastAnchor = glm::dvec3(1e18);
    glGenTextures(1, &depth); glBindTexture(GL_TEXTURE_2D, depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, n, n, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
    glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) { DQ_WARN("shadow map: framebuffer incomplete, shadows off"); glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &depth); fbo = depth = 0; size = 0; }
    return ok;
  }

  void draw(GameSession& s, const FrameCtx& f, vec3 L, glm::dvec3 anchor, float R) {
    // the light's box round the anchor: R across, deep enough for the hills and towers that throw shade into it
    const float back = R + 160.f;
    const vec3 up = std::abs(L.y) > 0.99f ? vec3(0, 0, 1) : vec3(0, 1, 0);
    const mat4 view = glm::lookAt(L * back, vec3(0), up);
    const mat4 proj = glm::ortho(-R, R, -R, R, 1.f, back * 2.f);
    vp = proj * view; lastAnchor = anchor; lastL = L; lastT = nowSeconds(); lastR = R; lastWorld = &s.world();
    GLint prevFbo = 0, prevVp[4]; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo); glGetIntegerv(GL_VIEWPORT, prevVp);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, size, size);
    glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDisable(GL_BLEND);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_CULL_FACE); glCullFace(GL_FRONT);   // the faces turned away from the light: no acne on the lit ones
    glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1.1f, 2.f);
    s.worldRenderer.drawShadowCasters(f, *shader, vp, anchor, R);
    glDisable(GL_POLYGON_OFFSET_FILL); glCullFace(GL_BACK);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
  }
};
} // namespace

void registerShadowMapLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<ShadowMapLayer>(); }); }

} // namespace dq
