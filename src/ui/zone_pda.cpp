// ui/zone_pda.cpp — the PDA, as in S.T.A.L.K.E.R. Call of Pripyat: drawn on the screen of the device held up in both
// hands (render/zone_fpgun.cpp gives its rect), a dark interface in thin grey frames with a tab bar along the top (Map,
// Tasks, Statistics, Relations, Contacts, Guide, Radio, Messages), the battery and the Zone's clock. The map: the active
// tasks on the left (pick one: it's tracked on the compass and the map centres on it), filter toggles over the map
// (Tasks, Stashes, Areas, Important places), the map itself — a satellite-like picture made once per seed from the traced
// layers (water, forest, open ground, built-up) with hill shading and roads, the buildings' footprints in roof greys
// close up — task areas as circles, the name of the place under the cursor or the selected task, icons for you, tasks,
// stashes and the anomaly fields you've been near, squads only near you and close up, a zoom / pan pad and scrollbars.
// Mouse: wheel, drag, double-click re-centres, click a place. Keys: W A S D pan, + - zoom, Up / Down tasks, Enter
// tracks, Space re-centres, 1-4 filters. Controller: sticks pan, LT / RT zoom, d-pad up / down tasks, A tracks, R3 / X
// re-centres, d-pad left / right + Y the filters, LB / RB tabs, B closes.
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "sim/zone.h"
#include "sim/zone_anomalies.h"
#include "sim/creature.h"
#include "sim/physics.h"
#include "gen/zone_plan.h"
#include "gen/zone_layers.h"
#include "core/registry.h"
#include <imgui.h>
#include <SDL.h>
#include "render/gl.h"
#include <thread>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <map>

namespace dq {

bool inventoryClosePressed(App& a, bool tabScreen);   // ui/ui_main.cpp
extern float zonePdaRect[4]; extern bool zonePdaRectOn;   // render/zone_fpgun.cpp: the held PDA's screen

using namespace ui;
namespace {
using namespace zone;
const ImU32 BG = IM_COL32(11, 13, 12, 250), PANEL = IM_COL32(18, 21, 20, 255), FRAME = IM_COL32(96, 100, 98, 255), FRAME2 = IM_COL32(58, 62, 60, 255),
            TXT = IM_COL32(208, 212, 202, 255), DIMT = IM_COL32(128, 134, 126, 255), HI = IM_COL32(232, 196, 96, 255), SEL = IM_COL32(52, 60, 56, 255),
            RED = IM_COL32(224, 86, 60, 255), GREEN = IM_COL32(120, 210, 120, 255), CYAN = IM_COL32(110, 210, 230, 255);
const ImVec4 V_HI(0.91f, 0.77f, 0.38f, 1), V_DIM(0.5f, 0.53f, 0.49f, 1), V_RED(0.88f, 0.34f, 0.24f, 1), V_GREEN(0.47f, 0.82f, 0.47f, 1);

float smooth(float a, float b, float x) { const float t = std::clamp((x - a) / (b - a), 0.f, 1.f); return t * t * (3 - 2 * t); }
float h01(int a, int b, int c) { return (float)(hash32(a, b, c, 991) % 10000) / 10000.f; }

// ---------------------------------------------------------------- the map picture
// the ground's colours over a w x h grid of S-block cells from (x0, z0): fields in a patchwork, forest crowns, the Red
// Forest, marsh, built-up yards, roads, water with a dark shore, all hill-shaded with the light from the north-west
void paintGround(u32 seed, int x0, int z0, int S, int w, int h, std::vector<u8>& px) {
  std::vector<float> H((size_t)w * h); std::vector<vec3> C((size_t)w * h); std::vector<u8> wet((size_t)w * h);
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
    const int x = x0 + i * S + S / 2, z = z0 + j * S + S / 2;
    const Ground g = S >= 8 ? groundCoarse(seed, x, z) : groundAt(seed, x, z);   // (the whole map from the coarse layers: no tile loads)
    const size_t q = (size_t)j * w + i;
    H[q] = g.h;
    const float fo = layerAt(ML_FOREST, (float)x, (float)z), op = layerAt(ML_OPEN, (float)x, (float)z), bu = layerAt(ML_BUILT, (float)x, (float)z);
    const std::string rid = g.region ? g.region->id : "";
    const int fx = (int)std::floor((x + 3000) / 170.f), fz = (int)std::floor((z + 3000) / 120.f);   // old collective-farm plots
    const float pt = h01(fx, fz, 1);
    vec3 c = glm::mix(vec3(112, 118, 74), vec3(146, 138, 94), pt);
    if (pt > 0.8f) c = vec3(122, 108, 80);   // ploughed, long ago
    c = glm::mix(c, vec3(98, 108, 66), 1 - smooth(0.2f, 0.7f, op) * 0.6f);   // unkempt meadow where the map has no open ground
    const float sp = h01(x >> 2, z >> 2, 2), cl = h01(x >> 4, z >> 4, 7);   // crowns, and clumps of them
    vec3 fc = glm::mix(vec3(32, 48, 28), vec3(58, 76, 44), sp * 0.7f + cl * 0.3f);
    if (rid == "redforest") fc = glm::mix(vec3(96, 62, 40), vec3(130, 88, 56), sp);
    c = glm::mix(c, fc, smooth(0.25f, 0.65f, fo));
    if (rid == "marsh") c = glm::mix(c, vec3(80, 88, 60), 0.6f);
    c = glm::mix(c, glm::mix(vec3(96, 96, 90), vec3(126, 122, 112), h01(x >> 2, z >> 2, 3)), smooth(0.3f, 0.75f, bu) * 0.85f);
    if (g.road && g.roadD < std::max(2.5f, S * 0.18f)) c = g.road == 1 ? vec3(122, 120, 114) : g.road == 3 ? vec3(84, 76, 70) : vec3(134, 118, 92);
    if (g.water) { wet[q] = 1; c = glm::mix(vec3(26, 40, 48), vec3(40, 58, 62), h01(x >> 3, z >> 3, 4) * 0.4f); }
    else if (rid == "marsh" && h01(x >> 4, z >> 4, 5) > 0.82f) c = glm::mix(c, vec3(44, 58, 56), 0.7f);   // standing water in the reeds
    C[q] = c;
  }
  px.assign((size_t)w * h * 4, 255);
  const float k = 0.05f * 20.f / S;
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
    const size_t q = (size_t)j * w + i;
    vec3 c = C[q];
    if (!wet[q]) {
      const float hx = H[(size_t)j * w + std::min(w - 1, i + 1)] - H[(size_t)j * w + std::max(0, i - 1)], hz = H[(size_t)std::min(h - 1, j + 1) * w + i] - H[(size_t)std::max(0, j - 1) * w + i];
      c *= std::clamp(1.f - (hx + hz) * k, 0.6f, 1.35f);
      bool shore = false; for (int t = 0; t < 4 && !shore; t++) { const int ii = i + (t == 0) - (t == 1), jj = j + (t == 2) - (t == 3); if (ii >= 0 && jj >= 0 && ii < w && jj < h && wet[(size_t)jj * w + ii]) shore = true; }
      if (shore) c *= 0.8f;
    }
    const float n = (h01(x0 / S + i, z0 / S + j, 6) - 0.5f) * 7;
    for (int t = 0; t < 3; t++) px[q * 4 + t] = (u8)std::clamp(c[t] + n, 0.f, 255.f);
  }
}
unsigned upload(int w, int h, const std::vector<u8>& px, unsigned tex) {
  if (!tex) glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  glBindTexture(GL_TEXTURE_2D, 0);
  return tex;
}
// the whole Zone, made once per seed on a worker thread (a pixel every 20 blocks), uploaded on the GL thread when done; 0 until then
constexpr int MAP_S = 20;
unsigned mapTexture(u32 seed) {
  struct Pic { u32 seed = 0; std::atomic<int> state{0}; int w = 0, h = 0; std::vector<u8> px; unsigned tex = 0; };
  static Pic* P = new Pic();
  if (P->state.load() != 0 && P->seed != seed) { if (P->state.load() == 1) return 0; P->state = 0; }
  if (P->state.load() == 0) {
    P->seed = seed; P->state = 1;
    std::thread([seed] { const int w = (MAX_X - MIN_X) / MAP_S, h = (MAX_Z - MIN_Z) / MAP_S; paintGround(seed, MIN_X, MIN_Z, MAP_S, w, h, P->px); P->w = w; P->h = h; P->state = 2; }).detach();
  }
  if (P->state.load() == 2) { P->tex = upload(P->w, P->h, P->px, P->tex); P->px.clear(); P->px.shrink_to_fit(); P->state = 3; }
  return P->state.load() == 3 ? P->tex : 0;
}
// close up: a 1600-block square round the view at a pixel every 4 blocks (tree crowns, yards, the road's edges), remade when the
// view moves away from it; (ox, oz) = its corner
constexpr int DET_S = 4, DET_N = 400;
unsigned detailTexture(u32 seed, vec2 want, int& ox, int& oz) {
  struct Pic { u32 seed = 0; std::atomic<int> state{0}; int cx = 0, cz = 0, tcx = 0, tcz = 0; u32 tseed = 0; std::vector<u8> px; unsigned tex = 0; };
  static Pic* D = new Pic();
  if (D->state.load() == 2) { D->tex = upload(DET_N, DET_N, D->px, D->tex); D->tcx = D->cx; D->tcz = D->cz; D->tseed = D->seed; D->state = 0; }
  const bool stale = !D->tex || D->tseed != seed || std::abs(want.x - D->tcx) > 420 || std::abs(want.y - D->tcz) > 420;
  if (stale && D->state.load() == 0) {
    D->seed = seed; D->cx = (int)std::round(want.x / 64) * 64; D->cz = (int)std::round(want.y / 64) * 64; D->state = 1;
    std::thread([] { paintGround(D->seed, D->cx - DET_N * DET_S / 2, D->cz - DET_N * DET_S / 2, DET_S, DET_N, DET_N, D->px); D->state = 2; }).detach();
  }
  if (!D->tex || D->tseed != seed) return 0;
  ox = D->tcx - DET_N * DET_S / 2; oz = D->tcz - DET_N * DET_S / 2;
  return D->tex;
}

// ---------------------------------------------------------------- the guide
struct Entry { std::string head, name, text; };
const std::vector<Entry>& guide() {
  static std::vector<Entry> G = [] {
    std::vector<Entry> g;
    const std::pair<const char*, const char*> MUT[] = {
      {"z_scab_dog", "Feral dogs gone bald and blistered. They hunt five and six together and circle before they rush. Shoot the first one in: the rest lose heart."},
      {"z_lurcher", "A long-legged dog with a nose for blood. It follows a wounded stalker for kilometres and springs when you reload."},
      {"z_hogspawn", "A sow the size of a car, slow to anger and slow to stop. It kicks like a truck; keep out of reach and aim for the head."},
      {"z_tusker", "A boar with a plate of hard skin over its skull. It charges in a straight line: step aside and shoot it in the flank."},
      {"z_rad_rat", "Rats the size of cats that pour out of cellars and sewers. One is nothing. Thirty are a problem."},
      {"z_crawler", "Goes on all fours through the ruins and drops on you from above. Look up in old buildings."},
      {"z_shroud", "The one you don't see: it bends the light round itself and walks up close before it grabs. Watch for the shimmer, listen for the breathing."},
      {"z_husk", "What the Zone leaves of a stalker whose mind it burned out. They still carry guns, and still shoot, badly."},
      {"z_puppeteer", "A stunted thing with a swollen head that drives husks like puppets and hammers your mind from cover. Find it first."},
      {"z_stoneback", "A hunched giant whose stamp shakes the ground. Keep your distance: its blows break bones through armour."},
      {"z_hurler", "Throws whatever lies about \xE2\x80\x94 bricks, barrels, car doors \xE2\x80\x94 without touching it. Kill it fast or keep a wall between you."},
      {"z_ridgecat", "A pale, long-bodied hunter that leaps from roofs and ridges, strikes, and is gone before you turn. It comes back."}};
    for (auto& [k, t] : MUT) {
      std::string n = reg().mobs.is_object() && reg().mobs.contains(k) ? reg().mobs[k].value("label", std::string(k)) : std::string(k);
      if (n.rfind("a ", 0) == 0) n = n.substr(2); if (!n.empty()) n[0] = (char)std::toupper((unsigned char)n[0]);
      g.push_back({"Mutants", n, t});
    }
    static const char* FAM[5] = {"thermal", "electric", "gravity", "chemical", "psy"};
    for (int k = 0; k < AK_COUNT; k++) { const AnoDef& d = anoDef(k); g.push_back({"Anomalies", d.name, std::string("A ") + FAM[std::clamp((int)d.family, 0, 4)] + " anomaly. " + d.about}); }
    static const std::map<std::string, const char*> FX = {{"carry", "carry weight %+.0f kg"}, {"regen", "heals %+.2f/s"}, {"burn", "burns %+.0f%%"}, {"shock", "shocks %+.0f%%"},
      {"chem", "chemical burns %+.0f%%"}, {"psy", "psy %+.0f%%"}, {"stamina", "stamina %+.0f%%"}, {"bleedHeal", "bleeding %+.0f%%"}, {"bullet", "bullets %+.0f%%"}};
    static const std::map<std::string, const char*> BORN = {{"thermal", "Born where the air burns."}, {"electric", "Grown in the arcs of an electric field."},
      {"gravity", "Pressed together where gravity has gone wrong."}, {"chemical", "Crystallised out of acid and gas."}, {"psy", "Found where the Zone gets into your head."}};
    for (const ZItem* z : allZoneItems()) {
      if (z->cat != "artifact" || !z->raw.contains("artifact")) continue;
      const json& a = z->raw["artifact"]; const std::string fam = a.value("family", "");
      std::string t = (BORN.count(fam) ? BORN.at(fam) : "") + std::string(" Tier ") + std::to_string(a.value("tier", 1)) + ". On the belt: ";
      bool first = true;
      const json fx = a.value("fx", json::object());
      for (auto& [k, v] : fx.items()) {
        if (!FX.count(k) || !v.is_number()) continue;
        const float x = v.get<float>() * (k == "carry" || k == "regen" ? 1.f : (k == "burn" || k == "shock" || k == "chem" || k == "psy" || k == "bullet" ? -100.f : 100.f));
        char b[64]; std::snprintf(b, sizeof b, FX.at(k), x); t += (first ? "" : ", ") + std::string(b); first = false;
      }
      const float rad = a.value("rad", 0.f);
      t += rad > 0 ? ". It gives off radiation." : rad < 0 ? ". It draws radiation out of you." : ".";
      char pr[48]; std::snprintf(pr, sizeof pr, " Worth about %.0f RU.", z->price); t += pr;
      g.push_back({"Artifacts", z->name, t});
    }
    return g;
  }();
  return G;
}

// ---------------------------------------------------------------- the screen
const char* TABS[] = {"Map", "Tasks", "Statistics", "Relations", "Contacts", "Guide", "Radio", "Messages"};
constexpr int NTABS = 8;
const char* FILTERS[] = {"Tasks", "Stashes", "Areas", "Important places"};

class PdaScreen : public Screen {
public:
  const char* id() const override { return "pda"; }
  bool pausesGame() const override { return false; }
  void update(App& a, double) override {
    if (inventoryClosePressed(a, true) || (a.input.pressed(Act::Pda) && frames > 2)) { a.popScreen(); return; }
    if (a.input.pressed(Act::MenuTabLeft)) tab = (tab + NTABS - 1) % NTABS;
    if (a.input.pressed(Act::MenuTabRight)) tab = (tab + 1) % NTABS;
    frames++;
  }
  void draw(App& a) override {
    GameSession* s = a.session && a.session->player ? a.session.get() : nullptr;
    if (!s) return;
    Player& p = *s->player; Level& lv = *s->level;
    float W = std::min(1100.f, a.width - 30.f), H = std::min(720.f, a.height - 30.f);
    ImVec2 at(a.width * 0.5f, a.height * 0.5f), piv(0.5f, 0.5f);
    if (zonePdaRectOn) { W = zonePdaRect[2] - zonePdaRect[0]; H = zonePdaRect[3] - zonePdaRect[1]; at = ImVec2(zonePdaRect[0], zonePdaRect[1]); piv = ImVec2(0, 0); }
    else if (frames < 12) return;   // the device is still coming up
    ImGui::SetNextWindowPos(at, ImGuiCond_Always, piv);
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, BG); ImGui::PushStyleColor(ImGuiCol_Border, FRAME2); ImGui::PushStyleColor(ImGuiCol_ChildBg, PANEL);
    ImGui::PushStyleColor(ImGuiCol_Header, SEL); ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(64, 72, 68, 255)); ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(74, 84, 78, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 5)); ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0); ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0);
    ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (tab == 0) fl |= ImGuiWindowFlags_NoNavInputs;   // (the map takes the sticks and the d-pad itself)
    ImGui::Begin("##pda", nullptr, fl);
    tabBar(a, lv);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
    switch (tab) {
      case 0: mapTab(a, *s, p, lv); break;
      case 1: tasksTab(a, p, lv); break;
      case 2: statsTab(p, lv); break;
      case 3: relationsTab(p); break;
      case 4: contactsTab(p, lv); break;
      case 5: guideTab(); break;
      case 6: radioTab(p, lv); break;
      default: messagesTab(lv); break;
    }
    ImGui::End();
    ImGui::PopStyleVar(3); ImGui::PopStyleColor(6);
  }
private:
  int tab = 0, frames = 0;
  // ---- the tab bar, the battery and the clock
  void tabBar(App& a, Level& lv) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetCursorScreenPos(); const float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetTextLineHeight() + 8;
    dl->AddRectFilled(o, o + ImVec2(w, h), IM_COL32(22, 25, 24, 255)); dl->AddRect(o, o + ImVec2(w, h), FRAME2);
    // the clock (0 = sunrise, 6:00) and the battery, on the right
    const double f = lv.dayFraction(); const int mins = ((int)(f * 1440) + 360) % 1440;
    char clk[16]; std::snprintf(clk, sizeof clk, "%02d:%02d", mins / 60, mins % 60);
    const float cw = ImGui::CalcTextSize(clk).x;
    dl->AddText(o + ImVec2(w - cw - 6, 4), TXT, clk);
    const ImVec2 b0 = o + ImVec2(w - cw - 40, 6), b1 = b0 + ImVec2(24, h - 12);
    dl->AddRect(b0, b1, DIMT, 1); dl->AddRectFilled(ImVec2(b1.x, b0.y + 3), ImVec2(b1.x + 2, b1.y - 3), DIMT);
    const int bars = 3 + (int)(std::fmod(a.time / 600.0, 1.0) < 0.5);
    for (int k = 0; k < bars; k++) dl->AddRectFilled(b0 + ImVec2(2 + k * 5.5f, 2), b0 + ImVec2(6 + k * 5.5f, h - 14), k == 0 && bars < 2 ? RED : GREEN);
    float x = o.x + 2; const float maxX = b0.x - 8;
    for (int i = 0; i < NTABS; i++) {
      const ImVec2 ts = ImGui::CalcTextSize(TABS[i]); const float tw = ts.x + 14;
      if (x + tw > maxX) break;
      const ImVec2 t0(x, o.y + 1), t1(x + tw, o.y + h - 1);
      const bool hov = ImGui::IsMouseHoveringRect(t0, t1);
      if (i == tab) { dl->AddRectFilled(t0, t1, SEL); dl->AddLine(ImVec2(t0.x, t1.y - 1), ImVec2(t1.x, t1.y - 1), HI, 2); }
      else if (hov) dl->AddRectFilled(t0, t1, IM_COL32(34, 38, 36, 255));
      dl->AddText(ImVec2(x + 7, o.y + 4), i == tab ? HI : hov ? TXT : DIMT, TABS[i]);
      dl->AddLine(ImVec2(t1.x, t0.y + 3), ImVec2(t1.x, t1.y - 3), FRAME2);
      if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) tab = i;
      x += tw;
    }
    ImGui::Dummy(ImVec2(w, h));
  }

  // ---------------------------------------------------------------- Map
  float mzoom = 0.3f; vec2 mpan{0}; bool mfollow = true, centred = false; int sel = 0, fcur = 0; bool filt[4] = {true, true, true, true};
  std::string focusName; vec2 focusAt{0};
  std::vector<Task> allTasks(Level& lv, const Player& p) { auto t = activeTasks(lv, p); for (auto& j : jobTasks(lv, p)) t.push_back(j); return t; }
  std::string tracked(const Player& p) { auto it = p.stats.find("zone"); return it != p.stats.end() && it->is_object() ? it->value("track", std::string()) : std::string(); }
  void track(Player& p, const Task& t) {
    p.stats["zone"]["track"] = t.id;
    if (t.hasTarget) { mpan = vec2(t.target.x, t.target.z); mfollow = false; focusName = t.where.empty() ? t.title : t.where; focusAt = mpan; }
  }
  void mapTab(App& a, GameSession& s, Player& p, Level& lv) {
    Input& in = a.input; ImGuiIO& io = ImGui::GetIO();
    const auto tasks = allTasks(lv, p);
    const std::string tr = tracked(p);
    sel = std::clamp(sel, 0, std::max(0, (int)tasks.size() - 1));
    // ---- the keys and the pad
    const bool kb = !in.textActive;
    if ((kb && in.keyPressed(SDL_SCANCODE_DOWN)) || in.padButtonPressed(13)) sel = std::min((int)tasks.size() - 1, sel + 1);
    if ((kb && in.keyPressed(SDL_SCANCODE_UP)) || in.padButtonPressed(12)) sel = std::max(0, sel - 1);
    if (((kb && in.keyPressed(SDL_SCANCODE_RETURN)) || in.padButtonPressed(0)) && !tasks.empty()) track(p, tasks[sel]);
    if ((kb && in.keyPressed(SDL_SCANCODE_SPACE)) || in.padButtonPressed(2) || in.padButtonPressed(11)) { mfollow = true; focusName.clear(); }
    if (in.padButtonPressed(14)) fcur = (fcur + 3) % 4;
    if (in.padButtonPressed(15)) fcur = (fcur + 1) % 4;
    if (in.padButtonPressed(3)) filt[fcur] = !filt[fcur];
    for (int k = 0; k < 4; k++) if (kb && in.keyPressed((SDL_Scancode)(SDL_SCANCODE_1 + k))) { filt[k] = !filt[k]; fcur = k; }
    float zin = 0; vec2 pan(0);
    for (auto& pd : in.pads()) {
      zin += (pd.rt - pd.lt) * io.DeltaTime * 1.8f;
      const vec2 st = pd.lstick + pd.rstick; if (glm::length(st) > 0.15f) { pan += st * (io.DeltaTime * 480.f / mzoom); mfollow = false; }
    }
    if (kb) {
      if (in.keyDown(SDL_SCANCODE_EQUALS) || in.keyDown(SDL_SCANCODE_KP_PLUS)) zin += io.DeltaTime * 1.6f;
      if (in.keyDown(SDL_SCANCODE_MINUS) || in.keyDown(SDL_SCANCODE_KP_MINUS)) zin -= io.DeltaTime * 1.6f;
      const vec2 k((in.keyDown(SDL_SCANCODE_D) ? 1.f : 0.f) - (in.keyDown(SDL_SCANCODE_A) ? 1.f : 0.f), (in.keyDown(SDL_SCANCODE_S) ? 1.f : 0.f) - (in.keyDown(SDL_SCANCODE_W) ? 1.f : 0.f));
      if (k != vec2(0)) { pan += k * (io.DeltaTime * 420.f / mzoom); mfollow = false; }
    }
    // ---- the left column: active tasks
    const float avW = ImGui::GetContentRegionAvail().x, avH = ImGui::GetContentRegionAvail().y;
    const float lw = std::clamp(avW * 0.27f, 150.f, 280.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 L0 = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(L0, L0 + ImVec2(lw, avH), PANEL); dl->AddRect(L0, L0 + ImVec2(lw, avH), FRAME2);
    dl->AddText(L0 + ImVec2(6, 4), HI, "Active tasks");
    dl->AddLine(L0 + ImVec2(4, 20), L0 + ImVec2(lw - 4, 20), FRAME2);
    float y = L0.y + 24;
    for (int i = 0; i < (int)tasks.size(); i++) {
      const Task& t = tasks[i];
      const float rh = ImGui::GetTextLineHeight() * 2 + 6;
      if (y + rh > L0.y + avH - 4) break;
      const ImVec2 r0(L0.x + 3, y), r1(L0.x + lw - 3, y + rh);
      const bool hov = ImGui::IsMouseHoveringRect(r0, r1), isTr = t.id == tr || (tr.empty() && i == 0);
      if (i == sel) dl->AddRectFilled(r0, r1, SEL); else if (hov) dl->AddRectFilled(r0, r1, IM_COL32(30, 34, 32, 255));
      if (i == sel) dl->AddRect(r0, r1, FRAME);
      dl->AddCircleFilled(r0 + ImVec2(7, 8), 3, isTr ? HI : DIMT);
      dl->PushClipRect(r0, r1, true);
      dl->AddText(r0 + ImVec2(14, 1), isTr ? HI : TXT, t.title.c_str());
      char sub[96];
      if (t.hasTarget) std::snprintf(sub, sizeof sub, "%s  %.0f m", t.where.c_str(), std::hypot(t.target.x - p.pos.x, t.target.z - p.pos.z));
      else std::snprintf(sub, sizeof sub, "%s", t.id.rfind("job:", 0) == 0 ? "fetch and return" : "");
      dl->AddText(r0 + ImVec2(14, 2 + ImGui::GetTextLineHeight()), DIMT, sub);
      dl->PopClipRect();
      if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { sel = i; track(p, t); }
      y += rh + 2;
    }
    if (tasks.empty()) dl->AddText(L0 + ImVec2(8, 28), DIMT, "None.");
    // ---- the filters across the top of the map
    const ImVec2 F0(L0.x + lw + 6, L0.y);
    const float mw = avW - lw - 6;
    float fx = F0.x; const float fh = ImGui::GetTextLineHeight() + 6;
    for (int k = 0; k < 4; k++) {
      const float tw = ImGui::CalcTextSize(FILTERS[k]).x + 24;
      const ImVec2 b0(fx, F0.y), b1(fx + tw, F0.y + fh);
      const bool hov = ImGui::IsMouseHoveringRect(b0, b1);
      dl->AddRectFilled(b0, b1, hov ? IM_COL32(34, 38, 36, 255) : PANEL); dl->AddRect(b0, b1, k == fcur && in.usingPad() ? HI : FRAME2);
      dl->AddRect(b0 + ImVec2(5, fh * 0.5f - 4), b0 + ImVec2(13, fh * 0.5f + 4), DIMT);
      if (filt[k]) dl->AddRectFilled(b0 + ImVec2(7, fh * 0.5f - 2), b0 + ImVec2(11, fh * 0.5f + 2), HI);
      dl->AddText(b0 + ImVec2(18, 3), filt[k] ? TXT : DIMT, FILTERS[k]);
      if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { filt[k] = !filt[k]; fcur = k; }
      fx += tw + 4;
    }
    // ---- the map
    const ImVec2 o(F0.x, F0.y + fh + 4), sz(mw, std::max(60.f, avH - fh - 4));
    ImGui::SetCursorScreenPos(o);
    ImGui::InvisibleButton("##map", sz);
    const bool hov = ImGui::IsItemHovered();
    const ImVec2 mid = o + sz * 0.5f;
    auto M = [&](float x, float z) { return ImVec2(mid.x + (x - mpan.x) * mzoom, mid.y + (z - mpan.y) * mzoom); };
    auto Wd = [&](ImVec2 q) { return vec2(mpan.x + (q.x - mid.x) / mzoom, mpan.y + (q.y - mid.y) / mzoom); };
    // the pad in the bottom right: zoom in / out, pan, back to you (held: keeps going)
    const float bs = 18; const ImVec2 pad0 = o + sz - ImVec2(bs * 3 + 14, bs * 3 + 14);
    int padHit = -1;
    for (int k = 0; k < 9; k++) {
      const ImVec2 b0 = pad0 + ImVec2((k % 3) * (bs + 1), (k / 3) * (bs + 1)), b1 = b0 + ImVec2(bs, bs);
      if (ImGui::IsMouseHoveringRect(b0, b1) && ImGui::IsMouseDown(ImGuiMouseButton_Left)) padHit = k;
    }
    if (padHit >= 0) {
      const float dt = io.DeltaTime, step = dt * 420.f / mzoom;
      switch (padHit) { case 0: zin += dt * 1.6f; break; case 2: zin -= dt * 1.6f; break; case 1: pan.y -= step; break; case 7: pan.y += step; break;
                        case 3: pan.x -= step; break; case 5: pan.x += step; break; case 4: mfollow = true; focusName.clear(); break; default: break; }
      if (padHit != 4 && padHit != 0 && padHit != 2) mfollow = false;
    }
    if (hov && io.MouseWheel != 0 && padHit < 0) {   // zoom about the cursor
      const vec2 before = Wd(io.MousePos);
      mzoom = std::clamp(mzoom * std::exp(io.MouseWheel * 0.18f), 0.012f, 3.f);
      mpan = before - vec2(io.MousePos.x - mid.x, io.MousePos.y - mid.y) / mzoom; mfollow = false;
    }
    mzoom = std::clamp(mzoom * std::exp(zin), 0.012f, 3.f);
    if (ImGui::IsItemActive() && padHit < 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2)) { pan -= vec2(io.MouseDelta.x, io.MouseDelta.y) / mzoom; mfollow = false; }
    if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { mfollow = true; focusName.clear(); }
    if (!centred) { centred = true; mzoom = 0.3f; }
    if (mfollow) mpan = vec2(p.pos.x, p.pos.z); else mpan += pan;
    mpan = glm::clamp(mpan, vec2(MIN_X, MIN_Z), vec2(MAX_X, MAX_Z));
    dl->PushClipRect(o, o + sz, true);
    dl->AddRectFilled(o, o + sz, IM_COL32(24, 28, 22, 255));
    const u32 seed = (u32)lv.info.seed;
    if (const unsigned t = mapTexture(seed)) {
      const int tw = (MAX_X - MIN_X) / MAP_S * MAP_S, th = (MAX_Z - MIN_Z) / MAP_S * MAP_S;
      dl->AddImage((ImTextureID)(intptr_t)t, M((float)MIN_X, (float)MIN_Z), M((float)(MIN_X + tw), (float)(MIN_Z + th)));
    } else dl->AddText(o + ImVec2(10, 10), DIMT, "Charting the Zone\xE2\x80\xA6");
    if (mzoom > 0.45f) { int ox, oz; if (const unsigned t = detailTexture((u32)lv.info.seed, mpan, ox, oz))
      dl->AddImage((ImTextureID)(intptr_t)t, M((float)ox, (float)oz), M((float)(ox + DET_N * DET_S), (float)(oz + DET_N * DET_S))); }
    const float lw2 = std::clamp(mzoom * 5, 1.f, 3.f);
    for (const Line& l : rivers()) for (size_t i = 0; i + 1 < l.pts.size(); i++) dl->AddLine(M(l.pts[i].x, l.pts[i].z), M(l.pts[i + 1].x, l.pts[i + 1].z), IM_COL32(34, 52, 60, 230), std::max(1.5f, l.width * mzoom));
    if (mzoom > 0.05f) for (const Line& l : roads()) for (size_t i = 0; i + 1 < l.pts.size(); i++)
      dl->AddLine(M(l.pts[i].x, l.pts[i].z), M(l.pts[i + 1].x, l.pts[i + 1].z), l.kind == 3 ? IM_COL32(70, 62, 58, 220) : l.kind == 1 ? IM_COL32(150, 148, 140, 210) : IM_COL32(140, 124, 98, 170), l.kind == 1 ? lw2 * 1.3f : lw2);
    const Plan& P = zonePlan(seed);
    const vec2 v0 = Wd(o), v1 = Wd(o + sz);
    if (mzoom > 0.12f) {   // the buildings' footprints in roof greys, close up
      for (const Placed& b : P.s) {
        if (b.x0 > v1.x || b.x0 + b.W < v0.x || b.z0 > v1.y || b.z0 + b.D < v0.y) continue;
        const int g = 84 + (int)(hash32((i32)b.seed, b.x0, b.z0, 3) % 40);
        const ImVec2 q0 = M((float)b.x0, (float)b.z0), q1 = M((float)(b.x0 + b.W), (float)(b.z0 + b.D));
        dl->AddRectFilled(q0 + ImVec2(1.5f, 1.5f), q1 + ImVec2(1.5f, 1.5f), IM_COL32(10, 12, 10, 120));   // its shadow
        dl->AddRectFilled(q0, q1, b.ruin > 0.5f ? IM_COL32(g - 14, g - 18, g - 22, 240) : IM_COL32(g, g - 2, g - 6, 245));
        if (mzoom > 0.5f) dl->AddLine(ImVec2(q0.x, (q0.y + q1.y) * 0.5f), ImVec2(q1.x, (q0.y + q1.y) * 0.5f), IM_COL32(g + 18, g + 16, g + 12, 200));   // the ridge
        dl->AddRect(q0, q1, IM_COL32(30, 30, 28, 200));
      }
    }
    // areas: the anomaly fields you have been near, the task areas
    const json zj = p.stats.value("zone", json::object());
    if (filt[2]) {
      static const ImU32 FC[5] = {IM_COL32(240, 120, 50, 200), IM_COL32(120, 170, 255, 200), IM_COL32(190, 140, 255, 200), IM_COL32(170, 230, 80, 200), IM_COL32(230, 100, 200, 200)};
      for (auto& fi : zj.value("fields", json::array())) {
        const int k = fi.get<int>(); if (k < 0 || k >= (int)P.fields.size()) continue;
        const AnomalyField& f = P.fields[k]; const ImVec2 c = M((float)f.x, (float)f.z); const float r = std::max(5.f, f.r * mzoom);
        dl->AddCircleFilled(c, r, (FC[f.family % 5] & 0x00FFFFFF) | 0x30000000); dl->AddCircle(c, r, FC[f.family % 5], 24, 1.2f);
        dl->AddTriangleFilled(c + ImVec2(0, -4), c + ImVec2(4, 3), c + ImVec2(-4, 3), FC[f.family % 5]);
      }
      for (const Task& t : tasks) if (t.hasTarget && t.id.rfind("job:", 0) == 0)
        for (auto& j : zj.value("jobs", json::array())) if ("job:" + j.value("key", std::string()) == t.id && j.value("state", "") == "go" && j.value("r", 0.f) > 20) {
          const float r = j.value("r", 150.f) * mzoom; const ImVec2 c = M(t.target.x, t.target.z);
          dl->AddCircleFilled(c, r, IM_COL32(232, 196, 96, 34)); dl->AddCircle(c, r, IM_COL32(232, 196, 96, 170), 40, 1.5f);
        }
    }
    // important places: bases, camps, hubs, the region names
    struct Spot { ImVec2 c; std::string name, sub; };
    std::vector<Spot> spots;
    if (filt[3]) {
      for (const Smart& m : P.smarts) {
        if (m.name.empty() || (m.kind != SM_BASE && m.kind != SM_HUB && !(mzoom > 0.08f && m.kind == SM_CAMP) && !(mzoom > 0.2f && (m.kind == SM_RUIN || m.kind == SM_POST)))) continue;
        const ImVec2 c = M((float)m.x, (float)m.z);
        if (c.x < o.x - 20 || c.y < o.y - 20 || c.x > o.x + sz.x + 20 || c.y > o.y + sz.y + 20) continue;
        if (m.kind == SM_HUB || m.kind == SM_BASE) {
          const ImU32 col = m.faction < F_MUTANT ? (attitude(p, m.faction) < 0 ? RED : attitude(p, m.faction) > 0 ? GREEN : TXT) : TXT;
          dl->AddQuadFilled(c + ImVec2(0, -5), c + ImVec2(5, 0), c + ImVec2(0, 5), c + ImVec2(-5, 0), col); dl->AddQuad(c + ImVec2(0, -5), c + ImVec2(5, 0), c + ImVec2(0, 5), c + ImVec2(-5, 0), IM_COL32(0, 0, 0, 220));
        } else dl->AddCircleFilled(c, 2.5f, IM_COL32(220, 216, 200, 200));
        const char* kind = m.kind == SM_HUB ? "trading post" : m.kind == SM_BASE ? "base" : m.kind == SM_CAMP ? "camp" : m.kind == SM_POST ? "post" : "ruin";
        spots.push_back({c, m.name, std::string(m.faction < F_MUTANT ? factionName(m.faction) : "") + (m.faction < F_MUTANT ? " " : "") + kind + ", danger " + std::to_string(m.danger)});
      }
      std::vector<std::pair<ImVec2, ImVec2>> used;   // (names that would overlap one already drawn are left out)
      for (const Region& r : regions()) {
        if (mzoom > 0.35f || (mzoom < 0.06f && r.danger < 3)) continue;
        const ImVec2 c = M((float)r.cx, (float)r.cz); const ImVec2 ts = ImGui::CalcTextSize(r.name), a0 = c - ts * 0.5f - ImVec2(4, 2), a1 = c + ts * 0.5f + ImVec2(4, 2);
        bool clash = false; for (auto& [u0, u1] : used) if (a0.x < u1.x && a1.x > u0.x && a0.y < u1.y && a1.y > u0.y) clash = true;
        if (clash) continue;
        used.push_back({a0, a1});
        dl->AddText(c - ts * 0.5f + ImVec2(1, 1), IM_COL32(0, 0, 0, 230), r.name); dl->AddText(c - ts * 0.5f, IM_COL32(236, 228, 200, 235), r.name);
      }
    }
    // stashes
    if (filt[1]) for (const StashView& st : knownStashes(p)) {
      const ImVec2 c = M(st.pos.x, st.pos.z); const ImU32 col = st.found ? DIMT : CYAN;
      dl->AddRectFilled(c - ImVec2(4, 3), c + ImVec2(4, 3), col); dl->AddRect(c - ImVec2(4, 3), c + ImVec2(4, 3), IM_COL32(0, 0, 0, 230)); dl->AddLine(c - ImVec2(4, 0), c + ImVec2(4, 0), IM_COL32(0, 0, 0, 200));
      spots.push_back({c, st.name, st.found ? "stash (emptied?)" : "stash"});
    }
    // squads: only close by and close up (A-Life knows them all; your PDA only hears the ones near you)
    if (mzoom > 0.25f) for (const SquadView& q : alifeSquads(lv)) {
      if (q.members <= 0 || std::hypot(q.pos.x - p.pos.x, q.pos.z - p.pos.z) > (q.faction == F_MUTANT ? 120.f : 250.f)) continue;
      const ImU32 c = q.faction == F_MUTANT ? IM_COL32(210, 60, 50, 230) : attitude(p, q.faction) < 0 ? IM_COL32(230, 130, 40, 235) : IM_COL32(120, 210, 120, 235);
      dl->AddCircleFilled(M(q.pos.x, q.pos.z), 3, c); dl->AddCircle(M(q.pos.x, q.pos.z), 3, IM_COL32(0, 0, 0, 200));
    }
    // tasks
    if (filt[0]) for (int i = 0; i < (int)tasks.size(); i++) if (tasks[i].hasTarget) {
      const Task& t = tasks[i]; const ImVec2 c = M(t.target.x, t.target.z);
      const bool isTr = t.id == tr || (tr.empty() && i == 0);
      const float pulse = isTr ? 7 + 2 * std::sin((float)ImGui::GetTime() * 4) : 6;
      const ImU32 col = isTr ? HI : IM_COL32(200, 170, 90, 200);
      dl->AddCircle(c, pulse, col, 16, 2); dl->AddLine(c + ImVec2(-4, 0), c + ImVec2(4, 0), col, 2); dl->AddLine(c + ImVec2(0, -4), c + ImVec2(0, 4), col, 2);
      spots.push_back({c, t.title, t.where});
      if (isTr && p.realm == REALM_MAIN) dl->AddLine(M(p.pos.x, p.pos.z), c, IM_COL32(232, 196, 96, 70), 1);
    }
    // you
    if (p.realm == REALM_MAIN) {
      const ImVec2 c = M(p.pos.x, p.pos.z); const vec3 Lk = lookVector(p.yaw, 0);
      dl->AddCircle(c, 10, IM_COL32(255, 240, 200, 110), 20, 1.5f);
      dl->AddTriangleFilled(c + ImVec2(Lk.x, Lk.z) * 9, c + ImVec2(-Lk.z, Lk.x) * 5, c + ImVec2(Lk.z, -Lk.x) * 5, IM_COL32(255, 244, 210, 255));
      dl->AddTriangle(c + ImVec2(Lk.x, Lk.z) * 9, c + ImVec2(-Lk.z, Lk.x) * 5, c + ImVec2(Lk.z, -Lk.x) * 5, IM_COL32(0, 0, 0, 220));
    }
    // the label: the place under the cursor, else the selected task's
    const Spot* lab = nullptr; float bd = 14 * 14;
    if (hov) for (const Spot& sp : spots) { const ImVec2 d = sp.c - io.MousePos; const float dd = d.x * d.x + d.y * d.y; if (dd < bd) { bd = dd; lab = &sp; } }
    if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && lab) { focusName = lab->name; focusAt = Wd(lab->c); }
    Spot fs;
    if (!lab && !focusName.empty()) { fs = {M(focusAt.x, focusAt.y), focusName, ""}; for (const Spot& sp : spots) if (sp.name == focusName) fs.sub = sp.sub; lab = &fs; }
    if (!lab && hov) { const vec2 w = Wd(io.MousePos); fs = {io.MousePos, regionName((int)w.x, (int)w.y), ""}; if (const Region* r = regionAt((int)w.x, (int)w.y)) fs.sub = "danger " + std::to_string(r->danger) + "/5"; lab = &fs; }
    if (lab) {
      const ImVec2 t1 = ImGui::CalcTextSize(lab->name.c_str()), t2 = ImGui::CalcTextSize(lab->sub.c_str());
      const ImVec2 b0 = lab->c + ImVec2(10, -t1.y - 6), b1 = b0 + ImVec2(std::max(t1.x, t2.x) + 10, t1.y + (lab->sub.empty() ? 0 : t2.y) + 6);
      dl->AddRectFilled(b0, b1, IM_COL32(12, 14, 13, 225)); dl->AddRect(b0, b1, FRAME);
      dl->AddText(b0 + ImVec2(5, 3), HI, lab->name.c_str()); if (!lab->sub.empty()) dl->AddText(b0 + ImVec2(5, 3 + t1.y), DIMT, lab->sub.c_str());
    }
    // the scale, the pad, the scrollbars, the frame
    { float len = 100 * mzoom; const char* l = "100 m"; if (len < 30) { len = 1000 * mzoom; l = "1 km"; } if (len > sz.x * 0.4f) { len = 10 * mzoom; l = "10 m"; }
      const ImVec2 b0 = o + ImVec2(10, sz.y - 16); dl->AddLine(b0, b0 + ImVec2(len, 0), TXT, 2); dl->AddLine(b0 - ImVec2(0, 4), b0 + ImVec2(0, 3), TXT, 2); dl->AddLine(b0 + ImVec2(len, -4), b0 + ImVec2(len, 3), TXT, 2);
      dl->AddText(b0 + ImVec2(len + 6, -8), TXT, l); }
    static const char* PADA[9] = {"+", "^", "-", "<", "o", ">", "", "v", ""};
    for (int k = 0; k < 9; k++) {
      if (!PADA[k][0]) continue;
      const ImVec2 b0 = pad0 + ImVec2((k % 3) * (bs + 1), (k / 3) * (bs + 1)), b1 = b0 + ImVec2(bs, bs);
      dl->AddRectFilled(b0, b1, padHit == k ? SEL : IM_COL32(16, 18, 17, 220)); dl->AddRect(b0, b1, FRAME);
      const char* g = PADA[k];
      const ImVec2 ts = ImGui::CalcTextSize(g); dl->AddText(b0 + (ImVec2(bs, bs) - ts) * 0.5f, TXT, g);
    }
    {   // scrollbars: where the view sits on the whole map; click or drag along one to go there
      const float sb = 5; const ImVec2 hb0(o.x, o.y + sz.y - sb), hb1(o.x + sz.x - sb, o.y + sz.y), vb0(o.x + sz.x - sb, o.y), vb1(o.x + sz.x, o.y + sz.y - sb);
      dl->AddRectFilled(hb0, hb1, IM_COL32(20, 22, 21, 230)); dl->AddRectFilled(vb0, vb1, IM_COL32(20, 22, 21, 230));
      const float mx0 = (v0.x - MIN_X) / (MAX_X - MIN_X), mx1 = (v1.x - MIN_X) / (MAX_X - MIN_X), mz0 = (v0.y - MIN_Z) / (MAX_Z - MIN_Z), mz1 = (v1.y - MIN_Z) / (MAX_Z - MIN_Z);
      const float hw = hb1.x - hb0.x, vh = vb1.y - vb0.y;
      dl->AddRectFilled(ImVec2(hb0.x + hw * std::clamp(mx0, 0.f, 1.f), hb0.y + 1), ImVec2(hb0.x + hw * std::clamp(mx1, 0.f, 1.f), hb1.y - 1), FRAME);
      dl->AddRectFilled(ImVec2(vb0.x + 1, vb0.y + vh * std::clamp(mz0, 0.f, 1.f)), ImVec2(vb1.x - 1, vb0.y + vh * std::clamp(mz1, 0.f, 1.f)), FRAME);
      if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::IsWindowFocused()) {
        const ImVec2 m = io.MousePos;
        if (m.x >= hb0.x && m.x < hb1.x && m.y >= hb0.y - 3 && m.y < hb1.y + 1) { mpan.x = MIN_X + (m.x - hb0.x) / hw * (MAX_X - MIN_X); mfollow = false; }
        if (m.x >= vb0.x - 3 && m.x < vb1.x + 1 && m.y >= vb0.y && m.y < vb1.y) { mpan.y = MIN_Z + (m.y - vb0.y) / vh * (MAX_Z - MIN_Z); mfollow = false; }
      }
    }
    if (const Region* r = p.realm == REALM_MAIN ? regionAt(ifloor(p.pos.x), ifloor(p.pos.z)) : nullptr) {
      char b[160]; std::snprintf(b, sizeof b, "%s  \xC2\xB7  danger %d/5", r->name, r->danger);
      dl->AddRectFilled(o + ImVec2(5, 5), o + ImVec2(13, 11) + ImGui::CalcTextSize(b), IM_COL32(0, 0, 0, 160)); dl->AddText(o + ImVec2(9, 7), HI, b);
    }
    const char* keys = in.usingPad() ? "Sticks move  LT/RT zoom  D-pad tasks  A track  X centre  Y filter" : "Drag / WASD move  Wheel / +- zoom  Up/Down, Enter: track  Space: centre";
    dl->AddText(o + ImVec2(10, sz.y - 34), IM_COL32(170, 174, 166, 200), keys);
    dl->AddRect(o, o + sz, FRAME);
    dl->PopClipRect();
    (void)s;
  }

  // ---------------------------------------------------------------- Tasks
  int tsel = 0;
  void tasksTab(App& a, Player& p, Level& lv) {
    const auto tasks = allTasks(lv, p); const std::string tr = tracked(p);
    const float lw = std::clamp(ImGui::GetContentRegionAvail().x * 0.36f, 160.f, 320.f);
    ImGui::BeginChild("##tl", ImVec2(lw, 0), ImGuiChildFlags_Borders);
    for (int i = 0; i < (int)tasks.size(); i++) {
      const bool isTr = tasks[i].id == tr || (tr.empty() && i == 0);
      if (isTr) ImGui::PushStyleColor(ImGuiCol_Text, V_HI);
      if (ImGui::Selectable((tasks[i].title + "##t" + std::to_string(i)).c_str(), tsel == i)) tsel = i;
      if (isTr) ImGui::PopStyleColor();
      if (ImGui::IsItemFocused()) tsel = i;
    }
    if (tasks.empty()) ImGui::TextDisabled("No tasks.");
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##td", ImVec2(0, 0), ImGuiChildFlags_Borders);
    padScrollHere();
    tsel = std::clamp(tsel, 0, std::max(0, (int)tasks.size() - 1));
    if (!tasks.empty()) {
      const Task& t = tasks[tsel];
      ImGui::TextColored(V_HI, "%s", t.title.c_str());
      ImGui::Separator();
      ImGui::PushTextWrapPos(); ImGui::TextUnformatted(t.text.c_str()); ImGui::PopTextWrapPos();
      if (t.hasTarget) ImGui::TextDisabled("%s, %.0f m away", t.where.c_str(), std::hypot(t.target.x - p.pos.x, t.target.z - p.pos.z));
      ImGui::Spacing();
      if (button(t.id == tr ? "Tracked" : "Track on the compass", t.id == tr ? BTN_PRIMARY : BTN_NORMAL, true) || a.input.padButtonPressed(3)) { p.stats["zone"]["track"] = t.id; }
      if (t.id.rfind("job:", 0) == 0) {
        ImGui::SameLine();
        if (button("Drop the job", BTN_DANGER, true)) {
          const json& J = p.stats["zone"]["jobs"];
          for (int k = 0; k < (int)J.size(); k++) if ("job:" + J[k].value("key", std::string()) == t.id) { dropJob(p, k); break; }
        }
      }
    }
    ImGui::Separator();
    const Emission em = emission(lv);
    ImGui::TextDisabled(em.phase == 0 ? "Next emission in about %.0f minutes" : "EMISSION: stay under cover (%.0f s)", em.phase == 0 ? em.left / 60 : em.left);
    ImGui::EndChild();
  }

  // ---------------------------------------------------------------- Statistics
  void statsTab(Player& p, Level& lv) {
    const json Z = p.stats.value("zone", json::object());
    ImGui::BeginChild("##st", ImVec2(0, 0), ImGuiChildFlags_Borders);
    padScrollHere();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto barRow = [&](const char* n, float v, float mx, ImU32 c, const char* unit) {
      ImGui::TextUnformatted(n); ImGui::SameLine(110);
      const ImVec2 q = ImGui::GetCursorScreenPos(); const float w = std::min(220.f, ImGui::GetContentRegionAvail().x * 0.5f), h = ImGui::GetTextLineHeight() - 4;
      dl->AddRectFilled(q + ImVec2(0, 2), q + ImVec2(w, 2 + h), IM_COL32(30, 32, 30, 255));
      dl->AddRectFilled(q + ImVec2(1, 3), q + ImVec2(1 + (w - 2) * std::clamp(v / mx, 0.f, 1.f), 1 + h), c); dl->AddRect(q + ImVec2(0, 2), q + ImVec2(w, 2 + h), FRAME2);
      ImGui::Dummy(ImVec2(w, h + 2)); ImGui::SameLine(); ImGui::TextDisabled("%.0f%s", v, unit);
    };
    ImGui::TextColored(V_HI, "Body");
    barRow("Health", p.hp / std::max(1.f, p.maxHp) * 100, 100, IM_COL32(180, 50, 40, 255), " %");
    barRow("Radiation", zget(p, "rad"), 100, IM_COL32(210, 200, 60, 255), "");
    barRow("Food", zget(p, "food", 100), 100, IM_COL32(170, 140, 80, 255), "");
    barRow("Water", zget(p, "water", 100), 100, IM_COL32(80, 140, 200, 255), "");
    barRow("Sleep", zget(p, "sleep", 100), 100, IM_COL32(130, 120, 190, 255), "");
    barRow("Psy", zget(p, "psy", 100), 100, IM_COL32(190, 90, 200, 255), "");
    barRow("Stamina", zget(p, "stamina", 100), 100, IM_COL32(200, 190, 110, 255), "");
    ImGui::TextDisabled("Carrying %.1f / %.1f kg", carryWeight(p), carryCap(p));
    ImGui::Separator();
    ImGui::TextColored(V_HI, "Record");
    const json K = Z.value("kills", json::object());
    const i64 days = lv.info.day - Z.value("day0", lv.info.day);
    ImGui::Text("Faction"); ImGui::SameLine(160); ImGui::TextDisabled("%s", factionName(faction(p)));
    ImGui::Text("Money"); ImGui::SameLine(160); ImGui::TextDisabled("%.0f RU", money(p));
    ImGui::Text("Days in the Zone"); ImGui::SameLine(160); ImGui::TextDisabled("%lld", (long long)days + 1);
    ImGui::Text("Stalkers killed"); ImGui::SameLine(160); ImGui::TextDisabled("%d", K.value("stalkers", 0));
    ImGui::Text("Mutants killed"); ImGui::SameLine(160); ImGui::TextDisabled("%d", K.value("mutants", 0));
    ImGui::Text("Artifacts found"); ImGui::SameLine(160); ImGui::TextDisabled("%d", Z.value("artFound", 0));
    ImGui::Text("Stashes found"); ImGui::SameLine(160); ImGui::TextDisabled("%d", Z.value("stashFound", 0));
    ImGui::Text("Jobs done"); ImGui::SameLine(160); ImGui::TextDisabled("%d", Z.value("jobsDoneN", 0));
    std::string by;
    for (int f = 0; f < F_MUTANT; f++) if (const int n = K.value(factionKey(f), 0)) by += (by.empty() ? "" : ", ") + std::string(factionName(f)) + " " + std::to_string(n);
    if (!by.empty()) { ImGui::PushTextWrapPos(); ImGui::TextDisabled("Killed: %s", by.c_str()); ImGui::PopTextWrapPos(); }
    ImGui::EndChild();
  }

  // ---------------------------------------------------------------- Relations
  void relationsTab(Player& p) {
    ImGui::BeginChild("##rel", ImVec2(0, 0), ImGuiChildFlags_Borders);
    padScrollHere();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::TextColored(V_HI, "Standing with you"); ImGui::SameLine(); ImGui::TextDisabled("  (jobs raise it; killing their people lowers it, killing their enemies raises it)");
    ImGui::Separator();
    for (int f = 0; f < F_MUTANT; f++) {
      const int st = standing(p, f), at = attitude(p, f);
      ImGui::TextUnformatted(factionName(f)); ImGui::SameLine(120);
      const ImVec2 q = ImGui::GetCursorScreenPos(); const float w = std::min(260.f, ImGui::GetContentRegionAvail().x * 0.5f), h = ImGui::GetTextLineHeight() - 4;
      const float midx = q.x + w * 0.5f, vx = midx + w * 0.5f * st / 1000.f;
      dl->AddRectFilled(q + ImVec2(0, 2), q + ImVec2(w, 2 + h), IM_COL32(30, 32, 30, 255));
      dl->AddRectFilled(ImVec2(std::min(midx, vx), q.y + 3), ImVec2(std::max(midx, vx), q.y + 1 + h), at < 0 ? RED : at > 0 ? GREEN : IM_COL32(170, 170, 150, 255));
      for (float tq : {(float)REP_HOSTILE, (float)REP_FRIENDLY}) dl->AddLine(ImVec2(midx + w * 0.5f * tq / 1000.f, q.y + 1), ImVec2(midx + w * 0.5f * tq / 1000.f, q.y + 3 + h), FRAME);
      dl->AddRect(q + ImVec2(0, 2), q + ImVec2(w, 2 + h), FRAME2);
      ImGui::Dummy(ImVec2(w, h + 2)); ImGui::SameLine();
      ImGui::TextColored(at < 0 ? V_RED : at > 0 ? V_GREEN : V_DIM, "%s  %+d", at < 0 ? "Enemy" : at > 0 ? "Friend" : "Neutral", st);
    }
    ImGui::Separator();
    ImGui::TextDisabled("Enemies shoot on sight and their traders won't deal with you. Friends talk, trade and give work.");
    ImGui::EndChild();
  }

  // ---------------------------------------------------------------- Contacts
  void contactsTab(Player& p, Level& lv) {
    ImGui::BeginChild("##contacts", ImVec2(0, 0), ImGuiChildFlags_Borders);
    padScrollHere();
    ImGui::TextColored(V_HI, "Stalkers on the network (within a kilometre)");
    ImGui::Separator();
    int n = 0;
    for (const SquadView& q : alifeSquads(lv)) {
      if (q.faction == F_MUTANT || q.members <= 0) continue;
      const float d = std::hypot(q.pos.x - p.pos.x, q.pos.z - p.pos.z);
      if (d > 1000) continue;
      const int at = attitude(p, q.faction);
      ImGui::TextColored(at < 0 ? V_RED : at > 0 ? V_GREEN : ImVec4(0.8f, 0.8f, 0.7f, 1), "%-12s x%d", factionName(q.faction), q.members);
      ImGui::SameLine(170); ImGui::TextDisabled("%s, %.0f m", q.goal.empty() ? "on the move" : q.goal.c_str(), d);
      if (++n >= 40) break;
    }
    if (!n) ImGui::TextDisabled("Nobody within range.");
    ImGui::EndChild();
  }

  // ---------------------------------------------------------------- Guide
  int gsel = 0;
  void guideTab() {
    const auto& G = guide();
    const float lw = std::clamp(ImGui::GetContentRegionAvail().x * 0.32f, 150.f, 280.f);
    ImGui::BeginChild("##gl", ImVec2(lw, 0), ImGuiChildFlags_Borders);
    std::string head;
    for (int i = 0; i < (int)G.size(); i++) {
      if (G[i].head != head) { head = G[i].head; ImGui::TextColored(V_HI, "%s", head.c_str()); }
      if (ImGui::Selectable(("  " + G[i].name + "##g" + std::to_string(i)).c_str(), gsel == i)) gsel = i;
      if (ImGui::IsItemFocused()) gsel = i;
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##gd", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (gsel >= 0 && gsel < (int)G.size()) {
      ImGui::TextColored(V_HI, "%s", G[gsel].name.c_str()); ImGui::SameLine(); ImGui::TextDisabled("  %s", G[gsel].head.c_str());
      ImGui::Separator();
      ImGui::PushTextWrapPos(); ImGui::TextUnformatted(G[gsel].text.c_str()); ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
  }

  // ---------------------------------------------------------------- Radio
  void radioTab(Player& p, Level& lv) {
    ImGui::BeginChild("##radio", ImVec2(0, 0), ImGuiChildFlags_Borders);
    const int st = p.stats.contains("zone") ? p.stats["zone"].value("radio", -1) : -1;
    const char* names[] = {"Zone FM \xE2\x80\x94 the Furnace's band", "Campfire \xE2\x80\x94 an old six-string", "Emergency band \xE2\x80\x94 news and warnings"};
    for (int i = 0; i < 3; i++) if (button((std::string(st == i ? "\xE2\x96\xB6 " : "   ") + names[i] + "##st" + std::to_string(i)).c_str(), st == i ? BTN_PRIMARY : BTN_NORMAL, false, -1)) p.stats["zone"]["radio"] = st == i ? -1 : i;
    if (button("Radio off", BTN_GHOST, true)) p.stats["zone"]["radio"] = -1;
    ImGui::Separator();
    if (st == 2) {
      const Emission em = emission(lv);
      ImGui::TextColored(V_RED, em.phase == 0 ? "Next emission in about %.0f min." : "EMISSION \xE2\x80\x94 %.0f s. Get underground.", em.phase == 0 ? em.left / 60 : em.left);
      auto news = pdaNews(lv);
      for (auto it = news.rbegin(); it != news.rend() && it - news.rbegin() < 8; ++it) { ImGui::PushTextWrapPos(); ImGui::TextUnformatted(it->c_str()); ImGui::PopTextWrapPos(); }
    } else ImGui::TextDisabled(st < 0 ? "The radio is off. In the Furnace bar and the camps' hubs a radio plays anyway." : "Playing. Close the PDA and it keeps playing.");
    ImGui::EndChild();
  }

  // ---------------------------------------------------------------- Messages
  void messagesTab(Level& lv) {
    auto news = pdaNews(lv);
    ImGui::BeginChild("##news", ImVec2(0, 0), ImGuiChildFlags_Borders);
    padScrollHere();
    if (news.empty()) ImGui::TextDisabled("Nothing on the network yet.");
    int k = 0;
    for (auto it = news.rbegin(); it != news.rend(); ++it, ++k) {
      ImGui::TextColored(V_DIM, "#%d", (int)news.size() - k); ImGui::SameLine(44);
      ImGui::PushTextWrapPos(); ImGui::TextUnformatted(it->c_str()); ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
  }
};
} // namespace

std::unique_ptr<Screen> makeZonePdaScreen() { return std::make_unique<PdaScreen>(); }

} // namespace dq
