// zone_emission.glsl — the emission (render/zone_emission_fx.cpp, render/zone_emission_fx.h).
// uMode 0, the sky: drawn at the far plane after the land, so only where no land is: a deck of torn black-red cloud racing
// south from the plant, lit from below; the lightning (a jagged stroke and the cloud lit round it); and the wave's front, a
// white-hot arc rolling over the sky from the north horizon to the south one, the sky behind it left burning.
// uMode 1, the screen of anyone caught in it: the picture shimmers, smears outward and splits its colours, a red pulse,
// the bolt's flash, the shove as the wave goes over, dark red at the edges.
uniform int uMode;
uniform mat4 uInvViewProj;
uniform sampler2D uTex;
uniform float uTime;
uniform vec4 uEm;      // x strength 0..1, y daylight (how lit the cloud is from above), z the screen's flash, w the push
uniform vec4 uBolt;    // xyz the bolt's direction (horizontal), w how bright it is now
uniform vec4 uSweep;   // x the front (dot with north), y its strength, z the bolt's seed
uniform vec3 uNorth;
#ifdef VERTEX
out vec2 vUV;
void main() {
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);   // one triangle over the whole view
  vUV = p;
  gl_Position = vec4(p * 2.0 - 1.0, uMode == 0 ? 1.0 : 0.0, 1.0);
}
#endif
#ifdef FRAGMENT
in vec2 vUV;
out vec4 fragColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
  vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}
float fbm(vec2 p) {
  float s = 0.0, a = 0.5;
  for (int i = 0; i < 5; i++) { s += a * noise(p); p = p * 2.03 + vec2(1.7, 9.2); a *= 0.5; }
  return s;
}
void main() {
  float k = uEm.x;
  if (uMode == 0) {
    vec4 w = uInvViewProj * vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
    vec3 d = normalize(w.xyz / w.w);
    float hz = smoothstep(-0.04, 0.22, d.y);
    // the cloud deck: projected on a plane overhead, two layers sliding at different speeds (rolling), torn by a third
    vec2 q = d.xz / (d.y + 0.16) * 1.2;
    vec2 flow = -uNorth.xz * uTime * (0.05 + 0.07 * k);
    float c1 = fbm(q + flow);
    float c2 = fbm(q * 2.1 - flow * 1.8 + vec2(c1 * 1.6, -c1));
    float c3 = fbm(q * 4.3 + flow * 2.6);
    float cl = smoothstep(0.32, 0.72, c1 * 0.65 + c2 * 0.45 - c3 * 0.12);
    float rim = smoothstep(0.12, 0.0, abs(c1 * 0.65 + c2 * 0.45 - 0.5)) * 0.8;   // the torn edges, lit red from below
    vec3 dark = vec3(0.07, 0.016, 0.012) + vec3(0.06, 0.02, 0.015) * uEm.y;
    vec3 glow = vec3(0.85, 0.26, 0.08);
    float ca = (0.25 + 0.6 * cl) * hz * k;
    vec3 col = mix(glow * 0.55, dark, cl) * ca + glow * rim * hz * k * 0.35;
    float a = ca;
    // the wave
    if (uSweep.y > 0.0) {
      float dn = dot(d, uNorth);
      float wob = (fbm(d.xz * 5.0 + vec2(uTime * 0.5, 0.0)) - 0.5) * 0.14 + (noise(d.xz * 22.0 - uTime) - 0.5) * 0.03;
      float x = dn - uSweep.x + wob;   // > 0: the sky it has already passed (nearer the north)
      float core = exp(-x * x / 0.0009), halo = exp(-x * x / 0.018);
      float behind = smoothstep(-0.02, 0.2, x) * exp(-max(x, 0.0) * 1.6);
      float up = smoothstep(-0.08, 0.06, d.y);
      vec3 wc = vec3(1.0, 0.88, 0.66) * core * 1.7 + vec3(1.0, 0.38, 0.12) * halo * 0.9 + vec3(0.75, 0.13, 0.05) * behind * (0.5 + 0.5 * cl);
      col += wc * uSweep.y * up;
      a = max(a, clamp(core + halo * 0.5, 0.0, 1.0) * uSweep.y * up);
    }
    // the lightning
    if (uBolt.w > 0.0) {
      vec3 b = normalize(uBolt.xyz);
      float da = atan(d.z, d.x) - atan(b.z, b.x);
      da = mod(da + 3.14159, 6.28318) - 3.14159;
      float top = 0.5, e = d.y;
      if (e > -0.03 && e < top) {
        float s = uSweep.z;
        float j = (noise(vec2(e * 14.0, s)) - 0.5) * 0.14 + (noise(vec2(e * 55.0, s + 3.0)) - 0.5) * 0.04;
        float wdt = 0.0025 + 0.004 * (1.0 - e / top);
        float stroke = smoothstep(wdt, 0.0, abs(da - j));
        // a fork off to one side, lower down
        float fe = clamp((0.32 - e) / 0.32, 0.0, 1.0);
        float fj = j + fe * fe * (hash(vec2(s, 1.0)) > 0.5 ? 0.09 : -0.09) + (noise(vec2(e * 40.0, s + 7.0)) - 0.5) * 0.03;
        stroke = max(stroke, smoothstep(wdt * 0.7, 0.0, abs(da - fj)) * step(e, 0.32) * 0.7);
        stroke *= smoothstep(top, top - 0.08, e);
        col += vec3(1.0, 0.93, 0.88) * stroke * uBolt.w * 2.2;
        a = max(a, stroke * uBolt.w);
      }
      float near = exp(-da * da * 5.0) * smoothstep(-0.06, 0.35, d.y);
      col += vec3(1.0, 0.55, 0.42) * near * uBolt.w * (0.25 + 0.6 * cl) + vec3(0.6, 0.3, 0.25) * uBolt.w * 0.18 * hz;
    }
    fragColor = vec4(col, clamp(a, 0.0, 1.0));   // premultiplied (ONE, ONE_MINUS_SRC_ALPHA)
    return;
  }
  // ---- the screen
  vec2 uv = vUV, dc = uv - 0.5;
  float push = uEm.w;
  uv += vec2(sin(uv.y * 21.0 + uTime * 4.7), cos(uv.x * 15.0 - uTime * 3.9)) * (0.0022 * k + 0.004 * push);
  uv += dc * push * 0.035 * sin(uTime * 23.0);
  float blur = 0.01 * k + 0.045 * push;
  float split = 0.003 * k + 0.008 * push;
  vec3 c = vec3(0.0);
  for (int i = 0; i < 8; i++) {
    float f = float(i) / 7.0;
    vec2 o = dc * blur * f;
    c.r += texture(uTex, uv - o - dc * split).r;
    c.g += texture(uTex, uv - o).g;
    c.b += texture(uTex, uv - o + dc * split).b;
  }
  c /= 8.0;
  float pulse = 0.72 + 0.28 * sin(uTime * 8.3);
  float l = dot(c, vec3(0.3, 0.59, 0.11));
  c = mix(c, vec3(l) * vec3(1.45, 0.52, 0.4) + vec3(0.045, 0.004, 0.0), clamp(0.32 * k * pulse, 0.0, 1.0));
  c += uEm.z * vec3(1.0, 0.8, 0.7) * 0.6;
  float v = length(dc);
  c *= 1.0 - smoothstep(0.28, 0.85, v) * (0.55 * k + 0.3 * push);
  fragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
#endif
