// client/client_fx.cpp — see client_fx.h.
#include "sim/zone.h"
#include "gen/zone_plan.h"
#include "sim/creature.h"
#include "gen/nether_bta.h"
#include "client/client_fx.h"
#include "client/session.h"
#include "client/app.h"
#include "audio/audio.h"
#include "gen/generator.h"
#include "core/registry.h"
#include "render/particles.h"
#include "ui/ui.h"
#include "sim/weather.h"
#include <algorithm>

namespace dq {

// ui/zone_talk.cpp defines this. It stays at namespace scope: MSVC puts an extern declared
// inside a block into the global namespace, and the Windows link then looks for ::zoneBarkUntil.
extern double zoneBarkUntil;

void ClientFx::particles(const std::string& kind, vec3 pos, int count, const json& extra) {
  if (s.app.settings.particles <= 0) return;
  dq::particles().spawn(kind, pos, s.app.settings.particles == 1 ? std::max(1, count / 3) : count, extra);
}
void ClientFx::message(u32 playerId, const std::string& text, const std::string& kind) {
  if (playerId && (!s.player || playerId != s.player->id)) return;
  if (hudTakesMessages()) return;   // the HUD shows it (as a toast or a chat line); toasting here too doubled every one
  hudToast(text, kind);
}
class PlayerController; PlayerController* localController(GameSession& s);   // client/play.h
float& controllerShake(PlayerController& c);   // client/play.cpp
void ClientFx::shake(u32 playerId, float strength) {
  if (!s.player || (playerId && playerId != s.player->id)) return;
  if (PlayerController* c = localController(s)) { float& k = controllerShake(*c); k = std::max(k, strength); }
}
// Exclusion: the guns' sounds (audio/zone_sfx.cpp). A shot is a different synth at the ear, across a
// yard, and over a hill — and another indoors. The bands follow the X-Ray rolloff (reference 10 m,
// psSoundRolloff 0.75): full inside ~13 m, the report out to ~48 m, the roll after that. Handling
// (dry, jam, mag, rack, a suppressor) stays the close sample.
// NPC shots arrive as "firework_blast"; the gun is the one in that stalker's hands. Underbarrel and RPG arrive
// as "firework_launch" at a low pitch and are not fireworks.
static std::string zoneSoundName(GameSession& s, const std::string& name, vec3 pos, float& pitch) {
  if (!s.level || !zone::zoneWorld(*s.level)) return name;
  std::string n = name;
  if (n == "firework_blast") {
    const zone::ZItem* gun = nullptr;
    float best = 3.5f;
    for (auto& e : s.level->entities()) {
      if (!e || e->removed) continue;
      auto* c = dynamic_cast<Creature*>(e.get());
      if (!c || c->heldItem.empty()) continue;
      const float d = glm::distance(c->pos, pos);
      if (d >= best) continue;
      const zone::ZItem* z = zone::zi(c->heldItem);
      if (!z || !z->isGun) continue;
      best = d; gun = z;
    }
    if (gun && gun->gun.suppressedBuiltIn) n = "zone_gun_supp";
    else if (gun && (gun->gun.launcher || gun->gun.cls == "launcher")) n = "zone_gun_rpg";
    else if (gun) n = "zone_gun_" + (gun->gun.cls.empty() ? std::string("ar") : gun->gun.cls);
    else n = pitch >= 1.4f ? "zone_gun_pistol" : pitch <= 0.8f ? "zone_gun_sniper" : pitch <= 0.95f ? "zone_gun_shotgun" : "zone_gun_ar";
    pitch = 0.97f + 0.06f * std::fmod(std::fabs(pos.x * 7.13f + pos.z * 3.7f), 1.f);
  } else if (n == "firework_launch") {
    if (pitch < 0.65f) { n = "zone_gun_rpg"; pitch = 1; }
    else if (pitch < 0.75f) { n = "zone_gun_gl"; pitch = 1; }
  } else if (n == "bow" && pitch < 0.6f) { n = "zone_gun_supp"; pitch = 1; }
  else if (n == "click") { n = pitch < 0.9f ? "zone_gun_jam" : "zone_gun_dry"; pitch = 1; }
  else if (n == "click_off" && pitch < 0.9f) { n = "zone_gun_mag_in"; pitch = 1; }
  else if (n == "door_iron_close" && pitch >= 1.9f) { n = "zone_gun_mag_out"; pitch = 1; }
  else if (n == "door_iron_open" && pitch >= 1.7f) { n = "zone_gun_rack"; pitch = 1; }
  if (n.rfind("zone_gun_", 0) != 0 || n == "zone_gun_supp" || n.find('_', 9) != std::string::npos) return n;
  if (n == "zone_gun_dry" || n == "zone_gun_jam" || n == "zone_gun_rack") return n;
  const Entity* me = s.player;
  const float d = me ? glm::distance(me->pos, pos) : 0.f;
  const int realm = me ? me->realm : REALM_MAIN;
  if (World* w = s.level->worldIfOpen(realm)) {
    const int sky = w->getLight((int)std::floor(pos.x), (int)std::floor(pos.y), (int)std::floor(pos.z)) >> 4;
    if (sky < 9 || realm != REALM_MAIN) return n + "_in";
  }
  if (d > 48.f) return n + "_far";
  if (d > 13.f) return n + "_mid";
  return n;
}
void ClientFx::sound(const std::string& nameIn, vec3 pos, float range, float volume, float pitchIn) {
  float pitch = pitchIn;
  const std::string name = zoneSoundName(s, nameIn, pos, pitch);
  float hear = range;
  if (name == "zone_gun_supp") hear = std::min(range, 18.f);   // the cough does not carry like a crack
  if (s.app.audio) s.app.audio->playAt(name, pos, hear, volume, pitch);
  if (s.app.settings.captions && s.player && glm::distance(s.player->pos, pos) < std::max(8.f, hear)) hudCaption(captionForSound(name), pos, true);
}
// The Geiger is the tube, and its rate is already the dose (sim/zone_core.cpp). This only swaps the sample
// while that dose is real, so a lamp in clean air stays a UI click. The detector's search pip is not that click.
// Inventory navigation calls AudioEngine::play("tick") itself and does not come through here.
void ClientFx::soundUi(u32 playerId, const std::string& name) {
  if (!s.app.audio || (playerId && s.player && playerId != s.player->id)) return;
  if (s.level && s.player && zone::zoneWorld(*s.level)) {
    float dose = 0;
    if (s.player->stats.contains("zone") && s.player->stats["zone"].is_object()) {
      dose = s.player->stats["zone"].value("env", 0.f);
      for (const ItemStack& st : s.player->inv) {
        if (st.empty()) continue;
        if (const zone::ZItem* z = zone::zi(st.id); z && z->cat == "artifact") dose += z->raw["artifact"].value("rad", 0.f) * 20.f;
      }
    }
    if (name == "tick") { s.app.audio->play("zone_det_beep", 0.55f, 0.97f + 0.06f * std::fmod(std::fabs((float)s.level->info.time * 0.17f), 1.f)); return; }
    if (name == "click" && dose > 0.001f) { s.app.audio->play("zone_geiger", 0.62f, 0.94f + 0.1f * std::fmod(dose * 1.7f + 0.13f, 1.f)); return; }
    if (name == "rift_hum") return;   // the tower siren is the warning
  }
  s.app.audio->play(name);
}
// The original plays your own steps, digging, placing and breaking flat and at full loudness (sfx.material);
// only sounds out in the world are placed (materialAt: half loudness, fading with distance, muffled by walls).
// Anything within arm's reach of the player is yours.
void ClientFx::material(const std::string& mat, const std::string& kind, vec3 pos) {
  if (!s.app.audio) return;
  if (s.player && glm::distance(s.player->pos + vec3(0, 0.9f, 0), pos) < 6.5f) { s.app.audio->material(mat, kind); return; }
  s.app.audio->materialAt(mat, kind, pos);
  if (s.app.settings.captions && s.player && glm::distance(s.player->pos, pos) < 20) hudCaption(captionForMaterial(mat, kind), pos, true);   // (not your own)
}
void ClientFx::voice(const std::string& creature, const std::string& kind, vec3 pos, float pitch) {
  if (s.app.audio) s.app.audio->voice(creature, kind, pos, pitch);
  if (s.app.settings.captions && s.player && glm::distance(s.player->pos, pos) < 24) hudCaption(captionForVoice(creature, kind), pos, true);
}

// The music of the place (music2.js biomeMood), read from the overworld generator's column under the player.
// TODO(gen): use the biome itself (and villages nearby) once the generators expose them.
static std::string placeMood(Level& lv, vec3 p) {
  if (zone::zoneWorld(lv)) return "zone";   // Exclusion: the Zone's own ambience (audio/music.cpp)
  if (!lv.gen) return "plains";
  DimGenerator* g = lv.gen->forDim(DIM_OVERWORLD);
  if (!g) return "plains";
  ColumnSample c; g->sample((int)std::floor(p.x), (int)std::floor(p.z), c);
  const std::string& top = reg().block(c.top).key, &leaves = reg().block(c.leaves).key;
  if (c.water >= 0 && c.water > c.height) return "ocean";
  // Better than Adventure's lands, each its own music (gen/overworld.h BtaVariant)
  static const char* BV[] = {"", "redlands", "glacier", "meadow", "mossmire", "thornscrub", "highlands", "tundra", "seasonal"};
  if (c.variant > 0 && c.variant < 9) return BV[c.variant];
  if (top.find("snow") != std::string::npos || top.find("ice") != std::string::npos) return "frost";
  if (top == "sand" || top.find("sandstone") != std::string::npos) return "desert";
  if (top.find("red_sand") != std::string::npos || top.find("terracotta") != std::string::npos) return "savanna";
  if (top.find("mud") != std::string::npos || top.find("clay") != std::string::npos) return "swamp";
  if (c.height > 64 + 48) return "peaks";
  if (c.tree && leaves.find("jungle") != std::string::npos) return "jungle";
  if (c.tree) return "forest";
  return "plains";
}

class AudioLayer : public RenderLayer {
public:
  explicit AudioLayer(GameSession& s) : sess(s) {}
  ~AudioLayer() override;
  const char* name() const override { return "audio"; }
  void update(GameSession& s, double dt) override {
    AudioEngine* a = s.app.audio.get();
    if (!a || !s.player || dt <= 0) return;
    Player& p = *s.player;
    World& w = s.world();
    // footsteps (local.js): every 2.2 blocks walked on the ground, the block underfoot's own step
    vec3 mv = p.pos - lastPos; lastPos = p.pos;
    float hs = std::sqrt(mv.x * mv.x + mv.z * mv.z) / (float)dt;
    if (hs > 50) hs = 0;   // teleported
    if (p.onGround && !p.flying && hs > 1 && !s.paused()) {
      stepAcc += hs * (float)dt;
      if (stepAcc > 2.2f) {
        stepAcc = 0;
        BlockId id = w.getBlock((int)std::floor(p.pos.x), (int)std::floor(p.pos.y - 0.2f), (int)std::floor(p.pos.z));
        if (id != AIR) a->material(reg().block(id).sound, "step");
      }
    }
    // what the music and the ambience need
    AudioScene sc;
    sc.inWorld = true; sc.paused = s.paused(); sc.dead = p.hp <= 0; sc.creative = p.creative();
    sc.world = &w; sc.player = p.pos; sc.head = vec3(s.frame.cam.pos);
    sc.dim = dimOfY((int)std::floor(p.pos.y));
    ivec3 h((int)std::floor(sc.head.x), (int)std::floor(sc.head.y), (int)std::floor(sc.head.z));
    sc.eyeWater = reg().tLiquid[w.getBlock(h.x, h.y, h.z)] == 1;
    sc.skyAtHead = w.getLight(h.x, h.y + 0, h.z) >> 4;
    double t = s.level->dayFraction();                                    // game.js computeEnv: env.day
    float sh = (float)std::sin(t * 2 * 3.14159265358979), x = std::clamp((sh + 0.2f) / 0.45f, 0.f, 1.f);
    sc.day = 0.2f + 0.8f * x * x * (3 - 2 * x);
    sc.time = s.level->info.time;
    if ((placeT -= dt) <= 0) {
      placeT = 2; place = placeMood(*s.level, p.pos);
      land = -1;
      if (sc.dim == DIM_NETHER && p.realm == REALM_MAIN && s.level->info.genVersion >= 5) {
        u8 a, b; float m;
        netherLandAt(s.level->info.seed, s.level->info.genVersion, (int)std::floor(p.pos.x), (int)std::floor(p.pos.y), (int)std::floor(p.pos.z), a, b, m);
        land = a;
      }
    }
    sc.place = place;
    sc.netherLand = sc.dim == DIM_NETHER ? land : -1;
    sc.worldSeed = (u32)s.level->info.seed; sc.realm = p.realm;
    // a jukebox with a disc in it within 40 blocks: its music plays, louder the nearer (looked for once a second)
    if ((jukeT -= dt) <= 0) {
      jukeT = 1; disc.clear(); discD = 1e9f; discAt = vec3(0);
      const int pcx = (int)std::floor(p.pos.x) >> 4, pcz = (int)std::floor(p.pos.z) >> 4, pd = dimOfY((int)std::floor(p.pos.y));
      for (int dz = -3; dz <= 3; dz++) for (int dx = -3; dx <= 3; dx++) if (const Chunk* c = w.chunk(pd, pcx + dx, pcz + dz)) for (auto& [i, e] : c->blockEntities) {
        if (!e.is_object() || e.value("type", "") != "jukebox" || !e.contains("disc") || !e["disc"].is_string()) continue;
        const int lx = i & 15, lz = (i >> 4) & 15, ly = i >> 8;
        const vec3 at((pcx + dx) * 16 + lx + 0.5f, dimBase(pd) + ly + 0.5f, (pcz + dz) * 16 + lz + 0.5f);
        const float d = glm::distance(at, p.pos);
        if (d > 40 || d >= discD) continue;
        const ItemDef* def = reg().item(e["disc"].get<std::string>());
        if (!def) continue;
        discD = d; disc = def->str("disc"); discAt = at;
      }
    }
    if (!disc.empty()) {
      sc.jukebox = true; sc.jukeboxDisc = disc; sc.jukeboxWorld = true;
      sc.jukeboxAt = discAt; sc.jukeboxRange = 40.f; sc.jukeboxVol = 1.f; sc.jukeboxGain = 1.f;
    }
    // Exclusion: the radio in the bar and the camps' hubs, and your PDA's radio. Which one wins is a
    // distance rank. place() then makes that one quieter, duller and off to one side. A stalker's guitar
    // is not this bed — it is a sound at the guitarist (campGuitar, below).
    bool campOn = false;
    if (zone::zoneWorld(*s.level) && p.realm == REALM_MAIN) {
      if ((zoneT -= dt) <= 0) {
        zoneT = 0.25f; zoneDisc.clear(); zoneRank = 0; zonePda = false;
        auto offer = [&](const char* d, vec3 at, float range, float vol, bool pda = false) {
          const float dist = glm::distance(at, p.pos);
          if (dist > range) return;
          const float ref = std::clamp(range * 0.18f, 2.2f, 8.f);
          const float att = dist > 0.05f ? std::min(1.f, ref / (0.75f * dist)) : 1.f;
          const float rank = att * vol;
          if (rank <= zoneRank) return;
          zoneRank = rank; zoneDisc = d; zoneAt = at; zoneRange = range; zoneVol = vol; zonePda = pda;
        };
        const zone::Plan& P = zone::zonePlan((u32)s.level->info.seed);
        const float baseY = (float)dimBase(DIM_OVERWORLD);
        for (const zone::Smart& m : P.smarts) if (m.kind == zone::SM_HUB)
          offer("zone_radio", vec3(m.x + 0.5f, baseY + m.y + 1.6f, m.z + 0.5f), 48.f, 0.9f);
        if (p.stats.contains("zone")) {
          const int st = p.stats["zone"].value("radio", -1);
          if (st == 0) offer("zone_radio", p.pos + vec3(0, 1.4f, 0), 6.f, 0.62f, true);
          else if (st == 1) offer("zone_guitar", p.pos + vec3(0, 1.4f, 0), 6.f, 0.62f, true);
        }
      }
      if (zonePda) zoneAt = p.pos + vec3(0, 1.4f, 0);
      if (!zoneDisc.empty() && !sc.jukebox) {
        sc.jukebox = true; sc.jukeboxDisc = zoneDisc; sc.jukeboxWorld = true;
        sc.jukeboxAt = zoneAt; sc.jukeboxRange = zoneRange; sc.jukeboxVol = zoneVol; sc.jukeboxGain = 1.f;
      }
      sc.zonePsy = zone::psyAt((int)std::floor(p.pos.x), (int)std::floor(p.pos.z));
      {   // stalkers standing about within earshot (two or more together, not fighting): their talk (audio/ambience.cpp)
        float bd = 30; int near = 0; vec3 at(0);
        for (auto& e : s.level->entities())
          if (!e->removed && e->realm == p.realm && e->type.rfind("z_npc_", 0) == 0 && e->hp > 0) {
            const float d = glm::distance(e->pos, p.pos);
            if (d > 34) continue;
            const auto* c = dynamic_cast<const Creature*>(e.get());
            if (c && (c->state == "attack" || c->state == "flee")) continue;
            near++;
            if (d < bd) { bd = d; at = e->pos; }
          }
        // zoneBarkUntil: a stalker has a line up, so the murmur waits (declared with the other dq:: names above)
        sc.zoneTalk = near >= 2 && bd < 30 && s.app.time >= zoneBarkUntil; sc.zoneTalkAt = at;
      }
      {   // the nearest of the Generators' energy spheres within earshot (gen/zone_gen.cpp: the ball's middle 11 above the ground)
        const zone::Plan& PS = zone::zonePlan((u32)s.level->info.seed);
        float bd = 55 * 55; sc.zoneHum = false;
        for (const auto& sp : PS.spheres) {
          const float dx = sp[0] + 0.5f - p.pos.x, dz = sp[2] + 0.5f - p.pos.z, d2 = dx * dx + dz * dz;
          if (d2 < bd) { bd = d2; sc.zoneHum = true; sc.zoneHumAt = vec3(sp[0] + 0.5f, (float)(dimBase(dimOfY((int)std::floor(p.pos.y))) + sp[1] + 11), sp[2] + 0.5f); }
        }
      }
      { const zone::Emission E = zone::emission(*s.level); sc.zoneEm = E.phase; sc.zoneEmT = E.t; sc.zoneEmLeft = E.left; sc.zoneEmStrength = E.strength; }
      campOn = true;
    }
    if (s.paused()) campOn = false;
    if (campOn) campGuitar(s, a, dt, p);
    else stopCamp(a);
    if (WeatherSystem* W = weatherOf(*s.level); W && p.realm == REALM_MAIN && sc.dim == DIM_OVERWORLD) {
      sc.rain = W->rainLevel; sc.gust = W->gust; sc.zoneFront = W->approach;   // the front, before the rain, is the pre-storm bed
    }
    // the nearest boss within 40 blocks (music2.js bossNear); (sleeping dragons and tamed ones don't count)
    float bd = 40;
    for (auto& e : s.level->entities()) {
      if (e->removed || e->realm != p.realm || e->isPlayer()) continue;
      auto it = reg().mobs.find(e->type);
      if (it == reg().mobs.end() || !it->value("boss", false)) continue;
      if (auto* c = dynamic_cast<Creature*>(e.get()); c && (c->state == "sleep" || !c->alive() || c->tamed)) continue;   // sleeping dragons don't count
      float d = glm::distance(e->pos, p.pos);
      if (d < bd) { bd = d; sc.boss = true; sc.bossId = e->id; sc.bossHp = e->maxHp > 0 ? e->hp / e->maxHp : 1;
        sc.bossDragon = e->type.find("dragon") != std::string::npos; sc.bossEerie = e->type == "herobrine"; }
    }
    sc.zoneEnding = false;
    if (Screen* top = s.app.topScreen()) sc.zoneEnding = std::string(top->id()) == "ending";
    a->setScene(sc);
  }
  void stopCamp(AudioEngine* a);
  // one zone_camp_guitar at a time per fire, from the lowest-id stalker there whose hands are on the guitar (hw.gt)
  void campGuitar(GameSession& s, AudioEngine* a, double dt, const Player& p);
  struct CampSong { u32 id = 0; int handle = 0; int prev = 0; double left = 0; };
  std::vector<CampSong> campSongs;
private:
  GameSession& sess;
  vec3 lastPos{0}; int land = -1; float jukeT = 0, discD = 1e9f; std::string disc; vec3 discAt{0};
  float zoneT = 0, zoneRank = 0, zoneRange = 36.f, zoneVol = 1.f; bool zonePda = false; vec3 zoneAt{0}; std::string zoneDisc;
  float stepAcc = 0;
  double placeT = 0;
  std::string place = "plains";
};

AudioLayer::~AudioLayer() {
  stopCamp(sess.app.audio.get());
  if (sess.app.audio) sess.app.audio->setScene(AudioScene());
}

void AudioLayer::stopCamp(AudioEngine* a) {
  if (a) for (auto& c : campSongs) {
    if (c.handle) a->stop(c.handle);
    if (c.prev) a->stop(c.prev);
  }
  campSongs.clear();
}

namespace {
bool guitarOn(const Entity& e) {
  if (!e.data.is_object()) return false;
  const auto h = e.data.find("hw");
  if (h == e.data.end() || !h->is_object()) return false;
  const auto v = h->find("gt");
  if (v == h->end()) return false;
  if (v->is_boolean()) return v->get<bool>();
  if (v->is_number()) return v->get<float>() > 0.5f;
  return false;
}
}

void AudioLayer::campGuitar(GameSession& s, AudioEngine* a, double dt, const Player& p) {
  struct Gtr { u32 id; vec3 pos; };
  std::vector<Gtr> g;
  for (auto& e : s.level->entities()) {
    if (!e || e->removed || e->realm != p.realm || e->hp <= 0) continue;
    if (e->type.rfind("z_npc_", 0) != 0) continue;
    if (!guitarOn(*e)) continue;
    if (glm::distance(e->pos, p.pos) > 31.f) continue;
    g.push_back({e->id, e->pos + vec3(0, 0.8f, 0)});
  }
  std::sort(g.begin(), g.end(), [](const Gtr& a, const Gtr& b) { return a.id < b.id; });
  std::vector<Gtr> picks;
  for (const Gtr& n : g) {
    bool taken = false;
    for (const Gtr& k : picks) if (std::hypot(k.pos.x - n.pos.x, k.pos.z - n.pos.z) < 9.f) { taken = true; break; }
    if (!taken) picks.push_back(n);
  }
  for (auto it = campSongs.begin(); it != campSongs.end();) {
    bool still = false;
    for (const Gtr& k : picks) if (k.id == it->id) { still = true; break; }
    if (!still) {
      if (it->handle) a->stop(it->handle);
      if (it->prev) a->stop(it->prev);
      it = campSongs.erase(it);
    } else ++it;
  }
  for (const Gtr& k : picks) {
    CampSong* song = nullptr;
    for (auto& c : campSongs) if (c.id == k.id) { song = &c; break; }
    if (!song) { campSongs.push_back({k.id, 0, 0, 0}); song = &campSongs.back(); }
    song->left -= dt;
    if (song->left > 0) continue;
    const int h = a->playAt("zone_camp_guitar", k.pos, 30.f, 0.95f, 1.f);
    if (!h) continue;
    a->follow(h, k.pos, 30.f, 0.95f, snd::kZoneCampGuitarLoop + 2.4);
    if (song->handle) song->prev = song->handle;
    song->handle = h;
    song->left = snd::kZoneCampGuitarLoop;
    if (s.app.settings.captions && glm::distance(p.pos, k.pos) < 32.f) hudCaption("Guitar", k.pos, true);
  }
}

void registerAudioLayers() {
  static bool done = false;
  if (done) return;
  done = true;
  registerRenderLayer([](GameSession& s) { return std::make_shared<AudioLayer>(s); });
}

} // namespace dq
