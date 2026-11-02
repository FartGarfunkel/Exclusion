// launcher/ui.cpp — the launcher's look (Exclusion): stained concrete and rusted steel drawn from the same
// seeded noise, dark gunmetal button plates with a 1px bevel (amber text on hover), dark olive/grey panels, Pixelify Sans.
#include "launcher/launcher.h"
#include "assets/tileset.h"
#include <glad/gl.h>
#include <imgui_internal.h>
#include <cstdarg>

namespace dq {

const char* tabId(LTab t) {
  switch (t) { case LTab::News: return "news"; case LTab::Server: return "server"; case LTab::Studio: return "studio"; case LTab::Options: return "options"; default: return ""; }
}

static ImVec4 rgba(int r, int g, int b, float a = 1) { return ImVec4(r / 255.f, g / 255.f, b / 255.f, a); }

void applyLauncherStyle() {
  ImGuiStyle& s = ImGui::GetStyle();
  s.WindowRounding = 4; s.ChildRounding = 4; s.FrameRounding = 0; s.PopupRounding = 2; s.GrabRounding = 0; s.ScrollbarRounding = 2; s.TabRounding = 0;
  s.WindowBorderSize = 2; s.ChildBorderSize = 2; s.FrameBorderSize = 2; s.PopupBorderSize = 2;
  s.WindowPadding = ImVec2(14, 14); s.FramePadding = ImVec2(8, 5); s.ItemSpacing = ImVec2(10, 8); s.ItemInnerSpacing = ImVec2(8, 6);
  s.ScrollbarSize = 12; s.GrabMinSize = 12;
  ImVec4* c = s.Colors;
  c[ImGuiCol_Text] = rgba(225, 222, 212);
  c[ImGuiCol_TextDisabled] = rgba(112, 110, 100);   // input hints
  c[ImGuiCol_WindowBg] = rgba(14, 15, 13, 0.97f);
  c[ImGuiCol_ChildBg] = rgba(10, 11, 9, 0.80f);
  c[ImGuiCol_PopupBg] = rgba(14, 15, 13, 0.98f);
  c[ImGuiCol_Border] = rgba(0, 0, 0);
  c[ImGuiCol_BorderShadow] = rgba(0, 0, 0, 0);
  c[ImGuiCol_FrameBg] = rgba(0, 0, 0);
  c[ImGuiCol_FrameBgHovered] = rgba(18, 19, 16);
  c[ImGuiCol_FrameBgActive] = rgba(26, 27, 23);
  c[ImGuiCol_TitleBg] = rgba(38, 40, 35); c[ImGuiCol_TitleBgActive] = rgba(38, 40, 35); c[ImGuiCol_TitleBgCollapsed] = rgba(38, 40, 35);
  c[ImGuiCol_ScrollbarBg] = rgba(0, 0, 0, 0.25f);
  c[ImGuiCol_ScrollbarGrab] = rgba(92, 94, 86); c[ImGuiCol_ScrollbarGrabHovered] = rgba(226, 184, 76); c[ImGuiCol_ScrollbarGrabActive] = rgba(240, 200, 96);
  c[ImGuiCol_CheckMark] = rgba(226, 184, 76);
  c[ImGuiCol_SliderGrab] = rgba(200, 162, 66); c[ImGuiCol_SliderGrabActive] = rgba(240, 200, 96);
  c[ImGuiCol_Button] = rgba(62, 65, 66); c[ImGuiCol_ButtonHovered] = rgba(78, 82, 82); c[ImGuiCol_ButtonActive] = rgba(48, 51, 52);
  c[ImGuiCol_Header] = rgba(46, 49, 42); c[ImGuiCol_HeaderHovered] = rgba(62, 68, 52); c[ImGuiCol_HeaderActive] = rgba(89, 104, 58);
  c[ImGuiCol_Separator] = rgba(255, 255, 255, 0.10f); c[ImGuiCol_SeparatorHovered] = rgba(226, 184, 76, 0.5f); c[ImGuiCol_SeparatorActive] = rgba(226, 184, 76, 0.8f);
  c[ImGuiCol_ResizeGrip] = rgba(0, 0, 0, 0); c[ImGuiCol_ResizeGripHovered] = rgba(226, 184, 76, 0.5f); c[ImGuiCol_ResizeGripActive] = rgba(226, 184, 76);
  c[ImGuiCol_Tab] = rgba(30, 32, 28); c[ImGuiCol_TabHovered] = rgba(56, 60, 48); c[ImGuiCol_TabSelected] = rgba(66, 72, 52);
  c[ImGuiCol_TableHeaderBg] = rgba(38, 40, 35); c[ImGuiCol_TableBorderStrong] = rgba(70, 72, 64); c[ImGuiCol_TableBorderLight] = rgba(70, 72, 64, 0.5f);
  c[ImGuiCol_TableRowBg] = rgba(0, 0, 0, 0); c[ImGuiCol_TableRowBgAlt] = rgba(255, 255, 255, 0.03f);
  c[ImGuiCol_TextSelectedBg] = rgba(226, 184, 76, 0.35f);
  c[ImGuiCol_NavCursor] = rgba(226, 184, 76);
  c[ImGuiCol_ModalWindowDimBg] = rgba(0, 0, 0, 0.6f);
}

// ---- textures: launcher.html draws its dirt and button pictures with a seeded generator (mulberry32)
namespace {
struct Mulberry { u32 a = 1234567; double next() { a += 0x6D2B79F5u; u32 t = (a ^ (a >> 15)) * (1u | a); t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t; return (double)(t ^ (t >> 14)) / 4294967296.0; } };
unsigned makeTex(int w, int h, const std::vector<u8>& px, bool repeat) {
  GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  return t;
}
}

void makeLauncherTextures(Launcher& L) {
  Mulberry rnd;
  // stained concrete slab, 32x32 texels shown at 64px (2px per texel): grey blotches, a dark seam along the top/left edge,
  // rust streaks running down from a few seeds
  static const u32 conc[6] = {0x3a3c38, 0x42443f, 0x4a4b46, 0x52524b, 0x5a5a52, 0x46473f};
  static const u32 rust[2] = {0x6b4a2e, 0x7a5032};
  const int N = 32;
  double grid[8][8]; for (auto& row : grid) for (double& g : row) g = rnd.next();
  auto blotch = [&](int x, int y) {   // wrapping bilinear value noise over an 8x8 grid
    double fx = x * 8.0 / N, fy = y * 8.0 / N; int x0 = (int)fx, y0 = (int)fy; double tx = fx - x0, ty = fy - y0;
    double a = grid[y0 & 7][x0 & 7], b = grid[y0 & 7][(x0 + 1) & 7], c = grid[(y0 + 1) & 7][x0 & 7], d = grid[(y0 + 1) & 7][(x0 + 1) & 7];
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
  };
  std::vector<u8> px(N * N * 4);
  for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
    double r = blotch(i, j) * 0.65 + rnd.next() * 0.35; u32 c = conc[clampv((int)(r * 6), 0, 5)];
    u8* p = &px[(j * N + i) * 4]; p[0] = c >> 16; p[1] = (c >> 8) & 255; p[2] = c & 255; p[3] = 255;
  }
  for (int i = 0; i < N; i++) if (rnd.next() < 0.22) {   // a rust streak: from y0 down for len texels, thinning out
    int y0 = (int)(rnd.next() * N), len = 6 + (int)(rnd.next() * 18); u32 c = rust[rnd.next() < 0.5 ? 0 : 1];
    for (int k = 0; k < len; k++) if (rnd.next() < 0.9 - 0.7 * k / len) {
      u8* p = &px[(((y0 + k) % N) * N + i) * 4]; p[0] = c >> 16; p[1] = (c >> 8) & 255; p[2] = c & 255;
    }
  }
  for (int k = 0; k < N; k++) {   // seams between slabs
    for (int q : {0, 1}) { u8* h = &px[(q * N + k) * 4]; u8* v = &px[(k * N + q) * 4]; for (u8* p : {h, v}) { p[0] = q ? 0x30 : 0x26; p[1] = q ? 0x32 : 0x28; p[2] = q ? 0x2e : 0x24; } }
  }
  L.texDirt = makeTex(N, N, px, true);   // (the member keeps its old name; it is the launcher's background tile)
  auto grey = [&](int r, int g, int b) {   // a gunmetal plate: faint horizontal brushing, no big noise
    std::vector<u8> q(100 * 10 * 4);
    for (int j = 0; j < 10; j++) { double row = (rnd.next() - 0.5) * 6;
      for (int i = 0; i < 100; i++) {
        double n = row + (rnd.next() - 0.5) * 9; u8* p = &q[(j * 100 + i) * 4];
        p[0] = (u8)clampv((int)(r + n), 0, 255); p[1] = (u8)clampv((int)(g + n), 0, 255); p[2] = (u8)clampv((int)(b + n), 0, 255); p[3] = 255;
      } }
    return makeTex(100, 10, q, false);
  };
  L.texBtn = grey(54, 58, 59); L.texBtnHi = grey(72, 77, 78); L.texGo = grey(89, 104, 58); L.texGoHi = grey(108, 126, 70);
  if (auto icon = readBinary(assetsDir() / "icon.png")) {
    int w, h; std::vector<u8> rgba;
    if (loadPngMem(icon->data(), icon->size(), w, h, rgba)) L.texIcon = makeTex(w, h, rgba, false);
  }
}

void drawDirt(ImDrawList* dl, ImVec2 a, ImVec2 b, const Launcher& L, float dark) {
  const float tile = 64;   // one concrete slab = 64px
  dl->AddImage((ImTextureID)(intptr_t)L.texDirt, a, b, ImVec2(a.x / tile, a.y / tile), ImVec2(b.x / tile, b.y / tile));
  if (dark > 0) dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, (int)(dark * 255)));
}

void shadowText(ImDrawList* dl, ImFont* f, ImVec2 pos, ImU32 col, ImU32 shadow, float off, const char* text, const char* end) {
  float sz = f->FontSize;
  if (off > 0) dl->AddText(f, sz, ImVec2(pos.x + off, pos.y + off), shadow, text, end);
  dl->AddText(f, sz, pos, col, text, end);
}

bool mcButton(Launcher& L, const char* label, ImVec2 size, BtnKind kind, bool enabled, ImFont* font) {
  ImFont* f = font ? font : L.fMid;
  const char* end = ImGui::FindRenderedTextEnd(label);
  ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, label, end);
  if (size.x <= 0) size.x = ts.x + 36;
  if (size.y <= 0) size.y = 40;
  if (!enabled) ImGui::BeginDisabled();
  bool pressed = ImGui::InvisibleButton(label, size);
  bool hot = enabled && (ImGui::IsItemHovered() || ImGui::IsItemFocused());
  bool held = enabled && ImGui::IsItemActive();
  if (!enabled) ImGui::EndDisabled();
  ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  if (held) { a.y += 1; b.y += 1; }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  unsigned tex = kind == BtnKind::Go ? (hot ? L.texGoHi : L.texGo) : (hot ? L.texBtnHi : L.texBtn);
  dl->AddImage((ImTextureID)(intptr_t)tex, a, b);
  // 1px lighter bevel top/left, 1px darker bottom/right, plus two rivets on wide plates
  dl->AddRectFilled(ImVec2(a.x + 2, a.y + 2), ImVec2(b.x - 2, a.y + 3), IM_COL32(255, 255, 255, 52));
  dl->AddRectFilled(ImVec2(a.x + 2, a.y + 3), ImVec2(a.x + 3, b.y - 2), IM_COL32(255, 255, 255, 52));
  dl->AddRectFilled(ImVec2(a.x + 2, b.y - 3), ImVec2(b.x - 2, b.y - 2), IM_COL32(0, 0, 0, 110));
  dl->AddRectFilled(ImVec2(b.x - 3, a.y + 3), ImVec2(b.x - 2, b.y - 3), IM_COL32(0, 0, 0, 110));
  if (size.x - ts.x > 40 && size.y >= 28) for (float rx : {a.x + 9, b.x - 9}) dl->AddCircleFilled(ImVec2(rx, (a.y + b.y) * 0.5f), 1.6f, IM_COL32(0, 0, 0, 120));
  if (!enabled) dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 110));
  dl->AddRect(a, b, hot ? IM_COL32(226, 184, 76, 255) : IM_COL32(0, 0, 0, 255), 0, 0, 2);
  ImVec2 tp(std::floor((a.x + b.x - ts.x) * 0.5f), std::floor((a.y + b.y - ts.y) * 0.5f) - 1);
  ImU32 col = !enabled ? IM_COL32(120, 118, 108, 255) : hot ? IM_COL32(226, 184, 76, 255) : IM_COL32(206, 204, 194, 255);
  shadowText(dl, f, tp, col, IM_COL32(8, 8, 6, 255), 2, label, end);
  return pressed && enabled;
}

void beginDokuPanel(const char* id, ImVec2 size, bool scroll, ImGuiWindowFlags extra) {
  ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, (scroll ? 0 : ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse) | extra);
  ImVec2 a = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->PushClipRectFullScreen();
  dl->AddRect(ImVec2(a.x + 2, a.y + 2), ImVec2(a.x + sz.x - 2, a.y + sz.y - 2), IM_COL32(255, 255, 255, 18), 3, 0, 2);   // inset 0 0 0 2px rgba(255,255,255,.07)
  dl->PopClipRect();
}
void endDokuPanel() { ImGui::EndChild(); }

void sideHeading(Launcher& L, const char* text) {
  std::string up = text; for (auto& ch : up) ch = (char)std::toupper((unsigned char)ch);
  ImGui::PushFont(L.fSmall);
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(C_DIM));
  ImGui::TextUnformatted(up.c_str());
  ImGui::PopStyleColor(); ImGui::PopFont();
}

void dimText(const char* fmt, ...) {
  char buf[2048]; va_list ap; va_start(ap, fmt); std::vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(C_DIM));
  ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(buf); ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
}

bool tabButton(Launcher& L, const char* label, bool active, ImVec2 size) {
  ImFont* f = L.fUi;
  const char* end = ImGui::FindRenderedTextEnd(label);
  ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, label, end);
  if (size.x <= 0) size.x = ts.x + 26;
  if (size.y <= 0) size.y = 34;
  bool pressed = ImGui::InvisibleButton(label, size);
  bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
  ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilledMultiColor(a, b, IM_COL32(58, 48, 39, 240), IM_COL32(58, 48, 39, 240), IM_COL32(34, 28, 23, 240), IM_COL32(34, 28, 23, 240));
  dl->AddRect(a, b, active ? C_EMBER : hot ? IM_COL32(124, 102, 80, 255) : IM_COL32(77, 63, 51, 255), 4, 0, 2);
  dl->AddText(f, f->FontSize, ImVec2(std::floor((a.x + b.x - ts.x) * 0.5f), std::floor((a.y + b.y - ts.y) * 0.5f)), active ? IM_COL32(226, 184, 79, 255) : IM_COL32(239, 228, 204, 255), label, end);
  return pressed;
}

void openUrl(const std::string& url) {
  if (SDL_OpenURL(url.c_str()) != 0) DQ_WARN("could not open %s: %s", url.c_str(), SDL_GetError());
}
void openPath(const fs::path& p) {
  std::error_code ec; fs::create_directories(p, ec);
  std::string g = fs::absolute(p, ec).generic_string();
#ifdef _WIN32
  openUrl("file:///" + g);
#else
  openUrl("file://" + g);
#endif
}

} // namespace dq
