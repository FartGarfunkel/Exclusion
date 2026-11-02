// ui/zone_screens.cpp — Exclusion's face: the Misery-style HUD (health and stamina bars, the status marks for
// radiation, bleeding, hunger, thirst, sleep and psy, the weight you carry, the gun's rounds and condition, the
// region you're in, a compass with your task's marker, the detector, the emission's warning and its red sky, the
// scope's view), and the screens: the PDA (the map of the Zone, your task, the news, squads near you), the
// weapon bench (fit and strip attachments, change rounds), the trader and the story's ending. Everything goes
// through the session's selected local player, so it works in split screen; every screen is driven by the menu
// keys and the controller as well as the mouse.
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "sim/zone.h"
#include "sim/creature.h"
#include "sim/physics.h"
#include "sim/combat.h"
#include "sim/gameplay.h"
#include "gen/zone_plan.h"
#include "core/registry.h"
#include <imgui.h>
#include "render/gl.h"
#include <thread>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <map>

namespace dq {

bool inventoryClosePressed(App& a, bool tabScreen);   // ui/ui_main.cpp
extern float zonePdaRect[4]; extern bool zonePdaRectOn;   // render/zone_fpgun.cpp: the held PDA's screen
extern float zoneDetRect[4]; extern bool zoneDetRectOn; extern std::string zoneDetId;   // ...and the held detector's

using namespace ui;   // text, textSize, Z, fonts, buttons (ui/theme.h)
namespace {
using namespace zone;
ImU32 rgba(int r, int g, int b, float a) { return IM_COL32(r, g, b, (int)std::clamp(a * 255.f, 0.f, 255.f)); }
const ImU32 AMBER = IM_COL32(226, 184, 76, 255), INK = IM_COL32(220, 214, 196, 255), DIM = IM_COL32(150, 146, 128, 255), PLATE = IM_COL32(16, 18, 14, 190);
std::string fmtKg(float v) { char b[32]; std::snprintf(b, sizeof b, "%.1f", v); return b; }
std::string fmtRu(double v) { char b[32]; std::snprintf(b, sizeof b, "%.0f RU", v); return b; }

void bar(ImDrawList* dl, ImVec2 a, float w, float h, float f, ImU32 c, ImU32 back) {
  dl->AddRectFilled(a, a + ImVec2(w, h), back);
  if (f > 0) dl->AddRectFilled(a + ImVec2(1, 1), a + ImVec2(1 + (w - 2) * std::clamp(f, 0.f, 1.f), h - 1), c);
  dl->AddRect(a, a + ImVec2(w, h), IM_COL32(0, 0, 0, 200));
}
// a status mark: a small plate with a word, coloured by how bad it is (0 fine .. 3 bad)
void mark(ImDrawList* dl, ImVec2& at, const char* word, int level, float z) {
  if (level <= 0) return;
  static const ImU32 C[4] = {0, IM_COL32(220, 200, 70, 230), IM_COL32(230, 130, 40, 240), IM_COL32(220, 50, 40, 255)};
  ImVec2 ts = textSize(F_SMALL, word);
  dl->AddRectFilled(at, at + ImVec2(ts.x + 8 * z, ts.y + 4 * z), PLATE, 2 * z);
  dl->AddRect(at, at + ImVec2(ts.x + 8 * z, ts.y + 4 * z), C[std::min(3, level)], 2 * z, 0, 1.5f * z);
  text(dl, F_SMALL, at + ImVec2(4 * z, 2 * z), C[std::min(3, level)], word);
  at.x += ts.x + 12 * z;
}
void vignette(ImDrawList* dl, float W, float H, ImU32 edge, float k) {
  if (k <= 0.01f) return;
  const ImU32 clear = edge & 0x00FFFFFF;
  const float t = std::min(W, H) * 0.22f * k;
  dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, t), edge, edge, clear, clear);
  dl->AddRectFilledMultiColor(ImVec2(0, H - t), ImVec2(W, H), clear, clear, edge, edge);
  dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(t, H), edge, clear, clear, edge);
  dl->AddRectFilledMultiColor(ImVec2(W - t, 0), ImVec2(W, H), clear, edge, edge, clear);
}

// ---------------------------------------------------------------- screens
// the Zone's map picture, made once per seed on a worker thread (a pixel every 24 m: hill shading, water, marsh, the Red
// Forest, roads), uploaded on the GL thread when done; 0 until then
unsigned zoneMapTexture(u32 seed) {
  struct Pic { u32 seed = 0; std::atomic<int> state{0}; int w = 0, h = 0; std::vector<u8> px; unsigned tex = 0; };
  static Pic* P = new Pic();
  if (P->state.load() != 0 && P->seed != seed) { if (P->state.load() == 1) return 0; if (P->tex) glDeleteTextures(1, &P->tex); P->tex = 0; P->state = 0; }
  if (P->state.load() == 0) {
    P->seed = seed; P->state = 1;
    std::thread([seed] {
      constexpr int S = 24;
      const int w = (MAX_X - MIN_X) / S, h = (MAX_Z - MIN_Z) / S;
      std::vector<float> H((size_t)w * h); std::vector<u8> kind((size_t)w * h);
      for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
        const Ground g = groundAt(seed, MIN_X + i * S, MIN_Z + j * S);
        H[(size_t)j * w + i] = g.h;
        u8 k = 0;
        if (g.water) k = 1; else if (g.road && g.roadD < 4) k = 2;
        else if (g.region && std::string(g.region->id) == "redforest") k = 3; else if (g.region && std::string(g.region->id) == "marsh") k = 4;
        kind[(size_t)j * w + i] = k;
      }
      std::vector<u8> px((size_t)w * h * 4);
      for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
        const size_t q = (size_t)j * w + i;
        const float hx = H[(size_t)j * w + std::min(w - 1, i + 1)] - H[(size_t)j * w + std::max(0, i - 1)], hz = H[(size_t)std::min(h - 1, j + 1) * w + i] - H[(size_t)std::max(0, j - 1) * w + i];
        const float shade = std::clamp(1.f - (hx + hz) * 0.035f, 0.55f, 1.35f), alt = std::clamp((H[q] - 64) / 60.f, 0.f, 1.f);
        vec3 c = glm::mix(vec3(86, 92, 60), vec3(120, 116, 86), alt);
        if (kind[q] == 1) c = vec3(40, 58, 64); else if (kind[q] == 3) c = vec3(110, 72, 50); else if (kind[q] == 4) c = vec3(66, 80, 56);
        if (kind[q] != 1) c *= shade;
        const float n = (float)((std::hash<long long>()((long long)i * 92821 + j * 31) % 1000) / 1000.0 - 0.5) * 8;
        for (int k = 0; k < 3; k++) px[q * 4 + k] = (u8)std::clamp(c[k] + n, 0.f, 255.f);
        px[q * 4 + 3] = 255;
      }
      P->w = w; P->h = h; P->px = std::move(px); P->state = 2;
    }).detach();
  }
  if (P->state.load() == 2) {
    glGenTextures(1, &P->tex); glBindTexture(GL_TEXTURE_2D, P->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, P->w, P->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, P->px.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    P->px.clear(); P->px.shrink_to_fit(); P->state = 3;
  }
  return P->state.load() == 3 ? P->tex : 0;
}

class PdaScreen : public Screen {
public:
  const char* id() const override { return "pda"; }
  bool pausesGame() const override { return false; }
  void update(App& a, double) override {
    if (inventoryClosePressed(a, true) || (a.input.pressed(Act::Pda) && frames > 2)) { a.popScreen(); return; }
    if (a.input.pressed(Act::MenuTabLeft)) tab = (tab + 4) % 5;
    if (a.input.pressed(Act::MenuTabRight)) tab = (tab + 1) % 5;
    frames++;
  }
  void draw(App& a) override {
    GameSession* s = a.session && a.session->player ? a.session.get() : nullptr;
    if (!s) return;
    Player& p = *s->player; Level& lv = *s->level;
    // on the held device's screen (render/zone_pda.cpp) once it's up; until then, in the middle
    float W = std::min(1100.f, a.width - 30.f), H = std::min(720.f, a.height - 30.f);
    ImVec2 at(a.width * 0.5f, a.height * 0.5f), piv(0.5f, 0.5f);
    if (zonePdaRectOn) { W = zonePdaRect[2] - zonePdaRect[0]; H = zonePdaRect[3] - zonePdaRect[1]; at = ImVec2(zonePdaRect[0], zonePdaRect[1]); piv = ImVec2(0, 0); }
    else if (frames < 12) return;   // the device is still coming up
    ImGui::SetNextWindowPos(at, ImGuiCond_Always, piv);
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(14, 22, 16, 245));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(60, 80, 50, 255));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(10, 16, 12, 255));
    ImGui::Begin("##pda", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor(3);
    ImGui::TextColored(ImVec4(0.89f, 0.72f, 0.3f, 1), "PDA");
    ImGui::SameLine(); ImGui::TextDisabled("  %s  \xC2\xB7  %s  \xC2\xB7  %s", regionName(ifloor(p.pos.x), ifloor(p.pos.z)).c_str(), factionName(faction(p)), fmtRu(money(p)).c_str());
    const char* tabs[] = {"Map", "Tasks", "News", "Contacts", "Radio"};
    for (int i = 0; i < 5; i++) { if (i) ImGui::SameLine(); if (button(tabs[i], tab == i ? BTN_PRIMARY : BTN_NORMAL, true)) tab = i; }
    ImGui::Separator();
    if (tab == 0) drawMap(*s, p, lv);
    else if (tab == 1) {
      for (const Task& t : activeTasks(lv, p)) {
        ImGui::TextColored(ImVec4(0.89f, 0.72f, 0.3f, 1), "%s", t.title.c_str());
        ImGui::PushTextWrapPos(); ImGui::TextUnformatted(t.text.c_str()); ImGui::PopTextWrapPos();
        if (t.hasTarget) ImGui::TextDisabled("Marked on your map and compass: %s, %.0f m away", t.where.c_str(), std::hypot(t.target.x - p.pos.x, t.target.z - p.pos.z));
        ImGui::Separator();
      }
      ImGui::TextDisabled("Health %.0f%%  \xC2\xB7  Radiation %.0f%%  \xC2\xB7  Food %.0f  \xC2\xB7  Water %.0f  \xC2\xB7  Sleep %.0f  \xC2\xB7  Psy %.0f",
                          p.hp / std::max(1.f, p.maxHp) * 100, zget(p, "rad"), zget(p, "food", 100), zget(p, "water", 100), zget(p, "sleep", 100), zget(p, "psy", 100));
      ImGui::TextDisabled("Carrying %s / %s kg", fmtKg(carryWeight(p)).c_str(), fmtKg(carryCap(p)).c_str());
      const Emission em = emission(lv);
      ImGui::TextDisabled(em.phase == 0 ? "Next emission in about %.0f minutes" : "EMISSION: stay under cover (%.0f s)", em.phase == 0 ? em.left / 60 : em.left);
    } else if (tab == 3) {
      // who's about: squads within a kilometre (your own people anywhere), and where everyone stands with you
      ImGui::BeginChild("##contacts", ImVec2(0, 0), ImGuiChildFlags_Borders);
      padScrollHere();
      ImGui::TextColored(ImVec4(0.89f, 0.72f, 0.3f, 1), "Stalkers on the network");
      int n = 0;
      for (const SquadView& q : alifeSquads(lv)) {
        if (q.faction == F_MUTANT) continue;
        const float d = std::hypot(q.pos.x - p.pos.x, q.pos.z - p.pos.z);
        if (d > 1000 && q.faction != faction(p)) continue;
        const int rel = factionRelation(q.faction, faction(p));
        ImGui::TextColored(rel < 0 ? ImVec4(0.9f, 0.45f, 0.2f, 1) : rel > 0 ? ImVec4(0.5f, 0.85f, 0.5f, 1) : ImVec4(0.8f, 0.8f, 0.7f, 1), "%-12s x%d", factionName(q.faction), q.members);
        ImGui::SameLine(170); ImGui::TextDisabled("%s, %.0f m%s", q.goal.empty() ? "on the move" : q.goal.c_str(), d, q.online ? "" : "");
        if (++n >= 40) break;
      }
      if (!n) ImGui::TextDisabled("Nobody within range.");
      ImGui::Separator();
      ImGui::TextColored(ImVec4(0.89f, 0.72f, 0.3f, 1), "Standings");
      for (int f = 0; f < F_MUTANT; f++) { const int r = factionRelation(f, faction(p)); ImGui::TextDisabled("%-12s %s", factionName(f), r < 0 ? "hostile" : r > 0 ? "friendly" : "neutral"); }
      ImGui::EndChild();
    } else if (tab == 4) {
      // the stalker radio: music from the bar's band, a campfire guitar, or the emergency band (the network's news)
      int st = p.stats.contains("zone") ? p.stats["zone"].value("radio", -1) : -1;
      const char* names[] = {"Zone FM â the Furnace's band", "Campfire â an old six-string", "Emergency band â news and warnings"};
      for (int i = 0; i < 3; i++) if (button((std::string(st == i ? "â¶ " : "   ") + names[i] + "##st" + std::to_string(i)).c_str(), st == i ? BTN_PRIMARY : BTN_NORMAL, false, -1)) p.stats["zone"]["radio"] = st == i ? -1 : i;
      if (button("Radio off", BTN_GHOST, true)) p.stats["zone"]["radio"] = -1;
      ImGui::Separator();
      if (st == 2) {
        const Emission em = emission(lv);
        ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.3f, 1), em.phase == 0 ? "Next emission in about %.0f min." : "EMISSION â %.0f s. Get underground.", em.phase == 0 ? em.left / 60 : em.left);
        auto news = pdaNews(lv);
        for (auto it = news.rbegin(); it != news.rend() && it - news.rbegin() < 8; ++it) { ImGui::PushTextWrapPos(); ImGui::TextUnformatted(it->c_str()); ImGui::PopTextWrapPos(); }
      } else ImGui::TextDisabled(st < 0 ? "The radio is off. In the Furnace bar and the camps' hubs a radio plays anyway." : "Playing. Close the PDA and it keeps playing.");
    } else {
      auto news = pdaNews(lv);
      ImGui::BeginChild("##news", ImVec2(0, 0), ImGuiChildFlags_Borders);
      padScrollHere();
      if (news.empty()) ImGui::TextDisabled("Nothing on the network yet.");
      for (auto it = news.rbegin(); it != news.rend(); ++it) { ImGui::PushTextWrapPos(); ImGui::TextUnformatted(it->c_str()); ImGui::PopTextWrapPos(); }
      ImGui::EndChild();
    }
    ImGui::End();
  }
private:
  int tab = 0, frames = 0;
  // ---- the map: the Zone's relief (a picture made once per seed from the terrain: hill shading, water, roads; ui below),
  // buildings, roads and rivers, squads you'd know of, your task, you. Centred on you; zoom with the wheel / RT and LT,
  // pan by dragging / the right stick, RS click (or double-click) re-centres.
  float mzoom = 0.32f; vec2 mpan{0}; bool mfollow = true;
  void drawMap(GameSession& s, Player& p, Level& lv) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 o = ImGui::GetCursorScreenPos(), sz(avail.x, std::max(60.f, avail.y - 2));
    ImGui::InvisibleButton("##map", sz);
    const bool hov = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();
    // controls
    float zin = 0;
    if (hov && io.MouseWheel != 0) zin += io.MouseWheel * 0.15f;
    if (ImGui::IsKeyDown(ImGuiKey_GamepadR2)) zin += io.DeltaTime * 1.6f;
    if (ImGui::IsKeyDown(ImGuiKey_GamepadL2)) zin -= io.DeltaTime * 1.6f;
    if (ImGui::IsKeyDown(ImGuiKey_Equal) || ImGui::IsKeyDown(ImGuiKey_KeypadAdd)) zin += io.DeltaTime * 1.6f;
    if (ImGui::IsKeyDown(ImGuiKey_Minus) || ImGui::IsKeyDown(ImGuiKey_KeypadSubtract)) zin -= io.DeltaTime * 1.6f;
    mzoom = std::clamp(mzoom * std::exp(zin), 0.012f, 2.5f);
    vec2 pan(0);
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2)) { pan -= vec2(io.MouseDelta.x, io.MouseDelta.y) / mzoom; mfollow = false; }
    for (auto& pd : s.app.input.pads()) if (glm::length(pd.rstick) > 0.2f) { pan += pd.rstick * (io.DeltaTime * 520.f / mzoom); mfollow = false; }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadR3) || (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))) mfollow = true;
    if (mfollow) mpan = vec2(p.pos.x, p.pos.z); else mpan += pan;
    mpan = glm::clamp(mpan, vec2(MIN_X, MIN_Z), vec2(MAX_X, MAX_Z));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(o, o + sz, true);
    const ImVec2 mid = o + sz * 0.5f;
    auto M = [&](float x, float z) { return ImVec2(mid.x + (x - mpan.x) * mzoom, mid.y + (z - mpan.y) * mzoom); };
    dl->AddRectFilled(o, o + sz, IM_COL32(26, 30, 22, 255));
    if (const unsigned t = zoneMapTexture((u32)lv.info.seed)) dl->AddImage((ImTextureID)(intptr_t)t, M((float)MIN_X, (float)MIN_Z), M((float)MAX_X, (float)MAX_Z));
    else dl->AddText(o + ImVec2(10, 10), DIM, "Charting the Zone\xE2\x80\xA6");
    const float lw = std::clamp(mzoom * 6, 1.f, 3.f);
    for (const Line& l : rivers()) for (size_t i = 0; i + 1 < l.pts.size(); i++) dl->AddLine(M(l.pts[i].x, l.pts[i].z), M(l.pts[i + 1].x, l.pts[i + 1].z), IM_COL32(52, 78, 92, 230), std::max(1.5f, l.width * mzoom));
    for (const Line& l : roads()) for (size_t i = 0; i + 1 < l.pts.size(); i++) dl->AddLine(M(l.pts[i].x, l.pts[i].z), M(l.pts[i + 1].x, l.pts[i + 1].z), l.kind == 3 ? IM_COL32(92, 80, 72, 230) : l.kind == 1 ? IM_COL32(168, 160, 140, 235) : IM_COL32(126, 116, 90, 200), l.kind == 1 ? lw * 1.4f : lw);
    if (mzoom > 0.12f) {   // buildings, close up
      const Plan& P = zonePlan((u32)lv.info.seed);
      const float rx = sz.x * 0.5f / mzoom + 40, rz = sz.y * 0.5f / mzoom + 40;
      for (const Placed& b : P.s) {
        if (b.x0 > mpan.x + rx || b.x0 + b.W < mpan.x - rx || b.z0 > mpan.y + rz || b.z0 + b.D < mpan.y - rz) continue;
        dl->AddRectFilled(M((float)b.x0, (float)b.z0), M((float)(b.x0 + b.W), (float)(b.z0 + b.D)), IM_COL32(58, 56, 50, 235));
        dl->AddRect(M((float)b.x0, (float)b.z0), M((float)(b.x0 + b.W), (float)(b.z0 + b.D)), IM_COL32(20, 20, 18, 200));
      }
    }
    for (const Region& r : regions()) {
      const ImVec2 c = M((float)r.cx, (float)r.cz); const ImVec2 ts = ImGui::CalcTextSize(r.name);
      if (mzoom < 0.05f && r.danger < 2) continue;
      dl->AddText(c - ts * 0.5f + ImVec2(1, 1), IM_COL32(0, 0, 0, 230), r.name); dl->AddText(c - ts * 0.5f, IM_COL32(225, 215, 180, 240), r.name);
    }
    // squads near you that you'd know of: your own faction anywhere, anyone within a kilometre
    for (const SquadView& q : alifeSquads(lv)) {
      const float d = std::hypot(q.pos.x - p.pos.x, q.pos.z - p.pos.z);
      if (q.faction == F_MUTANT ? d > 400 : (q.faction != faction(p) && d > 1000)) continue;
      const ImU32 c = q.faction == F_MUTANT ? IM_COL32(210, 60, 50, 230) : factionRelation(q.faction, faction(p)) < 0 ? IM_COL32(230, 130, 40, 235) : IM_COL32(120, 210, 120, 235);
      dl->AddCircleFilled(M(q.pos.x, q.pos.z), 3.5f, c); dl->AddCircle(M(q.pos.x, q.pos.z), 3.5f, IM_COL32(0, 0, 0, 200));
    }
    for (const Task& t : activeTasks(lv, p)) if (t.hasTarget) {
      const ImVec2 c = M(t.target.x, t.target.z); const float pulse = 7 + 2 * std::sin((float)ImGui::GetTime() * 4);
      dl->AddCircle(c, pulse, AMBER, 16, 2); dl->AddLine(c + ImVec2(-5, 0), c + ImVec2(5, 0), AMBER, 2); dl->AddLine(c + ImVec2(0, -5), c + ImVec2(0, 5), AMBER, 2);
    }
    if (p.realm == REALM_MAIN) {
      const ImVec2 c = M(p.pos.x, p.pos.z); const vec3 L = lookVector(p.yaw, 0);
      dl->AddCircle(c, 11, IM_COL32(255, 240, 200, 120), 20, 1.5f);
      dl->AddTriangleFilled(c + ImVec2(L.x, L.z) * 10, c + ImVec2(-L.z, L.x) * 5.5f, c + ImVec2(L.z, -L.x) * 5.5f, IM_COL32(255, 244, 210, 255));
    }
    // the corner: where you are, the scale, the keys
    if (const Region* r = regionAt(ifloor(p.pos.x), ifloor(p.pos.z))) {
      char b[160]; std::snprintf(b, sizeof b, "%s  \xC2\xB7  danger %d/5", r->name, r->danger);
      dl->AddRectFilled(o + ImVec2(6, 6), o + ImVec2(14, 12) + ImGui::CalcTextSize(b), IM_COL32(0, 0, 0, 160), 2);
      dl->AddText(o + ImVec2(10, 8), AMBER, b);
    }
    { const float m100 = 100 * mzoom; float len = m100; const char* lab = "100 m"; if (m100 < 30) { len = 1000 * mzoom; lab = "1 km"; } if (len > sz.x * 0.4f) { len = 10 * mzoom; lab = "10 m"; }
      const ImVec2 b0 = o + ImVec2(12, sz.y - 14); dl->AddLine(b0, b0 + ImVec2(len, 0), INK, 2); dl->AddLine(b0 - ImVec2(0, 4), b0 + ImVec2(0, 4), INK, 2); dl->AddLine(b0 + ImVec2(len, -4), b0 + ImVec2(len, 4), INK, 2);
      dl->AddText(b0 + ImVec2(len + 6, -8), INK, lab); }
    const char* keys = "Zoom: wheel / RT LT   Move: drag / right stick   Re-centre: double-click / RS";
    dl->AddText(o + ImVec2(sz.x - ImGui::CalcTextSize(keys).x - 10, sz.y - 20), DIM, keys);
    dl->AddRect(o, o + sz, IM_COL32(90, 90, 70, 255));
    dl->PopClipRect();
    (void)s;
  }
};

class WeaponScreen : public Screen {
public:
  const char* id() const override { return "weapon"; }
  bool pausesGame() const override { return false; }
  void update(App& a, double) override {
    if (inventoryClosePressed(a, true)) { a.popScreen(); return; }
  }
  void draw(App& a) override {
    GameSession* s = a.session && a.session->player ? a.session.get() : nullptr;
    if (!s) return;
    Player& p = *s->player;
    ImGui::SetNextWindowPos(ImVec2(a.width * 0.5f, a.height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(860.f, a.width - 30.f), std::min(600.f, a.height - 30.f)));
    ImGui::Begin("##weapon", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::TextColored(ImVec4(0.89f, 0.72f, 0.3f, 1), "Weapon bench");
    ImGui::SameLine(); ImGui::TextDisabled("  fit and strip attachments, change rounds  \xC2\xB7  Esc / B to close");
    // the guns you carry
    std::vector<int> guns;
    for (int i = 0; i < (int)p.inv.size(); i++) if (const ZItem* z = p.inv[i].empty() ? nullptr : zi(p.inv[i].id); z && z->isGun) guns.push_back(i);
    if (guns.empty()) { ImGui::TextDisabled("You carry no guns."); ImGui::End(); return; }
    if (std::find(guns.begin(), guns.end(), sel) == guns.end()) sel = std::find(guns.begin(), guns.end(), p.selected) != guns.end() ? p.selected : guns[0];
    for (size_t k = 0; k < guns.size(); k++) {
      if (k) ImGui::SameLine();
      const ZItem* z = zi(p.inv[guns[k]].id);
      if (button((z->name + "##g" + std::to_string(guns[k])).c_str(), guns[k] == sel ? BTN_PRIMARY : BTN_NORMAL, true)) sel = guns[k];
    }
    ImGui::Separator();
    ItemStack& g = p.inv[sel];
    const ZItem* gz = zi(g.id);
    const int saved = p.selected; p.selected = sel; GunHud H = gunHud(p); p.selected = saved;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 at = ImGui::GetCursorScreenPos(); ImGui::Dummy(ImVec2(96, 96));
    drawItem(dl, g, at, 96, 1, false);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::Text("%s", gz->name.c_str());
    ImGui::TextDisabled("%s, %s  \xC2\xB7  condition %d%%  \xC2\xB7  %.0f rounds/min  \xC2\xB7  %s kg", gz->gun.cls.c_str(), gz->gun.cal.c_str(), H.cond, gz->gun.rpm, fmtKg(itemWeight(g)).c_str());
    ImGui::TextDisabled("Loaded: %d / %d  %s  \xC2\xB7  %d spare", H.mag, H.clip, H.ammo.c_str(), H.reserve);
    if (button("Change rounds", BTN_NORMAL, true)) cycleAmmo(p, sel);
    ImGui::SameLine(); if (button("Unload", BTN_NORMAL, true)) unloadGun(p, sel);
    ImGui::EndGroup();
    ImGui::Separator();
    json att = gunAttachments(g);
    if (!g.tag.is_object() && !gz->gun.defaultOptic.empty()) att["optic"] = gz->gun.defaultOptic;
    static const std::map<std::string, const char*> NAMES = {{"optic", "Optic"}, {"muzzle", "Muzzle"}, {"under", "Underbarrel"}, {"mag", "Magazine"}};
    const auto slots = gunSlots(g.id);
    if (slots.empty()) ImGui::TextDisabled("This gun takes no attachments.");
    for (const std::string& slot : slots) {
      ImGui::TextColored(ImVec4(0.89f, 0.72f, 0.3f, 1), "%s", NAMES.at(slot));
      ImGui::SameLine(140);
      if (att.contains(slot) && att[slot].is_string()) {
        const ZItem* a2 = zi(att[slot].get<std::string>());
        ImGui::Text("%s", a2 ? a2->name.c_str() : att[slot].get<std::string>().c_str());
        ImGui::SameLine();
        if (button(("Strip##" + slot).c_str(), BTN_GHOST, true)) { std::string why; if (!detachFrom(p, sel, slot, why)) hudToast(why, "warn"); }
      } else ImGui::TextDisabled("(empty)");
      // what in your bags fits here
      const json& fits = gz->gun.fits.value(slot, json::array());
      bool any = false;
      for (int i = 0; i < (int)p.inv.size(); i++) {
        const ItemStack& s2 = p.inv[i];
        if (s2.empty() || std::find(fits.begin(), fits.end(), json(s2.id)) == fits.end()) continue;
        const ZItem* a2 = zi(s2.id);
        if (!any) { ImGui::Indent(140); any = true; }
        if (button(("Fit " + (a2 ? a2->name : s2.id) + "##f" + std::to_string(i)).c_str(), BTN_NORMAL, true)) { std::string why; if (!attachTo(p, sel, i, why)) hudToast(why, "warn"); }
      }
      if (any) ImGui::Unindent(140);
      else { ImGui::Indent(140); std::string list; for (auto& f : fits) if (const ZItem* a2 = zi(f.get<std::string>())) list += (list.empty() ? "" : ", ") + a2->name; ImGui::TextDisabled("Takes: %s", list.c_str()); ImGui::Unindent(140); }
    }
    ImGui::End();
  }
private:
  int sel = -1;
};

class TradeScreen : public Screen {
public:
  explicit TradeScreen(std::string who) : who(std::move(who)) {}
  const char* id() const override { return "trade"; }
  void update(App& a, double) override {
    if (inventoryClosePressed(a, true)) { a.popScreen(); return; }
  }
  void draw(App& a) override {
    GameSession* s = a.session && a.session->player ? a.session.get() : nullptr;
    if (!s) return;
    Player& p = *s->player; Level& lv = *s->level;
    if (stock.empty()) stock = traderStock(lv, who);
    static const std::map<std::string, const char*> NAMES = {{"hamlet", "Moroz"}, {"molot", "The barman"}, {"yanov", "Yanov's quartermaster"}, {"zaton", "The Volga's skipper"}, {"kalina", "Dr. Herman"}};
    const char* name = NAMES.count(who) ? NAMES.at(who) : "Trader";
    ImGui::SetNextWindowPos(ImVec2(a.width * 0.5f, a.height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(1000.f, a.width - 30.f), std::min(640.f, a.height - 30.f)));
    ImGui::Begin("##trade", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::TextColored(ImVec4(0.89f, 0.72f, 0.3f, 1), "%s", name);
    ImGui::SameLine(); ImGui::TextDisabled("  Your money: %s  \xC2\xB7  carrying %s / %s kg  \xC2\xB7  Esc / B to leave", fmtRu(money(p)).c_str(), fmtKg(carryWeight(p)).c_str(), fmtKg(carryCap(p)).c_str());
    ImGui::Separator();
    const float half = ImGui::GetContentRegionAvail().x * 0.5f - 6;
    ImGui::BeginChild("##buy", ImVec2(half, 0), ImGuiChildFlags_Borders);
    padScrollHere();
    ImGui::TextDisabled("Buy");
    for (size_t i = 0; i < stock.size(); i++) {
      TradeLine& t = stock[i];
      if (t.n <= 0) continue;
      const ZItem* z = zi(t.id);
      const int each = z && z->cat == "ammo" ? std::min(t.n, 30) : 1;
      const double cost = t.price * each;
      ImVec2 at = ImGui::GetCursorScreenPos(); ImGui::Dummy(ImVec2(28, 28)); drawItem(ImGui::GetWindowDrawList(), ItemStack(t.id, each), at, 28, 1, false); ImGui::SameLine();
      const std::string lbl = (z ? z->name : t.id) + (each > 1 ? " x" + std::to_string(each) : "") + "  " + fmtRu(cost) + "##b" + std::to_string(i);
      if (button(lbl.c_str(), BTN_NORMAL, true, 0, money(p) < cost)) {
        if (p.give(ItemStack(t.id, each)) == 0) { addMoney(p, -cost); t.n -= each; }
        else hudToast("No room in your bags", "warn");
      }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##sell", ImVec2(0, 0), ImGuiChildFlags_Borders);
    padScrollHere();
    ImGui::TextDisabled(who == "kalina" ? "Sell (the scientists pay full price for artifacts)" : "Sell");
    for (int i = 0; i < (int)p.inv.size(); i++) {
      ItemStack& st = p.inv[i];
      if (st.empty()) continue;
      const ZItem* z = zi(st.id); if (!z || z->cat == "money") continue;
      double price = sellPrice(st);
      if (who == "kalina" && z->cat == "artifact") price = z->price * st.n;
      if (price <= 0) continue;
      ImVec2 at = ImGui::GetCursorScreenPos(); ImGui::Dummy(ImVec2(28, 28)); drawItem(ImGui::GetWindowDrawList(), st, at, 28); ImGui::SameLine();
      const std::string lbl = z->name + (st.n > 1 ? " x" + std::to_string(st.n) : "") + "  +" + fmtRu(price) + "##s" + std::to_string(i);
      if (button(lbl.c_str(), BTN_GHOST, true)) { addMoney(p, price); st.clear(); }
    }
    ImGui::EndChild();
    ImGui::End();
  }
private:
  std::string who; std::vector<TradeLine> stock;
};

class EndingScreen : public Screen {
public:
  const char* id() const override { return "ending"; }
  bool onBack(App&) override { return choice.empty(); }   // choose first
  void draw(App& a) override {
    GameSession* s = a.session && a.session->player ? a.session.get() : nullptr;
    if (!s) return;
    Player& p = *s->player;
    ImGui::SetNextWindowPos(ImVec2(a.width * 0.5f, a.height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(760.f, a.width - 30.f), std::min(520.f, a.height - 30.f)));
    ImGui::Begin("##ending", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::PushTextWrapPos();
    if (choice.empty()) {
      ImGui::TextColored(ImVec4(0.89f, 0.72f, 0.3f, 1), "The light");
      ImGui::TextUnformatted("At the bottom of the sarcophagus a light hangs in the air like a held breath. It is warm. It knows your name, and the one you came here hunting. It asks what you want.");
      ImGui::Spacing();
      if (button("\"Make me rich.\"", BTN_NORMAL, false, -1)) choice = "rich";
      if (button("\"Make the Zone disappear.\"", BTN_NORMAL, false, -1)) choice = "gone";
      if (button("\"Make me the one they talk about.\"", BTN_NORMAL, false, -1)) choice = "legend";
      if (button("\"Where is the Ferryman?\"", BTN_PRIMARY, false, -1)) choice = "ferryman";
    } else {
      static const std::map<std::string, const char*> E = {
        {"rich", "Coins pour from the light, warm and heavy, and keep on pouring. They fill your hands, your pockets, the room. The ceiling of the sarcophagus groans under the weight of everything you asked for, and comes down."},
        {"gone", "The light flares white. When you can see again, it is very quiet. You walk out of the plant into a Zone that is still there, and you no longer remember why you came. The Choir find you on the road and welcome you home."},
        {"legend", "The light goes into you. From then on stalkers tell stories about a man who walks out of the centre every spring and leads the hopeful in. Nobody he leads comes back. You are the new Ferryman."},
        {"ferryman", "The light flickers, and behind it you see him: the Ferryman, wired into a Choir machine, feeding it the wishes of everyone who ever came this far. \"It lies,\" he says. \"It always did. Help me break it.\" You do. The light goes out, and the Zone shudders like a sleeper turning over. The way home is open \xE2\x80\x94 if you still want it."},
      };
      ImGui::TextColored(ImVec4(0.89f, 0.72f, 0.3f, 1), "%s", choice == "ferryman" ? "The truth" : "Be careful what you wish for");
      ImGui::TextUnformatted(E.at(choice));
      ImGui::Spacing();
      if (button("Go on in the Zone", BTN_PRIMARY, false, -1)) {
        p.stats["zone"]["ending"] = choice;
        if (choice == "rich") addMoney(p, 50000);
        if (choice == "ferryman") s->level->info.data["zoneStory"]["lightOut"] = true;
        a.popScreen();
      }
    }
    ImGui::PopTextWrapPos();
    ImGui::End();
  }
private:
  std::string choice;
};

// ---------------------------------------------------------------- the HUD
class ZoneHud : public RenderLayer {
public:
  const char* name() const override { return "zoneHud"; }
  int order() const override { return 6000; }
  void update(GameSession& s, double dt) override {
    if (!s.player || !zoneWorld(*s.level)) return;
    App& a = s.app; Player& p = *s.player;
    if (a.topScreen()) return;
    Input& in = a.input;
    if (in.pressed(Act::Pda)) { a.pushScreen(std::make_unique<PdaScreen>()); return; }
    if (in.pressed(Act::Weapon)) { a.pushScreen(std::make_unique<WeaponScreen>()); return; }
    if (p.stats.contains("zone") && p.stats["zone"].value("ending", "") == std::string("choose")) { a.pushScreen(std::make_unique<EndingScreen>()); return; }
    // a trader in front of you: use opens the trade
    if (in.pressed(Act::Use)) {
      const glm::dvec3 o(p.pos.x, p.pos.y + p.eye, p.pos.z); const vec3 L = lookVector(p.yaw, p.pitch);
      for (auto& e : s.level->entities()) {
        if (e->removed || e->type != "z_npc_trader" || e->realm != p.realm) continue;
        const double t = rayAABB(o, glm::dvec3(L), glm::dvec3(e->pos.x - 0.5, e->pos.y, e->pos.z - 0.5), glm::dvec3(e->pos.x + 0.5, e->pos.y + 2, e->pos.z + 0.5));
        if (t >= 0 && t < 4) { a.pushScreen(std::make_unique<TradeScreen>(e->data.value("trader", std::string("hamlet")))); return; }
      }
    }
    // the detector's beep
    const DetectorHud D = detectorHud(*s.level, p);
    beepT -= (float)dt;
    if (D.has && D.dist >= 0 && beepT <= 0) { beepT = std::clamp(D.dist / D.range, 0.08f, 1.f) * 1.4f; s.level->fx->soundUi(p.id, "tick"); }
    // a new region's name, like a level's title
    const std::string rn = p.realm == REALM_MAIN ? regionName(ifloor(p.pos.x), ifloor(p.pos.z)) : "Your bunker";
    if (rn != region) { region = rn; regionT = 5; }
    regionT -= (float)dt;
  }
  void drawHud(GameSession& s) override {
    if (!s.player || !zoneWorld(*s.level)) return;
    App& a = s.app; Player& p = *s.player; Level& lv = *s.level;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float W = (float)a.width, H = (float)a.height, z = Z(), t = (float)a.time;
    // ---- screen effects: the emission, wounds, radiation, the psy field, the scope
    const Emission em = emission(lv);
    if (em.phase >= 1 && p.realm == REALM_MAIN) {
      dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), rgba(150, 20, 10, 0.22f * em.strength * (em.phase == 2 ? 1.f + 0.3f * std::sin(t * 9) : 1.f)));
      vignette(dl, W, H, rgba(90, 0, 0, 0.8f * em.strength), 1.5f);
    }
    const float hpF = p.hp / std::max(1.f, p.maxHp);
    if (hpF < 0.4f && !p.dead) vignette(dl, W, H, rgba(120, 0, 0, (0.4f - hpF) * 2.2f * (0.75f + 0.25f * std::sin(t * 4))), 1.3f);
    if (zget(p, "bleed") > 0.05f) vignette(dl, W, H, rgba(140, 0, 0, std::min(0.5f, zget(p, "bleed") * 0.15f) * (0.5f + 0.5f * std::sin(t * 6))), 0.8f);
    if (zget(p, "env") > 0.3f) vignette(dl, W, H, rgba(150, 170, 40, std::min(0.35f, zget(p, "env") * 0.12f)), 0.9f);
    if (zget(p, "psy", 100) < 70) vignette(dl, W, H, rgba(90, 20, 120, (70 - zget(p, "psy", 100)) / 70.f * 0.7f * (0.6f + 0.4f * std::sin(t * 2.3f))), 1.6f);
    const GunHud G = gunHud(p);
    if (G.has && G.ads && G.scope) {   // looking through a scope: the black ring and the reticle
      const float R = std::min(W, H) * 0.42f; const ImVec2 c(W * 0.5f, H * 0.5f);
      for (int i = 0; i < 4; i++) dl->AddCircle(c, R + 60 * (i + 1), IM_COL32(0, 0, 0, 255), 64, 120);
      dl->AddRectFilled(ImVec2(0, 0), ImVec2(c.x - R - 1, H), IM_COL32(0, 0, 0, 255)); dl->AddRectFilled(ImVec2(c.x + R + 1, 0), ImVec2(W, H), IM_COL32(0, 0, 0, 255));
      dl->AddCircle(c, R, IM_COL32(0, 0, 0, 255), 64, 6);
      const ImU32 rc = IM_COL32(20, 20, 20, 230);
      if (G.reticle == "scope") {   // PSO-style chevrons and the range scale
        dl->AddTriangleFilled(c + ImVec2(-6, 10) * z, c + ImVec2(6, 10) * z, c, rc);
        for (int k = 1; k <= 3; k++) dl->AddTriangleFilled(c + ImVec2(-4, 10 + k * 16) * z, c + ImVec2(4, 10 + k * 16) * z, c + ImVec2(0, k * 16) * z, rc);
        dl->AddLine(c - ImVec2(R, 0), c - ImVec2(14 * z, 0), rc, 1.5f); dl->AddLine(c + ImVec2(14 * z, 0), c + ImVec2(R, 0), rc, 1.5f);
      } else { dl->AddLine(c - ImVec2(R, 0), c + ImVec2(R, 0), rc, 1); dl->AddLine(c - ImVec2(0, R), c + ImVec2(0, R), rc, 1); dl->AddCircle(c, 18 * z, rc, 24, 1.2f); }
    } else if (G.has && G.ads && (G.reticle == "dot" || G.reticle == "holo")) {
      const ImVec2 c(W * 0.5f, H * 0.5f);
      if (G.reticle == "holo") dl->AddCircle(c, 9 * z, IM_COL32(255, 60, 40, 220), 24, 1.5f);
      dl->AddCircleFilled(c, 2 * z, IM_COL32(255, 50, 30, 255));
    }
    if (s.hudHidden || p.dead) return;
    // ---- top left: the region and the time
    if (regionT > 0) {
      const float k = std::min(1.f, regionT) * std::min(1.f, (5 - regionT) * 2);
      textShadow(dl, F_H3, ImVec2(24 * z, 24 * z), rgba(226, 214, 190, k), region.c_str(), ImVec2(2, 2) * z, rgba(0, 0, 0, k * 0.8f));
      if (const Region* r = regionAt(ifloor(p.pos.x), ifloor(p.pos.z)); r && p.realm == REALM_MAIN) text(dl, F_SMALL, ImVec2(24 * z, 56 * z), rgba(170, 164, 140, k), r->blurb, 420 * z);
    }
    // ---- top: the compass, with the task's marker
    {
      const float cw = 360 * z, cx = W * 0.5f, cy = 14 * z;
      dl->AddRectFilled(ImVec2(cx - cw * 0.5f, cy), ImVec2(cx + cw * 0.5f, cy + 20 * z), PLATE, 3 * z);
      const float fov = 1.6f;
      auto at = [&](float yawDir) { float d = std::remainder(yawDir - p.yaw, 6.28318f); return d; };   // yaw angles
      const char* dirs[] = {"N", "W", "S", "E"};
      for (int i = 0; i < 4; i++) { const float d = -at(i * 1.5708f); if (std::abs(d) < fov) text(dl, F_SMALL, ImVec2(cx + d / fov * cw * 0.5f - 4 * z, cy + 3 * z), i == 0 ? AMBER : INK, dirs[i]); }
      for (const Task& tk : activeTasks(lv, p)) if (tk.hasTarget && p.realm == REALM_MAIN) {
        const float ty = std::atan2(-(tk.target.x - p.pos.x), -(tk.target.z - p.pos.z));
        const float d = std::clamp(-at(ty), -fov, fov);
        const ImVec2 m(cx + d / fov * cw * 0.5f, cy + 10 * z);
        dl->AddTriangleFilled(m + ImVec2(-5, 8) * z, m + ImVec2(5, 8) * z, m + ImVec2(0, 0), AMBER);
        char b[48]; std::snprintf(b, sizeof b, "%.0f m", std::hypot(tk.target.x - p.pos.x, tk.target.z - p.pos.z));
        textCentered(dl, F_SMALL, ImVec2(cx, cy + 32 * z), AMBER, (tk.title + "  " + b).c_str(), IM_COL32(0, 0, 0, 200));
      }
    }
    // ---- the emission's banner
    if (em.phase == 1 || em.phase == 2) {
      char b[96];
      if (em.phase == 1) std::snprintf(b, sizeof b, "EMISSION IN %d:%02d \xE2\x80\x94 GET UNDER A ROOF OR UNDERGROUND", (int)em.left / 60, (int)em.left % 60);
      else std::snprintf(b, sizeof b, underCover(lv, p) ? "EMISSION \xE2\x80\x94 you are under cover" : "EMISSION \xE2\x80\x94 YOU ARE IN THE OPEN!");
      textCentered(dl, F_BIG, ImVec2(W * 0.5f, 74 * z), (std::fmod(t, 1.f) < 0.5f || em.phase == 2) ? IM_COL32(255, 90, 60, 255) : IM_COL32(255, 200, 120, 255), b, IM_COL32(0, 0, 0, 220));
    }
    // ---- bottom left: health, stamina and the status marks
    {
      const float bw = 220 * z, x0 = 18 * z; float y = H - 74 * z;
      dl->AddRectFilled(ImVec2(x0 - 8 * z, y - 8 * z), ImVec2(x0 + bw + 8 * z, H - 12 * z), PLATE, 4 * z);
      bar(dl, ImVec2(x0, y), bw, 10 * z, hpF, IM_COL32(180, 40, 30, 255), IM_COL32(40, 12, 10, 220)); y += 14 * z;
      bar(dl, ImVec2(x0, y), bw, 6 * z, zget(p, "stamina", 100) / 100, IM_COL32(200, 190, 110, 255), IM_COL32(40, 38, 20, 220)); y += 12 * z;
      ImVec2 m(x0, y);
      const float rad = zget(p, "rad"), bl = zget(p, "bleed"), fd = zget(p, "food", 100), wt = zget(p, "water", 100), sl = zget(p, "sleep", 100), ps = zget(p, "psy", 100);
      mark(dl, m, "RAD", rad > 70 ? 3 : rad > 40 ? 2 : rad > 12 ? 1 : 0, z);
      mark(dl, m, "BLEED", bl > 2 ? 3 : bl > 0.8f ? 2 : bl > 0.05f ? 1 : 0, z);
      mark(dl, m, "HUNGER", fd < 10 ? 3 : fd < 25 ? 2 : fd < 40 ? 1 : 0, z);
      mark(dl, m, "THIRST", wt < 10 ? 3 : wt < 25 ? 2 : wt < 40 ? 1 : 0, z);
      mark(dl, m, "SLEEP", sl < 10 ? 3 : sl < 25 ? 2 : sl < 40 ? 1 : 0, z);
      mark(dl, m, "PSY", ps < 30 ? 3 : ps < 60 ? 2 : ps < 90 ? 1 : 0, z);
      const float cw = carryWeight(p), cap = carryCap(p);
      mark(dl, m, "WEIGHT", cw > cap * 1.25f ? 3 : cw > cap ? 2 : cw > cap * 0.85f ? 1 : 0, z);
      const std::string kg = fmtKg(cw) + " / " + fmtKg(cap) + " kg";
      text(dl, F_SMALL, ImVec2(x0 + bw - textSize(F_SMALL, kg.c_str()).x, H - 74 * z - 22 * z), DIM, kg.c_str());
      if (zget(p, "env") > 0.05f) { char b[32]; std::snprintf(b, sizeof b, "%.2f Sv/h", zget(p, "env")); text(dl, F_SMALL, ImVec2(x0, H - 74 * z - 22 * z), IM_COL32(220, 210, 80, 255), b); }
    }
    // ---- bottom right: the gun
    if (G.has) {
      char b[96]; std::snprintf(b, sizeof b, "%d", G.mag);
      char r[64]; std::snprintf(r, sizeof r, "/ %d", G.reserve);
      const float x1 = W - 22 * z, y1 = H - 20 * z;
      ImVec2 bs = textSize(F_DISP, b), rs = textSize(F_BIG, r);
      dl->AddRectFilled(ImVec2(x1 - 230 * z, y1 - 66 * z), ImVec2(x1 + 8 * z, y1 + 8 * z), PLATE, 4 * z);
      text(dl, F_DISP, ImVec2(x1 - rs.x - 8 * z - bs.x, y1 - bs.y), G.mag == 0 ? IM_COL32(230, 80, 60, 255) : INK, b);
      text(dl, F_BIG, ImVec2(x1 - rs.x, y1 - rs.y - 2 * z), DIM, r);
      text(dl, F_SMALL, ImVec2(x1 - 222 * z, y1 - 62 * z), AMBER, G.name.c_str());
      text(dl, F_SMALL, ImVec2(x1 - 222 * z, y1 - 46 * z), DIM, G.ammo.c_str());
      bar(dl, ImVec2(x1 - 222 * z, y1 - 4 * z), 60 * z, 4 * z, G.cond / 100.f, G.cond > 60 ? IM_COL32(120, 180, 90, 255) : G.cond > 30 ? IM_COL32(220, 170, 50, 255) : IM_COL32(220, 60, 40, 255), IM_COL32(30, 30, 20, 220));
      if (G.reloading) textCentered(dl, F_MID, ImVec2(W * 0.5f, H * 0.5f + 40 * z), INK, "Reloading\xE2\x80\xA6", IM_COL32(0, 0, 0, 200));
      if (G.jammed) textCentered(dl, F_MID, ImVec2(W * 0.5f, H * 0.5f + 40 * z), IM_COL32(255, 100, 60, 255), "JAMMED \xE2\x80\x94 reload to clear", IM_COL32(0, 0, 0, 200));
    }
    // ---- hit markers: a red arc on the side the blow came from, fading over a second and a half
    if (p.data.is_object() && p.data.contains("hitFrom") && p.data["hitFrom"].is_array() && p.data["hitFrom"].size() == 2) {
      const float age = (float)(gameClock(lv) - p.lastHurtT);
      if (age >= 0 && age < 1.5f) {
        const float hx = p.data["hitFrom"][0].get<float>() - p.pos.x, hz = p.data["hitFrom"][1].get<float>() - p.pos.z;
        const float rel = std::atan2(-hx, -hz) - p.yaw, al = 1 - age / 1.5f;
        const ImVec2 c(W * 0.5f, H * 0.5f); const float R = std::min(W, H) * 0.22f;
        dl->PathArcTo(c, R, -PI * 0.5f - rel - 0.32f, -PI * 0.5f - rel + 0.32f, 18);
        dl->PathStroke(IM_COL32(200, 30, 20, (int)(210 * al)), 0, 7 * z);
      }
    }
    // ---- the detector's display, on the screen of the one in your left hand (render/zone_fpgun.cpp finds where it is)
    const DetectorHud D = detectorHud(lv, p);
    if (D.has && zoneDetRectOn) {
      const ImVec2 r0(zoneDetRect[0], zoneDetRect[1]), r1(zoneDetRect[2], zoneDetRect[3]), c = (r0 + r1) * 0.5f, sz = r1 - r0;
      const float u = std::min(sz.x, sz.y), tm = (float)ImGui::GetTime();
      const ImU32 G1 = IM_COL32(90, 250, 130, 235), G2 = IM_COL32(40, 140, 70, 200), G3 = IM_COL32(20, 70, 36, 170);
      const float prox = D.dist >= 0 ? std::clamp(1 - D.dist / D.range, 0.f, 1.f) : 0.f;
      const bool on = D.dist >= 0 && beepT > 0.0f && beepT < 0.1f + prox * 0.05f + 0.06f;
      const float flick = lv.info.data.is_object() && emission(lv).phase == 2 ? (std::fmod(tm * 17.f, 1.f) < 0.3f ? 0.3f : 1.f) : 1.f;   // the wave scrambles it
      auto A = [&](ImU32 col, float k) { return (col & 0x00FFFFFFu) | ((ImU32)(((col >> 24) & 255) * k * flick) << 24); };
      dl->PushClipRect(r0, r1, true);
      if (zoneDetId == "detector_echo") {
        // a row of lamps that fill as you close in, a big lamp that flashes with each beep, the readout
        const int n = 10, lit = D.dist >= 0 ? 1 + (int)(prox * (n - 1)) : 0;
        const float lw = sz.x * 0.8f / n;
        for (int i = 0; i < n; i++) {
          const ImVec2 q0(r0.x + sz.x * 0.1f + i * lw + 1, r0.y + sz.y * 0.16f), q1 = q0 + ImVec2(lw - 2, sz.y * 0.2f);
          const ImU32 lc = i < lit ? (i > 6 ? IM_COL32(255, 80, 50, 240) : i > 3 ? IM_COL32(255, 190, 60, 240) : G1) : IM_COL32(40, 30, 22, 200);
          dl->AddRectFilled(q0, q1, A(lc, 1));
        }
        dl->AddCircleFilled(ImVec2(c.x, r0.y + sz.y * 0.68f), u * 0.14f, on ? A(IM_COL32(255, 70, 40, 255), 1) : A(IM_COL32(70, 20, 14, 220), 1));
        text(dl, F_SMALL, ImVec2(r0.x + 3, r1.y - 14), A(G2, 1), D.dist >= 0 ? "SIGNAL" : "--");
      } else if (zoneDetId == "detector_bear") {
        // the dial: rings, tick marks, a needle pointing the way to the nearest find, the distance underneath
        const float R = u * 0.46f;
        dl->AddCircle(c, R, A(G2, 1), 48, 1.5f); dl->AddCircle(c, R * 0.55f, A(G3, 1), 40, 1.f);
        for (int k = 0; k < 16; k++) { const float an = k * TAU / 16; const ImVec2 v(std::sin(an), -std::cos(an)); dl->AddLine(c + v * R * 0.84f, c + v * R * (k % 4 ? 0.92f : 0.98f), A(G2, 1), k % 4 ? 1.f : 2.f); }
        if (D.dist >= 0) {
          const float rel = std::atan2(-D.dir.x, -D.dir.z) - p.yaw + std::sin(tm * 9.f) * 0.04f * (1 - prox);
          const ImVec2 v(-std::sin(rel), -std::cos(rel));
          dl->AddTriangleFilled(c + v * R * 0.8f, c + ImVec2(-v.y, v.x) * R * 0.08f, c - ImVec2(-v.y, v.x) * R * 0.08f, A(on ? IM_COL32(255, 90, 60, 255) : IM_COL32(230, 70, 50, 235), 1));
          char b[24]; std::snprintf(b, sizeof b, "%.0fm", D.dist); textCentered(dl, F_SMALL, ImVec2(c.x, c.y + R * 0.45f), A(G1, 1), b, 0);
        } else textCentered(dl, F_SMALL, ImVec2(c.x, c.y + R * 0.4f), A(G3, 1), "- - -", 0);
        dl->AddCircleFilled(c, u * 0.035f, A(G1, 1));
      } else {
        // the radar: rings, cross hairs, a sweep, a blip for every find in range (north up = the way you face)
        const float R = u * 0.47f, sweep = std::fmod(tm * 2.2f, TAU);
        for (int k = 1; k <= 3; k++) dl->AddCircle(c, R * k / 3.f, A(G3, 1), 48, 1.f);
        dl->AddLine(c - ImVec2(R, 0), c + ImVec2(R, 0), A(G3, 1)); dl->AddLine(c - ImVec2(0, R), c + ImVec2(0, R), A(G3, 1));
        for (int k = 0; k < 10; k++) { const float an = sweep - k * 0.06f; dl->AddLine(c, c + ImVec2(std::sin(an), -std::cos(an)) * R, A(G2, 0.7f - k * 0.065f), 2.f); }
        for (const vec3& q : D.dots) {
          const vec3 d = q - p.pos; const float dd = std::hypot(d.x, d.z) / std::max(1.f, D.range);
          if (dd > 1) continue;
          const float an = std::atan2(-d.x, -d.z) - p.yaw;
          const ImVec2 at = c + ImVec2(-std::sin(an), -std::cos(an)) * dd * R;
          float since = std::fmod(sweep - std::atan2(at.x - c.x, -(at.y - c.y)) + TAU * 2, TAU);
          const float glow = std::clamp(1.2f - since / 2.5f, 0.35f, 1.f);
          dl->AddCircleFilled(at, std::max(2.5f, u * 0.035f), A(IM_COL32(170, 255, 180, 255), glow));
        }
        dl->AddCircleFilled(c, std::max(2.f, u * 0.025f), A(G1, 1));
        char b[24]; if (D.dist >= 0) std::snprintf(b, sizeof b, "%.0f m", D.dist); else std::snprintf(b, sizeof b, "%.0f m range", D.range);
        text(dl, F_SMALL, ImVec2(r0.x + 4, r1.y - 14), A(G2, 1), b);
      }
      dl->PopClipRect();
    }
  }
private:
  std::string region; float regionT = 0, beepT = 0;
};
} // namespace

void registerZoneFpGun();   // render/zone_fpgun.cpp
void registerZonePda();     // render/zone_pda.cpp

void registerZoneClient() {
  registerZoneFpGun();
  registerZonePda();
  static bool tabs = false;
  if (!tabs) {
    tabs = true;
    registerMenuTab({"pda", "PDA", [](App& a) { a.pushScreen(std::make_unique<PdaScreen>()); }, nullptr});
    registerMenuTab({"weapon", "Weapon", [](App& a) { a.pushScreen(std::make_unique<WeaponScreen>()); }, nullptr});
  }
  static bool layer = false;
  if (!layer) { layer = true; registerRenderLayer([](GameSession&) -> std::shared_ptr<RenderLayer> { return std::make_shared<ZoneHud>(); }); }
}

} // namespace dq
