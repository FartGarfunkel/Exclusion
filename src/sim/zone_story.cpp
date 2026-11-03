// sim/zone_story.cpp — Exclusion's story: a man with no memory, a PDA with one task on it — FIND THE FERRYMAN, the
// one stalker said to have come back from the plant — and the trail from Old Hamlet to the light inside the
// sarcophagus that the Choir say grants wishes. Each step is a place on the map (the plan's smart terrains):
// reach it and the next one opens, with a line in your PDA and sometimes something in your hands. Silencing the
// Duga Array (a world flag, lv.info.data.zoneStory.arrayOff) takes the psy wall down for everyone. The story is
// per player (stats.zone.story), so co-op partners can be at different steps.
#include "sim/zone.h"
#include "sim/controller.h"
#include "gen/zone_plan.h"
#include "core/registry.h"
#include <cmath>

namespace dq::zone {

namespace {
struct Step { const char* id; const char* title; const char* text; const char* place; float radius; const char* reward; int money; const char* news; };
const Step STEPS[] = {
  {"moroz", "Talk to Moroz", "You came to in a cellar in Old Hamlet. Moroz, the trader, pulled you out of a burnt-out army truck on the highway. Your PDA holds one task: FIND THE FERRYMAN. Go and talk to him at his counter.", "@trader", 4, "detector_echo", 0, ""},
  {"drive", "The drive in the barracks", "Moroz will tell you what he knows about the Ferryman if you bring back a flash drive his courier hid in the army hangar by the Perimeter checkpoint. The soldiers shoot on sight.", "Perimeter checkpoint", 40, "flash_drive", 1500, ""},
  {"grach", "The digger at the Scrapyard", "Moroz: \"The Ferryman went north with two others, Thorn and Lamplighter. Grach digs the Scrapyard for scrap \xE2\x80\x94 he saw them pass. Mind the dogs.\"", "Scrapyard hangar", 25, "detector_bear", 0, ""},
  {"furnace", "The Furnace bar", "Grach: \"They stopped at the Furnace in Molot Works. The barman remembers everyone who pays.\" Go north to Molot Works.", "The Furnace bar", 25, "", 0, ""},
  {"thorn", "Thorn's PDA", "The barman: \"Thorn went down into the sewers under the Agrarian Institute after something the Ferryman left there. He never came up.\" Find Thorn's PDA.", "Institute sewers", 20, "documents", 3000, "Someone came up out of the Institute sewers alive."},
  {"kalina", "The scientists at Lake Kalina", "Thorn's last entries: the Ferryman paid the scientists at Lake Kalina to measure the hum from the Array. Nobody gets past the Red Forest while it sings. Find the research bunker.", "Kalina research bunker", 20, "helmet_psy", 0, ""},
  {"mine", "The Ferryman's cache", "The scientists give you a prototype psy helmet: \"It buys you minutes, not hours.\" The Ferryman left a cache in the old mine in the Red Forest \xE2\x80\x94 his way past the Array.", "The old mine", 20, "psyblock", 4000, ""},
  {"array", "Silence the Array", "The cache holds a hand-drawn plan of the Array's bunker and the emitter's cut-off. Get into the bunker under the Duga Array and shut it down.", "Array bunker", 18, "", 10000, "THE ARRAY HAS GONE QUIET. The way north is open."},
  {"yanov", "Yanov Station", "With the Array silent, every stalker in the Zone is heading north. They gather at Yanov Station by the Jupiter plant.", "Yanov Station", 30, "", 0, ""},
  {"lamplighter", "Lamplighter", "At Yanov they say the Ferryman's last guide, Lamplighter, drinks his days away on the Volga barge in the Zaton backwaters.", "The Volga barge", 25, "", 0, ""},
  {"square", "The Monolith's square", "Lamplighter: \"He went to the city to learn the Monolith's way into the plant. He said the light in there answers \xE2\x80\x94 and that it lies.\" Search the square in Pripyat.", "Pripyat square", 40, "medkit_sci", 0, ""},
  {"heart", "The light in the sarcophagus", "The Monolith's notes speak of a light at the heart of the sarcophagus that grants one wish to whoever reaches it. The Ferryman went in. Follow him.", "The sarcophagus", 30, "", 0, "Someone has gone into the sarcophagus."},
};
constexpr int NSTEPS = sizeof(STEPS) / sizeof(STEPS[0]);

bool placeOf(const Level& lv, const char* name, vec3& out) {
  const Plan& P = zonePlan((u32)lv.info.seed);
  const int by = dimBase(DIM_OVERWORLD);
  if (std::string(name) == "@trader") { out = vec3(P.traderX + 0.5f, by + P.traderY, P.traderZ + 0.5f); return true; }
  for (const Smart& s : P.smarts) if (s.name == name) { out = vec3(s.x + 0.5f, by + s.y, s.z + 0.5f); return true; }
  return false;
}
int stageOf(const Player& p) { return (int)zget(p, "story", 0); }
} // namespace

std::string storyStage(const Player& p) { const int s = stageOf(p); return s < NSTEPS ? STEPS[s].id : "done"; }

std::vector<Task> activeTasks(const Level& lv, const Player& p) {
  std::vector<Task> out;
  const int s = stageOf(p);
  if (s >= NSTEPS) { out.push_back({"free", "The Zone is yours", "Whatever you chose in there, the Zone goes on. Hunt artifacts, take sides, dig out your bunker.", false, vec3(0), ""}); return out; }
  Task t; t.id = STEPS[s].id; t.title = STEPS[s].title; t.text = STEPS[s].text; t.where = STEPS[s].place;
  t.hasTarget = placeOf(lv, STEPS[s].place, t.target);
  return {t};
}

// called from adjustInput (sim/zone_core.cpp) on the player's own machine
void storyTick(PlayerController& pc, float dt) {
  Player& p = pc.p; Level& lv = pc.lv;
  json& Z = p.stats["zone"];
  // the start: what Moroz found on you
  if (!Z.contains("kit")) {
    Z["kit"] = true;
    for (auto [id, n] : std::initializer_list<std::pair<const char*, int>>{{"knife", 1}, {"pm", 1}, {"ammo_9x18", 14}, {"bandage", 2}, {"bread", 1}, {"water", 1}, {"bolt", 10}, {"headlamp", 1}}) {
      ItemStack s(id, n);
      if (const ItemDef* d = reg().item(id); d && d->hasTool) s.d = d->tool.durability * 11 / 20;   // the pistol's seen hard use (Misery: you start with next to nothing)
      p.give(s);
    }
    if (p.armor.size() > 1 && p.armor[1].empty()) p.armor[1] = ItemStack("jacket_leather", 1, 320); else p.give(ItemStack("jacket_leather", 1, 320));
    pc.toastMsg("FIND THE FERRYMAN \xE2\x80\x94 open your PDA (Tab / Back) for the task", "rare");
  }
  // (a head torch: in the kit since alpha.2, so the torch key works at night; players who started before get theirs now)
  if (!Z.contains("kitLamp")) {
    Z["kitLamp"] = true;
    bool has = false; for (auto& st : p.inv) if (!st.empty() && st.id == "headlamp") has = true;
    if (!has) p.give(ItemStack("headlamp", 1));
  }
  float t = Z.value("storyT", 0.f) - dt; Z["storyT"] = t;
  if (t > 0) return;
  Z["storyT"] = 1.0f;
  const int s = stageOf(p);
  if (s >= NSTEPS || p.realm != REALM_MAIN) return;
  static u32 checked = 0;
  if (checked != (u32)lv.info.seed + 1) {   // a story place the map lacks would stall the trail there: say so in the log
    checked = (u32)lv.info.seed + 1; vec3 q;
    for (const Step& st : STEPS) if (!placeOf(lv, st.place, q)) DQ_WARN("zone story: '%s' (step %s) is not on this map", st.place, st.id);
  }
  vec3 at; if (!placeOf(lv, STEPS[s].place, at)) return;
  const bool talked = std::string(STEPS[s].place) == "@trader" && Z.value("talked_hamlet", false);   // used him (ui/zone_screens.cpp)
  if (!talked && std::hypot(p.pos.x - at.x, p.pos.z - at.z) > STEPS[s].radius) return;
  // done: rewards, the world's flags, the next step
  const Step& S = STEPS[s];
  if (S.reward[0]) p.give(ItemStack(S.reward, std::string(S.reward) == "psyblock" ? 3 : 1));
  if (S.money) addMoney(p, S.money);
  if (std::string(S.id) == "array") { json& st = lv.info.data["zoneStory"]; if (!st.is_object()) st = json::object(); st["arrayOff"] = true; }
  if (std::string(S.id) == "heart") Z["ending"] = "choose";
  if (S.news[0]) pdaNewsAdd(lv, S.news);
  Z["story"] = s + 1;
  pc.toastMsg(s + 1 < NSTEPS ? std::string("New task: ") + STEPS[s + 1].title : "You have reached the light.", "rare");
  lv.fx->soundUi(p.id, "bell");
}

void registerZoneStory() {}

} // namespace dq::zone
