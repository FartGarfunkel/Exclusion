// render/zone_prop_layer.cpp — the Zone's vehicles and big props (gen/zone_details.h): each kit / variant / look / paint is a
// voxel model (assets/zone_prop_models.cpp) built once on a worker, then drawn where the plan put it, lit by the world's light
// above it. The props never move: the list near the camera is re-read when the camera crosses 8 m.
#include "render/item_layers.h"
#include "client/session.h"
#include "render/frame.h"
#include "assets/voxel.h"
#include "assets/zone_prop_models.h"
#include "gen/zone_details.h"
#include "sim/zone.h"
#include "sim/level.h"
#include "world/dims.h"
#include <future>
#include <unordered_map>

namespace dq {
namespace {

struct PropMesh {
  std::future<bool> job;
  std::shared_ptr<VoxelMeshData> md;
  vec3 origin{0}; ivec3 size{0};
  VoxelMeshGL gl;
  bool done = false;
};

class ZonePropLayer : public RenderLayer {
public:
  const char* name() const override { return "zone-props"; }
  int order() const override { return 105; }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!s.level || !s.player || s.player->realm != REALM_MAIN || f.dim != DIM_OVERWORLD || !zone::zoneWorld(*s.level)) return;
    const u32 seed = (u32)s.level->info.seed;
    const float R = std::clamp(f.fogEnd + 24.f, 96.f, 260.f);
    const ivec2 cell((int)std::floor(f.cam.pos.x / 8), (int)std::floor(f.cam.pos.z / 8));
    if (cell != cell_ || seed != seed_ || R != R_) {
      cell_ = cell; seed_ = seed; R_ = R;
      zone::zonePropsIn(seed, (int)(f.cam.pos.x - R), (int)(f.cam.pos.z - R), (int)(f.cam.pos.x + R), (int)(f.cam.pos.z + R), near_);
    }
    const auto& P = zone::zoneProps(seed);
    const auto& K = zone::propKits();
    World& w = s.world();
    VoxelRenderer& VR = voxelRenderer();
    bool begun = false;
    int starts = 0;
    const double base = dimBase(DIM_OVERWORLD);
    for (int i : near_) {
      const zone::Prop& p = P[i];
      const double dx = p.x - f.cam.pos.x, dz = p.z - f.cam.pos.z;
      if (dx * dx + dz * dz > (double)R * R) continue;
      const u32 key = (u32)p.kit | (u32)p.var << 8 | (u32)p.look << 12 | (u32)(p.tint % 6) << 16;
      PropMesh& m = meshes_[key];
      if (!m.done) {
        if (!m.job.valid()) {
          if (starts++ >= 2) continue;
          m.md = std::make_shared<VoxelMeshData>();
          auto md = m.md; vec3* o = &m.origin; ivec3* sz = &m.size;
          const int kit = p.kit, var = p.var, look = p.look, tint = p.tint % 6;
          m.job = std::async(std::launch::async, [md, o, sz, kit, var, look, tint] { return buildPropModel(kit, var, look, tint, *md, *o, *sz); });
          continue;
        }
        if (m.job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) continue;
        if (m.job.get() && !m.md->verts.empty()) m.gl.upload(*m.md);
        m.md.reset(); m.done = true;
      }
      if (!m.gl.ready()) continue;
      const mat4& M = zone::zonePropMatrix(seed, i);
      const vec3 ext = vec3(m.size) / PROP_VOX;
      const vec3 cLocal = m.origin + ext * .5f;
      const vec3 cw = vec3(M * vec4(cLocal, 1));
      const vec3 rel = f.cam.rel(glm::dvec3(cw.x, cw.y + base, cw.z));
      if (!f.cam.frustum.sphere(rel, glm::length(ext) * .5f)) continue;
      // light: the air above its roof and beside it (the brightest: a wreck in the open is lit by the sky)
      const zone::PropKit& k = K[p.kit];
      u8 best = 0;
      for (const vec3 q : {vec3(0, k.H + .6f, 0), vec3(0, k.H * .5f, k.W * .5f + .7f), vec3(0, k.H * .5f, -k.W * .5f - .7f), vec3(k.L * .5f + .7f, k.H * .5f, 0)}) {
        const vec3 a = vec3(M * vec4(q, 1));
        const u8 l = w.getLight((int)std::floor(a.x), (int)std::floor(a.y + base), (int)std::floor(a.z));
        if ((l >> 4) > (best >> 4) || ((l >> 4) == (best >> 4) && (l & 15) > (best & 15))) best = l;
      }
      const VoxelLight L{(best >> 4) / 15.f, (best & 15) / 15.f};
      mat4 Mr = M;
      const vec3 t = f.cam.rel(glm::dvec3(M[3].x, (double)M[3].y + base, M[3].z));
      Mr[3] = vec4(t, 1);
      if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
      VR.draw(m.gl, Mr * glm::translate(mat4(1), m.origin) * glm::scale(mat4(1), vec3(1.f / PROP_VOX)), L);
    }
  }

private:
  ivec2 cell_{INT32_MIN};
  u32 seed_ = 0; float R_ = 0;
  std::vector<int> near_;
  std::unordered_map<u32, PropMesh> meshes_;
};

} // namespace

void registerZonePropLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<ZonePropLayer>(); }); }

} // namespace dq
