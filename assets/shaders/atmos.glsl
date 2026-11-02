// atmos.glsl — screen-space light and shade over the finished world, before the hand (render/atmos_layer.cpp):
//   step 0: ambient occlusion at half size, from the depth alone (corners, crevices, under eaves and round feet)
//   step 1: put it on the picture (multiplied in), softened, fading out with distance
//   step 2: light shafts' source at a quarter size: the open sky (and the far fog) round the sun
//   step 3: the shafts: that smeared out from the sun (radial blur)
//   step 4: put shafts and a faint lens flare on the picture (added)
#ifdef VERTEX
out vec2 vUV;
void main() {   // one triangle over the screen
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  vUV = p;
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
#endif
#ifdef FRAGMENT
in vec2 vUV;
out vec4 fragColor;
uniform int uStep;
uniform sampler2D uDepth;     // the frame's depth (full size)
uniform sampler2D uSrc;       // step 1: the AO; step 3: the shaft source; step 4: the shafts
uniform vec2 uTexel;          // one texel of what this step reads
uniform vec2 uNearFar;        // the camera's planes
uniform vec2 uProjK;          // proj[0][0], proj[1][1]
uniform float uAspect;
uniform int uSamples;         // AO samples (0 = none)
uniform float uAoK;           // how dark AO gets
uniform vec2 uSunUV;          // the sun (or moon) on the screen
uniform vec2 uFog;            // the fog's start and end (blocks): far, fogged land lets the light through
uniform int uRaySteps;
uniform vec3 uShaftCol;       // shafts' colour * strength
uniform vec2 uFlare;          // x the flare's strength, y how far onto the screen the sun is (its cover comes from the shafts)
uniform float uFrame;

float linZ(float d) {
  float z = d * 2.0 - 1.0;
  return 2.0 * uNearFar.x * uNearFar.y / (uNearFar.y + uNearFar.x - z * (uNearFar.y - uNearFar.x));
}
// view-space position of a screen point (camera looks down -z)
vec3 viewPos(vec2 uv) {
  float z = linZ(textureLod(uDepth, uv, 0.0).r);
  return vec3((uv * 2.0 - 1.0) / uProjK * z, -z);
}
float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

void main() {
  if (uStep == 0) {
    float d = textureLod(uDepth, vUV, 0.0).r;
    if (d >= 0.99999 || uSamples == 0) { fragColor = vec4(1.0); return; }
    vec3 P = viewPos(vUV);
    float z = -P.z;
    if (z > 64.0) { fragColor = vec4(1.0); return; }
    // the normal from the nearer neighbour on each axis (no halo round edges)
    vec2 tx = uTexel;
    vec3 px = viewPos(vUV + vec2(tx.x, 0.0)), nx = viewPos(vUV - vec2(tx.x, 0.0));
    vec3 py = viewPos(vUV + vec2(0.0, tx.y)), ny = viewPos(vUV - vec2(0.0, tx.y));
    vec3 dx = abs(px.z - P.z) < abs(nx.z - P.z) ? px - P : P - nx;
    vec3 dy = abs(py.z - P.z) < abs(ny.z - P.z) ? py - P : P - ny;
    vec3 N = normalize(cross(dx, dy));
    const float R = 0.7;                                  // blocks
    vec2 rad = R * uProjK * 0.5 / z;                      // that radius on the screen (uv units)
    float occ = 0.0;
    float a0 = ign(gl_FragCoord.xy + uFrame * 7.0) * 6.2831853;
    for (int i = 0; i < 16; i++) {
      if (i >= uSamples) break;
      float t = (float(i) + 0.5) / float(uSamples);
      float a = a0 + float(i) * 2.399963;                 // golden angle spiral
      vec2 o = vec2(cos(a), sin(a)) * rad * mix(0.15, 1.0, t * t);
      vec3 S = viewPos(vUV + o);
      vec3 D = S - P;
      float l = length(D);
      float w = 1.0 - smoothstep(R * 0.6, R * 1.6, l);    // too far away to shade this point
      occ += max(dot(N, D / max(l, 1e-4)) - 0.15, 0.0) * w;
    }
    occ /= float(uSamples);
    fragColor = vec4(vec3(1.0 - clamp(occ * 1.6, 0.0, 1.0)), 1.0);
    return;
  }
  if (uStep == 1) {
    // a 2x2 box of bilinear taps over the half-size AO: smooth, and it stays put on the blocks
    vec3 ao = (texture(uSrc, vUV + uTexel * vec2(0.5, 0.5)).rgb + texture(uSrc, vUV + uTexel * vec2(-0.5, 0.5)).rgb
             + texture(uSrc, vUV + uTexel * vec2(0.5, -0.5)).rgb + texture(uSrc, vUV + uTexel * vec2(-0.5, -0.5)).rgb) * 0.25;
    float z = linZ(textureLod(uDepth, vUV, 0.0).r);
    float k = uAoK * (1.0 - smoothstep(24.0, 56.0, z)) * (1.0 - smoothstep(uFog.x, uFog.y, z));
    fragColor = vec4(mix(vec3(1.0), ao, k), 1.0);
    return;
  }
  if (uStep == 2) {
    // what lets light by near the sun: open sky, and land far enough in the fog
    vec2 o = uTexel * 0.25;
    float open = 0.0;
    for (int i = 0; i < 4; i++) {
      float d = textureLod(uDepth, vUV + vec2(i & 1, i >> 1) * 2.0 * o - o, 0.0).r;
      open += d >= 0.99999 ? 1.0 : smoothstep(uFog.x, uFog.y, linZ(d)) * 0.7;
    }
    open *= 0.25;
    vec2 q = (vUV - uSunUV) * vec2(uAspect, 1.0);
    float r2 = dot(q, q);
    float glow = exp(-r2 * 28.0) + 0.25 * exp(-r2 * 4.0);
    fragColor = vec4(vec3(open * glow), 1.0);
    return;
  }
  if (uStep == 3) {
    vec2 dir = (uSunUV - vUV) / float(uRaySteps) * 0.9;
    vec2 p = vUV + dir * ign(gl_FragCoord.xy);
    float acc = 0.0, decay = 1.0, wsum = 0.0;
    for (int i = 0; i < 64; i++) {
      if (i >= uRaySteps) break;
      acc += texture(uSrc, p).r * decay;
      wsum += decay;
      decay *= 0.965;
      p += dir;
    }
    fragColor = vec4(vec3(acc / max(wsum, 1e-4)), 1.0);
    return;
  }
  // step 4: shafts, and a lens flare: a few soft ghosts strung along the line from the sun through the middle
  vec3 c = texture(uSrc, vUV).rgb * uShaftCol;
  if (uFlare.x > 0.0 && uFlare.y > 0.0) {
    vec2 axis = vec2(0.5) - uSunUV;
    vec2 a = vec2(uAspect, 1.0);
    float k = uFlare.x * uFlare.y * clamp(texture(uSrc, clamp(uSunUV, 0.0, 1.0)).r * 0.9, 0.0, 1.0);   // the sun behind a tree: no flare
    const vec4 G[5] = vec4[5](vec4(0.45, 0.035, 0.9, 0.55), vec4(0.8, 0.06, 0.55, 0.9), vec4(1.25, 0.022, 1.0, 0.75),
                              vec4(1.55, 0.09, 0.45, 0.7), vec4(2.1, 0.045, 0.8, 1.0));
    for (int i = 0; i < 5; i++) {
      vec2 gp = uSunUV + axis * G[i].x;
      float r = length((vUV - gp) * a);
      float disc = smoothstep(G[i].y, G[i].y * 0.55, r);
      vec3 tint = mix(vec3(0.55, 0.75, 1.0), vec3(1.0, 0.75, 0.45), G[i].z);
      c += tint * disc * G[i].w * k * 0.18;
    }
    // a faint halo ring round the middle of the view
    float rr = length((vUV - (uSunUV + axis * 1.0)) * a);
    c += vec3(0.8, 0.85, 1.0) * smoothstep(0.03, 0.0, abs(rr - 0.32)) * k * 0.05;
  }
  fragColor = vec4(c, 1.0);
}
#endif
