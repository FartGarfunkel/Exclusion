// sim/zone.h — Exclusion's gameplay on top of the engine: the item catalogue (data/zone_items.json), each player's
// Zone state (radiation, bleeding, hunger, thirst, sleep, stamina, psy, money: Player::stats["zone"], which rides
// the SAVE message), carry weight, guns and attachments (sim/zone_guns.cpp), anomalies, artifacts and detectors,
// emissions, the bunker, A-Life (sim/zone_alife.cpp), traders and the story (sim/zone_story.cpp).
// Client-owned numbers (the body, the bags) are simulated on the machine that owns the player; world things
// (artifacts, A-Life, emissions' schedule) on the host. The emission schedule comes from the world clock, so
// every machine agrees on it without messages.
#pragma once
#include "sim/player.h"
#include "sim/level.h"
#include <string>
#include <vector>
#include <functional>

namespace dq { class PlayerController; }

namespace dq::zone {

constexpr float DMG_SCALE = 0.2f;          // catalogue damage (out of 100 HP) -> engine health (20)

struct GunDef {
  std::string cls, cal, model, defaultOptic;
  float rpm = 600, dmg = 25, reload = 2.3f, kick = 1, kickH = 1, spreadMul = 1, speedMul = 1;
  int mag = 30, pellets = 1, pierce = 0;
  bool slowReload = false, autoFire = true, suppressedBuiltIn = false, launcher = false;
  json fits;                                // slot -> [attachment ids]
};
struct ZItem {
  std::string id, name, cat;                // cat: weapon melee ammo attach suit helmet med food detector throw grenade toolkit repair gear money part trophy artifact
  float w = 0, price = 0; int stack = 1;
  json raw;                                 // the whole catalogue entry
  bool isGun = false; GunDef gun;
};
const ZItem* zi(const std::string& id);
const std::vector<const ZItem*>& allZoneItems();
bool zoneWorld(const Level& lv);
// the character skin a player shows in the Zone: the worn suit's look (data/textures/entity/zone/...), else the lone stalker
std::string suitSkin(const Player& p);

// ---- per-player state (Player::stats["zone"]) ----
float zget(const Player& p, const char* k, float def = 0);
void zset(Player& p, const char* k, float v);
void zadd(Player& p, const char* k, float v, float lo = 0, float hi = 100);
double money(const Player& p);
void addMoney(Player& p, double v);
int faction(const Player& p);               // the player's faction (Drifters by default)

// ---- weight (Misery: no hard wall, a soft one) ----
float itemWeight(const ItemStack& s);
float carryWeight(const Player& p);
float carryCap(const Player& p);           // base 50 kg + suit + artifacts + pills
float moveSlow(const Player& p);           // 1 normal .. 0 can't move
bool noSprint(const Player& p);

// ---- outfits: resistances (0..0.8) ----
struct Resist { float bullet = 0, burn = 0, shock = 0, chem = 0, rad = 0, psy = 0; int belt = 0; float carry = 0; };
Resist resist(const Player& p);
// artifacts on the belt (the slots the suit gives: the first `belt` slots of Player::hook... see beltItems)
std::vector<const ItemStack*> belt(const Player& p);
// a lead sheet folded into a box (tag.box = artifact ids). -1 = a plain sheet. Off-belt artifacts keep their tax;
// what is in the box does not irradiate and does not apply.
int leadBoxed(const ItemStack& sheet);
bool packLead(ItemStack& artifact, ItemStack& sheet, std::string& why);
bool unpackLead(Player& p, ItemStack& sheet, std::string& why);
bool sealLead(Player& p, ItemStack& artifact, std::string& why);

// ---- guns ----
bool gunTick(PlayerController& pc, float dt, const PlayerInput& in);   // true = the held gun took the input
float gunFovScale(const Player& p);
struct GunHud { bool has = false; std::string name, ammo; int mag = 0, clip = 0, reserve = 0, cond = 100; bool reloading = false, ads = false, jammed = false, scope = false; float reloadT = 0; std::string reticle; float spread = 0; };
GunHud gunHud(const Player& p);
// the last med / food used (sim/zone_core.cpp useZoneItem): by whom, what, how long it takes (its "use.time"), a counter
// that render/zone_fpgun.cpp watches to play it (Misery: the item comes up to the face, the bandage is wrapped...)
struct ZoneUse { u32 by = 0; std::string id; int serial = 0; float time = 1; };
extern ZoneUse zoneLastUse;
std::vector<std::string> gunSlots(const std::string& gunId);
json gunAttachments(const ItemStack& gun);
bool attachTo(Player& p, int gunInv, int attInv, std::string& why);    // inv indices (main 0..35)
bool detachFrom(Player& p, int gunInv, const std::string& slot, std::string& why);
bool unloadGun(Player& p, int gunInv);
void cycleAmmo(Player& p, int gunInv);

// ---- input shaping: exhausted, overweight, bleeding ----
void adjustInput(PlayerController& pc, float dt, PlayerInput& in);

// ---- emissions (from the world clock) ----
struct Emission { int phase = 0; float t = 0, left = 0; float strength = 0; };  // phase 0 calm, 1 warning, 2 wave, 3 aftermath
Emission emission(const Level& lv);
bool underCover(Level& lv, const Player& p);

// ---- detectors ----
struct DetectorHud { bool has = false; int tier = 0; float range = 0; float dist = -1; vec3 dir{0}; std::vector<vec3> dots; };
DetectorHud detectorHud(Level& lv, const Player& p);

// ---- anomalies (sim/zone_anomaly.cpp; the roster, where each one is and its ground: sim/zone_anomalies.h) ----
// The ones near this machine's players and what they are doing (every machine runs them, so the client draws the same).
struct AnoLive {
  u32 id = 0; u8 kind = 0, family = 0, tier = 1; bool pool = false;
  vec3 c{0};            // centre, at its feet (a pool: its surface)
  float r = 1;          // reach; a wanderer's loop / path radius
  vec3 pos{0};          // the moving part (fireball, ball lightning, the blister), else c
  vec3 to{0};           // a skip's partner
  float charge = 1;     // arc nest charge 0..1; gas pocket puffs left
  float fire = 0;       // seconds left of it going off (burst, jet, discharge, cloud, steam)
  float cd = 0;         // recharging / dispersed
  float st = 0;         // its state's clock
  int state = 0;        // undertow 1 clenching; spinner 1 holding; wanderfire / blister 1 chasing
  u32 target = 0;       // what it holds or chases
  float hidden = 0;     // ball lightning / the blister: gone for now
  std::vector<vec4> bits;   // small blisters (xyz, life); wisps (xyz, phase); ash moss strands (anchor xyz, length)
  vec3 hitAt{0}; float hitT = 0;   // where its last strike went (arcs)
  u32 lastItem = 0; float dotT = 0;
};
const std::vector<AnoLive>& anomaliesLive(Level& lv);
// what the anomalies are doing to a player's head (the client's screen effects)
struct AnoSense { float drowse = 0, asleep = 0, flash = 0, gas = 0, skip = 0, spin = 0, wisps = 0; };
AnoSense anomalySense(Level& lv, u32 playerId);
// the body's share (adjustInput, the local player): pulls, burns, shocks, slowing, sinking, drowsiness
void anomalyBody(PlayerController& pc, float dt, PlayerInput& in, const std::function<void(float, const char*)>& hurt);
i64 emissionCycle(const Level& lv);   // which emission the world clock is in (-1 before the first)

// ---- the bunker ----
bool travelBunker(Level& lv, Player& p);

// ---- traders ----
struct TradeLine { std::string id; int n; double price; };
std::vector<TradeLine> traderStock(Level& lv, const std::string& trader);
double sellPrice(const ItemStack& s);
double buyPrice(const std::string& id);

// ---- the story and tasks ----
struct Task { std::string id, title, text; bool hasTarget = false; vec3 target{0}; std::string where; };
std::vector<Task> activeTasks(const Level& lv, const Player& p);
void storyTick(PlayerController& pc, float dt, bool useEdge = false, bool sneaking = false);
std::string storyStage(const Player& p);
// the line this stalker can answer for the current step (talk steps). storyTold grants the step.
bool storyAsk(const Player& p, const Level& lv, const Entity& npc, std::string& ask, std::string& answer);
std::string storyTold(Player& p, Level& lv);
std::vector<std::string> pdaNews(Level& lv);
void pdaNewsAdd(Level& lv, const std::string& line);

// ---- A-Life ----
struct SquadView { vec3 pos; int faction; int members; std::string goal; bool online; int id = -1; std::string species; };
std::vector<SquadView> alifeSquads(Level& lv);
// where a squad is headed and what that place is (sim/zone_npc.cpp: what its online members do there)
struct SquadGoal { bool ok = false; vec3 pos{0}; int kind = 0; int faction = 0; float radius = 12; bool arrived = false; int size = 0; };
SquadGoal alifeGoal(Level& lv, int squadId);

// ---- standing, jobs, talk, stashes, bodies (sim/zone_jobs.cpp) ----
// Each faction's standing with a player (Player::stats.zone.rep holds what he has done; the factions' own relations
// give the base): -1000..1000; hostile at REP_HOSTILE or below (their stalkers shoot, their traders refuse), friendly
// at REP_FRIENDLY or above. Killing a faction's member lowers it, killing its enemies raises it, jobs raise it.
constexpr int REP_HOSTILE = -500, REP_FRIENDLY = 500;
int standing(const Player& p, int f);
int attitude(const Player& p, int f);           // -1 hostile, 0 neutral, 1 friendly
void addRep(Player& p, int f, int v);
int npcFactionOf(const Entity& e);              // a z_npc_* creature's faction (traders: whose trader), else F_NONE
int traderFaction(const std::string& trader);
std::string npcName(const Entity& e);           // a stalker's name (stable per entity)
bool npcFriendlyTo(const Entity& npc, const Player& p);   // can be talked to
bool npcIsLeader(Level& lv, const Entity& npc);           // the first of a squad holding a base, hub or camp: gives jobs

struct JobOffer { std::string key, type, title, text; json job; };
std::vector<JobOffer> jobOffers(Level& lv, const Player& p, const Entity& giver);
bool takeJob(Level& lv, Player& p, const JobOffer& o, std::string& why);
// jobs this stalker can close for you (his faction's, or his own as a trader): index into stats.zone.jobs
std::vector<int> jobsToTurnIn(const Player& p, const Entity& npc);
bool turnInJob(Level& lv, Player& p, int idx, std::string& msg);
void dropJob(Player& p, int idx);
std::vector<Task> jobTasks(const Level& lv, const Player& p);   // the jobs as PDA tasks (id = "job:<key>")
std::vector<std::string> rumours(Level& lv, Player& p, const Entity& npc);   // what he's heard (may put a stash on your map)
void jobsTick(PlayerController& pc, float dt);                 // the owner's machine: stashes found, fetches carried, counters

// stashes the player knows of (stats.zone.stashes: {id, x, y, z, name, found}); revealStash adds one near `near`
struct StashView { int id; vec3 pos; std::string name; bool found; };
std::vector<StashView> knownStashes(const Player& p);
bool revealStash(Level& lv, Player& p, vec3 near, std::string* where = nullptr);

// ---- names for HUD ----
std::string regionName(int x, int z);

void registerZone();          // initCore
void registerZoneAlife();     // (called by registerZone)
void registerZoneStory();
void registerZoneNpcs();
void registerZoneJobs();      // sim/zone_jobs.cpp: bodies, stashes, the stalker's PDA (called by registerZone)
void registerZoneMutants();   // sim/zone_mutants.cpp: the mutants' brains (called by registerZoneNpcs)
void registerZoneAnomalies(); // sim/zone_anomaly.cpp (called by registerZone)

// ---- loot furniture and crates (sim/zone_crates.cpp; gen/zone_loot.cpp places them) ----
// smash a wooden crate (`zoneBreak` blocks: melee, a bullet): splinters, its few things (or a roll if it has none), the crates
// stacked on it drop down a block
void breakCrate(Level& lv, World& w, int x, int y, int z);
// a two-tall furniture's upper half (`furnTop`) hands Use / breaking on to its base below; false: not one
bool furnBase(World& w, int x, int& y, int z);

} // namespace dq::zone
