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
#include "audio/audio.h"
#include "audio/zone_local.h"
#include <imgui.h>
#include <SDL.h>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <cfloat>

namespace dq {

double zoneBarkUntil = 0;   // while a stalker has a line up, the camp murmur waits (client/client_fx.cpp)

bool inventoryClosePressed(App& a, bool tabScreen);   // ui/ui_main.cpp
std::unique_ptr<Screen> makeZoneTradeScreen(const std::string& who);   // ui/zone_screens.cpp
std::unique_ptr<Screen> makeZoneTechScreen();   // ui/zone_craft.cpp

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
      {
        std::string ask, answer;
        if (storyAsk(p, lv, *e, ask, answer)) opts.push_back({ask, [&, ask, answer] {
          say(ask, true);
          const std::string heard = storyTold(p, lv);
          say(heard.empty() ? answer : heard);
        }});
      }
      if (trader) opts.push_back({att < 0 ? "Can you repair my gear? (he won't)" : "Can you repair my gear?", [&, att] {
        say("Can you repair my gear?", true);
        if (att < 0) { say("Not for you. Take your junk somewhere else."); return; }
        say("Show me the toolkit. Basic, advanced, or expert. I work to what you brought, and I don't keep it. A bench in the field stops at sixty.");
        uiDefer([](App& x) { x.pushScreen(makeZoneTechScreen()); });
      }});
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

// Camp barks. Original lines, one at a time. A local taunt or idle clip, when miniaudio can open it,
// takes the place of the murmur for a warning, contact, a greeting, or an idle. zone_murmur stays the
// bed when no clip is playing. The ending screen stays quiet so the outro is alone.
enum Sit { S_HURT, S_EMIT, S_WARN, S_HIT, S_FIRE, S_GREET, S_IDLE, S_N };
const char* barkLine(int fac, int sit, u32 salt) {
  if (fac < 0 || fac > F_INSTITUTE) fac = F_DRIFTER;
  if (sit < 0 || sit >= S_N) sit = S_IDLE;
  static const char* L[8][S_N][2] = {
    {{"I'm hit. Don't just stand there.", "Still up. Bandage, then we move."}, {"Sky's wrong. Get under something.", "Two minutes, if you're quick."}, {"Keep walking. This patch is taken.", "Hands where I can see them."}, {"Contact. Don't bunch up.", "On the left. Watch the left."}, {"Fire's the only honest thing out here.", "Pass the bottle. No, the other one."}, {"You still breathing? Good.", "Road's quiet. That never lasts."}, {"A bolt's the best friend you'll ever have.", "Moroz shorted me on a Moth Wing. Said it was used."}},
    {{"He shot me. Shoot him back.", "That's my blood. You're paying for it."}, {"Blowout. Under the truck, move.", "Leave the loot. The sky doesn't bargain."}, {"Last chance. Drop the pack.", "Wrong camp, stalker."}, {"They're coming in. Shoot.", "Flank them. I'll take the front."}, {"Count it again. He shorted us.", "If he comes back, we take the boots."}, {"Toll road. What've you got?", "Nice jacket. Be a shame."}, {"Dogs on the gorge road. Go round.", "The hangar's ours after dark. Remember that."}},
    {{"Man down on the line.", "I'm leaking. Cover me."}, {"Emission. Masks on, inside.", "Hold the shelter. Nobody runs."}, {"Closed zone. Turn around.", "One more step and we fire."}, {"Contact front. Hold.", "Short bursts. Don't waste it."}, {"Keep the noise down.", "Perimeter's quiet. I don't like quiet."}, {"Papers. Slowly.", "State your business, then go."}, {"The fence is there for a reason.", "Log the patrol. Then eat."}},
    {{"Vest took most of it. Most.", "Still standing. Don't stop."}, {"Emission. Into the post, all of you.", "Count heads. I want everyone under a roof."}, {"This ground is held. Leave it.", "We don't share a fire with raiders."}, {"Mutants on the wire. Up.", "Pick your shots. Make them count."}, {"Clean the rifles before you sleep.", "The Zone doesn't take the night off."}, {"Armed, and sane? Then we can talk.", "Point the mutants away from camp."}, {"Someone has to hold the road.", "Check the seals. Then sit."}},
    {{"I'm fine. It's a scratch. It's not.", "Leave me a bandage and go."}, {"The sky's burning. Roof. Now.", "Laugh later. Move now."}, {"Back off. This camp's spoken for.", "You want a fight, you'll get one."}, {"Here they come. Spread out.", "I said spread out."}, {"Sit down. The argument waits till morning.", "The Zone keeps what it wants. We're visiting."}, {"You look lost. That's normal.", "Pull up a crate. The tea's awful."}, {"North of the Radar, don't rush.", "Every vow to quit is a joke the Zone tells."}},
    {{"Contract doesn't cover bleeding out.", "Patch it. The clock's still running."}, {"Emission. Job pauses. We don't.", "Inside. The client can wait."}, {"You're not on the list.", "Turn around. This one's paid for."}, {"Targets. Earn it.", "Left side. I've got right."}, {"Hold till dawn. Dawn can hurry.", "You pay, we shoot. That's the whole of it."}, {"You hiring, or lost?", "Keep your hands off the crates."}, {"No names on the radio.", "We leave when it's done. Not before."}},
    {{"The hum gets in the wound.", "I was told not to fall. I am sorry."}, {"The sky speaks. Cover your ears and run.", "It is only the wind. It is not only the wind."}, {"You were not called. Leave.", "This ground is listening. Don't add to it."}, {"They come. Stand with us.", "Do not break the line."}, {"The fire is a small sun. Stay near it.", "We were told to wait. We are waiting."}, {"You hear it too.", "Sit, if you can be quiet."}, {"The light at the plant lies.", "Don't answer what the plant asks."}},
    {{"Note the blood loss. Then stop it.", "I would prefer not to be the sample."}, {"Readings are climbing. Shelter, please.", "The wave is early. Log it later."}, {"This bunker is not a thoroughfare.", "Back away from the instruments."}, {"Hostile contact. The rifles, not the probes.", "Stay low. The data can wait."}, {"Log the reading, then eat.", "If it glows, do not pocket it."}, {"You have a detector? Good. Use it.", "Wipe your boots. The floor is a sensor."}, {"Yantar pays in cash and questions.", "The hum from the Radar is not weather."}},
  };
  return L[fac][sit][salt & 1];
}

class ZoneBarkLayer : public RenderLayer {
public:
  const char* name() const override { return "zoneBarks"; }
  int order() const override { return 40; }
  void update(GameSession& s, double) override {
    if (!s.player || !s.level || !zoneWorld(*s.level) || s.player->dead) return;
    if (const Screen* top = s.app.topScreen()) {
      const std::string id = top->id();
      if (id == "talk" || id == "ending") return;   // the ending outro plays alone
    }
    const double now = s.app.time;
    if (now < showUntil || now < nextAt) return;
    Player& p = *s.player; Level& lv = *s.level;
    const Emission em = emission(lv);
    Entity* best = nullptr; int pri = 99; float bd = 1e9f; int fac = 0; int sit = S_IDLE;
    for (auto& ent : lv.entities()) {
      Entity* e = ent.get();
      if (!e || e->removed || e->hp <= 0 || e->realm != p.realm || e->type.rfind("z_npc_", 0) != 0) continue;
      const float d = glm::distance(e->pos, p.pos);
      if (d > 30.f) { met.erase(e->id); continue; }
      if (d > 22.f || cool[e->id] > now) continue;
      const int f = npcFactionOf(*e);
      if (f < 0 || f > F_INSTITUTE) continue;
      auto* c = dynamic_cast<Creature*>(e);
      const bool hurt = e->maxHp > 0 && e->hp < e->maxHp * 0.72f;
      const bool warn = attitude(p, f) < 0;
      const bool fire = c && c->sitting && !c->sleeping;
      int sp = S_IDLE, pr = 6;
      if (hurt) { sp = S_HURT; pr = 0; }
      else if (em.phase == 1 || em.phase == 2) { sp = S_EMIT; pr = 1; }
      else if (warn && d < 16.f) { sp = S_WARN; pr = 2; }
      else if (c && c->state == "aim" && d < 16.f) { sp = S_HIT; pr = 3; }
      else if (c && c->sleeping) continue;
      else if (fire) { sp = S_FIRE; pr = 4; }
      else if (d < 7.f && !met.count(e->id)) { sp = S_GREET; pr = 5; }
      else if (e->type == "z_npc_trader" || d > 12.f) continue;
      else { sp = S_IDLE; pr = 6; }
      if (pr < pri || (pr == pri && d < bd)) { pri = pr; bd = d; best = e; fac = f; sit = sp; }
    }
    if (!best) return;
    if (pri >= 4 && sit != S_GREET && ((best->id + (u32)now) % 5) != 0) return;
    const char* line = barkLine(fac, sit, best->id ^ (u32)now);
    if (!line || !line[0]) return;
    text = line; from = best->pos + vec3(0, best->h + 0.55f, 0); rgb = FAC_COL[fac];
    showUntil = now + 4.2; nextAt = now + (pri <= 3 ? 8.0 : 14.0); cool[best->id] = now + 26;
    zoneBarkUntil = showUntil;
    if (sit == S_GREET) met.insert(best->id);
    bool voiced = false;
    if (s.app.audio && (sit == S_WARN || sit == S_HIT || sit == S_GREET || sit == S_IDLE)) {
      using M = snd::LocalStalkerAudio;
      const M::Moment moment = sit == S_WARN ? M::Warn : sit == S_HIT ? M::Contact : sit == S_GREET ? M::Greet : M::Idle;
      const std::filesystem::path clip = M::line(fac, moment, best->id ^ (u32)(now * 4));
      float secs = 0;
      if (!clip.empty() && s.app.audio->playFileAt(clip.string(), best->pos + vec3(0, 1.5f, 0), 22, 0.95f, 1, &secs) && secs > 0.05f) {
        voiced = true;
        zoneBarkUntil = std::max(showUntil, now + secs + 0.35);
        nextAt = std::max(nextAt, zoneBarkUntil);
      }
    }
    if (!voiced) lv.fx->sound("zone_murmur", best->pos + vec3(0, 1.4f, 0), 16, 0.8f, 0.9f + (best->id % 5) * 0.035f);
  }
  void drawHud(GameSession& s) override {
    if (text.empty() || s.app.time >= showUntil || !s.player) return;
    Camera& cam = s.frame.cam;
    const vec4 c = cam.viewProj * vec4(cam.rel(glm::dvec3(from)), 1);
    if (c.w <= 0.12f) return;
    const float W = (float)s.app.width, H = (float)s.app.height;
    const ImVec2 sp((c.x / c.w * 0.5f + 0.5f) * W, (1.f - (c.y / c.w * 0.5f + 0.5f)) * H);
    ImFont* font = ImGui::GetFont();
    const float sz = ImGui::GetFontSize();
    const ImVec2 ts = font->CalcTextSizeA(sz, FLT_MAX, 0, text.c_str());
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 a(sp.x - ts.x * 0.5f - 8, sp.y - ts.y - 6), b(sp.x + ts.x * 0.5f + 8, sp.y + 4);
    dl->AddRectFilled(a, b, IM_COL32(8, 10, 9, 190), 3);
    dl->AddRect(a, b, IM_COL32(160, 150, 120, 180));
    dl->AddText(font, sz, ImVec2(sp.x - ts.x * 0.5f, sp.y - ts.y - 1), rgb, text.c_str());
  }
private:
  static constexpr ImU32 FAC_COL[8] = {IM_COL32(230, 220, 190, 255), IM_COL32(220, 120, 90, 255), IM_COL32(170, 190, 150, 255), IM_COL32(220, 160, 80, 255), IM_COL32(160, 200, 120, 255), IM_COL32(180, 180, 190, 255), IM_COL32(190, 150, 220, 255), IM_COL32(140, 200, 210, 255)};
  std::string text; vec3 from{0}; ImU32 rgb = IM_COL32(230, 220, 190, 255);
  double showUntil = 0, nextAt = 0;
  std::unordered_map<u32, double> cool;
  std::unordered_set<u32> met;
};
} // namespace

std::unique_ptr<Screen> makeZoneTalkScreen(u32 npc) { return std::make_unique<TalkScreen>(npc); }
void registerZoneBarks() { registerRenderLayer([](GameSession&) -> std::shared_ptr<RenderLayer> { return std::make_shared<ZoneBarkLayer>(); }); }

} // namespace dq
