// common.glsl — shared lighting, fog, wind and micro-voxel helpers (include from any shader).
// Lighting follows Cubyz: sun and block are added in quadrature, sun scaled by uAmbient (1 by day,
// a blue floor at night). Nothing falls below env.minLight.
#ifndef DQ_COMMON
#define DQ_COMMON
#ifndef MICRO_VOXELS
#define MICRO_VOXELS 1
#endif
// Shader packs: the colour grade every world shader puts on its final colour. A pack's common.glsl can
// define it and then include builtin:common.glsl (the game's own file) to change the whole look in two lines.
#ifndef DQ_GRADE
#define DQ_GRADE(c) (c)
#endif

uniform float uTime;
uniform float uDaylight;      // sky light multiplier (original env.day)
uniform vec3 uSunDir;
uniform vec3 uSkyColor;
uniform vec3 uFogColor;
// the open sky (render/world_renderer.cpp FrameCtx::apply): its horizon and the sun's glow, as sky.glsl paints them, and how far
// the far fog turns to the sky behind the land (0: the fog colour only: the Nether, the End, under water, a thick mist)
uniform vec3 uSkyHorizon, uSkyGlow;
uniform float uFogSky;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec3 uNearFog;
uniform vec4 uSeason;    // seasons: grass and leaves turn toward rgb by a (0 = summer green)   // the old fog: start, end, how thick (0 = none); the far lands stay faint beyond it
uniform float uBrightness;    // settings.brightness: Minecraft's brightness curve (mcBrightness), 0 moody .. 1 bright
uniform float uMicroDist;     // micro-voxel relief distance
uniform float uWind;          // wind strength (original env.wind: 0.45 + strength * 0.9, 0 when waving is off)
uniform vec2 uWindDir;        // the way the wind blows (x, z): plants and leaves lean with the gusts
uniform float uSea;           // storm swell on open water
uniform float uMinLight;      // light floor
uniform vec3 uAmbient;        // max(night blue, day scalar). Item previews set this to 1.
uniform int uEyeFluid;        // camera in 0 air, 1 water, 2 lava
uniform vec3 uBlockLightColor;

// tile pages (see TileTextures): layer < uPageSize in the first array, the rest in the second
uniform sampler2DArray uTiles;
uniform sampler2DArray uTiles2;
uniform sampler2DArray uHeights;
uniform sampler2DArray uHeights2;
uniform sampler2DArray uNormals;      // baked smooth normal maps: xy = tangent-space normal, z = crevice darkening, w = blurred depth
uniform sampler2DArray uNormals2;
uniform float uPageSize;
// animated tiles: which layer each tile shows now (render/tile_anim.cpp; 64 x 64, one entry per layer). The frames
// are layers of their own, so an animation is this little table changing, not pictures copied into the array.
uniform usampler2D uAnimMap;
float animLayer(float layer) { int l = int(layer); return float(texelFetch(uAnimMap, ivec2(l & 63, l >> 6), 0).r); }
vec4 tileColorGrad(vec2 uv, float layer, vec2 dx, vec2 dy) {
  return layer < uPageSize ? textureGrad(uTiles, vec3(uv, layer), dx, dy) : textureGrad(uTiles2, vec3(uv, layer - uPageSize), dx, dy);
}
vec4 tileColorLod(vec2 uv, float layer, float lod) {
  return layer < uPageSize ? textureLod(uTiles, vec3(uv, layer), lod) : textureLod(uTiles2, vec3(uv, layer - uPageSize), lod);
}
vec4 tileHeightAt(vec2 uv, float layer) {
  return layer < uPageSize ? textureLod(uHeights, vec3(uv, layer), 0.0) : textureLod(uHeights2, vec3(uv, layer - uPageSize), 0.0);
}

vec4 tileNormalAt(vec2 uv, float layer) {
  return layer < uPageSize ? textureLod(uNormals, vec3(uv, layer), 0.0) : textureLod(uNormals2, vec3(uv, layer - uPageSize), 0.0);
}

vec3 faceNormal(int n) {
  if (n == 0) return vec3(1, 0, 0);
  if (n == 1) return vec3(-1, 0, 0);
  if (n == 2) return vec3(0, 1, 0);
  if (n == 3) return vec3(0, -1, 0);
  if (n == 4) return vec3(0, 0, 1);
  if (n == 5) return vec3(0, 0, -1);
  return vec3(0, 1, 0);
}
// Face shading by direction, the same ratios as the near mesh and the distant walls
// (x 170/255, up 1, down 145/255, z 205/255). Micro-voxel side walls use this so they agree with the block face.
float axisShade(vec3 n) {
  vec3 a = abs(n);
  if (a.y >= a.x && a.y >= a.z) return n.y > 0.0 ? 1.0 : 145.0 / 255.0;
  return a.x >= a.z ? 170.0 / 255.0 : 205.0 / 255.0;
}
float faceShade(int n) { return n == 6 ? 1.0 : axisShade(faceNormal(n)); }
float lightCurve(float l) { return l / (3.2 - 2.2 * l); }
// Dynamic point lights (render/dyn_light.cpp, port of the original dynLight): carried torches, glowing drops, lit TNT,
// fireballs. Position relative to the camera's block, w = level 0..15. Light falls off one level per block and
// only lights faces turned toward it. Each light has its own colour (uDynColor): a held torch is warm even when the
// block-light volume here is some other colour, such as glow lichen's green. Returns a block light 0..1.
uniform vec4 uDyn[8];
uniform vec3 uDynColor[8];
uniform int uDynN;
uniform vec4 uSpot;      // the player's own beam: xyz where it starts (camera block relative), w reach in blocks (0 = none)
uniform vec4 uSpotDir;   // xyz where it points, w the cosine of its edge
float dynLight(vec3 pos, vec3 n) {
  float best = 0.0;
  for (int i = 0; i < 8; i++) {
    if (i >= uDynN) break;
    vec3 d = uDyn[i].xyz - pos;
    float l = length(d);
    float lv = (uDyn[i].w - l) / 15.0;
    if (lv <= best) continue;
    float face = clamp(dot(n, d / max(l, 0.001)) * 2.0 + 0.6, 0.0, 1.0);
    best = max(best, lv * face);
  }
  if (uSpot.w > 0.0) {
    // the head torch (or a gun's light): a cone thrown ahead of the player, bright in the middle, a long reach
    vec3 d = uSpot.xyz - pos;
    float l = length(d);
    vec3 L = d / max(l, 0.001);
    float c = dot(-L, uSpotDir.xyz);
    float cone = smoothstep(uSpotDir.w, mix(uSpotDir.w, 1.0, 0.55), c);
    float hot = smoothstep(mix(uSpotDir.w, 1.0, 0.7), 1.0, c);
    float face = clamp(dot(n, L) * 1.6 + 0.3, 0.0, 1.0);
    float reach = clamp(1.0 - l / uSpot.w, 0.0, 1.0);
    best = max(best, pow(reach, 0.6) * (0.78 * cone + 0.22 * hot) * face);
  }
  return clamp(best, 0.0, 1.0);
}
<<<<<<< ours
// Exclusion's light (render/shadow_map.cpp, sky.cpp zoneLook): sky light takes a colour (moonlight blue-grey at night;
// stored as 1 - colour so a shader nobody set it on stays neutral) and the sun's / moon's shadow (gSkyShade, set by the
// shader before it asks for the light: 1 in full light)
uniform vec3 uSkyDim;
float gSkyShade = 1.0;
vec3 skyTerm(float sky) { return vec3(lightCurve(sky) * uDaylight * gSkyShade) * (1.0 - uSkyDim); }
// the sun / moon shadow map: uShadowMat takes a point relative to the camera's block into the map
uniform sampler2DShadow uShadowMap;
uniform mat4 uShadowMat;
uniform vec4 uShadowK;     // x how dark the shade is (0 = no shadows), y fade start, z fade end (blocks), w normal offset (blocks)
uniform vec3 uLightDir;    // the sun, or the moon at night
uniform int uShadowTaps;   // 4, 8 or 12 (soft edges)
uniform float uShadowTexel;
float sunShadow(vec3 rel, vec3 n) {
  if (uShadowK.x <= 0.0) return 1.0;
  float d = length(rel);
  float fade = smoothstep(uShadowK.y, uShadowK.z, d);
  if (fade >= 1.0) return 1.0;
  float ndl = dot(n, uLightDir);
  float facing = smoothstep(-0.02, 0.1, ndl);   // turned away from the light: in its own shade
  float vis = 0.0;
  if (facing > 0.0) {
    vec3 s = (uShadowMat * vec4(rel + n * uShadowK.w, 1.0)).xyz;
    if (s.x <= 0.0 || s.y <= 0.0 || s.x >= 1.0 || s.y >= 1.0 || s.z >= 1.0) return 1.0;
    float r = uShadowTexel * 1.4;
    vis = texture(uShadowMap, s);
    if (uShadowTaps >= 4) {
      vis += texture(uShadowMap, s + vec3(r, r * 0.4, 0.0)) + texture(uShadowMap, s + vec3(-r * 0.4, r, 0.0))
           + texture(uShadowMap, s + vec3(-r, -r * 0.4, 0.0)) + texture(uShadowMap, s + vec3(r * 0.4, -r, 0.0));
      float k = 5.0;
      if (uShadowTaps >= 8) {
        float q = r * 2.1;
        vis += texture(uShadowMap, s + vec3(q * 0.7, q * 0.7, 0.0)) + texture(uShadowMap, s + vec3(-q * 0.7, q * 0.7, 0.0))
             + texture(uShadowMap, s + vec3(-q * 0.7, -q * 0.7, 0.0)) + texture(uShadowMap, s + vec3(q * 0.7, -q * 0.7, 0.0));
        k += 4.0;
      }
      if (uShadowTaps >= 12) {
        float q = r * 2.6;
        vis += texture(uShadowMap, s + vec3(q, 0.0, 0.0)) + texture(uShadowMap, s + vec3(-q, 0.0, 0.0))
             + texture(uShadowMap, s + vec3(0.0, q, 0.0)) + texture(uShadowMap, s + vec3(0.0, -q, 0.0));
        k += 4.0;
      }
      vis /= k;
    }
    vis *= facing;
  }
  return mix(1.0 - uShadowK.x * (1.0 - vis), 1.0, fade);
}
vec3 lighting(float sky, float block, bool emissive) {
  if (emissive) return vec3(1.0);
  vec3 lc = max(skyTerm(sky), vec3(lightCurve(block)) * uBlockLightColor);
  return max(lc, vec3(uMinLight));
=======
// the same lights, already through lightCurve and tinted by each light's own colour
vec3 dynLightRgb(vec3 pos, vec3 n) {
  vec3 acc = vec3(0.0);
  for (int i = 0; i < 8; i++) {
    if (i >= uDynN) break;
    vec3 d = uDyn[i].xyz - pos;
    float l = length(d);
    float lv = (uDyn[i].w - l) / 15.0;
    if (lv <= 0.0) continue;
    float face = clamp(dot(n, d / max(l, 0.001)) * 2.0 + 0.6, 0.0, 1.0);
    acc = max(acc, vec3(lightCurve(clamp(lv * face, 0.0, 1.0))) * uDynColor[i]);
  }
  return acc;
}
// Minecraft's brightness option (LightTexture: 1 - (1 - l)^4, mixed in by the gamma setting): dark places open up,
// full light stays as it is. Options → Brightness 0 (Moody) leaves the light as it was.
vec3 mcBrightness(vec3 l) {
  vec3 inv = 1.0 - clamp(l, 0.0, 1.0);
  return mix(l, 1.0 - inv * inv * inv * inv, clamp(uBrightness, 0.0, 1.0));
}
// sun31 and block31 are Cubyz's 0..31 channels. sqrt(sun^2 + block^2), the brightness curve, then the floor.
vec3 cubyzCombine(vec3 sun31, vec3 block31) {
  vec3 amb = sun31 * uAmbient;
  vec3 q = sqrt(amb * amb + block31 * block31);
  return max(mcBrightness(min(q, vec3(31.0)) / 31.0), vec3(uMinLight));
}
// A carried light, in the same 0..31 units: level*17 is the byte, one step of 8 per block.
vec3 dynBlock31(vec3 pos, vec3 n) {
  vec3 acc = vec3(0.0);
  for (int i = 0; i < 8; i++) {
    if (i >= uDynN) break;
    vec3 d = uDyn[i].xyz - pos;
    float l = length(d);
    float raw = uDyn[i].w * 17.0 - l * 8.0;
    if (raw <= 0.0) continue;
    float face = clamp(dot(n, d / max(l, 0.001)) * 2.0 + 0.6, 0.0, 1.0);
    acc = max(acc, vec3((raw / 8.0) * face) * uDynColor[i]);
  }
  return acc;
}
vec3 lighting(float sky, float block, bool emissive) {
  if (emissive) return vec3(1.0);
  return cubyzCombine(vec3(sky * 31.0), vec3(block * 31.0) * uBlockLightColor);
>>>>>>> theirs
}
// coloured block light (render/light_colour_layer.cpp): the colour of the light where this point is, from the volume
// round the camera; rel = the point relative to the camera's block (as vRel)
uniform sampler3D uLightVol;
uniform int uLightVolOn;
uniform vec3 uLightVolOrigin, uLightVolSize;
vec3 lightingAt(float sky, float block, bool emissive, vec3 rel, vec3 n) {
  if (emissive) return vec3(1.0);
  vec3 col = uBlockLightColor;
<<<<<<< ours
  if (all(greaterThan(t, vec3(0.0))) && all(lessThan(t, vec3(1.0)))) col = texture(uLightVol, t).rgb * 1.0;
  vec3 lc = max(skyTerm(sky), vec3(lightCurve(block)) * col);
  return max(lc, vec3(uMinLight));
}
// The fog: the land's distance fog, plus (Exclusion, sky.cpp zoneLook) a ground fog that pools low (thick in the valleys and
// the marsh, thickest at dawn) and the sun's or moon's light caught in the haze when you look toward it. dir = the view
// vector (eye to the point, any length); vec3(0) when the caller doesn't know it (no ground fog, no glow).
uniform vec4 uHeightFog;   // x the eye's height above the fog's floor, y density at the floor (per block), z how fast it thins upward (blocks), w max
uniform vec3 uFogSun;      // the light caught in the haze toward uLightDir (colour * strength)
float groundFog(float dist, vec3 v) {
  if (uHeightFog.y <= 0.0) return 0.0;
  float H = uHeightFog.z;
  float dy = v.y * dist;
  float k = abs(dy) > 0.05 * H ? (1.0 - exp(-dy / H)) / (dy / H) : 1.0 - 0.5 * dy / H;
  float od = uHeightFog.y * exp(-max(uHeightFog.x, -2.0 * H) / H) * dist * k;
  return min(1.0 - exp(-od), uHeightFog.w);
}
=======
  if (uLightVolOn != 0) {
    vec3 t = (rel - uLightVolOrigin) / uLightVolSize;
    if (all(greaterThan(t, vec3(0.0))) && all(lessThan(t, vec3(1.0)))) col = texture(uLightVol, t).rgb;
  }
  // Entities and particles only have a 0..1 nibble. The volume tints that gray. Terrain carries its own RGB
  // and does not sample the volume (that would paint the colour a second time).
  vec3 block31 = vec3(block * 31.0) * col;
  if (uDynN > 0) block31 = max(block31, dynBlock31(rel, n));
  return cubyzCombine(vec3(sky * 31.0), block31);
}
// the sky behind something seen along dir (sky.glsl's zenith-horizon gradient and the sun's glow round a low sun; looking down,
// the horizon): far land fades into exactly the sky behind it, no band of fog colour between them at sunset
vec3 fogSkyColour(vec3 dir) {
  vec3 d = normalize(dir);
  vec3 c = mix(uSkyHorizon, uSkyColor, pow(clamp(d.y, 0.0, 1.0), 0.45));
  c += uSkyGlow * pow(max(dot(d, uSunDir), 0.0), 6.0) * clamp(1.0 - abs(d.y) * 2.0, 0.0, 1.0);
  return c;
}
// fog lying low (render/height_fog.h): x density at the camera, y falloff height, z on
uniform vec4 uHeightFog;
>>>>>>> theirs
vec3 applyFog(vec3 c, float dist, vec3 dir) {
  float far = smoothstep(uFogStart, uFogEnd, dist), f = far;
  if (uNearFog.z > 0.0) f = max(f, smoothstep(uNearFog.x, uNearFog.y, dist) * uNearFog.z);
<<<<<<< ours
  vec3 fc = uFogColor;
  float dl = length(dir);
  if (dl > 0.001) {
    vec3 v = dir / dl;
    float g = groundFog(dist, v);
    f = 1.0 - (1.0 - f) * (1.0 - g);
    float mu = max(dot(v, uLightDir), 0.0);
    fc += uFogSun * (pow(mu, 6.0) * 0.6 + pow(mu, 40.0) * 0.35) * f;
  }
=======
  if (uHeightFog.z > 0.0 && dot(dir, dir) > 1e-8) {
    // the fog met along the ray through an exponential layer: x * H * (1 - e^-k) / dy, k = dist * dy / H
    // going down it thickens only to the floor's cap (w): past that point the rest of the ray is at the cap
    float dy = normalize(dir).y, k = dist * dy / uHeightFog.y, depth;
    if (abs(k) < 1e-3) depth = uHeightFog.x * dist;
    else if (k < -uHeightFog.w) {
      float tc = -uHeightFog.w * uHeightFog.y / dy;   // where the cap is reached
      depth = uHeightFog.x * uHeightFog.y * (1.0 - exp(uHeightFog.w)) / dy + (dist - tc) * uHeightFog.x * exp(uHeightFog.w);
    } else depth = uHeightFog.x * uHeightFog.y * (1.0 - exp(-k)) / dy;
    f = max(f, 1.0 - exp(-max(depth, 0.0)));
  }
  vec3 fc = uFogSky > 0.0 ? mix(uFogColor, fogSkyColour(dir), far * uFogSky) : uFogColor;   // (the thicker, the more it's the sky)
>>>>>>> theirs
  return mix(c, fc, f);
}

// Wind sway and waves — the original sway codes (render/gl.js terrainVS):
//   1 plant tip, 2 leaves, 3 water, 4..15 hanging (swing grows), 16..31 partway up a solid plant,
//   32..47 water surface + how open it is, 48..63 / 64..79 cloth flapping across z / x,
//   80..95 the tip of a 3D grass blade (fades into the ground with distance: chunk.glsl)
vec3 waveOffset(float sway, vec3 world) {
  if (sway < 0.5) return vec3(0.0);
  vec3 o = vec3(0.0);
  float t = uTime;
  vec2 w = world.xz;
  float gust = 0.6 + 0.4 * sin(t * 0.35 + w.x * 0.02 + w.y * 0.015);
  if (sway > 79.5) {
    // the tip of a 3D grass blade (render/mesh_plants.cpp emitGrassTufts): a quick flutter on the gusts
    float k = (sway - 80.0) / 15.0;
    o.x += (sin(t * 2.3 + w.x * 0.9 + w.y * 0.4) + 0.5 * sin(t * 4.1 + w.x * 2.3 - w.y * 1.7)) * 0.05 * uWind * gust * k;
    o.z += cos(t * 1.9 + w.y * 0.8 + w.x * 0.3) * 0.04 * uWind * gust * k;
    return o;
  }
  if (sway > 47.5) {
    bool acrossX = sway > 63.5;
    float k = (sway - (acrossX ? 64.0 : 48.0)) / 15.0;
    float ph = w.x * 0.37 + w.y * 0.29;
    float wave = sin(t * 5.1 - k * 4.2 + ph) * 0.65 + sin(t * 3.3 - k * 2.6 + ph * 1.7) * 0.35;
    float a = k * (0.07 + 0.2 * uWind) * gust * wave;
    if (acrossX) o.x += a; else o.z += a;
    o.y -= abs(a) * 0.2 * k;
  } else if (sway > 31.5) {
    float k = (sway - 32.0) / 15.0, A = k * (0.04 + 0.14 * uSea);
    float ripple = sin(t * 1.6 + w.x * 0.8 + w.y * 0.55) * 0.035 + sin(t * 1.05 - w.x * 0.37 + w.y * 0.77) * 0.025;
    float swell = sin(t * 0.9 + w.x * 0.21 + w.y * 0.13) * 0.6 + sin(t * 0.63 - w.x * 0.09 + w.y * 0.17) * 0.4;
    float chop = sin(t * 2.7 + w.x * 1.3 - w.y * 0.9) * uSea * k;
    o.y += ripple * (1.0 + 0.6 * uSea * k) + swell * A + chop * 0.025 - A * 0.7 - 0.06;
  } else if (sway < 1.5 || sway > 15.5) {
    float pk = sway > 15.5 ? (sway - 16.0) / 15.0 : 1.0;
    o.x += (sin(t * 1.9 + w.x * 0.37 + w.y * 0.23) + 0.4 * sin(t * 3.3 + w.x * 1.1)) * 0.075 * uWind * gust * pk;
    o.z += cos(t * 1.5 + w.y * 0.31 + w.x * 0.11) * 0.055 * uWind * gust * pk;
    o.xz += uWindDir * (gust * gust - 0.3) * 0.06 * uWind * pk;   // bowing with the gusts
  } else if (sway < 2.5) {
    float ph = w.x * 0.45 + w.y * 0.38 + world.y * 0.5;
    o.x += sin(t * 1.4 + ph) * 0.028 * uWind * gust;
    o.y += sin(t * 1.9 + ph * 1.3) * 0.018 * uWind * gust;
    o.z += cos(t * 1.2 + ph) * 0.028 * uWind * gust;
    o.xz += uWindDir * (gust - 0.6) * 0.05 * uWind;   // the canopy heaves downwind on a gust
  } else if (sway < 3.5) {
    o.y += sin(t * 1.6 + w.x * 0.8 + w.y * 0.55) * 0.035 + sin(t * 1.05 - w.x * 0.37 + w.y * 0.77) * 0.025 - 0.06;
  } else {
    float k = (sway - 4.0) / 11.0;
    float ph = floor(w.x) * 1.7 + floor(w.y) * 2.3;
    o.x += sin(t * 1.7 + ph) * 0.07 * k * (0.35 + uWind) * gust;
    o.z += cos(t * 1.3 + ph * 0.7) * 0.05 * k * (0.35 + uWind) * gust;
  }
  return o;
}

// older call form (kinds 0 none, 1 plant, 2 leaves, 3 liquid, 4 hanging) kept for other shaders
vec3 waveOffset(uint wave, vec3 world, vec3 local) { return waveOffset(wave == 4u ? 10.0 : float(wave), world); }
vec3 waterColor(vec3 c, vec3 viewVec, vec3 n) {
  vec3 v = normalize(-viewVec);
  float fres = pow(1.0 - max(0.0, dot(v, n)), 3.0);
  vec3 h = normalize(v + uSunDir);
  float spec = pow(max(0.0, dot(h, n)), 120.0) * uDaylight;
  return mix(c, mix(uSkyColor, uFogColor, 0.3), fres * 0.6) + spec * 0.6;
}

float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float vnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash12(i), hash12(i + vec2(1, 0)), f.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), f.x), f.y);
}
#endif
