// ui/zone_craft.cpp — the Zone's two work places, Misery style, instead of the block game's crafting table, furnace,
// anvil and the rest (ui/ui_main.cpp onScreenOpen sends them here in a Zone world; sim/zone_core.cpp opens the cooking
// one from a campfire):
//  * cooking (a stove, a campfire): raw mutant meat (the mutants' trophies) roasted, one or all;
//  * the workbench (the bunker's table, an anvil): bandages, bolts and repair kits from salvage, and a field repair
//    of worn guns and suits from parts, capped at 60%. Past that, a trader who has your toolkit (basic 70, advanced
//    85, expert 100) does the work. The toolkit stays in your bag. There is no upgrade tree.
// A plain list of buttons like the trade screen, so the mouse, the keyboard and a controller (ImGui's pad navigation,
// B to leave) all work it. The bags are the player's own (client-owned), changed here like the trader does.
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "sim/zone.h"
#include "sim/gameplay.h"
#include "core/registry.h"
#include <imgui.h>
#include <cmath>
#include <algorithm>

namespace dq {

bool inventoryClosePressed(App& a, bool tabScreen);   // ui/ui_main.cpp
using namespace ui;

namespace {
using namespace zone;
struct In { const char* id; int n; };
struct Recipe { bool cook; const char* out; int n; In in[3]; };
const Recipe RECIPES[] = {
  {true, "mutant_meat_cooked", 1, {{"mutant_meat", 1}}},
  {false, "bandage", 1, {{"cloth", 2}}},
  {false, "bolt", 10, {{"scrap_metal", 1}}},
  {false, "repair_weapon", 1, {{"scrap_metal", 2}, {"springs", 1}, {"gun_oil", 1}}},
  {false, "repair_armor", 1, {{"cloth", 3}, {"scrap_metal", 1}, {"wires", 1}}},
};

int have(const Player& p, const std::string& id) { int n = 0; for (auto& s : p.inv) if (!s.empty() && s.id == id) n += s.n; return n; }
void take(Player& p, const std::string& id, int n) {
  for (auto& s : p.inv) { if (n <= 0) break; if (s.empty() || s.id != id) continue; const int k = std::min(n, s.n); s.n -= k; n -= k; if (s.n <= 0) s.clear(); }
}
std::string nameOf(const std::string& id) { const ZItem* z = zi(id); return z ? z->name : id; }
int maxDur(const ItemStack& s) { const ItemDef* d = s.def(); if (!d) return 0; return d->hasTool ? d->tool.durability : d->props.contains("armor") ? d->props["armor"].value("durability", 0) : 0; }
float condOf(const ItemStack& s) { const int m = maxDur(s); return m > 0 ? 1.f - std::clamp((float)s.d / m, 0.f, 1.f) : 1.f; }

class ZoneCraftScreen : public Screen {
public:
  explicit ZoneCraftScreen(bool cook) : cook(cook) {}
  const char* id() const override { return "zonecraft"; }
  void update(App& a, double) override { if (inventoryClosePressed(a, true)) a.popScreen(); }
  void draw(App& a) override {
    GameSession* s = a.session && a.session->player ? a.session.get() : nullptr;
    if (!s) return;
    Player& p = *s->player;
    ImGui::SetNextWindowPos(ImVec2(a.width * 0.5f, a.height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(720.f, a.width - 30.f), std::min(560.f, a.height - 30.f)));
    ImGui::Begin("##zonecraft", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::TextColored(ImVec4(0.89f, 0.72f, 0.3f, 1), "%s", cook ? "Cooking" : "Workbench");
    ImGui::SameLine(); ImGui::TextDisabled(cook ? "  Raw meat off the mutants you kill, roasted over the fire  \xC2\xB7  Esc / B to leave" : "  Salvage into kit, worn gear back into shape  \xC2\xB7  Esc / B to leave");
    ImGui::Separator();
    ImGui::BeginChild("##list", ImVec2(0, 0));
    padScrollHere();
    int k = 0;
    for (const Recipe& r : RECIPES) {
      if (r.cook != cook) continue;
      bool can = true; std::string need;
      for (const In& i : r.in) { if (!i.id) break; const int h = have(p, i.id); if (h < i.n) can = false; need += (need.empty() ? "" : ", ") + std::to_string(i.n) + " " + nameOf(i.id) + " (" + std::to_string(h) + ")"; }
      ImVec2 at = ImGui::GetCursorScreenPos(); ImGui::Dummy(ImVec2(32, 32)); drawItem(ImGui::GetWindowDrawList(), ItemStack(r.out, r.n), at, 32, can ? 1.f : 0.45f); ImGui::SameLine();
      ImGui::BeginGroup();
      ImGui::Text("%s%s", nameOf(r.out).c_str(), r.n > 1 ? (" x" + std::to_string(r.n)).c_str() : "");
      ImGui::TextDisabled("Needs %s", need.c_str());
      ImGui::EndGroup(); ImGui::SameLine(0, 24);
      if (button((std::string(cook ? "Cook" : "Make") + "##m" + std::to_string(k)).c_str(), BTN_PRIMARY, true, 100, !can)) make(a, p, r, 1);
      ImGui::SameLine();
      if (button((std::string(cook ? "Cook all" : "Make all") + "##a" + std::to_string(k)).c_str(), BTN_GHOST, true, 110, !can)) make(a, p, r, 99);
      k++;
    }
    if (!cook) repairs(a, p);
    ImGui::EndChild();
    ImGui::End();
  }
private:
  bool cook;
  void make(App& a, Player& p, const Recipe& r, int times) {
    int made = 0;
    for (; made < times; made++) {
      bool ok = true; for (const In& i : r.in) { if (!i.id) break; if (have(p, i.id) < i.n) ok = false; }
      if (!ok) break;
      if (const int left = p.give(ItemStack(r.out, r.n)); left > 0) { take(p, r.out, r.n - left); hudToast("No room in your bags", "warn"); break; }   // (what did fit goes back)
      for (const In& i : r.in) { if (!i.id) break; take(p, i.id, i.n); }
    }
    if (made > 0) { hudToast((cook ? "Cooked " : "Made ") + nameOf(r.out) + (made * r.n > 1 ? " x" + std::to_string(made * r.n) : ""), "good"); if (a.session && a.session->level) a.session->level->fx->soundUi(p.id, cook ? "fizz" : "anvil"); }
  }
  // worn guns and gear: +25% a go, from parts. The bench is a field fix and stops at 60%.
  // A technician (ui/zone_talk.cpp) goes higher, and only as far as the toolkit you are carrying.
  void repairs(App& a, Player& p) {
    const float cap = 0.6f;
    ImGui::Spacing(); ImGui::Separator();
    ImGui::TextColored(ImVec4(0.89f, 0.72f, 0.3f, 1), "Repairs"); ImGui::SameLine();
    ImGui::TextDisabled("  (field fix, up to 60%%. A technician with a toolkit goes to 70, 85 or 100.)");
    int shown = 0;
    auto row = [&](ItemStack& st, int key) {
      const ZItem* z = st.empty() ? nullptr : zi(st.id); if (!z) return;
      const bool gun = z->isGun || z->cat == "melee", gear = z->cat == "suit" || z->cat == "helmet";
      if ((!gun && !gear) || maxDur(st) <= 0) return;
      const float c = condOf(st);
      if (c >= 0.995f) return;
      shown++;
      const In need[2] = {gun ? In{"scrap_metal", 1} : In{"cloth", 2}, gun ? In{"gun_oil", 1} : In{"scrap_metal", 1}};
      const bool parts = have(p, need[0].id) >= need[0].n && have(p, need[1].id) >= need[1].n, room = c < cap - 0.005f;
      ImVec2 at = ImGui::GetCursorScreenPos(); ImGui::Dummy(ImVec2(32, 32)); drawItem(ImGui::GetWindowDrawList(), st, at, 32, 1, false); ImGui::SameLine();
      ImGui::BeginGroup();
      ImGui::Text("%s  %d%%", z->name.c_str(), (int)std::round(c * 100));
      ImGui::TextDisabled("%s", room ? ("+25% for " + std::to_string(need[0].n) + " " + nameOf(need[0].id) + " (" + std::to_string(have(p, need[0].id)) + ") and " + std::to_string(need[1].n) + " " + nameOf(need[1].id) + " (" + std::to_string(have(p, need[1].id)) + ")").c_str() : "As good as these tools get it");
      ImGui::EndGroup(); ImGui::SameLine(0, 24);
      if (button(("Repair##r" + std::to_string(key)).c_str(), BTN_PRIMARY, true, 100, !(parts && room))) {
        const float nc = std::min(cap, c + 0.25f);
        st.d = std::max(0, (int)std::round(maxDur(st) * (1 - nc)));
        for (const In& i : need) take(p, i.id, i.n);
        hudToast("Repaired " + z->name + " to " + std::to_string((int)std::round(nc * 100)) + "%", "good");
        if (a.session && a.session->level) a.session->level->fx->soundUi(p.id, "anvil");
      }
    };
    for (int i = 0; i < (int)p.inv.size(); i++) row(p.inv[i], i);
    for (int i = 0; i < (int)p.armor.size(); i++) row(p.armor[i], 100 + i);
    if (!shown) ImGui::TextDisabled("Nothing you carry or wear is worn.");
  }
};

struct Kit { const char* id; const char* name; float cap; int price; };
const Kit KITS[] = {{"toolkit_basic", "Basic toolkit", 0.70f, 250}, {"toolkit_adv", "Advanced toolkit", 0.85f, 700}, {"toolkit_expert", "Expert toolkit", 1.00f, 1600}};
const Kit* bestKit(const Player& p) {
  const Kit* b = nullptr;
  for (const Kit& k : KITS) if (have(p, k.id) > 0 && (!b || k.cap > b->cap)) b = &k;
  return b;
}

// the technician at a trader: the same +25% and the same parts as the bench, plus his fee, up to the toolkit you brought.
// Basic 70%, advanced 85%, expert 100%. No toolkit, he sends you back to the field bench. The kit is not taken.
class ZoneTechScreen : public Screen {
public:
  const char* id() const override { return "zonetech"; }
  void update(App& a, double) override { if (inventoryClosePressed(a, true)) a.popScreen(); }
  void draw(App& a) override {
    GameSession* s = a.session && a.session->player ? a.session.get() : nullptr;
    if (!s) return;
    Player& p = *s->player;
    const Kit* kit = bestKit(p);
    ImGui::SetNextWindowPos(ImVec2(a.width * 0.5f, a.height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(760.f, a.width - 30.f), std::min(560.f, a.height - 30.f)));
    ImGui::Begin("##zonetech", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::TextColored(ImVec4(0.89f, 0.72f, 0.3f, 1), "Technician");
    ImGui::SameLine(); ImGui::TextDisabled("  Bring the toolkit. I work to what it can do, and I don't keep it.  \xC2\xB7  Esc / B");
    ImGui::Separator();
    ImGui::BeginChild("##tech", ImVec2(0, 0));
    padScrollHere();
    for (const Kit& k : KITS) {
      const bool got = have(p, k.id) > 0;
      ImGui::TextColored(got ? ImVec4(0.55f, 0.82f, 0.5f, 1) : ImVec4(0.55f, 0.57f, 0.53f, 1), "%s", got ? "carried" : "missing");
      ImGui::SameLine();
      ImGui::Text("%s  \xE2\x80\x94  %d%%, %d RU a go", k.name, (int)std::round(k.cap * 100), k.price);
    }
    ImGui::Spacing();
    if (!kit) ImGui::TextDisabled("No toolkit in your bags. A field bench will take a gun to 60%%, and no further.");
    else ImGui::Text("Working to %s (%d%%). You have %s RU.", kit->name, (int)std::round(kit->cap * 100), std::to_string((long long)std::llround(money(p))).c_str());
    int shown = 0, key = 0;
    auto row = [&](ItemStack& st) {
      const ZItem* z = st.empty() ? nullptr : zi(st.id); if (!z) return;
      const bool gun = z->isGun || z->cat == "melee", gear = z->cat == "suit" || z->cat == "helmet";
      if ((!gun && !gear) || maxDur(st) <= 0) return;
      const float c = condOf(st);
      if (c >= 0.995f) return;
      shown++; key++;
      const In need[2] = {gun ? In{"scrap_metal", 1} : In{"cloth", 2}, gun ? In{"gun_oil", 1} : In{"scrap_metal", 1}};
      const bool parts = have(p, need[0].id) >= need[0].n && have(p, need[1].id) >= need[1].n;
      const bool room = kit && c < kit->cap - 0.005f;
      const bool pay = kit && money(p) >= kit->price;
      ImVec2 at = ImGui::GetCursorScreenPos(); ImGui::Dummy(ImVec2(32, 32)); drawItem(ImGui::GetWindowDrawList(), st, at, 32, 1, false); ImGui::SameLine();
      ImGui::BeginGroup();
      ImGui::Text("%s  %d%%", z->name.c_str(), (int)std::round(c * 100));
      if (!kit) ImGui::TextDisabled("Needs a toolkit");
      else if (!room) ImGui::TextDisabled("As far as this toolkit goes");
      else ImGui::TextDisabled("+25%% for %d RU, %d %s (%d) and %d %s (%d)", kit->price, need[0].n, nameOf(need[0].id).c_str(), have(p, need[0].id), need[1].n, nameOf(need[1].id).c_str(), have(p, need[1].id));
      ImGui::EndGroup(); ImGui::SameLine(0, 24);
      if (button(("Repair##t" + std::to_string(key)).c_str(), BTN_PRIMARY, true, 100, !(kit && parts && room && pay))) {
        if (!kit) return;
        const float nc = std::min(kit->cap, c + 0.25f);
        st.d = std::max(0, (int)std::round(maxDur(st) * (1 - nc)));
        addMoney(p, -kit->price);
        for (const In& i : need) take(p, i.id, i.n);
        hudToast("Repaired " + z->name + " to " + std::to_string((int)std::round(nc * 100)) + "%", "good");
        if (a.session && a.session->level) a.session->level->fx->soundUi(p.id, "anvil");
      }
    };
    for (auto& st : p.inv) row(st);
    for (auto& st : p.armor) row(st);
    if (kit && !shown) ImGui::TextDisabled("Nothing you carry or wear is worn.");
    ImGui::EndChild();
    ImGui::End();
  }
};
} // namespace

std::unique_ptr<Screen> makeZoneCraftScreen(bool cook) { return std::make_unique<ZoneCraftScreen>(cook); }
std::unique_ptr<Screen> makeZoneTechScreen() { return std::make_unique<ZoneTechScreen>(); }

} // namespace dq
