// colour_filter.glsl — the colour-blind filters (Options → Graphics → Colour filter; BTA's accessibility shaders): the
// finished picture is read back and the colours a kind of colour blindness loses are moved to ones it keeps
// (daltonising: Machado et al. 2009's simulation, Fidaner's shift of the lost difference).
uniform sampler2D uTex;
uniform int uKind;   // 1 protanopia (red), 2 deuteranopia (green), 3 tritanopia (blue), 4 Exclusion's Zone grade
uniform float uTime;
uniform float uExposure;   // Exclusion: the eye opening up at night (moonlit ground readable, shade still black)
uniform float uEm;   // Exclusion: an emission (0..1): the grade lets the red through (render/colour_filter_layer.cpp)

#ifdef VERTEX
out vec2 vUV;
void main() {
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);   // one triangle over the whole view
  vUV = p;
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT
in vec2 vUV;
out vec4 fragColor;
void main() {
  vec3 c = texture(uTex, vUV).rgb;
  if (uKind == 4) {
    // the Zone's look (Misery / STALCRAFT): a faint lens fringe at the edges, greens drained hardest (the dead grass),
    // a filmic S-curve with crushed blacks, cold grey-blue shadows against sickly olive-amber highlights, dark corners,
    // and grain that lives mostly in the shadows
    vec2 d = vUV - 0.5;
    float v = length(d);
    vec2 off = d * 0.006 * smoothstep(0.25, 0.75, v);
    c = vec3(texture(uTex, vUV + off).r, c.g, texture(uTex, vUV - off).b);
    c = c * uExposure / (1.0 + (uExposure - 1.0) * c);   // lifts the dark (x uExposure near black), white stays white
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    float green = clamp((c.g - max(c.r, c.b)) * 4.0, 0.0, 1.0);
    float sat = 0.56 - green * 0.24 + clamp((c.r - c.g) * 2.0, 0.0, 1.0) * 0.12;   // rust and fire keep a little more
    sat = mix(sat, 0.95, uEm * 0.75);
    vec3 g = mix(vec3(l), c, sat);
    g = max(g - 0.022, 0.0) / 0.978;                       // crushed blacks
    g = mix(g, g * g * (3.0 - 2.0 * g), 0.42);              // S-curve
    float lg = dot(g, vec3(0.299, 0.587, 0.114));
    vec3 shadow = mix(vec3(0.90, 0.97, 1.06), vec3(1.0), uEm), high = mix(vec3(1.07, 1.03, 0.84), vec3(1.05, 0.98, 0.92), uEm);
    g *= mix(shadow, high, smoothstep(0.12, 0.72, lg));
    g = mix(g, g * vec3(0.98, 1.0, 0.9) + vec3(0.012, 0.012, 0.0), 0.5);   // a dusty olive lift
    g *= 1.0 - smoothstep(0.36, 0.86, v) * 0.58;
    float n = fract(sin(dot(vUV * 1000.0 + vec2(uTime, uTime * 1.7), vec2(12.9898, 78.233))) * 43758.5453);
    g += (n - 0.5) * mix(0.05, 0.018, smoothstep(0.0, 0.5, lg));
    fragColor = vec4(clamp(g, 0.0, 1.0), 1.0);
    return;
  }
  mat3 sim;   // column-major: these are the rows of Machado's matrices, transposed
  if (uKind == 1) sim = mat3(0.152286, 0.114503, -0.003882, 1.052583, 0.786281, -0.048116, -0.204868, 0.099216, 1.051998);
  else if (uKind == 2) sim = mat3(0.367322, 0.280085, -0.011820, 0.860646, 0.672501, 0.042940, -0.227968, 0.047413, 0.968881);
  else sim = mat3(1.255528, -0.078411, 0.004733, -0.076749, 0.930809, 0.691367, -0.178779, 0.147602, 0.303900);
  vec3 err = c - sim * c;
  vec3 shift = vec3(0.0, 0.7 * err.r + err.g, 0.7 * err.r + err.b);
  if (uKind == 3) shift = vec3(err.r + 0.7 * err.b, err.g + 0.7 * err.b, 0.0);
  fragColor = vec4(clamp(c + shift, 0.0, 1.0), 1.0);
}
#endif
