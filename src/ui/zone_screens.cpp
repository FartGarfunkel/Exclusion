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
#include "gen/zone_plan.h"
#include "core/registry.h"
#include <imgui.h>
#include <cmath>
#include <algorithm>
#include <map>

namespace dq {

bool inventoryClosePressed(App& a, bool tabScreen);   // ui/ui_main.cpp

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
class PdaScreen : public Screen {
public:
  const char* id() const override { return "pda"; }
  bool pausesGame() const override { return false; }
  void update(App& a, double) override {
    if (inventoryClosePressed(a, true) || (a.input.pressed(Act::Pda) && frames > 2)) { a.popScreen(); return; }
    if (a.input.pressed(Act::MenuTabLeft)) tab = (tab + 2) % 3;
    if (a.input.pressed(Act::MenuTabRight)) tab = (tab + 1) % 3;
    frames++;
  }
  void draw(App& a) override {
    GameSession* s = a.session && a.session->player ? a.session.get() : nullptr;
    if (!s) return;
    Player& p = *s->player; Level& lv = *s->level;
    const float W = std::min(1100.f, a.width - 30.f), H = std::min(720.f, a.height - 30.f);
    ImGui::SetNextWindowPos(ImVec2(a.width * 0.5f, a.height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::Begin("##pda", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::TextColored(ImVec4(0.89f, 0.72f, 0.3f, 1), "PDA");
    ImGui::SameLine(); ImGui::TextDisabled("  %s  \xC2\xB7  %s  \xC2\xB7  %s", regionName(ifloor(p.pos.x), ifloor(p.pos.z)).c_str(), factionName(faction(p)), fmtRu(money(p)).c_str());
    const char* tabs[] = {"Map", "Tasks", "News"};
    for (int i = 0; i < 3; i++) { if (i) ImGui::SameLine(); if (button(tabs[i], tab == i ? BTN_PRIMARY : BTN_NORMAL, true)) tab = i; }
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
  void drawMap(GameSession& s, Player& p, Level& lv) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float mapH = avail.y - 4, sc = mapH / float(MAX_Z - MIN_Z), mapW = (MAX_X - MIN_X) * sc;
    ImVec2 o = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(mapW, mapH));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto M = [&](float x, float z) { return ImVec2(o.x + (x - MIN_X) * sc, o.y + (z - MIN_Z) * sc); };
    dl->AddRectFilled(o, o + ImVec2(mapW, mapH), IM_COL32(38, 44, 30, 255));
    for (const Region& r : regions()) {
      static const ImU32 D[6] = {0, IM_COL32(80, 110, 60, 120), IM_COL32(110, 110, 50, 120), IM_COL32(130, 100, 40, 130), IM_COL32(140, 70, 40, 140), IM_COL32(150, 40, 40, 150)};
      dl->AddCircleFilled(M((float)r.cx, (float)r.cz), r.r * sc, D[std::clamp(r.danger, 1, 5)], 40);
    }
    for (const Line& l : rivers()) for (size_t i = 0; i + 1 < l.pts.size(); i++) dl->AddLine(M(l.pts[i].x, l.pts[i].z), M(l.pts[i + 1].x, l.pts[i + 1].z), IM_COL32(60, 90, 120, 255), std::max(1.5f, l.width * sc));
    for (const Line& l : roads()) for (size_t i = 0; i + 1 < l.pts.size(); i++) dl->AddLine(M(l.pts[i].x, l.pts[i].z), M(l.pts[i + 1].x, l.pts[i + 1].z), l.kind == 3 ? IM_COL32(120, 100, 90, 255) : l.kind == 1 ? IM_COL32(170, 165, 150, 255) : IM_COL32(120, 110, 80, 200), l.kind == 1 ? 2.f : 1.f);
    for (const Region& r : regions()) { ImVec2 c = M((float)r.cx, (float)r.cz); ImVec2 ts = ImGui::CalcTextSize(r.name); dl->AddText(c - ts * 0.5f + ImVec2(1, 1), IM_COL32(0, 0, 0, 220), r.name); dl->AddText(c - ts * 0.5f, INK, r.name); }
    // squads near you that you'd know of: your own faction anywhere, anyone within a kilometre
    for (const SquadView& q : alifeSquads(lv)) {
      const float d = std::hypot(q.pos.x - p.pos.x, q.pos.z - p.pos.z);
      if (q.faction == F_MUTANT ? d > 400 : (q.faction != faction(p) && d > 1000)) continue;
      const ImU32 c = q.faction == F_MUTANT ? IM_COL32(200, 60, 60, 220) : factionRelation(q.faction, faction(p)) < 0 ? IM_COL32(230, 120, 40, 230) : IM_COL32(120, 200, 120, 230);
      dl->AddCircleFilled(M(q.pos.x, q.pos.z), 3, c);
    }
    for (const Task& t : activeTasks(lv, p)) if (t.hasTarget) { ImVec2 c = M(t.target.x, t.target.z); dl->AddCircle(c, 7, AMBER, 12, 2); dl->AddLine(c + ImVec2(-4, 0), c + ImVec2(4, 0), AMBER); dl->AddLine(c + ImVec2(0, -4), c + ImVec2(0, 4), AMBER); }
    if (p.realm == REALM_MAIN) {
      ImVec2 c = M(p.pos.x, p.pos.z);
      const vec3 L = lookVector(p.yaw, 0);
      dl->AddTriangleFilled(c + ImVec2(L.x, L.z) * 9, c + ImVec2(-L.z, L.x) * 5, c + ImVec2(L.z, -L.x) * 5, IM_COL32(255, 240, 200, 255));
    }
    dl->AddRect(o, o + ImVec2(mapW, mapH), IM_COL32(90, 90, 70, 255));
    // the legend at the side
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(0.89f, 0.72f, 0.3f, 1), "The Zone");
    ImGui::TextDisabled("Green: your people\nOrange: hostile stalkers\nRed: mutants (close by)\nAmber cross: your task");
    ImGui::Separator();
    if (const Region* r = regionAt(ifloor(p.pos.x), ifloor(p.pos.z))) { ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 300); ImGui::TextUnformatted(r->name); ImGui::TextDisabled("%s", r->blurb); ImGui::TextDisabled("Danger %d of 5", r->danger); ImGui::PopTextWrapPos(); }
    ImGui::EndGroup();
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
    // ---- the detector, above the hotbar
    const DetectorHud D = detectorHud(lv, p);
    if (D.has) {
      const ImVec2 c(W * 0.5f + 250 * z, H - 120 * z);
      dl->AddRectFilled(c - ImVec2(60, 34) * z, c + ImVec2(60, 34) * z, IM_COL32(24, 30, 22, 220), 4 * z);
      const bool on = D.dist >= 0 && beepT > 0.0f && beepT < 0.12f + std::clamp(D.dist / D.range, 0.08f, 1.f) * 0.2f;
      dl->AddCircleFilled(c + ImVec2(-48, -22) * z, 4 * z, D.dist >= 0 ? (on ? IM_COL32(120, 255, 80, 255) : IM_COL32(40, 90, 30, 255)) : IM_COL32(40, 50, 40, 255));
      text(dl, F_SMALL, c + ImVec2(-40, -28) * z, DIM, D.tier == 1 ? "Echo" : D.tier == 2 ? "Bear" : "Veles");
      if (D.tier >= 2 && D.dist >= 0) {
        const float ty = std::atan2(-D.dir.x, -D.dir.z), rel = ty - p.yaw;
        const ImVec2 v(-std::sin(rel), -std::cos(rel));
        dl->AddLine(c, c + v * 22 * z, IM_COL32(150, 255, 110, 255), 2.5f * z);
        dl->AddCircleFilled(c + v * 22 * z, 3 * z, IM_COL32(150, 255, 110, 255));
        char b[24]; std::snprintf(b, sizeof b, "%.0f m", D.dist); text(dl, F_SMALL, c + ImVec2(14, 14) * z, INK, b);
      } else if (D.dist >= 0) { char b[24]; std::snprintf(b, sizeof b, "signal"); text(dl, F_SMALL, c + ImVec2(-20, 0) * z, IM_COL32(150, 255, 110, 255), b); }
      else text(dl, F_SMALL, c + ImVec2(-28, 0) * z, DIM, "no signal");
    }
  }
private:
  std::string region; float regionT = 0, beepT = 0;
};
} // namespace

void registerZoneFpGun();   // render/zone_fpgun.cpp

void registerZoneClient() {
  registerZoneFpGun();
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
