// colour_filter.glsl — the colour-blind filters (Options → Graphics → Colour filter; BTA's accessibility shaders): the
// finished picture is read back and the colours a kind of colour blindness loses are moved to ones it keeps
// (daltonising: Machado et al. 2009's simulation, Fidaner's shift of the lost difference).
uniform sampler2D uTex;
uniform int uKind;   // 1 protanopia (red), 2 deuteranopia (green), 3 tritanopia (blue), 4 Exclusion's Zone grade
uniform float uTime;

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
    // the Zone's look (the Misery mod's grade): washed-out colour, an olive-brown cast, harder contrast, dark corners, grain
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    vec3 g = mix(vec3(l), c, 0.52);
    g *= vec3(1.03, 1.0, 0.86);
    g = (g - 0.5) * 1.12 + 0.47;
    float v = distance(vUV, vec2(0.5));
    g *= 1.0 - smoothstep(0.42, 0.85, v) * 0.5;
    float n = fract(sin(dot(vUV * 1000.0 + vec2(uTime, uTime * 1.7), vec2(12.9898, 78.233))) * 43758.5453);
    g += (n - 0.5) * 0.03;
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
