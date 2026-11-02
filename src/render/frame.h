// render/frame.h — per-frame values every renderer shares (camera, time, sky, fog, settings).
#pragma once
#include "render/camera.h"
#include "render/gl.h"

namespace dq {

// Tile arrays are split in pages (GL guarantees 2048 layers on the hardware we target).
struct TileTextures { GLuint color[2] = {0, 0}, height[2] = {0, 0}, normal[2] = {0, 0}; int page = 2048; void bind(Shader& s, int firstUnit = 0) const; };

struct SkyEnv;   // render/sky.h
struct DynLight { glm::dvec3 pos; float level; };   // a point light: world position, level 0..15 (render/dyn_light.cpp)

struct FrameCtx {
  Camera cam;
  double time = 0;           // seconds since start (animation)
  float daylight = 1;        // sky light multiplier
  vec3 sunDir{0, 1, 0};
  vec3 skyColor{0.5f, 0.7f, 1.f}, fogColor{0.7f, 0.8f, 1.f};
  float fogStart = 64, fogEnd = 128;
  vec4 season{0};            // seasons: the colour the grass and leaves turn toward (rgb) and how far (a)
  vec3 nearFog{0};           // the old fog, layered in front of the far one: x start, y end, z how thick (0 = none)
  float wind = 0.3f;
  float sea = 0;             // storm swell on open water (weather), 0..1
  float minLight = 0.05f;    // light floor (original env.minLight: 0.025 + brightness * 0.07, more in the Depths/End)
  int eyeFluid = 0;          // camera eye in 0 air, 1 water, 2 lava (underwater fog + tint)
  vec3 blockLightColor{1.0f, 0.85f, 0.62f};
  GLuint lightVol = 0; bool lightVolOn = false; glm::dvec3 lightVolMin{0};   // coloured block light (render/light_colour_layer.cpp, unit 12)
  int width = 1280, height = 720;
  const Settings* settings = nullptr;
  int dim = 2;               // band the camera is in
  // sky & weather extras (render/sky.cpp fills them every frame)
  vec3 horizonColor{0.7f, 0.8f, 1.f};
  float rain = 0, thunder = 0, flash = 0;   // rain/snow 0..1 where the camera is (wet look), thunderstorm, lightning flash
  float mist = 0;            // mist, fog bank, sandstorm, blizzard 0..1
  float cloudBank = 0;       // inside the cloud bank between the Overworld and the Skylands
  vec2 windDir{1, 0};        // the way the wind blows (x, z); strength is `wind`
  bool underwater = false;
  std::vector<DynLight> dyn;   // up to 8 carried/dropped/burning lights of the camera's realm (set by DynLightLayer)
  int dynRealm = -1;
  std::shared_ptr<const SkyEnv> sky;   // this view's full sky (render/sky.h; portal views get their own)
  int portalDepth = 0;       // > 0 while drawing the view through a portal (render/portal_view.cpp)
  // Exclusion's look (render/sky.cpp zoneLook, shadow_map.cpp, dyn_light.cpp; passed to every world shader by apply)
  int quality = 2;           // Settings::gfxQuality this frame: 0 low, 1 medium, 2 high, 3 ultra
  vec3 skyTint{1};           // the colour of sky light (moonlight blue-grey on Zone nights)
  vec4 heightFog{0};         // ground fog (common.glsl groundFog): x eye height above its floor, y density, z falloff (blocks), w max
  vec3 fogSun{0};            // the sun's / moon's light caught in the haze toward it
  vec3 lightDir{0, 1, 0};    // the sun by day, the moon by night (shadows, glints, the glow in the fog)
  float wet = 0;             // how rain-soaked the open ground is, 0..1 (eases in and dries off)
  GLuint shadowTex = 0;      // the sun / moon shadow map (0 = none this frame)
  mat4 shadowVP{1};          // its view-projection, relative to shadowAnchor
  glm::dvec3 shadowAnchor{0};
  vec4 shadowK{0};           // x how dark the shade, y/z fade start/end (blocks), w normal offset (blocks)
  float shadowTexel = 0;     // one texel of the map (0..1 units)
  int shadowTaps = 0;
  glm::dvec3 spotPos{0}; vec3 spotDir{0, 0, -1}; float spotRange = 0, spotCos = 0.85f;   // the player's head torch / gun light
  // set common.glsl uniforms on a bound shader
  void apply(Shader& s) const;
};

} // namespace dq
