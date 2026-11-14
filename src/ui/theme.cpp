// ui/theme.cpp — styles.css in ImGui (see theme.h).
#include "core/tool_parts.h"
#include "common/ui_scale.h"
#include "ui/theme.h"
#include "ui/paged.h"
#include "client/app.h"
#include "client/session.h"
#include "assets/icons.h"
#include "assets/gui_textures.h"
#include "audio/audio.h"
#include "render/gl.h"
#include "render/instruments.h"
#include "core/item_glint.h"
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

namespace dq {
namespace ui {

namespace {
struct FontSet { ImFont* f[F_COUNT]{}; float zoom = 0, dpi = 1; };
FontSet g_fonts;
const float FONT_PX[F_COUNT] = {12.5f, 15, 13, 17, 20, 24, 32, 64, 84, 12};
unsigned g_spriteTex = 0;
float g_zoom = 1;

float computeZoom(const App& a) {
  // Monitor tier (2K 125%, 3K 150%, 4K 200%), capped so the HUD still fits this window.
  int edge = std::max(0, std::min(a.width, a.height));
  if (a.window) {
    int idx = SDL_GetWindowDisplayIndex(a.window);
    if (idx < 0) idx = 0;
    SDL_Rect b{};
    if (SDL_GetDisplayBounds(idx, &b) == 0 && b.w > 0 && b.h > 0) edge = std::min(b.w, b.h);
  }
  return uiZoomFromSetting(a.settings.guiScale, autoUiZoom(a.width, a.height, edge));
}

void buildFonts(App& a, float zoom) {
  ImGuiIO& io = ImGui::GetIO();
  float dpi = 1; int dw, dh; SDL_GL_GetDrawableSize(a.window, &dw, &dh); if (a.width > 0) dpi = std::max(1.f, (float)dw / a.width);
  if (g_fonts.zoom == zoom && g_fonts.dpi == dpi && g_fonts.f[0]) return;
  bool had = g_fonts.f[0] != nullptr || io.Fonts->IsBuilt();
  if (had) ImGui_ImplOpenGL3_DestroyFontsTexture();
  io.Fonts->Clear();
  ImFontConfig cfg; cfg.OversampleH = 1; cfg.OversampleV = 1; cfg.PixelSnapH = true;
  static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2010, 0x2027, 0x2030, 0x203A, 0x2190, 0x21FF, 0x2212, 0x2212, 0x25A0, 0x25FF, 0x2600, 0x26FF, 0x2700, 0x27BF, 0};
  static const ImWchar ascii[] = {0x0020, 0x00FF, 0x2010, 0x2027, 0};
  std::string uiF = (assetsDir() / "fonts" / "PixelifySans.ttf").string(), tF = (assetsDir() / "fonts" / "Jersey15.ttf").string();
  bool haveUi = fs::exists(uiF), haveT = fs::exists(tF);
  for (int i = 0; i < F_COUNT; i++) {
    bool display = i == F_H3 || i == F_DISP || i == F_TITLE || i == F_HUGE;
    // ImGui sizes a font by its ascent+descent, CSS by its em: Pixelify Sans is 1.2 em tall, Jersey 15 1.0 em
    float size = std::round(FONT_PX[i] * zoom * dpi * (display ? 1.f : 1.2f));
    if (i == F_MONO) { ImFontConfig c2; c2.SizePixels = std::max(13.f, std::round(13 * zoom * dpi)); g_fonts.f[i] = io.Fonts->AddFontDefault(&c2); continue; }
    const std::string& file = display ? tF : uiF;
    bool have = display ? haveT : haveUi;
    g_fonts.f[i] = have ? io.Fonts->AddFontFromFileTTF(file.c_str(), size, &cfg, display ? ascii : ranges) : io.Fonts->AddFontDefault();
  }
  // symbols the pixel font doesn't have (♥ ▲ ▼ arrows ✕ ⚔ ✦ ◯ ▢ △), painted as glyphs of their own
  struct Sym { ImWchar cp; const char* rows[9]; };
  static const Sym SYMS[] = {
    {0x2665, {".###.###.", "#########", "#########", "#########", "#########", ".#######.", "..#####..", "...###...", "....#...."}},
    {0x25B2, {"....#....", "...###...", "...###...", "..#####..", "..#####..", ".#######.", ".#######.", "#########", "........."}},
    {0x25BC, {".........", "#########", ".#######.", ".#######.", "..#####..", "..#####..", "...###...", "...###...", "....#...."}},
    {0x2192, {".........", ".....#...", ".....##..", "########.", "#########", "########.", ".....##..", ".....#...", "........."}},
    {0x2190, {".........", "...#.....", "..##.....", ".########", "#########", ".########", "..##.....", "...#.....", "........."}},
    {0x2191, {"....#....", "...###...", "..#####..", ".#######.", "...###...", "...###...", "...###...", "...###...", "...###..."}},
    {0x2193, {"...###...", "...###...", "...###...", "...###...", "...###...", ".#######.", "..#####..", "...###...", "....#...."}},
    {0x2715, {"#.......#", "##.....##", ".##...##.", "..##.##..", "...###...", "..##.##..", ".##...##.", "##.....##", "#.......#"}},
    {0x2694, {"#.......#", ".#.....#.", "..#...#..", "...#.#...", "....#....", "...#.#...", ".##...##.", ".#.....#.", "#.......#"}},
    {0x2726, {"....#....", "....#....", "...###...", "..#####..", "#########", "..#####..", "...###...", "....#....", "....#...."}},
    {0x25EF, {"..#####..", ".#.....#.", "#.......#", "#.......#", "#.......#", "#.......#", "#.......#", ".#.....#.", "..#####.."}},
    {0x25A2, {".#######.", "#.......#", "#.......#", "#.......#", "#.......#", "#.......#", "#.......#", "#.......#", ".#######."}},
    {0x25B3, {"....#....", "...#.#...", "...#.#...", "..#...#..", "..#...#..", ".#.....#.", ".#.....#.", "#########", "........."}},
  };
  struct Pending { int rect; const Sym* sym; int px; };
  std::vector<Pending> pend;
  for (int i = 0; i < F_COUNT; i++) {
    bool display = i == F_H3 || i == F_DISP || i == F_TITLE || i == F_HUGE;
    if (display || i == F_MONO || !haveUi) continue;
    float size = std::round(FONT_PX[i] * zoom * dpi * 1.2f);
    int px = std::max(5, (int)std::round(size * 0.52f));
    for (auto& sy : SYMS) pend.push_back({io.Fonts->AddCustomRectFontGlyph(g_fonts.f[i], sy.cp, px, px, px + std::round(size * 0.1f), ImVec2(std::round(size * 0.05f), std::round(size * 0.77f) - px)), &sy, px});
  }
  io.Fonts->Build();
  {
    unsigned char* tex = nullptr; int tw = 0, th = 0;
    io.Fonts->GetTexDataAsRGBA32(&tex, &tw, &th);
    for (auto& pd : pend) {
      const ImFontAtlasCustomRect* r = io.Fonts->GetCustomRectByIndex(pd.rect);
      if (!r || !r->IsPacked()) continue;
      for (int y = 0; y < pd.px; y++) for (int x = 0; x < pd.px; x++) {
        bool on = pd.sym->rows[y * 9 / pd.px][x * 9 / pd.px] == '#';
        unsigned char* d = tex + ((size_t)(r->Y + y) * tw + r->X + x) * 4;
        d[0] = d[1] = d[2] = 255; d[3] = on ? 255 : 0;
      }
    }
  }
  io.FontGlobalScale = 1.f / dpi;
  if (had) ImGui_ImplOpenGL3_CreateFontsTexture();
  g_fonts.zoom = zoom; g_fonts.dpi = dpi;
  // the App's font slots point at these from now on
  a.fontUi = g_fonts.f[F_BODY]; a.fontSmall = g_fonts.f[F_MID]; a.fontUiBig = g_fonts.f[F_BIG]; a.fontTitle = g_fonts.f[F_TITLE];
  a.uiScale = zoom;
}

// 9×9 sprites from hud.js installHudSprites, plus the CSS filters (poison, chill, wither, glowing shields)
void buildSprites() {
  const char* heart[9] = {".kkk.kkk.", "kRRRkRRRk", "kRWRRRRRk", "kRRRRRRRk", "kRRRRRRRk", ".kRRRRRk.", "..kRRRk..", "...kRk...", "....k...."};
  const char* bubble[9] = {"..kkkkk..", ".kBBBBBk.", "kBWWBBBBk", "kBWBBBBBk", "kBBBBBBBk", "kBBBBBBBk", "kBBBBBBBk", ".kBBBBBk.", "..kkkkk.."};
  const char* pop[9] = {".........", "..k...k..", ".k.B.B.k.", "...B.B...", ".kB...Bk.", "...B.B...", ".k.B.B.k.", "..k...k..", "........."};
  const char* shield[9] = {"kkkkkkkkk", "kSSSSLLLk", "kSWSSLLLk", "kSSSSLLLk", "kSSSSLLLk", ".kSSSLLk.", "..kSSLk..", "...kSk...", "....k...."};
  auto pal = [](char c) -> u32 { switch (c) { case 'k': return 0x140808; case 'R': return 0xe5484d; case 'W': return 0xffb3b0; case 'D': return 0x3a1a1a;
    case 'S': return 0xcfd6de; case 'L': return 0x90a0b0; case 'B': return 0x5aa6f0; default: return 0xffffffffu; } };
  std::vector<u8> px((size_t)9 * 9 * SP_COUNT * 4, 0);
  // CSS filter helpers: hue-rotate / saturate / brightness / grayscale on a colour
  auto filter = [](vec3 c, float hue, float sat, float bright, float gray) {
    float a = hue * PI / 180.f, cs = std::cos(a), sn = std::sin(a);
    mat3 m(0.213f + cs * 0.787f - sn * 0.213f, 0.213f - cs * 0.213f + sn * 0.143f, 0.213f - cs * 0.213f - sn * 0.787f,
           0.715f - cs * 0.715f - sn * 0.715f, 0.715f + cs * 0.285f + sn * 0.140f, 0.715f - cs * 0.715f + sn * 0.715f,
           0.072f - cs * 0.072f + sn * 0.928f, 0.072f - cs * 0.072f - sn * 0.283f, 0.072f + cs * 0.928f + sn * 0.072f);
    c = glm::clamp(m * c, 0.f, 1.f);
    float l = glm::dot(c, vec3(0.2126f, 0.7152f, 0.0722f));
    c = glm::mix(vec3(l), c, sat);
    l = glm::dot(c, vec3(0.2126f, 0.7152f, 0.0722f));
    c = glm::mix(c, vec3(l), gray);
    return glm::clamp(c * bright, 0.f, 1.f);
  };
  auto put = [&](int sprite, const char* const* rows, auto mapChar, int tint) {
    for (int y = 0; y < 9; y++) for (int x = 0; x < 9; x++) {
      char ch = mapChar(rows[y][x], x);
      u32 c = pal(ch); if (c == 0xffffffffu) continue;
      vec3 v(((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f);
      if (tint == HT_POISON) v = filter(v, 100, 0.9f, 0.95f, 0);
      else if (tint == HT_CHILL) v = filter(v, 195, 0.7f, 1.15f, 0);
      else if (tint == HT_WITHER) v = filter(v, 0, 1, 0.35f, 1);
      else if (tint == 100) v = filter(v * 1.25f, 160, 1, 1, 0);
      u8* d = &px[(((size_t)sprite * 9 + y) * 9 + x) * 4];
      d[0] = (u8)(v.r * 255); d[1] = (u8)(v.g * 255); d[2] = (u8)(v.b * 255); d[3] = 255;
    }
  };
  auto same = [](char c, int) { return c; };
  auto half = [](char c, int x) { return (c == 'R' || c == 'W') && x > 4 ? 'D' : c; };
  auto empty = [](char c, int) { return (c == 'R' || c == 'W') ? 'D' : c; };
  for (int t : {HT_NONE, HT_POISON, HT_CHILL, HT_WITHER}) { put(t + 0, heart, same, t); put(t + 1, heart, half, t); put(t + 2, heart, empty, t); }
  put(SP_SHIELD, shield, same, 0); put(SP_SHIELD_GLOW, shield, same, 100);
  put(SP_BUBBLE, bubble, same, 0); put(SP_BUBBLE_POP, pop, same, 0);
  g_spriteTex = makeTexture2D(9, 9 * SP_COUNT, px.data(), true, false, false);
}
}

ImFont* font(FontId f) { return g_fonts.f[f] ? g_fonts.f[f] : ImGui::GetFont(); }
float fontPx(FontId f) { return FONT_PX[f]; }
float Z() { return g_zoom; }
bool navVisible() { ImGuiContext& g = *GImGui; return g.NavCursorVisible && g.NavHighlightItemUnderNav; }
ImU32 withAlpha(ImU32 c, float a) { return (c & 0x00FFFFFF) | ((ImU32)clampv((int)(((c >> 24) & 255) * a), 0, 255) << 24); }
void playClick() { if (app().audio) app().audio->play("click"); }

void init(App& a) {
  g_zoom = computeZoom(a);
  DQ_LOG("interface %d%%", (int)std::lround(g_zoom * 100.f));
  buildFonts(a, g_zoom);
  buildSprites();
  ImGuiStyle& s = ImGui::GetStyle();
<<<<<<< ours
  // the Zone's menus: square, worn gunmetal, 1px edges, amber for whatever is picked
  s.WindowRounding = 0; s.ChildRounding = 0; s.FrameRounding = 0; s.PopupRounding = 0; s.GrabRounding = 0; s.ScrollbarRounding = 0;
  s.WindowBorderSize = 0; s.FrameBorderSize = 1; s.PopupBorderSize = 1; s.ChildBorderSize = 0;
  s.ItemSpacing = ImVec2(8, 8); s.ScrollbarSize = 8; s.GrabMinSize = 14;
  ImVec4* c = s.Colors;
  auto C = [](u32 hex, float a = 1) { return ImVec4(((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f, a); };
  c[ImGuiCol_Text] = C(0xd6d0b9); c[ImGuiCol_TextDisabled] = C(0x8c8875);
  c[ImGuiCol_WindowBg] = C(0x111209, 0.95f); c[ImGuiCol_ChildBg] = C(0, 0); c[ImGuiCol_PopupBg] = C(0x0f100b, 0.98f);
  c[ImGuiCol_Border] = C(0x3a3b2d); c[ImGuiCol_BorderShadow] = C(0, 0);
  c[ImGuiCol_FrameBg] = C(0x0a0b08); c[ImGuiCol_FrameBgHovered] = C(0x14150f); c[ImGuiCol_FrameBgActive] = C(0x14150f);
  c[ImGuiCol_CheckMark] = C(0xe2b84c); c[ImGuiCol_SliderGrab] = C(0xe2b84c); c[ImGuiCol_SliderGrabActive] = C(0xf2cc6a);
  c[ImGuiCol_Button] = C(0x1c1d16); c[ImGuiCol_ButtonHovered] = C(0x2b2a1c); c[ImGuiCol_ButtonActive] = C(0x3a3220);
  c[ImGuiCol_Header] = C(0xe2b84c, 0.16f); c[ImGuiCol_HeaderHovered] = C(0xe2b84c, 0.26f); c[ImGuiCol_HeaderActive] = C(0xe2b84c, 0.36f);
  c[ImGuiCol_ScrollbarBg] = C(0x0a0b08, 0.5f); c[ImGuiCol_ScrollbarGrab] = C(0x3a3b2d); c[ImGuiCol_ScrollbarGrabHovered] = C(0x8a6a2c); c[ImGuiCol_ScrollbarGrabActive] = C(0xe2b84c);
  c[ImGuiCol_Separator] = C(0x3a3b2d); c[ImGuiCol_NavCursor] = C(0xe2b84c); c[ImGuiCol_TextSelectedBg] = C(0xe2b84c, 0.35f);
  c[ImGuiCol_ModalWindowDimBg] = C(0x040503, 0.66f);
=======
  // square and grey like BTA's screens and the launcher's Options tab (black fields with a grey frame, white checks)
  s.WindowRounding = 0; s.ChildRounding = 0; s.FrameRounding = 0; s.PopupRounding = 0; s.GrabRounding = 0; s.ScrollbarRounding = 0;
  s.WindowBorderSize = 0; s.FrameBorderSize = 2; s.PopupBorderSize = 2; s.ChildBorderSize = 0;
  s.ItemSpacing = ImVec2(8, 8); s.ScrollbarSize = 10; s.GrabMinSize = 14;
  ImVec4* c = s.Colors;
  auto C = [](u32 hex, float a = 1) { return ImVec4(((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f, a); };
  c[ImGuiCol_Text] = C(0xe0e0e0); c[ImGuiCol_TextDisabled] = C(0xa0a0a0);
  c[ImGuiCol_WindowBg] = C(0x101010, 0.94f); c[ImGuiCol_ChildBg] = C(0, 0); c[ImGuiCol_PopupBg] = C(0x101010, 0.98f);
  c[ImGuiCol_Border] = C(0xa0a0a0); c[ImGuiCol_BorderShadow] = C(0, 0);
  c[ImGuiCol_FrameBg] = C(0x000000); c[ImGuiCol_FrameBgHovered] = C(0x141414); c[ImGuiCol_FrameBgActive] = C(0x1c1c1c);
  c[ImGuiCol_CheckMark] = C(0xffffff); c[ImGuiCol_SliderGrab] = C(0xc6c6c6); c[ImGuiCol_SliderGrabActive] = C(0xffffff);
  c[ImGuiCol_Button] = C(0x6f6f6f); c[ImGuiCol_ButtonHovered] = C(0x7e88bf); c[ImGuiCol_ButtonActive] = C(0x5a5a5a);
  c[ImGuiCol_Header] = C(0x404040); c[ImGuiCol_HeaderHovered] = C(0x5a5f80); c[ImGuiCol_HeaderActive] = C(0x6e6e8e);
  c[ImGuiCol_ScrollbarBg] = C(0x000000, 0.6f); c[ImGuiCol_ScrollbarGrab] = C(0x808080); c[ImGuiCol_ScrollbarGrabHovered] = C(0xa0a0a0); c[ImGuiCol_ScrollbarGrabActive] = C(0xc0c0c0);
  c[ImGuiCol_Separator] = C(0x000000); c[ImGuiCol_NavCursor] = C(0xffffff); c[ImGuiCol_TextSelectedBg] = C(0x3050ff, 0.45f);
  c[ImGuiCol_ModalWindowDimBg] = C(0x101010, 0.6f);
>>>>>>> theirs
  // pads are fed by beginFrame (standard layout, pad maps, Nintendo swap)
  ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_Manual, nullptr, 0);
}

void feedNav(App& a) {
  ImGuiIO& io = ImGui::GetIO();
  Input& in = a.input;
  if (in.padCount() == 0 || in.setupActive) { if (in.padCount() == 0) io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad; return; }
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  auto key = [&](ImGuiKey k, int b) { io.AddKeyEvent(k, in.padButtonDown(b)); };
  key(ImGuiKey_GamepadFaceDown, 0); key(ImGuiKey_GamepadFaceRight, 1);
  key(ImGuiKey_GamepadDpadUp, 12); key(ImGuiKey_GamepadDpadDown, 13); key(ImGuiKey_GamepadDpadLeft, 14); key(ImGuiKey_GamepadDpadRight, 15);
  vec2 l(0);
  for (auto& p : in.pads()) {
    if (in.selected() >= 0) { auto& ids = in.sourceList()[in.selected()].pads; if (std::find(ids.begin(), ids.end(), p.id) == ids.end()) continue; }
    if (glm::length(p.lstick) > glm::length(l)) l = p.lstick;
  }
  auto ana = [&](ImGuiKey k, float v) { io.AddKeyAnalogEvent(k, v > 0.3f, std::max(0.f, v)); };
  ana(ImGuiKey_GamepadLStickLeft, -l.x); ana(ImGuiKey_GamepadLStickRight, l.x); ana(ImGuiKey_GamepadLStickUp, -l.y); ana(ImGuiKey_GamepadLStickDown, l.y);
}

static float g_baseZoom = 1;
float baseZoom() { return g_baseZoom; }
void localZoom(float k) {
  g_zoom = g_baseZoom * k;
  ImGui::GetIO().FontGlobalScale = k / std::max(1.f, g_fonts.dpi);
}

void beginFrame(App& a) {
  float z = std::round(computeZoom(a) * 20.f) / 20.f;
  if (z != g_zoom || !g_fonts.f[0]) { g_zoom = z; buildFonts(a, z); }
  g_baseZoom = g_zoom;
  ImGuiIO& io = ImGui::GetIO();
  a.input.textActive = io.WantTextInput || (a.session && a.session->uiWantsText);
  feedNav(a);
}

// ---- text ---------------------------------------------------------------------------------------------
static float sizeOf(FontId f, float scale) { return font(f)->FontSize * ImGui::GetIO().FontGlobalScale * scale; }
ImVec2 textSize(FontId f, const char* t, float wrap, float scale) { return font(f)->CalcTextSizeA(sizeOf(f, scale), FLT_MAX, wrap, t); }
void text(ImDrawList* dl, FontId f, ImVec2 p, ImU32 c, const char* t, float wrap, float scale) { dl->AddText(font(f), sizeOf(f, scale), ImVec2(std::round(p.x), std::round(p.y)), c, t, nullptr, wrap); }
void textShadow(ImDrawList* dl, FontId f, ImVec2 p, ImU32 c, const char* t, ImVec2 off, ImU32 sh, float wrap, float scale) {
  text(dl, f, p + off, sh, t, wrap, scale); text(dl, f, p, c, t, wrap, scale);
}
void textOutline(ImDrawList* dl, FontId f, ImVec2 p, ImU32 c, const char* t, float w, ImU32 o, float scale) {
  for (ImVec2 d : {ImVec2(w, 0), ImVec2(-w, 0), ImVec2(0, w), ImVec2(0, -w)}) text(dl, f, p + d, o, t, 0, scale);
  text(dl, f, p, c, t, 0, scale);
}
void textCentered(ImDrawList* dl, FontId f, ImVec2 center, ImU32 c, const char* t, ImU32 shadow, ImVec2 off) {
  ImVec2 s = textSize(f, t); ImVec2 p(center.x - s.x * 0.5f, center.y - s.y * 0.5f);
  if (shadow) text(dl, f, p + off * Z(), shadow, t);
  text(dl, f, p, c, t);
}
void label(FontId f, ImU32 c, const char* t, float wrap) {
  ImGui::PushFont(font(f)); ImGui::PushStyleColor(ImGuiCol_Text, c);
  if (wrap >= 0) { ImGui::PushTextWrapPos(wrap > 0 ? ImGui::GetCursorPosX() + wrap : 0.f); ImGui::TextUnformatted(t); ImGui::PopTextWrapPos(); }
  else ImGui::TextUnformatted(t);
  ImGui::PopStyleColor(); ImGui::PopFont();
}
std::string ellipsize(FontId f, const std::string& s, float maxW) {
  if (textSize(f, s.c_str()).x <= maxW) return s;
  std::string t = s;
  while (!t.empty() && textSize(f, (t + "\xE2\x80\xA6").c_str()).x > maxW) { t.pop_back(); while (!t.empty() && ((u8)t.back() & 0xC0) == 0x80) t.pop_back(); }
  return t + "\xE2\x80\xA6";
}

// ---- backgrounds ----------------------------------------------------------------------------------------
void drawTiled(ImDrawList* dl, const char* tileName, ImVec2 a, ImVec2 b, float tilePx, ImU32 tint) {
  int t = reg().tile(tileName);
  IconRef r = t >= 0 ? itemIcons().tile(t) : IconRef();
  if (!r.valid() || tilePx < 2) return;
  dl->PushClipRect(a, b, true);
  for (float y = a.y; y < b.y; y += tilePx) for (float x = a.x; x < b.x; x += tilePx)
    dl->AddImage((ImTextureID)(intptr_t)r.tex, ImVec2(x, y), ImVec2(x + tilePx, y + tilePx), ImVec2(r.uv0.x, r.uv0.y), ImVec2(r.uv1.x, r.uv1.y), tint);
  dl->PopClipRect();
}
void radialVignette(ImDrawList* dl, ImVec2 c, ImVec2 R, float inner, ImU32 outer, float reach) {
  const int N = 64;
  ImU32 clear = outer & 0x00FFFFFF;
  auto ring = [&](float f0, float f1, ImU32 c0, ImU32 c1) {
    dl->PrimReserve(N * 6, N * 4);
    for (int i = 0; i < N; i++) {
      float a0 = (float)i / N * TAU, a1 = (float)(i + 1) / N * TAU;
      ImVec2 d0(std::cos(a0) * R.x, std::sin(a0) * R.y), d1(std::cos(a1) * R.x, std::sin(a1) * R.y);
      ImDrawIdx b = (ImDrawIdx)dl->_VtxCurrentIdx;
      ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
      dl->PrimWriteVtx(c + d0 * f0, uv, c0); dl->PrimWriteVtx(c + d1 * f0, uv, c0);
      dl->PrimWriteVtx(c + d1 * f1, uv, c1); dl->PrimWriteVtx(c + d0 * f1, uv, c1);
      dl->PrimWriteIdx(b); dl->PrimWriteIdx(b + 1); dl->PrimWriteIdx(b + 2);
      dl->PrimWriteIdx(b); dl->PrimWriteIdx(b + 2); dl->PrimWriteIdx(b + 3);
    }
  };
  ring(inner, 1, clear, outer);
  ring(1, reach, outer, outer);
}
void bevel(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 tl, ImU32 br, float w) {
  dl->AddRectFilled(a, ImVec2(b.x, a.y + w), tl); dl->AddRectFilled(a, ImVec2(a.x + w, b.y), tl);
  dl->AddRectFilled(ImVec2(a.x, b.y - w), b, br); dl->AddRectFilled(ImVec2(b.x - w, a.y), b, br);
}

void btnFace(ImDrawList* dl, ImVec2 a, ImVec2 b, bool hot, bool held, float al) {
  const float z = Z();
  if (held) { a.y += 1 * z; b.y += 1 * z; }   // :active { translateY(1px) }
  dl->AddRectFilledMultiColor(a, b, withAlpha(col::slotHi, al), withAlpha(col::slotHi, al), withAlpha(col::slot, al), withAlpha(col::slot, al));
  dl->AddRectFilled(ImVec2(a.x, b.y - (held ? 1 : 3) * z), b, IM_COL32(0, 0, 0, (int)(89 * al)));   // inset 0 -3px 0 rgba(0,0,0,.35)
  dl->AddRectFilled(a, ImVec2(b.x, a.y + 2 * z), IM_COL32(255, 255, 255, (int)(15 * al)));        // inset 0 2px 0 rgba(255,255,255,.06)
  dl->AddRect(a + ImVec2(1, 1) * z, b - ImVec2(1, 1) * z, withAlpha(hot ? col::lineHi : col::line, al), 3 * z, 0, 2 * z);   // border: 2px, radius 4
}

void dropShadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float offY, float blur, float alpha) {
  const int N = 12;
  const int la = (int)std::lround(255.f * (1.f - std::pow(1.f - alpha, 1.f / N)));   // N layers that add up to `alpha` in the middle
  for (int j = 0; j < N; j++) {
    float e = blur - 2 * blur * (j + 0.5f) / N;   // +blur (outermost) .. -blur (innermost): the fade across the edge
    ImVec2 p0(a.x - e, a.y + offY - e), p1(b.x + e, b.y + offY + e);
    if (p1.x <= p0.x || p1.y <= p0.y) continue;
    dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, la), std::max(0.f, e));
  }
}

// the lines of `t` as ImGui would wrap them: [begin, end) pairs
static void wrapLines(FontId f, const char* t, float wrap, float scale, std::vector<std::pair<const char*, const char*>>& out) {
  ImFont* fo = font(f); const float k = sizeOf(f, scale) / fo->FontSize;
  const char* end = t + std::strlen(t);
  for (const char* s = t;;) {
    const char* nl = (const char*)std::memchr(s, '\n', (size_t)(end - s));
    const char* le = nl ? nl : end; const char* p = s;
    if (p == le) out.push_back({p, p});
    while (p < le) {
      const char* w = wrap > 0 ? fo->CalcWordWrapPositionA(k, p, le, wrap) : le;
      if (w <= p) { w = p + 1; while (w < le && ((u8)*w & 0xC0) == 0x80) w++; }
      out.push_back({p, w});
      p = w;
      while (p < le && *p == ' ') p++;   // the space a line broke at
    }
    if (!nl) break;
    s = nl + 1;
  }
}
float textLines(ImDrawList* dl, FontId f, ImVec2 pos, ImU32 c, const char* t, float wrap, float lineH, float scale) {
  std::vector<std::pair<const char*, const char*>> L; wrapLines(f, t, wrap, scale, L);
  const float sz = sizeOf(f, scale), off = (lineH - sz) * 0.5f;
  for (size_t i = 0; i < L.size(); i++)
    if (L[i].second > L[i].first) dl->AddText(font(f), sz, ImVec2(std::round(pos.x), std::round(pos.y + i * lineH + off)), c, L[i].first, L[i].second);
  return (float)L.size() * lineH;
}
ImVec2 textLinesSize(FontId f, const char* t, float wrap, float lineH, float scale) {
  std::vector<std::pair<const char*, const char*>> L; wrapLines(f, t, wrap, scale, L);
  const float sz = sizeOf(f, scale); float w = 0;
  for (auto& l : L) w = std::max(w, font(f)->CalcTextSizeA(sz, FLT_MAX, 0, l.first, l.second).x);
  return ImVec2(w, (float)L.size() * lineH);
}
float baselineOf(FontId f, float scale) { ImFont* fo = font(f); return fo->Ascent * sizeOf(f, scale) / fo->FontSize; }

// ---- buttons --------------------------------------------------------------------------------------------
static bool itemHot() { return ImGui::IsItemHovered() || (ImGui::IsItemFocused() && navVisible()); }

<<<<<<< ours
// While a custom widget's InvisibleButton is submitted ImGui's own nav rectangle is hidden: these widgets draw their own
// focus (the amber bar, corner brackets).
struct NoNavRect { NoNavRect() { ImGui::PushStyleColor(ImGuiCol_NavCursor, IM_COL32(0, 0, 0, 0)); } ~NoNavRect() { ImGui::PopStyleColor(); } };
// a focused slider / combo keeps left and right for itself (else ImGui's nav would also jump to the next column)
static void claimLeftRight(ImGuiID id) {
  for (ImGuiKey k : {ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight, ImGuiKey_LeftArrow, ImGuiKey_RightArrow})
    ImGui::SetKeyOwner(k, id);
}
// 0..1 easing of a widget's highlight, kept in the window's storage under the item id
static float hotFade(ImGuiID id, bool hot, float speed = 12) {
  ImGuiStorage* st = ImGui::GetStateStorage();
  float v = st->GetFloat(id, 0), dt = ImGui::GetIO().DeltaTime;
  v = hot ? std::min(1.f, v + dt * speed) : std::max(0.f, v - dt * speed * 0.7f);
  st->SetFloat(id, v);
  return v;
}
static bool g_keepCase = false;
KeepCase::KeepCase() { g_keepCase = true; }
KeepCase::~KeepCase() { g_keepCase = false; }
static std::string stripId(const char* lbl) { std::string t = lbl; size_t h = t.find("##"); if (h != std::string::npos) t.resize(h); return t; }

// .mcbtn is now the Zone's menu entry: a plain uppercase line of text; hovered or focused, a faint amber bar fading to
// the right with a small marker at its left end, and the text warms to amber
bool mcButton(const char* lbl, ImVec2 size, bool disabled) {
  ImVec2 p = ImGui::GetCursorScreenPos();
  bool clicked;
  { NoNavRect nn; ImGui::BeginDisabled(disabled); clicked = ImGui::InvisibleButton(lbl, size, ImGuiButtonFlags_EnableNav); ImGui::EndDisabled(); }
  bool hot = !disabled && itemHot(), held = !disabled && ImGui::IsItemActive();
  float v = hotFade(ImGui::GetItemID(), hot);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float z = Z();
  ImVec2 a = p, b(p.x + size.x, p.y + size.y);
  if (v > 0.01f) {
    ImU32 l = withAlpha(col::amber, 0.20f * v), r = withAlpha(col::amber, 0);
    dl->AddRectFilledMultiColor(a, b, l, r, r, l);
    ImU32 hl = withAlpha(col::amber, 0.55f * v);
    dl->AddRectFilledMultiColor(a, ImVec2(b.x, a.y + 1 * z), hl, r, r, hl);
    dl->AddRectFilledMultiColor(ImVec2(a.x, b.y - 1 * z), b, hl, r, r, hl);
    dl->AddRectFilled(ImVec2(a.x, a.y + size.y * 0.22f), ImVec2(a.x + 3 * z, b.y - size.y * 0.22f), withAlpha(col::amber, v));
  }
  std::string t = upper(stripId(lbl));
  FontId f = size.y >= 34 * z ? F_H3 : F_BTN;
  float sp = (f == F_H3 ? 2.5f : 1.5f) * z;
  ImVec2 ts = textSpacedSize(f, t.c_str(), sp);
  float room = size.x - 30 * z;
  if (ts.x > room) { sp = 0.5f * z; ts = textSpacedSize(f, t.c_str(), sp); }
  float k = ts.x > room ? std::max(0.7f, room / ts.x) : 1.f;
  ts = textSpacedSize(f, t.c_str(), sp, k);
  ImVec2 tp(a.x + (16 + 6 * v) * z, a.y + (size.y - ts.y) * 0.5f + (held ? 1 * z : 0));
  ImU32 tc = disabled ? col::rgb(0x55534a) : mix(col::zText, col::amberHi, v);
  textSpaced(dl, f, tp + ImVec2(1, 1) * z, IM_COL32(0, 0, 0, 170), t.c_str(), sp, k);
  textSpaced(dl, f, tp, tc, t.c_str(), sp, k);
  if (clicked) playClick();
  return clicked;
}
=======
// every menu button is the launcher's grey button (ui/paged.cpp classicButton), so the game's menus match the
// launcher's Options tab and BTA's screens
bool mcButton(const char* lbl, ImVec2 size, bool disabled) { return classicButton(lbl, size, disabled, F_MID); }
>>>>>>> theirs

// .btn — square worn plates with a 1px edge; uppercase spaced labels. Primary = the amber action, danger = rust red,
// ghost = bare text. Hovered or focused: an amber edge and a marker on the left; focused by pad/keys: corner brackets.
bool button(const char* lbl, BtnStyle st, bool tiny, float width, bool disabled) {
  (void)st;   // BTA has one kind of button: the grey one (the style only ever changed its colour)
  float z = Z();
<<<<<<< ours
  std::string t = stripId(lbl);
  if (!g_keepCase) t = upper(t);
  FontId f = tiny ? F_MID : F_BODY;
  float sp = (tiny ? 1.f : 1.5f) * z;
  ImVec2 ts = textSpacedSize(f, t.c_str(), sp);
  float h = (tiny ? 30 : 44) * z, padX = (tiny ? 10 : 18) * z;
  float w = width > 0 ? width : width < 0 ? ImGui::GetContentRegionAvail().x : ts.x + padX * 2 + 4 * z;
  ImVec2 p = ImGui::GetCursorScreenPos();
  bool clicked;
  { NoNavRect nn; ImGui::BeginDisabled(disabled); clicked = ImGui::InvisibleButton(lbl, ImVec2(w, h), ImGuiButtonFlags_EnableNav); ImGui::EndDisabled(); }
  bool hot = !disabled && itemHot(), held = !disabled && ImGui::IsItemActive(), focus = ImGui::IsItemFocused() && navVisible();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float al = disabled ? 0.4f : 1;
  ImVec2 a(p.x, p.y + (held ? 1 * z : 0)), b(p.x + w, p.y + h + (held ? 1 * z : 0));
  ImU32 top = col::rgb(0x202119), bot = col::rgb(0x15160f), border = hot ? withAlpha(col::amber, 0.75f) : col::zEdge, tc = hot ? col::amberHi : col::zText;
  if (st == BTN_PRIMARY) { top = col::rgb(0x3b2d14); bot = col::rgb(0x261c0c); border = hot ? col::amber : col::rgb(0x8c6a2c); tc = hot ? col::rgb(0xfff0c8) : col::amberHi; }
  else if (st == BTN_DANGER) { top = col::rgb(0x2c1711); bot = col::rgb(0x1d0f0b); border = hot ? col::rgb(0xc4583a) : col::rgb(0x6e3424); tc = hot ? col::rgb(0xffb8a2) : col::rgb(0xd88c76); }
  else if (st == BTN_GHOST) { border = 0; tc = hot ? col::amberHi : col::zDim; }
  if (st != BTN_GHOST) {
    dl->AddRectFilledMultiColor(a, b, withAlpha(top, al), withAlpha(top, al), withAlpha(bot, al), withAlpha(bot, al));
    grain(dl, a, b, 0.6f * al);
    dl->AddRectFilled(a, ImVec2(b.x, a.y + 1 * z), IM_COL32(255, 255, 255, (int)(14 * al)));
    if (hot) { ImU32 l = withAlpha(col::amber, 0.14f), r = withAlpha(col::amber, 0); dl->AddRectFilledMultiColor(a, b, l, r, r, l); }
  } else if (hot) {
    ImU32 l = withAlpha(col::amber, 0.16f), r = withAlpha(col::amber, 0);
    dl->AddRectFilledMultiColor(a, b, l, r, r, l);
  }
  if (border) dl->AddRect(a, b, withAlpha(border, al), 0, 0, 1 * z);
  if (hot) dl->AddRectFilled(ImVec2(a.x + 1 * z, a.y + h * 0.25f), ImVec2(a.x + 3 * z, b.y - h * 0.25f), col::amber);
  if (focus) cornerBrackets(dl, a - ImVec2(3, 3) * z, b + ImVec2(3, 3) * z, 6 * z, col::amber, 1.5f * z);
  float room = w - 10 * z;
  if (ts.x > room) { sp = 0; ts = textSpacedSize(f, t.c_str(), sp); }
  float k = ts.x > room ? std::max(0.72f, room / ts.x) : 1.f;
  if (k < 1) ts = textSpacedSize(f, t.c_str(), sp, k);
  ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), tp(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f);
  dl->PushClipRect(a, b, true);
  textSpaced(dl, f, tp + ImVec2(1, 1) * z, IM_COL32(0, 0, 0, (int)(170 * al)), t.c_str(), sp, k);
  textSpaced(dl, f, tp, withAlpha(tc, al), t.c_str(), sp, k);
  dl->PopClipRect();
  if (clicked) playClick();
  return clicked;
=======
  std::string t = lbl; size_t hh = t.find("##"); if (hh != std::string::npos) t.resize(hh);
  const FontId f = F_MID;
  ImVec2 ts = textSize(f, t.c_str());
  float h = (tiny ? 30 : 40) * z, padX = (tiny ? 12 : 20) * z;
  float w = width > 0 ? width : width < 0 ? ImGui::GetContentRegionAvail().x : ts.x + padX * 2 + 4 * z;
  return classicButton(lbl, ImVec2(w, h), disabled, f);
>>>>>>> theirs
}

// the toggle: a sunk square; on = an amber block inside it
bool checkBox(const char* id, bool* v) {
  float z = Z(), s = ImGui::GetFrameHeight();
  ImVec2 p = ImGui::GetCursorScreenPos();
  bool clicked;
  { NoNavRect nn; clicked = ImGui::InvisibleButton(id, ImVec2(s, s), ImGuiButtonFlags_EnableNav); }
  if (clicked) { *v = !*v; playClick(); }
  bool hot = itemHot(), focus = ImGui::IsItemFocused() && navVisible();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, p + ImVec2(s, s), col::rgb(0x090a07));
  dl->AddRect(p, p + ImVec2(s, s), hot ? col::amber : col::zEdgeHi, 0, 0, 1 * z);
  float in = std::max(3.f * z, s * 0.22f);
  if (*v) { dl->AddRectFilled(p + ImVec2(in, in), p + ImVec2(s - in, s - in), col::amber); grain(dl, p + ImVec2(in, in), p + ImVec2(s - in, s - in), 0.8f); }
  if (focus) cornerBrackets(dl, p - ImVec2(3, 3) * z, p + ImVec2(s + 3 * z, s + 3 * z), 5 * z, col::amber, 1.5f * z);
  return clicked;
}

bool checkbox(const char* lbl, bool* v, const char* desc) {
  float z = Z(), s = ImGui::GetFrameHeight();
  std::string t = stripId(lbl);
  ImVec2 ts = textSize(F_BODY, t.c_str());
  ImVec2 p = ImGui::GetCursorScreenPos();
  // the whole line (box and words) is one button, like a <label>
  bool clicked;
  { NoNavRect nn; clicked = ImGui::InvisibleButton(lbl, ImVec2(s + 10 * z + ts.x, s), ImGuiButtonFlags_EnableNav); }
  if (clicked) { *v = !*v; playClick(); }
  bool hot = itemHot(), focus = ImGui::IsItemFocused() && navVisible();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, p + ImVec2(s, s), col::rgb(0x090a07));
  dl->AddRect(p, p + ImVec2(s, s), hot ? col::amber : col::zEdgeHi, 0, 0, 1 * z);
  float in = std::max(3.f * z, s * 0.22f);
  if (*v) dl->AddRectFilled(p + ImVec2(in, in), p + ImVec2(s - in, s - in), col::amber);
  text(dl, F_BODY, ImVec2(p.x + s + 10 * z, p.y + (s - ts.y) * 0.5f), hot ? col::amberHi : col::zText, t.c_str());
  if (focus) cornerBrackets(dl, p - ImVec2(3, 3) * z, p + ImVec2(s + 13 * z + ts.x, s + 3 * z), 5 * z, col::amber, 1.5f * z);
  if (desc && *desc) { ImGui::Indent(s + 10 * z); label(F_SMALL, col::dim, desc, 0); ImGui::Unindent(s + 10 * z); }
  return clicked;
}

bool slider(const char* id, float* v, float mn, float mx, float step, const char* shown, float width) {
<<<<<<< ours
  // an instrument scale: a sunk groove with ticks, rust-to-amber fill up to a square grip
  float z = Z(), w = width > 0 ? width : ImGui::GetContentRegionAvail().x, h = 20 * z;
  ImVec2 p = ImGui::GetCursorScreenPos();
  { NoNavRect nn; ImGui::InvisibleButton(id, ImVec2(w, h), ImGuiButtonFlags_EnableNav); }
  bool ch = false;
  float r = 4 * z, x0 = p.x + r, x1 = p.x + w - r;
  if (ImGui::IsItemActive()) {
    float t = clampv((ImGui::GetIO().MousePos.x - x0) / std::max(1.f, x1 - x0), 0.f, 1.f);
    float nv = mn + t * (mx - mn);
    if (step > 0) nv = clampv(std::round((nv - mn) / step) * step + mn, mn, mx);
    if (nv != *v) { *v = nv; ch = true; }
  }
  // controller / arrows: left and right change a focused slider straight away (ui.js menuNav)
  bool focus = ImGui::IsItemFocused() && navVisible();
  if (ImGui::IsItemFocused()) claimLeftRight(ImGui::GetItemID());
  if (focus && !ImGui::IsItemActive()) {
    int dx = app().input.navStep().x;
    if (dx) { *v = clampv(*v + step * dx, mn, mx); if (step > 0) *v = std::round((*v - mn) / step) * step + mn; ch = true; }
  }
  bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive() || focus;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float t = mx > mn ? clampv((*v - mn) / (mx - mn), 0.f, 1.f) : 0, kx = x0 + (x1 - x0) * t, cy = p.y + h * 0.5f;
  ImVec2 g0(p.x, cy - 3 * z), g1(p.x + w, cy + 3 * z);
  dl->AddRectFilled(g0, g1, col::rgb(0x090a07));
  if (kx > g0.x + 1 * z) dl->AddRectFilledMultiColor(g0 + ImVec2(1, 1) * z, ImVec2(kx, g1.y - 1 * z), col::rgb(0x7a3c1c), col::amber, col::amber, col::rgb(0x7a3c1c));
  dl->AddRect(g0, g1, hot ? withAlpha(col::amber, 0.6f) : col::zEdge, 0, 0, 1 * z);
  for (int i = 0; i <= 10; i++) {
    float x = std::round(x0 + (x1 - x0) * i / 10.f);
    dl->AddRectFilled(ImVec2(x, g1.y + 2 * z), ImVec2(x + 1 * z, g1.y + (i % 5 == 0 ? 6 : 4) * z), col::zEdgeHi);
  }
  ImVec2 k0(kx - 3 * z, p.y + 1 * z), k1(kx + 3 * z, p.y + h - 5 * z);
  dl->AddRectFilled(k0 - ImVec2(1, 1) * z, k1 + ImVec2(1, 1) * z, IM_COL32(0, 0, 0, 200));
  dl->AddRectFilled(k0, k1, hot ? col::amberHi : col::zText);
  if (focus) cornerBrackets(dl, p - ImVec2(4, 2) * z, p + ImVec2(w + 4 * z, h + 2 * z), 5 * z, col::amber, 1.5f * z);
  (void)shown;
  return ch;
=======
  // BTA's slider (ui/paged.cpp mcSlider): a dark track, a button-faced handle, the value written on it
  const float z = Z(), w = width > 0 ? width : ImGui::GetContentRegionAvail().x;
  return mcSlider(id, v, mn, mx, step, shown ? std::string(shown) : std::string(), ImVec2(w, 20 * z), nullptr);
>>>>>>> theirs
}

bool combo(const char* id, int* cur, const std::vector<std::string>& items, float width) {
  if (width != 0) ImGui::SetNextItemWidth(width);
  bool ch = false;
  const char* preview = *cur >= 0 && *cur < (int)items.size() ? items[*cur].c_str() : "";
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * Z(), 5 * Z()));
  bool open;
  { NoNavRect nn; open = ImGui::BeginCombo(id, preview); }
  ImGui::PopStyleVar();
  if (open) {
    for (int i = 0; i < (int)items.size(); i++) {
      bool sel = i == *cur;
      if (ImGui::Selectable(items[i].c_str(), sel)) { *cur = i; ch = true; }
      if (sel) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  } else if (ImGui::IsItemFocused()) {
    // a pad's left / right (or the arrow keys) steps through the choices without opening the list, like the STALKER options
    claimLeftRight(ImGui::GetItemID());
    int dx = navVisible() ? app().input.navStep().x : 0, n = (int)items.size();
    if (dx && n > 0) { *cur = ((*cur + dx) % n + n) % n; ch = true; }
  }
  if (!open && ImGui::IsItemFocused() && navVisible()) {
    float z = Z();
    cornerBrackets(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin() - ImVec2(3, 3) * z, ImGui::GetItemRectMax() + ImVec2(3, 3) * z, 5 * z, col::amber, 1.5f * z);
  }
  if (ch) playClick();
  return ch;
}

bool inputText(const char* id, std::string* s, const char* hint, size_t maxLen, float width, int flags) {
  ImGui::SetNextItemWidth(width);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10 * Z(), 10 * Z()));
  bool ch = hint ? ImGui::InputTextWithHint(id, hint, s, flags) : ImGui::InputText(id, s, flags);
  ImGui::PopStyleVar();
<<<<<<< ours
  if (ImGui::IsItemActive()) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), col::amber, 4 * Z(), 0, 2 * Z());
=======
  if (ImGui::IsItemActive()) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 255, 255, 255), 0, 0, 2 * Z());
>>>>>>> theirs
  if (maxLen && s->size() > maxLen) s->resize(maxLen);
  return ch;
}

void padScrollHere() {
  float d = app().input.padScroll();
  if (d == 0) return;
  ImGuiContext& g = *GImGui;
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) || g.NavId == 0 || ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
    ImGui::SetScrollY(ImGui::GetScrollY() + d);
}

// ---- the Zone's menu kit ----------------------------------------------------------------------------------
std::string upper(const std::string& s) { std::string o = s; for (char& c : o) if (c >= 'a' && c <= 'z') c = (char)(c - 32); return o; }
ImU32 mix(ImU32 a, ImU32 b, float t) {
  auto ch = [&](int sh) { float x = ((a >> sh) & 255) + (((b >> sh) & 255) - (float)((a >> sh) & 255)) * t; return (ImU32)clampv((int)std::lround(x), 0, 255) << sh; };
  return ch(0) | ch(8) | ch(16) | ch(24);
}
// letter-spaced text: each glyph advanced by its own width plus `spacing` pixels
template <class F> static void eachGlyph(FontId f, const char* t, float spacing, float scale, F fn) {
  ImFont* fo = font(f); const float sz = sizeOf(f, scale), k = sz / fo->FontSize;
  float x = 0;
  for (const char* s = t; *s;) {
    unsigned int c = 0; int n = ImTextCharFromUtf8(&c, s, nullptr);
    if (n <= 0) break;
    fn(s, s + n, x);
    x += fo->GetCharAdvance((ImWchar)c) * k + spacing;
    s += n;
  }
}
ImVec2 textSpacedSize(FontId f, const char* t, float spacing, float scale) {
  float w = 0;
  eachGlyph(f, t, spacing, scale, [&](const char*, const char*, float x) { w = x; });
  ImFont* fo = font(f); float k = sizeOf(f, scale) / fo->FontSize;
  if (*t) { const char* last = t; for (const char* s = t; *s; s++) if (((u8)*s & 0xC0) != 0x80) last = s; unsigned int c = 0; ImTextCharFromUtf8(&c, last, nullptr); w += fo->GetCharAdvance((ImWchar)c) * k; }
  return ImVec2(w, sizeOf(f, scale));
}
void textSpaced(ImDrawList* dl, FontId f, ImVec2 p, ImU32 c, const char* t, float spacing, float scale) {
  ImFont* fo = font(f); const float sz = sizeOf(f, scale);
  eachGlyph(f, t, spacing, scale, [&](const char* b, const char* e, float x) { dl->AddText(fo, sz, ImVec2(std::round(p.x + x), std::round(p.y)), c, b, e); });
}

// 256² of worn grain: fine dark/light specks, soft stains and a few scratches, tiled 1 texel = 1 pixel
static unsigned g_grainTex = 0;
static unsigned grainTex() {
  if (g_grainTex) return g_grainTex;
  const int N = 256;
  std::vector<u8> px((size_t)N * N * 4, 0);
  u32 st = 0x2545F491u;
  auto rnd = [&]() { st ^= st << 13; st ^= st >> 17; st ^= st << 5; return st; };
  auto frnd = [&]() { return (rnd() & 0xffffff) / 16777216.f; };
  // stains: a 16x16 grid of random values, smoothly interpolated (wraps, so it tiles)
  float grid[16][16]; for (auto& r : grid) for (float& v : r) v = frnd();
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
    float gx = x / 16.f, gy = y / 16.f; int ix = (int)gx, iy = (int)gy; float fx = gx - ix, fy = gy - iy;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float s = grid[iy & 15][ix & 15] * (1 - fx) * (1 - fy) + grid[iy & 15][(ix + 1) & 15] * fx * (1 - fy) + grid[(iy + 1) & 15][ix & 15] * (1 - fx) * fy + grid[(iy + 1) & 15][(ix + 1) & 15] * fx * fy;
    float n = frnd();
    u8* o = &px[((size_t)y * N + x) * 4];
    float stain = std::max(0.f, s - 0.55f) * 2.2f;             // 0..1 in the darker blotches
    if (n > 0.82f) { o[0] = o[1] = o[2] = 235; o[3] = (u8)(10 + (n - 0.82f) * 120); }   // light specks
    else { o[0] = 6; o[1] = 5; o[2] = 3; o[3] = (u8)std::min(255.f, (1 - n) * 22 + stain * 70); }
  }
  for (int k = 0; k < 90; k++) {   // scratches: thin straight-ish lines, mostly light
    float x = (float)(rnd() % N), y = (float)(rnd() % N), ang = frnd() * TAU, len = 5 + (float)(rnd() % 38);
    bool light = rnd() % 4 != 0; u8 al = (u8)(light ? 18 + rnd() % 26 : 70);
    for (int i = 0; i < (int)len; i++) {
      int xx = (((int)std::lround(x + std::cos(ang) * i)) % N + N) % N, yy = (((int)std::lround(y + std::sin(ang) * i)) % N + N) % N;
      u8* o = &px[((size_t)yy * N + xx) * 4]; o[0] = o[1] = o[2] = light ? 220 : 0; o[3] = al;
    }
  }
  g_grainTex = makeTexture2D(N, N, px.data(), true, false, true);
  return g_grainTex;
}
void grain(ImDrawList* dl, ImVec2 a, ImVec2 b, float alpha) {
  if (alpha <= 0 || b.x <= a.x || b.y <= a.y) return;
  unsigned t = grainTex(); if (!t) return;
  float k = 1.f / (256.f * std::max(1.f, std::round(Z())));
  dl->AddImage((ImTextureID)(intptr_t)t, a, b, ImVec2(a.x * k, a.y * k), ImVec2(b.x * k, b.y * k), IM_COL32(255, 255, 255, (int)clampv(alpha * 255.f, 0.f, 255.f)));
}
void cornerBrackets(ImDrawList* dl, ImVec2 a, ImVec2 b, float len, ImU32 c, float th) {
  dl->AddRectFilled(a, ImVec2(a.x + len, a.y + th), c); dl->AddRectFilled(a, ImVec2(a.x + th, a.y + len), c);
  dl->AddRectFilled(ImVec2(b.x - len, a.y), ImVec2(b.x, a.y + th), c); dl->AddRectFilled(ImVec2(b.x - th, a.y), ImVec2(b.x, a.y + len), c);
  dl->AddRectFilled(ImVec2(a.x, b.y - th), ImVec2(a.x + len, b.y), c); dl->AddRectFilled(ImVec2(a.x, b.y - len), ImVec2(a.x + th, b.y), c);
  dl->AddRectFilled(ImVec2(b.x - len, b.y - th), b, c); dl->AddRectFilled(ImVec2(b.x - th, b.y - len), b, c);
}
static float hash1(int i, int k) { u32 h = (u32)i * 374761393u + (u32)k * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; return ((h ^ (h >> 16)) & 0xffff) / 65536.f; }
void zoneBackdrop(ImDrawList* dl, ImVec2 a, ImVec2 b, float t, float al) {
  float z = Z(), W = b.x - a.x, H = b.y - a.y;
  // the air of the Zone: dust motes drifting on the wind, a few catching the light
  for (int i = 0; i < 70; i++) {
    float sx = hash1(i, 1), sy = hash1(i, 2), sp = 0.4f + hash1(i, 3);
    float x = a.x + std::fmod(sx * W + t * (6 + 14 * sp) * z, W);
    float y = a.y + std::fmod(sy * H + std::sin(t * 0.25f + i) * 12 * z + t * (1.5f + 3 * sp) * z, H);
    float flick = 0.6f + 0.4f * std::sin(t * (0.7f + sp) + i * 1.7f);
    float r = (0.6f + 1.4f * hash1(i, 4)) * z;
    dl->AddCircleFilled(ImVec2(x, y), r, IM_COL32(226, 210, 170, (int)((14 + 40 * hash1(i, 5)) * flick * al)), 6);
  }
  // vignette, darker at the foot, and the grain on top
  radialVignette(dl, ImVec2(a.x + W * 0.5f, a.y + H * 0.48f), ImVec2(W * 0.72f, H * 0.78f), 0.3f, IM_COL32(0, 0, 0, (int)(200 * al)));
  dl->AddRectFilledMultiColor(ImVec2(a.x, a.y + H * 0.6f), b, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, (int)(150 * al)), IM_COL32(0, 0, 0, (int)(150 * al)));
  grain(dl, a, b, 0.55f * al);
}
void trefoil(ImDrawList* dl, ImVec2 c, float r, ImU32 cl, float rot) {
  // three 60° blades between 0.2 r and r, a hub of 0.14 r (the IEC sign's proportions, near enough)
  const int S = 14; const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
  for (int bl = 0; bl < 3; bl++) {
    float a0 = rot - PI * 0.5f + bl * TAU / 3 - PI / 6;
    dl->PrimReserve(S * 6, (S + 1) * 2);
    ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
    for (int j = 0; j <= S; j++) {
      float an = a0 + (PI / 3) * j / S, cs = std::cos(an), sn = std::sin(an);
      dl->PrimWriteVtx(ImVec2(c.x + cs * r * 0.22f, c.y + sn * r * 0.22f), uv, cl);
      dl->PrimWriteVtx(ImVec2(c.x + cs * r, c.y + sn * r), uv, cl);
    }
    for (int j = 0; j < S; j++) {
      ImDrawIdx i0 = base + j * 2;
      dl->PrimWriteIdx(i0); dl->PrimWriteIdx(i0 + 1); dl->PrimWriteIdx(i0 + 3);
      dl->PrimWriteIdx(i0); dl->PrimWriteIdx(i0 + 3); dl->PrimWriteIdx(i0 + 2);
    }
  }
  dl->AddCircleFilled(c, r * 0.14f, cl, 24);
}
void sectionLabel(const char* t, float w) {
  float z = Z();
  std::string s = upper(t);
  ImVec2 p = ImGui::GetCursorScreenPos(); ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 ts = textSpacedSize(F_MID, s.c_str(), 2 * z);
  dl->AddRectFilled(ImVec2(p.x, p.y + 2 * z), ImVec2(p.x + 3 * z, p.y + ts.y - 1 * z), col::rust);
  textSpaced(dl, F_MID, ImVec2(p.x + 10 * z, p.y), col::amber, s.c_str(), 2 * z);
  float lx = p.x + 10 * z + ts.x + 10 * z, ly = std::round(p.y + ts.y * 0.5f);
  if (w > lx - p.x) dl->AddRectFilledMultiColor(ImVec2(lx, ly), ImVec2(p.x + w, ly + 1 * z), col::zEdgeHi, withAlpha(col::zEdge, 0), withAlpha(col::zEdge, 0), col::zEdgeHi);
  ImGui::Dummy(ImVec2(w, ts.y + 6 * z));
}
float keycap(ImDrawList* dl, ImVec2 p, const char* key, float h) {
  float z = Z();
  ImVec2 ts = textSize(F_SMALL, key);
  float w = std::max(h, ts.x + 10 * z);
  dl->AddRectFilled(p, p + ImVec2(w, h), IM_COL32(0, 0, 0, 150));
  dl->AddRect(p, p + ImVec2(w, h), col::zEdgeHi, 0, 0, 1 * z);
  text(dl, F_SMALL, ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f), col::amberHi, key);
  return w;
}
float padHints(ImDrawList* dl, ImVec2 pos, const std::vector<Hint>& items, bool right) {
  App& a = app(); float z = Z(), h = 20 * z, gap = 18 * z;
  bool pad = a.input.padCount() > 0;
  struct Part { std::string key, what; float w; };
  std::vector<Part> parts; float total = 0;
  for (auto& it : items) {
    std::string k = pad && it.pad >= 0 ? padButtonName(it.pad, a.settings.padLayout) : it.key ? it.key : "";
    if (k.empty()) continue;
    std::string w = upper(it.what);
    float kw = std::max(h, textSize(F_SMALL, k.c_str()).x + 10 * z);
    float ww = kw + 7 * z + textSpacedSize(F_SMALL, w.c_str(), 1.5f * z).x;
    parts.push_back({k, w, ww}); total += ww + (parts.size() > 1 ? gap : 0);
  }
  float x = right ? pos.x - total : pos.x;
  for (auto& pt : parts) {
    float kw = keycap(dl, ImVec2(x, pos.y), pt.key.c_str(), h);
    textSpaced(dl, F_SMALL, ImVec2(x + kw + 7 * z, pos.y + (h - textSize(F_SMALL, "A").y) * 0.5f), col::zDim, pt.what.c_str(), 1.5f * z);
    x += pt.w + gap;
  }
  return total;
}
int confirmBox(const char* id, const char* title, const char* body, const char* yes, const char* no, bool danger) {
  App& a = app(); float z = Z();
  if (!ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
  float w = std::min(480 * z, a.width - 32 * z);
  ImGui::SetNextWindowPos(ImVec2(a.width * 0.5f, a.height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(w, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0);
  ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(0, 0, 0, 0));
  int r = -1;
  bool open = ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
  ImGui::PopStyleColor(); ImGui::PopStyleVar(2);
  if (!open) return -1;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 p = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
  sz.y = std::max(sz.y, 120 * z);
  dl->AddRectFilledMultiColor(p, p + sz, col::zPanelHi, col::zPanelHi, col::zPanel, col::zPanel);
  grain(dl, p, p + sz, 1);
  float headH = 46 * z;
  dl->AddRectFilled(p, p + ImVec2(sz.x, headH), IM_COL32(0, 0, 0, 60));
  ImU32 acc = danger ? col::rust : col::amber;
  dl->AddRectFilledMultiColor(ImVec2(p.x, p.y + headH - 1 * z), ImVec2(p.x + sz.x, p.y + headH), acc, withAlpha(acc, 0), withAlpha(acc, 0), acc);
  std::string T = upper(title);
  ImVec2 ts = textSpacedSize(F_H3, T.c_str(), 2 * z);
  dl->AddRectFilled(ImVec2(p.x + 16 * z, p.y + headH * 0.5f - 8 * z), ImVec2(p.x + 19 * z, p.y + headH * 0.5f + 8 * z), acc);
  textSpaced(dl, F_H3, ImVec2(p.x + 28 * z, p.y + (headH - ts.y) * 0.5f), col::zText, T.c_str(), 2 * z);
  ImGui::SetCursorPos(ImVec2(20 * z, headH + 16 * z));
  label(F_BODY, col::zText, body, w - 40 * z);
  ImGui::Dummy(ImVec2(1, 10 * z));
  float bw = (w - 40 * z - 10 * z) * 0.5f;
  ImGui::SetCursorPosX(20 * z);
  if (button(yes, danger ? BTN_DANGER : BTN_PRIMARY, false, bw)) r = 1;
  ImGui::SameLine(0, 10 * z);
  if (ImGui::IsWindowAppearing()) { ImGui::SetKeyboardFocusHere(); if (a.input.padCount() > 0) ImGui::SetNavCursorVisible(true); }
  if (button(no, BTN_NORMAL, false, bw)) r = 0;
  ImGui::Dummy(ImVec2(1, 8 * z));
  if (!ImGui::IsWindowAppearing() && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))) r = 0;
  sz = ImGui::GetWindowSize();
  dl->AddRect(p, p + sz, col::zEdge, 0, 0, 1 * z);
  cornerBrackets(dl, p, p + sz, 12 * z, acc, 2 * z);
  if (r >= 0) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
  return r;
}

// ---- panels ---------------------------------------------------------------------------------------------
// The Zone's panel: a worn gunmetal plate (olive-black, grain and scratches), a darker header with an amber marker and
// the title in spaced capitals, an amber hairline under it fading to the right, a 1px edge and amber PDA corners.
bool Panel::begin(const char* id, const char* title, float maxW, const char* closeLabel, float fixedH) {
  App& a = app();
  float z = Z(), pad = 16 * z;
<<<<<<< ours
  closed = false; tabH = 0; tabChanged = false;
  // backdrop (.panel-screen): the game or the title darkened, heavier at the edges
  ImDrawList* bgd = ImGui::GetBackgroundDrawList();
  bgd->AddRectFilled(ImVec2(0, 0), ImVec2((float)a.width, (float)a.height), IM_COL32(4, 5, 3, 150));
  radialVignette(bgd, ImVec2(a.width * 0.5f, a.height * 0.5f), ImVec2(a.width * 0.7f, a.height * 0.75f), 0.35f, IM_COL32(0, 0, 0, 130));
=======
  closed = false;
  // backdrop (.panel-screen)
  ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2((float)a.width, (float)a.height), IM_COL32(16, 16, 16, 160));   // BTA's dim over the world
>>>>>>> theirs
  width = std::min(maxW * z, a.width - pad * 2);
  float maxH = a.height - pad * 2;
  ImGui::SetNextWindowPos(ImVec2(a.width * 0.5f, a.height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  if (fixedH > 0) ImGui::SetNextWindowSize(ImVec2(width, std::min(fixedH * z, maxH)));
  else ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, maxH));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8 * z, 8 * z));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
  bool open = ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | (fixedH > 0 ? 0 : ImGuiWindowFlags_AlwaysAutoResize));
  ImGui::PopStyleColor();
  ImGui::PushFont(font(F_BODY));
  dl = ImGui::GetWindowDrawList();
  pos = ImGui::GetWindowPos(); size = ImGui::GetWindowSize();
  hasFooter = false; footH = 0;
<<<<<<< ours
  ImVec2 b0 = pos, b1 = pos + ImVec2(width, std::max(size.y, 60 * z));
  dropShadow(bgd, b0, b1, 8 * z, 22 * z, 0.7f);
  // body (drawn under everything)
  dl->AddRectFilledMultiColor(b0, b1, col::rgb(0x1a1b14, 250), col::rgb(0x17180f, 250), col::rgb(0x111209, 250), col::rgb(0x121309, 250));
  grain(dl, b0, b1, 1);
  // header
  headH = 32 * z + 24 * z;
  ImVec2 h1 = b0 + ImVec2(width, headH);
  dl->AddRectFilledMultiColor(b0, h1, col::rgb(0x22231a), col::rgb(0x1c1d15), col::rgb(0x15160f), col::rgb(0x141510));
  grain(dl, b0, h1, 0.6f);
  dl->AddRectFilled(ImVec2(b0.x, h1.y - 1 * z), h1, IM_COL32(0, 0, 0, 220));
  dl->AddRectFilledMultiColor(ImVec2(b0.x, h1.y - 2 * z), ImVec2(h1.x, h1.y - 1 * z), withAlpha(col::amber, 0.85f), withAlpha(col::amber, 0.05f), withAlpha(col::amber, 0.05f), withAlpha(col::amber, 0.85f));
  dl->AddRectFilled(ImVec2(b0.x, h1.y - 3 * z), ImVec2(b0.x + 90 * z, h1.y - 1 * z), col::rust);
  std::string T = upper(title);
  float sp = 3 * z;
  ImVec2 ts = textSpacedSize(F_DISP, T.c_str(), sp);
  float maxT = width - 48 * z - (closeLabel ? textSize(F_BODY, closeLabel).x + 60 * z : 0);
  if (ts.x > maxT) { sp = 0; ts = textSpacedSize(F_DISP, T.c_str(), sp); }
  float tk = ts.x > maxT ? std::max(0.6f, maxT / ts.x) : 1.f;
  if (tk < 1) ts = textSpacedSize(F_DISP, T.c_str(), sp, tk);
  float cy = b0.y + headH * 0.5f;
  dl->AddRectFilled(ImVec2(b0.x + 16 * z, cy - 10 * z), ImVec2(b0.x + 20 * z, cy + 10 * z), col::amber);
  textSpaced(dl, F_DISP, ImVec2(b0.x + 30 * z + 2 * z, cy - ts.y * 0.5f + 2 * z), IM_COL32(0, 0, 0, 200), T.c_str(), sp, tk);
  textSpaced(dl, F_DISP, ImVec2(b0.x + 30 * z, cy - ts.y * 0.5f), col::zText, T.c_str(), sp, tk);
=======
  // the launcher's look (its Options tab, BTA's screens): the darker brown dirt for the body, a lighter dirt band for
  // the title with the name centred in white, black lines between, a black frame
  const float U = 2 * z, tile = 32 * U;
  ImVec2 b0 = pos, b1 = pos + ImVec2(width, std::max(size.y, 60 * z));
  classicDirt(dl, b0, b1, tile, 0.62f);
  headH = 32 * z + 24 * z;
  ImVec2 h1 = b0 + ImVec2(width, headH);
  classicDirt(dl, b0, h1, tile, 0.3f);
  dl->AddLine(ImVec2(b0.x, h1.y), ImVec2(h1.x, h1.y), IM_COL32(0, 0, 0, 255), U);
  dl->AddRect(b0, b1, IM_COL32(0, 0, 0, 255), 0, 0, U);
  ImVec2 ts = textSize(F_BIG, title);
  textShadow(dl, F_BIG, ImVec2(std::floor(b0.x + (width - ts.x) * 0.5f), std::floor(b0.y + (headH - ts.y) * 0.5f)), IM_COL32(255, 255, 255, 255), title, ImVec2(2, 2) * z, IM_COL32(63, 63, 63, 255));
>>>>>>> theirs
  if (closeLabel) {
    float bw = textSpacedSize(F_BODY, upper(closeLabel).c_str(), 1.5f * z).x + 40 * z;
    bool pad = a.input.padCount() > 0;
    float capW = pad ? std::max(20 * z, textSize(F_SMALL, padButtonName(1, a.settings.padLayout).c_str()).x + 10 * z) + 6 * z : 0;
    float bx = b0.x + width - 14 * z - bw;
    if (pad) keycap(dl, ImVec2(bx - capW + 6 * z, cy - 10 * z), padButtonName(1, a.settings.padLayout).c_str(), 20 * z);
    ImGui::SetCursorScreenPos(ImVec2(bx, b0.y + (headH - 44 * z) * 0.5f));
    std::string lbl = std::string(closeLabel) + "##close";
    if (button(lbl.c_str(), BTN_GHOST, false, bw)) closed = true;
  }
  ImGui::SetCursorScreenPos(ImVec2(b0.x, b0.y + headH));
  return open;
}
bool Panel::tabs(const std::vector<std::string>& names, int* cur) {
  App& a = app();
  float z = Z(), th = 40 * z;
  int n = (int)names.size(), old = *cur;
  if (n <= 0) return false;
  if (a.input.pressed(Act::MenuTabLeft)) *cur = (*cur + n - 1) % n;
  if (a.input.pressed(Act::MenuTabRight)) *cur = (*cur + 1) % n;
  ImVec2 p = ImGui::GetCursorScreenPos();
  dl->AddRectFilled(p, p + ImVec2(width, th), IM_COL32(0, 0, 0, 80));
  dl->AddRectFilled(ImVec2(p.x, p.y + th - 1 * z), ImVec2(p.x + width, p.y + th), col::zEdge);
  bool pad = a.input.padCount() > 0;
  float x = p.x + 16 * z;
  if (pad) x += keycap(dl, ImVec2(x, p.y + (th - 20 * z) * 0.5f), "LB", 20 * z) + 10 * z;
  ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);   // the pad's shoulder buttons page; the d-pad stays in the body
  for (int i = 0; i < n; i++) {
    std::string L = upper(names[i]);
    ImVec2 ts = textSpacedSize(F_BODY, L.c_str(), 2 * z);
    float w = ts.x + 30 * z;
    ImGui::SetCursorScreenPos(ImVec2(x, p.y));
    ImGui::PushID(i);
    if (ImGui::InvisibleButton("##tab", ImVec2(w, th - 1 * z))) *cur = i;
    ImGui::PopID();
    bool hot = ImGui::IsItemHovered(), sel = i == *cur;
    if (sel) {
      dl->AddRectFilledMultiColor(ImVec2(x, p.y), ImVec2(x + w, p.y + th), withAlpha(col::amber, 0), withAlpha(col::amber, 0), withAlpha(col::amber, 0.2f), withAlpha(col::amber, 0.2f));
      dl->AddRectFilled(ImVec2(x, p.y + th - 3 * z), ImVec2(x + w, p.y + th - 1 * z), col::amber);
    } else if (hot) dl->AddRectFilled(ImVec2(x, p.y + th - 2 * z), ImVec2(x + w, p.y + th - 1 * z), withAlpha(col::amber, 0.5f));
    textSpaced(dl, F_BODY, ImVec2(x + 15 * z, p.y + (th - ts.y) * 0.5f), sel ? col::amberHi : hot ? col::zText : col::zDim, L.c_str(), 2 * z);
    x += w + 2 * z;
  }
  ImGui::PopItemFlag();
  if (pad) keycap(dl, ImVec2(x + 8 * z, p.y + (th - 20 * z) * 0.5f), "RB", 20 * z);
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + th));
  tabH = th;
  tabChanged = *cur != old;
  if (tabChanged) playClick();
  return tabChanged;
}
void Panel::beginBody(float footerH) {
  App& a = app();
  float z = Z();
  footH = footerH * z;
  float maxBody = a.height - 32 * z - headH - tabH - footH;
  if (footerH > 0) maxBody -= 2 * z;
  ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, std::max(80.f, maxBody)));
  if (tabChanged) ImGui::SetNextWindowScroll(ImVec2(0, 0));
  ImGui::BeginChild("##body", ImVec2(width, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_None);
  bodyMin = ImGui::GetWindowPos(); bodyMax = bodyMin + ImGui::GetWindowSize();
  padScrollHere();
  // a new tab: the pad's highlight starts on its first control
  if (tabChanged && a.input.padCount() > 0) { ImGui::SetKeyboardFocusHere(); ImGui::SetNavCursorVisible(true); }
}
void Panel::endBody() { ImGui::EndChild(); }
void Panel::beginFooter() {
  float z = Z();
  hasFooter = true;
  ImVec2 p = ImGui::GetCursorScreenPos();
<<<<<<< ours
  dl->AddRectFilled(ImVec2(pos.x, p.y - 8 * z), ImVec2(pos.x + width, p.y - 7 * z), col::zEdge);
  dl->AddRectFilled(ImVec2(pos.x, p.y - 7 * z), ImVec2(pos.x + width, p.y + 200 * z), IM_COL32(0, 0, 0, 50));
=======
  dl->AddRectFilled(ImVec2(pos.x, p.y - 8 * z), ImVec2(pos.x + width, p.y - 6 * z), IM_COL32(0, 0, 0, 255));   // BTA's black line over the button band
>>>>>>> theirs
  ImGui::SetCursorScreenPos(ImVec2(p.x + 16 * z, p.y + 4 * z));
  ImGui::BeginGroup();
}
void Panel::endFooter() { ImGui::EndGroup(); ImGui::Dummy(ImVec2(1, 8 * Z())); }
void Panel::end() {
  float z = Z();
  ImVec2 sz = ImGui::GetWindowSize();
<<<<<<< ours
  ImDrawList* d = ImGui::GetWindowDrawList();
  d->PushClipRectFullScreen();
  d->AddRect(pos, pos + ImVec2(width, sz.y), col::zEdge, 0, 0, 1 * z);
  cornerBrackets(d, pos - ImVec2(3, 3) * z, pos + ImVec2(width + 3 * z, sz.y + 3 * z), 14 * z, withAlpha(col::amber, 0.85f), 2 * z);
  d->PopClipRect();
=======
  ImGui::GetWindowDrawList()->AddRect(pos, pos + ImVec2(width, sz.y), IM_COL32(0, 0, 0, 255), 0, 0, 2 * z);
>>>>>>> theirs
  ImGui::PopFont();
  ImGui::End();
  ImGui::PopStyleVar(2);
}

// ---- slots ------------------------------------------------------------------------------------------------
void drawSlot(ImDrawList* dl, ImVec2 p, float s, const SlotLook& lk) {
  float z = Z();
  ImVec2 b = p + ImVec2(s, s);
  if (lk.inv) {
    ImU32 bg = lk.bg ? lk.bg : lk.out && lk.ready ? col::rgb(0x2a2d20) : lk.hover ? col::rgb(0x25271f) : col::sl;
    dl->AddRectFilled(p, b, bg);
    float w = 2 * z;
    bevel(dl, p, b, col::slLo, col::slHi, w);
    dl->AddRectFilled(p + ImVec2(w, w), ImVec2(b.x - w, p.y + w + 1 * z), IM_COL32(0, 0, 0, 153));
    dl->AddRectFilled(p + ImVec2(w, w), ImVec2(p.x + w + 1 * z, b.y - w), IM_COL32(0, 0, 0, 153));
    if (lk.out && lk.ready) dl->AddRect(p + ImVec2(w, w), b - ImVec2(w, w), withAlpha(col::gold, 0.45f), 0, 0, 2 * z);
    if (lk.sel) dl->AddRect(p + ImVec2(w, w), b - ImVec2(w, w), col::gold, 0, 0, 2 * z);
    if (lk.focus) { dl->AddRect(p - ImVec2(3, 3) * z, b + ImVec2(3, 3) * z, col::black, 0, 0, 2 * z); dl->AddRect(p - ImVec2(1, 1) * z, b + ImVec2(1, 1) * z, col::gold, 0, 0, 3 * z); }
  } else {
    // HUD slot (.slot): rounded, dark, the selected one framed in cream with an ember inset
    float r = 3 * z;
    dl->AddRectFilled(p, b, col::slot, r);
    dl->AddRectFilled(p + ImVec2(2, 2) * z, ImVec2(b.x - 2 * z, p.y + 4 * z), IM_COL32(0, 0, 0, 89));
    dl->AddRectFilled(p + ImVec2(2, 2) * z, ImVec2(p.x + 4 * z, b.y - 2 * z), IM_COL32(0, 0, 0, 89));
    if (lk.sel) {
      dl->AddRect(p - ImVec2(2, 2) * z, b + ImVec2(2, 2) * z, col::black, r + 2 * z, 0, 2 * z);
      dl->AddRect(p, b, col::rgb(0xe8e4d4), r, 0, 2 * z);
      dl->AddRect(p + ImVec2(2, 2) * z, b - ImVec2(2, 2) * z, col::rgb(0xe2b84c, 128), 0, 0, 2 * z);
    } else dl->AddRect(p, b, col::rgb(0x0a0b09), r, 0, 2 * z);
  }
  if (lk.full) { ImVec2 q(b.x - 9 * z, p.y + 2 * z); dl->AddRectFilled(q - ImVec2(1, 1) * z, q + ImVec2(8, 8) * z, col::black); dl->AddRectFilled(q, q + ImVec2(7, 7) * z, col::amber, 1 * z); }
}

void drawIcon(ImDrawList* dl, const std::string& key, ImVec2 p, float s, float alpha) {
  // the compass, death compass and clock show their live face (game/instruments.js .ico.dyn-*)
  GLuint itex = 0; vec4 iuv;
  if (instrumentIcon(key, itex, iuv)) { dl->AddImage((ImTextureID)(intptr_t)itex, p, p + ImVec2(s, s), ImVec2(iuv.x, iuv.y), ImVec2(iuv.z, iuv.w), IM_COL32(255, 255, 255, (int)(255 * alpha))); return; }
  IconRef r = itemIcons().get(key);
  if (!r.valid()) return;
  dl->AddImage((ImTextureID)(intptr_t)r.tex, p, p + ImVec2(s, s), ImVec2(r.uv0.x, r.uv0.y), ImVec2(r.uv1.x, r.uv1.y), IM_COL32(255, 255, 255, (int)(255 * alpha)));
}

void drawItem(ImDrawList* dl, const ItemStack& st, ImVec2 p, float s, float alpha, bool count) {
  if (st.empty()) return;
  float in = s * 0.12f;
  const std::string pk = partToolModelKey(st);   // a mixed tool or a gem set in one: its own icon (core/tool_parts.h)
  IconRef dyn = pk.empty() ? IconRef() : itemIcons().dynamic(pk);
  if (dyn.valid()) dl->AddImage((ImTextureID)(intptr_t)dyn.tex, p + ImVec2(in, in), p + ImVec2(s - in, s - in), ImVec2(dyn.uv0.x, dyn.uv0.y), ImVec2(dyn.uv1.x, dyn.uv1.y), IM_COL32(255, 255, 255, (int)(255 * alpha)));
  else drawIcon(dl, st.id, p + ImVec2(in, in), s - in * 2, alpha);
  if (itemGlints(st)) drawGlint(dl, st.id, p + ImVec2(in, in), s - in * 2, alpha);
  const ItemDef* d = st.def();
  int dur = 0;
  if (d && (d->hasTool || d->has("armor"))) dur = toolMaxDurability(st);
  if (dur && st.d) {
    float f = std::max(0.f, 1 - (float)st.d / dur);
    ImVec2 a(p.x + in, p.y + s - 4 * Z() - 3 * Z()), b(p.x + s - in, p.y + s - 4 * Z());
    dl->AddRectFilled(a, b, col::rgb(0x111111));
    float hue = f * 110.f / 360.f; float r, g, bb; ImGui::ColorConvertHSVtoRGB(hue, 0.8f, 0.9f, r, g, bb);
    dl->AddRectFilled(a, ImVec2(a.x + (b.x - a.x) * f, b.y), ImGui::GetColorU32(ImVec4(r, g, bb, alpha)));
  }
  if (count && st.n > 1) {
    std::string n = std::to_string(st.n);
    float k = s * 0.42f / fontPx(F_H3) / Z();
    ImVec2 ts = textSize(F_H3, n.c_str(), 0, k);
    ImVec2 q(p.x + s - 3 * Z() - ts.x, p.y + s - 1 * Z() - ts.y * 0.92f);
    text(dl, F_H3, q + ImVec2(2, 2) * Z(), withAlpha(col::rgb(0x1a1a1a), alpha), n.c_str(), 0, k);
    text(dl, F_H3, q, withAlpha(col::rgb(0xfff6e0), alpha), n.c_str(), 0, k);
  }
}

void drawSprite(ImDrawList* dl, int sp, ImVec2 p, float s, float alpha) {
  if (!g_spriteTex) return;
  float v0 = (float)sp / (float)SP_COUNT, v1 = (float)(sp + 1) / (float)SP_COUNT;
  dl->AddImage((ImTextureID)(intptr_t)g_spriteTex, p, p + ImVec2(s, s), ImVec2(0, v0), ImVec2(1, v1), IM_COL32(255, 255, 255, (int)(255 * alpha)));
}

} // namespace ui
} // namespace dq
