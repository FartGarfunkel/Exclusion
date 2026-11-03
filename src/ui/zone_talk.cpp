// ui/zone_talk.cpp — talking to a stalker (use on one who isn't hostile): what he says at the top, your lines under it —
// any news (rumours from A-Life: packs, enemies, anomaly fields, now and then a stash for your map), trade (traders,
// unless you're their faction's enemy), work (traders and the leaders of camps and bases offer up to three jobs from
// data/zone_jobs.json; the rest send you to them), handing in a job done for his faction, goodbye. The world goes on
// while you talk. Mouse, keys (arrows / Enter, or 1-9) and the controller (d-pad / A, B leaves).
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "sim/zone.h"
#include "sim/creature.h"
#include "gen/zone_plan.h"
#include <imgui.h>
#include <SDL.h>
#include <functional>

namespace dq {

bool inventoryClosePressed(App& a, bool tabScreen);   // ui/ui_main.cpp
std::unique_ptr<Screen> makeZoneTradeScreen(const std::string& who);   // ui/zone_screens.cpp

using namespace ui;
namespace {
using namespace zone;
const ImVec4 V_HI(0.91f, 0.77f, 0.38f, 1), V_TXT(0.84f, 0.85f, 0.8f, 1), V_DIM(0.55f, 0.57f, 0.53f, 1), V_RED(0.88f, 0.4f, 0.3f, 1), V_GREEN(0.5f, 0.82f, 0.5f, 1);

class TalkScreen : public Screen {
public:
  explicit TalkScreen(u32 id) : npc(id) {}
  const char* id() const override { return "talk"; }
  bool pausesGame() const override { return false; }
  Entity* who(App& a) { return a.session && a.session->level ? a.session->level->entity(npc) : nullptr; }
  void update(App& a, double) override {
    Player* p = a.session ? a.session->player : nullptr; Entity* e = who(a);
    if (!p || !e || e->removed || p->dead || glm::distance(e->pos, p->pos) > 7.f || (e->type != "z_npc_trader" && !npcFriendlyTo(*e, *p))) { a.popScreen(); return; }
    if (++frames > 2 && inventoryClosePressed(a, true)) { if (mode != 0) { mode = 0; return; } a.popScreen(); return; }
    // he turns to you
    if (auto* c = dynamic_cast<Creature*>(e)) { const vec3 d = p->pos - c->pos; c->yaw = std::atan2(-d.x, -d.z); }
  }
  void draw(App& a) override {
    GameSession* s = a.session && a.session->player ? a.session.get() : nullptr;
    Entity* e = who(a);
    if (!s || !e) return;
    Player& p = *s->player; Level& lv = *s->level;
    const int f = npcFactionOf(*e); const bool trader = e->type == "z_npc_trader";
    const int att = attitude(p, f);
    if (!greeted) {
      greeted = true; name = npcName(*e);
      static const char* HELLO[3][3] = {{"What do you want? Make it quick.", "You've got some nerve, coming here.", "Talk. Then go."},
                                        {"Hm. Another one. What do you need?", "Mind the fire. What's on your mind?", "Stalker. Something you want?"},
                                        {"Good to see you in one piece!", "Sit down, friend. What's the news?", "Ah, it's you. Good hunting lately?"}};
      say(trader && att < 0 ? "I don't deal with your kind. Say your piece and get out." : HELLO[att + 1][e->id % 3]);
    }
    ImGui::SetNextWindowPos(ImVec2(a.width * 0.5f, a.height * 0.62f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(720.f, a.width - 30.f), std::min(440.f, a.height - 30.f)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(14, 16, 15, 240)); ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(96, 100, 98, 255)); ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(20, 23, 22, 255));
    ImGui::Begin("##talk", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor(3);
    ImGui::TextColored(V_HI, "%s", name.c_str()); ImGui::SameLine();
    ImGui::TextDisabled("  %s%s  \xC2\xB7  ", factionName(f), trader ? " trader" : npcIsLeader(lv, *e) ? ", in charge here" : "");
    ImGui::SameLine(0, 0); ImGui::TextColored(att < 0 ? V_RED : att > 0 ? V_GREEN : V_DIM, "%s", att < 0 ? "enemy" : att > 0 ? "friend" : "neutral");
    ImGui::Separator();
    // what's been said
    const float optH = ImGui::GetFrameHeightWithSpacing() * 5.6f;
    ImGui::BeginChild("##said", ImVec2(0, -optH), ImGuiChildFlags_Borders);
    padScrollHere();
    for (auto& [mine, line] : log) {
      ImGui::TextColored(mine ? V_DIM : V_HI, "%s", mine ? "You:" : (name + ":").c_str()); ImGui::SameLine();
      ImGui::PushTextWrapPos(); ImGui::TextColored(mine ? V_DIM : V_TXT, "%s", line.c_str()); ImGui::PopTextWrapPos();
    }
    if (scrollDown) { ImGui::SetScrollHereY(1.f); scrollDown = false; }
    ImGui::EndChild();
    // your lines
    std::vector<std::pair<std::string, std::function<void()>>> opts;
    if (mode == 0) {
      opts.push_back({"Any news?", [&, e] { say("Any news?", true); auto r = rumours(lv, p, *e); for (size_t k = 0; k < r.size() && k < 3; k++) say(r[(k + told) % r.size()]); told += 1; }});
      if (trader) opts.push_back({att < 0 ? "Let's trade. (he won't)" : "Let's trade.", [&, e, att] {
        if (att < 0) { say("Let's trade.", true); say("Not with you. Not for any money."); return; }
        const std::string t = e->data.value("trader", std::string("hamlet")); uiDefer([t](App& x) { x.pushScreen(makeZoneTradeScreen(t)); }); }});
      for (int idx : jobsToTurnIn(p, *e)) {
        const std::string t = p.stats["zone"]["jobs"][idx].value("title", std::string("the job"));
        opts.push_back({"About the job: " + t, [&, idx, t] {
          std::string msg; say("About the job: " + t + ".", true);
          if (turnInJob(lv, p, idx, msg)) { say("Good work. Here's what we agreed."); hudToast("Job done: " + msg, "rare"); } else say(msg);
        }});
      }
      opts.push_back({"Got any work for me?", [&, e] {
        say("Got any work for me?", true);
        if (!npcIsLeader(lv, *e)) { say(trader ? "Not today." : "Ask whoever's in charge here, or the traders. I just keep watch."); return; }
        offers = jobOffers(lv, p, *e);
        if (offers.empty()) { say("Nothing right now. Come back tomorrow."); return; }
        say(offers.size() == 1 ? "There's one thing." : "A few things, if you're up to it."); mode = 1;
      }});
      opts.push_back({"Goodbye.", [] { uiDefer([](App& x) { x.popScreen(); }); }});
    } else if (mode == 1) {
      for (size_t k = 0; k < offers.size(); k++) {
        const JobOffer& o = offers[k];
        opts.push_back({o.title + "  (" + std::to_string(o.job.value("money", 0)) + " RU)", [&, k] { pick = (int)k; say(offers[k].title + "?", true); say(offers[k].text); mode = 2; }});
      }
      opts.push_back({"Never mind.", [&] { mode = 0; }});
    } else if (mode == 2 && pick >= 0 && pick < (int)offers.size()) {
      opts.push_back({"I'll do it.", [&] {
        std::string why; say("I'll do it.", true);
        if (takeJob(lv, p, offers[pick], why)) { say("Good. It's in your PDA."); hudToast("New job: " + offers[pick].title, "rare"); offers.erase(offers.begin() + pick); }
        else say(why);
        mode = 0;
      }});
      opts.push_back({"Not this one.", [&] { mode = 1; }});
    } else mode = 0;
    ImGui::Spacing();
    int hit = -1;
    for (int k = 0; k < (int)opts.size(); k++) {
      if (focusFirst && k == 0) ImGui::SetKeyboardFocusHere();
      if (button(((k < 9 ? std::to_string(k + 1) + ". " : "") + opts[k].first + "##o" + std::to_string(k)).c_str(), BTN_GHOST, false, -1)) hit = k;
    }
    focusFirst = false;
    if (!a.input.textActive) for (int k = 0; k < (int)opts.size() && k < 9; k++) if (a.input.keyPressed((SDL_Scancode)(SDL_SCANCODE_1 + k))) hit = k;
    const int was = mode;
    if (hit >= 0) opts[hit].second();
    if (mode != was) focusFirst = true;
    ImGui::End();
  }
private:
  u32 npc; int frames = 0, mode = 0, pick = -1, told = 0; bool greeted = false, scrollDown = false, focusFirst = true;
  std::string name; std::vector<std::pair<bool, std::string>> log; std::vector<JobOffer> offers;
  void say(const std::string& l, bool mine = false) { log.push_back({mine, l}); if (log.size() > 40) log.erase(log.begin()); scrollDown = true; }
};
} // namespace

std::unique_ptr<Screen> makeZoneTalkScreen(u32 npc) { return std::make_unique<TalkScreen>(npc); }

} // namespace dq
