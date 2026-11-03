// sim/zone_anomaly.cpp — the anomalies at work (the roster and where each one sits: sim/zone_anomalies.h; their tells:
// render/zone_anomaly_fx.cpp), the artifacts they grow and the detectors that find them.
//  * AnoSys (a LevelSystem on every machine, so a client draws what the host simulates): keeps the anomalies near
//    players live, runs their clocks (charging, recharging, wandering, chasing), sets them off when something steps in
//    or a bolt flies in, and hurts creatures (the host). What it does to a player is queued for that player's own machine.
//  * anomalyBody (adjustInput, where the player is local): the pulls, burns, shocks, cuts, slowing, sinking and sleep.
//  * creatures (A-Life online, mutants) step round the anomalies they know (creatureSteerHook, sim/creature.cpp).
//  * artifacts: per field and emission, each family its own; a held detector of a high enough tier brings them out.
#include "sim/zone.h"
#include "sim/controller.h"
#include "sim/combat.h"
#include "sim/gameplay.h"
#include "sim/creature.h"
#include "sim/steam.h"
#include "core/registry.h"
#include "sim/zone_anomalies.h"
#include "sim/commands.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <map>
#include <set>

namespace dq { extern void (*creatureSteerHook)(Level&, Creature&, float& want, float& dir); }

namespace dq::zone {

namespace {
constexpr float NEAR_R = 72;
inline float frnd() { static u32 s = 0x2545F491u; s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFFFF) / 16777216.f; }
inline float tierMul(int t) { return 0.8f + 0.12f * t; }
inline float hyp(vec3 a) { return std::sqrt(a.x * a.x + a.z * a.z); }
inline vec3 flat(vec3 a) { const float l = hyp(a); return l > 1e-3f ? vec3(a.x / l, 0, a.z / l) : vec3(1, 0, 0); }
enum Res { R_PHYS, R_BURN, R_SHOCK, R_CHEM, R_RAD, R_PSY };

struct Hit { u32 who; float dmg; int res; vec3 knock; bool set; float fire, bleed, psy, flash; bool tp; vec3 to; double t; bool done = false; };
struct Thing { Entity* e; int k; vec3 p; float h; };   // k: 0 player, 1 creature, 2 item

std::string itemId(const Entity& e) {
  auto it = e.data.find("stack"); if (it == e.data.end() || !it->is_object()) return {};
  return it->value("id", std::string());
}

struct AnoSys : LevelSystem {
  std::vector<AnoLive> live;
  std::vector<Hit> hits;
  std::unordered_map<u32, AnoSense> sense;
  float scanT = 0, artT = 0;
  i64 lastCyc = -99;
  std::map<std::string, u32> artEnt;
  std::set<std::string> known;         // artifacts seen lying out there this emission (gone since = picked up)
  const char* name() const override { return "zone_anomalies"; }

  void tick(Level& lv, double dtd) override {
    if (!zoneWorld(lv)) return;
    const float dt = (float)std::min(dtd, 0.1);
    if ((scanT -= dt) <= 0) { scanT = 0.5f; rescan(lv); }
    if (!lv.isRemote() && (artT -= dt) <= 0) { artT = 0.5f; artifacts(lv); }
    const double now = lv.info.time;
    hits.erase(std::remove_if(hits.begin(), hits.end(), [&](const Hit& h) { return h.done || now - h.t > 1.0 || now < h.t; }), hits.end());
    if (live.empty()) return;
    // what can set them off
    std::vector<Thing> things;
    for (auto& up : lv.entities()) {
      Entity* e = up.get();
      if (e->removed || e->realm != REALM_MAIN) continue;
      if (e->isPlayer()) { auto* p = static_cast<Player*>(e); if (!p->dead && p->mode != "spectator") things.push_back({e, 0, e->pos, e->h}); continue; }
      if (e->type == "item") { const std::string id = itemId(*e); const ZItem* z = zi(id); if (z && z->cat == "artifact") continue; if (!id.empty()) things.push_back({e, 2, e->pos, 0.3f}); continue; }
      if (auto* c = dynamic_cast<Creature*>(e); c && c->alive()) things.push_back({e, 1, e->pos, e->h});
    }
    for (AnoLive& L : live) step(lv, L, things, dt, now);
  }

  // ---- the live list: the anomalies within NEAR_R of a player, their state carried over
  void rescan(Level& lv) {
    const u32 seed = (u32)lv.info.seed;
    const int by = dimBase(DIM_OVERWORLD);
    std::vector<const ZAnomaly*> near;
    for (auto& up : lv.entities()) {
      if (!up->isPlayer() || up->removed || up->realm != REALM_MAIN) continue;
      zoneAnomaliesNear(seed, ifloor(up->pos.x), ifloor(up->pos.z), (int)NEAR_R, near);
    }
    std::sort(near.begin(), near.end()); near.erase(std::unique(near.begin(), near.end()), near.end());
    std::unordered_map<u32, size_t> old; for (size_t i = 0; i < live.size(); i++) old[live[i].id] = i;
    std::vector<AnoLive> next; next.reserve(near.size());
    for (const ZAnomaly* a : near) {
      if (auto it = old.find(a->id); it != old.end()) { next.push_back(std::move(live[it->second])); continue; }
      AnoLive L;
      L.id = a->id; L.kind = a->kind; L.family = a->family; L.tier = a->tier; L.pool = a->pool; L.r = a->r;
      L.c = vec3(a->x + 0.5f, (float)(by + a->y), a->z + 0.5f); L.pos = L.c;
      L.to = vec3(a->tx + 0.5f, (float)(by + a->ty), a->tz + 0.5f);
      L.charge = a->kind == AK_GAS ? 3.f + a->tier / 2 : 1.f;
      if (a->kind == AK_ARCNEST) L.charge = (a->id % 100) / 100.f;
      if (a->kind == AK_BLISTER) L.pos = L.c + vec3(0, 1.3f, 0);
      if (a->kind == AK_ASHMOSS) {   // strands from the undersides of the tree's limbs
        int H; std::vector<ivec3> limbs; ashMossTree(*a, H, limbs);
        for (size_t i = 0; i < limbs.size(); i++) {
          const double h = hashFloat((i32)a->id, limbs[i].x, limbs[i].z, (i32)i);
          if (h > 0.8) continue;
          const vec3 top(limbs[i].x + 0.2f + (float)h * 0.6f, (float)(by + limbs[i].y), limbs[i].z + 0.8f - (float)h * 0.6f);
          const float len = std::min(1.3f + (float)hashFloat((i32)a->id, (i32)i, 7, 3) * 1.7f, top.y - L.c.y - 0.9f);
          if (len > 0.4f) L.bits.push_back(vec4(top, len));
        }
      }
      next.push_back(std::move(L));
    }
    live = std::move(next);
  }

  void push(const Hit& h) { hits.push_back(h); }
  void hurtThing(Level& lv, const Thing& t, const AnoLive& L, float cat, int res, vec3 knock = vec3(0), bool set = false, float fire = 0, float bleed = 0, float psy = 0, float flash = 0) {
    const float d = cat * tierMul(L.tier);
    if (t.k == 0) { push({t.e->id, d, res, knock, set, fire, bleed, psy, flash, false, vec3(0), lv.info.time}); return; }
    if (t.k != 1 || lv.isRemote()) return;
    if (d > 0) { DamageSource s; s.type = "anomaly"; s.knock = 0; s.bypass = true; damageEntity(lv, *t.e, d * DMG_SCALE, s); }
    if (knock != vec3(0)) t.e->vel = set ? knock : t.e->vel + knock;
    if (fire > 0) if (auto* c = dynamic_cast<Creature*>(t.e)) c->burn = std::max(c->burn, fire);
  }
  void sound(Level& lv, const char* s, vec3 at, float range = 32, float vol = 1, float pitch = 1) { if (!lv.isRemote()) lv.fx->sound(s, at, range, vol, pitch); }
  void parts(Level& lv, const char* k, vec3 at, int n) { if (!lv.isRemote()) lv.fx->particles(k, at, n); }
  AnoLive* partner(const AnoLive& L) { for (AnoLive& o : live) if (o.kind == AK_SKIP && std::abs(o.c.x - L.to.x) < 0.6f && std::abs(o.c.z - L.to.z) < 0.6f) return &o; return nullptr; }
  bool hostItem(const Level& lv, const Thing& t) const { return t.k == 2 && !lv.isRemote(); }

  // ---- one anomaly, one tick
  void step(Level& lv, AnoLive& L, std::vector<Thing>& things, float dt, double now) {
    L.cd = std::max(0.f, L.cd - dt); L.fire = std::max(0.f, L.fire - dt); L.hitT = std::max(0.f, L.hitT - dt);
    L.st -= dt; L.dotT -= dt;
    const bool dot = L.dotT <= 0; if (dot) L.dotT = 0.25f;   // creatures' burns and shocks come in quarter-second bites
    auto rel = [&](const Thing& t) { return t.p - L.c; };
    auto inside = [&](const Thing& t, float r, float lo, float hi) { const vec3 d = rel(t); return hyp(d) < r && d.y > lo && d.y < hi; };
    auto mid = [&](const Thing& t) { return t.p + vec3(0, t.h * 0.5f, 0); };
    switch (L.kind) {
      case AK_UNDERTOW: {
        if (L.cd > 0) break;
        const float R = L.r + 2.5f;
        bool core = false;
        for (Thing& t : things) {
          if (!inside(t, R, -1.5f, 4.f)) continue;
          const vec3 d = rel(t); const float dh = hyp(d);
          if (t.k != 0 && !lv.isRemote()) {   // the pull on creatures and bolts (players: anomalyBody)
            const float k = (L.state ? 30.f : 6.f + 26.f * (1 - dh / R) * (1 - dh / R)) * (t.k == 2 ? 1.6f : 1.f);
            t.e->vel += glm::normalize(L.c + vec3(0, 0.4f, 0) - mid(t) + vec3(0, 0.001f, 0)) * dt * k;
          }
          if (dh < 1.3f && d.y < 2.5f) core = true;
        }
        if (L.state == 0 && core) { L.state = 1; L.st = 0.55f; sound(lv, "zone_ano_suck", L.c, 30); }
        if (L.state == 1 && L.st <= 0) {   // the burst
          L.state = 0; L.fire = 0.45f; L.cd = 5 + frnd() * 2;
          sound(lv, "zone_ano_burst", L.c, 48); parts(lv, "dust", L.c + vec3(0, 1, 0), 40); parts(lv, "explosion", L.c + vec3(0, 0.8f, 0), 4);
          for (Thing& t : things) {
            if (!inside(t, 2.9f, -1.f, 3.5f)) continue;
            const vec3 out = flat(rel(t)) * 8.f + vec3(0, 9, 0);
            if (t.k == 2) { if (hostItem(lv, t)) t.e->vel = vec3((frnd() - 0.5f) * 8, 12, (frnd() - 0.5f) * 8); continue; }
            hurtThing(lv, t, L, 70, R_PHYS, out, true, 0, 1.5f);
            if (t.k == 0) lv.fx->shake(t.e->id, 0.5f);
          }
        }
        break;
      }
      case AK_KICKBACK:
        if (L.cd > 0) break;
        for (Thing& t : things) {
          if (L.st > 0 || !inside(t, L.r, -0.5f, 2.5f)) continue;
          L.fire = 0.35f; L.st = 0.6f;
          sound(lv, "zone_ano_thump", L.c, 30); parts(lv, "gust", L.c + vec3(0, 1, 0), 10);
          const vec3 out = flat(rel(t));
          if (t.k == 2) { if (hostItem(lv, t)) t.e->vel = vec3(-t.e->vel.x * 0.4f, 4, -t.e->vel.z * 0.4f) + out * 3.f; L.cd = 3.f; L.lastItem = t.e->id; continue; }   // a bolt knocks it out a while
          hurtThing(lv, t, L, 15, R_PHYS, out * 12.f + vec3(0, 5, 0), true);
        }
        break;
      case AK_SPINNER:
        if (L.state == 0) {
          if (L.cd > 0) break;
          for (Thing& t : things) if (inside(t, L.r, -0.5f, 2.f)) { L.state = 1; L.target = t.e->id; L.st = 1.8f; sound(lv, "zone_ano_spin", L.c, 32); break; }
        } else {
          Entity* e = lv.entity(L.target);
          const vec3 hold = L.c + vec3(0, 1.4f + (1.8f - std::max(0.f, L.st)) * 1.5f, 0);
          const float a = (float)now * (5 + (1.8f - std::max(0.f, L.st)) * 6);
          if (e && !e->removed && !e->isPlayer() && !lv.isRemote()) e->vel = (hold + vec3(std::cos(a), 0, std::sin(a)) * 0.7f - e->pos) * 7.f;
          if (!e || e->removed || L.st <= 0) {   // tear and fling
            L.state = 0; L.cd = 4.5f; L.fire = 0.4f;
            sound(lv, "zone_ano_burst", hold, 40, 0.9f, 1.3f); parts(lv, "dust", hold, 30);
            for (Thing& t : things) {
              const bool held = t.e->id == L.target;
              if (!held && !inside(t, L.r + 0.5f, -0.5f, 5.f)) continue;
              const vec3 out = flat(rel(t)) * 10.f + vec3(0, 4, 0);
              if (t.k == 2) { if (hostItem(lv, t)) t.e->vel = out * 1.5f; continue; }
              hurtThing(lv, t, L, held ? 45 : 20, R_PHYS, out, true, 0, held ? 2.f : 0.f);
            }
            L.target = 0;
          }
        }
        break;
      case AK_SHARDS:
        for (Thing& t : things) {
          if (!inside(t, L.r, -0.5f, 2.f)) continue;
          if (t.k == 2) { if (L.lastItem != t.e->id) { L.lastItem = t.e->id; sound(lv, "zone_ano_chime", t.p, 20, 0.7f, 1.2f); } continue; }   // bolts pass through
          if (t.k == 1 && dot && hyp(t.e->vel) > 3.5f) hurtThing(lv, t, L, 3, R_PHYS);
        }
        break;
      case AK_SKIP:
        if (L.cd > 0) break;
        for (Thing& t : things) {
          if (!inside(t, L.r, -0.5f, 2.f)) continue;
          const vec3 to = L.to + flat(L.to - L.c) * 1.6f + vec3(0, 0.1f, 0);   // out of the partner, a stride on
          L.cd = 3; L.fire = 0.5f; if (AnoLive* o = partner(L)) { o->cd = 3; o->fire = 0.5f; }
          sound(lv, "zone_ano_skip", L.c, 30); sound(lv, "zone_ano_skip", L.to, 30, 1, 0.8f); parts(lv, "wisp", L.c + vec3(0, 1, 0), 16); parts(lv, "wisp", L.to + vec3(0, 1, 0), 16);
          if (t.k == 0) { Hit h{t.e->id, 5 * tierMul(L.tier), R_PHYS, vec3(0), true, 0, 0, 0, 0, true, to, now}; push(h); }
          else if (!lv.isRemote()) { t.e->pos = t.e->prevPos = to + vec3(0, 0.3f, 0); t.e->vel = flat(L.to - L.c) * 2.f; if (t.k == 1) hurtThing(lv, t, L, 5, R_PHYS); }
          break;
        }
        break;
      case AK_FLARE:
        if (L.fire > 0) {
          for (Thing& t : things) if (inside(t, L.r + 0.3f, -0.5f, 4.5f)) {
            if (t.k == 1 && dot) hurtThing(lv, t, L, 10, R_BURN, vec3(0), false, 3);
            if (hostItem(lv, t)) t.e->vel.y = std::max(t.e->vel.y, 6.f);
          }
        } else if (L.cd <= 0) {
          for (Thing& t : things) if (inside(t, L.r + 0.5f, -0.5f, 2.5f)) { L.fire = 2.2f; L.cd = 5.7f; sound(lv, "zone_ano_flare", L.c, 40); parts(lv, "flame", L.c + vec3(0, 0.5f, 0), 30); break; }
        }
        break;
      case AK_WANDERFIRE: {
        const float R = L.r, ph = (L.id % 628) / 100.f, th = (float)now * 2.6f / R + ph;
        const vec3 loop = L.c + vec3(std::cos(th) * R * (1 + 0.25f * std::sin(2 * th)), 1.1f + 0.3f * std::sin(3 * th), std::sin(th) * R * (1 + 0.25f * std::sin(2 * th)));
        vec3 want = loop; float speed = 3.5f;
        if (L.state == 1) {
          Entity* e = lv.entity(L.target);
          if (!e || e->removed || L.st <= 0 || hyp(e->pos - L.c) > R + 12) { L.state = 0; L.cd = 6; }
          else { want = e->pos + vec3(0, e->type == "item" ? 0.2f : 1.f, 0); speed = 4.4f; }
        } else if (L.cd <= 0) {
          for (Thing& t : things) {
            const float d = glm::length(mid(t) - L.pos);
            if ((t.k == 2 && d < 3.5f) || (t.k != 2 && d < 7 && hyp(t.p - L.c) < R + 8)) { L.state = 1; L.target = t.e->id; L.st = t.k == 2 ? 2.f : 3.f; sound(lv, "zone_ano_flare", L.pos, 30, 0.6f, 1.4f); break; }
          }
        }
        const vec3 d = want - L.pos; const float l = glm::length(d);
        if (l > 25) L.pos = loop; else if (l > 1e-3f) L.pos += d * std::min(1.f, speed * dt / l);
        for (Thing& t : things) if (t.k == 1 && dot && glm::length(mid(t) - L.pos) < 1.5f) hurtThing(lv, t, L, 9, R_BURN, vec3(0), false, 3);
        break;
      }
      case AK_STEAM: {
        const int bx = ifloor(L.c.x), by = ifloor(L.c.y) - 1, bz = ifloor(L.c.z);
        const bool gust = steamGust(now, bx, by, bz);
        if (gust && L.fire <= 0) sound(lv, "zone_ano_steam", L.c, 30);
        L.fire = gust ? 0.2f : 0.f;
        if (gust) for (Thing& t : things) if (inside(t, L.r + 0.3f, -0.5f, 5.f)) {
          if (t.k == 1) { if (dot) hurtThing(lv, t, L, 5, R_BURN); if (!lv.isRemote()) t.e->vel.y = std::max(t.e->vel.y, 5.5f); }
          if (hostItem(lv, t)) t.e->vel.y = std::max(t.e->vel.y, 9.f);
        }
        break;
      }
      case AK_ASHMOSS:
        for (Thing& t : things) {
          if (t.k == 0 || hyp(rel(t)) > 3.5f) continue;
          for (const vec4& s : L.bits) {
            if (std::hypot(t.p.x - s.x, t.p.z - s.z) > (t.k == 2 ? 0.35f : 0.5f) || t.p.y > s.y || t.p.y + t.h < s.y - s.w) continue;
            if (t.k == 2) { if (L.lastItem != t.e->id) { L.lastItem = t.e->id; L.fire = 0.6f; L.hitAt = vec3(s.x, t.p.y, s.z); sound(lv, "zone_ano_sizzle", t.p, 20); parts(lv, "ember_spark", t.p, 10); } }
            else if (dot) hurtThing(lv, t, L, 5, R_BURN, vec3(0), false, 2);
            break;
          }
        }
        break;
      case AK_ARCNEST: {
        if (L.cd <= 0) L.charge = std::min(1.f, L.charge + dt / (4.4f - L.tier * 0.4f));
        if (L.charge < 0.45f) break;
        Thing* best = nullptr; float bd = 1e9f;
        for (Thing& t : things) if (inside(t, L.r + 0.3f, -0.5f, 3.f)) { const float d = hyp(rel(t)); if (d < bd) { bd = d; best = &t; } }
        if (best) {
          L.fire = 0.3f; L.hitAt = mid(*best); L.hitT = 0.35f;
          sound(lv, "zone_ano_zap", L.c, 36); parts(lv, "spark", L.hitAt, 24);
          if (best->k == 2) { if (hostItem(lv, *best)) best->e->vel *= 0.2f; }
          else for (Thing& t : things) if (t.k != 2 && inside(t, L.r + 0.3f, -0.5f, 3.f)) hurtThing(lv, t, L, 45 * L.charge, R_SHOCK, flat(rel(t)) * 4.f + vec3(0, 2, 0));
          L.charge = 0; L.cd = 0.8f;
        } else if (L.charge >= 1 && frnd() < dt * 0.45f) {   // it lets go at nothing
          const float a = frnd() * 6.283f, d = L.r * (0.4f + frnd() * 0.6f);
          L.fire = 0.2f; L.hitAt = L.c + vec3(std::cos(a) * d, 0.05f, std::sin(a) * d); L.hitT = 0.3f; L.charge = 0.55f;
          sound(lv, "zone_ano_zap", L.c, 28, 0.55f, 1.2f);
        }
        break;
      }
      case AK_BALL: {
        if (L.hidden > 0) { L.hidden -= dt; }
        const float R = L.r, ph = (L.id % 628) / 100.f, th = (float)now * 3.f / R + ph, rr = R * (1 + 0.22f * std::sin(3 * th));
        L.pos = L.c + vec3(std::cos(th) * rr, 1.3f + 0.4f * std::sin(5 * th), std::sin(th) * rr);
        if (L.hidden > 0) break;
        for (Thing& t : things) {
          if (glm::length(mid(t) - L.pos) > (t.k == 2 ? 1.2f : 1.6f)) continue;
          L.hidden = 2.5f; L.fire = 0.3f; L.hitAt = mid(t); L.hitT = 0.3f;
          sound(lv, "zone_ano_zap", L.pos, 36, 1, 0.9f); parts(lv, "spark", L.pos, 30);
          if (t.k != 2) hurtThing(lv, t, L, 30, R_SHOCK, flat(t.p - L.pos) * 6.f + vec3(0, 3, 0));
          break;
        }
        break;
      }
      case AK_FLASHPOP:
        if (L.cd > 0) break;
        for (Thing& t : things) {
          if (!inside(t, L.r, -0.5f, 2.f)) continue;
          L.fire = 0.25f; L.cd = 5;
          sound(lv, "zone_ano_pop", L.c, 36); parts(lv, "spark", L.c + vec3(0, 0.6f, 0), 30);
          for (Thing& o : things) {
            if (o.k == 2) continue;
            const float d = glm::length(mid(o) - L.c);
            if (d < 1.9f) hurtThing(lv, o, L, 25, R_SHOCK, flat(rel(o)) * 6.f + vec3(0, 4, 0), false, 0, 0, 0, 1.f);
            else if (o.k == 0 && d < 8) push({o.e->id, 0, R_SHOCK, vec3(0), false, 0, 0, 0, 1.f - d / 8, false, vec3(0), now});
          }
          break;
        }
        break;
      case AK_SPARKPOOL:
        for (Thing& t : things) {
          if (hyp(rel(t)) > L.r + 0.3f || t.p.y > L.c.y + 0.2f || t.p.y < L.c.y - 3.f) continue;
          if (t.k == 2) { if (L.lastItem != t.e->id) { L.lastItem = t.e->id; L.fire = 0.5f; L.hitAt = t.p; L.hitT = 0.4f; sound(lv, "zone_ano_zap", t.p, 26, 0.6f, 1.3f); parts(lv, "spark", t.p, 16); } }
          else if (t.k == 1 && dot) { hurtThing(lv, t, L, 3, R_SHOCK); if (!lv.isRemote()) t.e->vel += vec3(frnd() - 0.5f, 0.6f, frnd() - 0.5f) * 3.f; }
        }
        break;
      case AK_ACID:   // no reaction to bolts: they just sink
        for (Thing& t : things) if (t.k == 1 && dot && hyp(rel(t)) < L.r + 0.3f && t.p.y < L.c.y + 0.3f && t.p.y > L.c.y - 3.f) hurtThing(lv, t, L, 2.5f, R_CHEM);
        break;
      case AK_GAS: {
        const float full = 3.f + L.tier / 2;
        if (L.charge < full) L.charge = std::min(full, L.charge + dt / 40.f);
        if (L.fire > 0) { for (Thing& t : things) if (t.k == 1 && dot && inside(t, L.r + 1.5f, -1.f, 3.5f)) hurtThing(lv, t, L, 3, R_CHEM); }
        if (L.cd > 0 || L.charge < 1) break;
        for (Thing& t : things) if (inside(t, L.r, -0.5f, 2.5f)) { L.fire = 5; L.charge -= 1; L.cd = 2; sound(lv, "zone_ano_puff", L.c, 30); break; }
        break;
      }
      case AK_BLISTER: {
        const vec3 home = L.c + vec3(0, 1.3f, 0);
        // the small ones it burst into
        for (vec4& b : L.bits) {
          b.w -= dt; vec3 p(b);
          Thing* near = nullptr; float nd = 9;
          for (Thing& t : things) if (t.k != 2) { const float d = glm::length(mid(t) - p); if (d < nd) { nd = d; near = &t; } }
          if (near) p += glm::normalize(mid(*near) - p + vec3(0, 0.001f, 0)) * dt * 1.8f; else p.y += dt * 0.3f;
          if (near && nd < 1.0f) { hurtThing(lv, *near, L, 8, R_CHEM); sound(lv, "zone_ano_blister", p, 20, 0.6f, 1.6f); parts(lv, "slime", p, 6); b.w = 0; }
          b = vec4(p, b.w);
        }
        L.bits.erase(std::remove_if(L.bits.begin(), L.bits.end(), [](const vec4& b) { return b.w <= 0; }), L.bits.end());
        if (L.hidden > 0) { L.hidden -= dt; if (L.hidden <= 0) L.pos = home; break; }
        vec3 want; float speed = 0.8f;
        Entity* e = L.state == 1 ? lv.entity(L.target) : nullptr;
        if (e && !e->removed && glm::length(e->pos - L.pos) < 12 && glm::length(e->pos - home) < 16) { want = e->pos + vec3(0, 1.2f, 0); speed = 1.4f; }
        else {
          L.state = 0;
          const float ph = (L.id % 628) / 100.f;
          want = home + vec3(std::sin((float)now * 0.3f + ph) * 2.5f, 0.3f * std::sin((float)now * 0.7f), std::cos((float)now * 0.23f + ph) * 2.5f);
          for (Thing& t : things) if (t.k == 0 && glm::length(t.p - L.pos) < 9) { L.state = 1; L.target = t.e->id; break; }
        }
        const vec3 d = want - L.pos; const float l = glm::length(d);
        if (l > 1e-3f) L.pos += d * std::min(1.f, speed * dt / l);
        for (Thing& t : things) {
          if (glm::length(mid(t) - L.pos) > (t.k == 2 ? 1.2f : 1.5f)) continue;
          L.fire = 0.4f; L.hidden = 25; L.state = 0;
          sound(lv, "zone_ano_blister", L.pos, 30); parts(lv, "slime", L.pos, 16);
          for (Thing& o : things) if (o.k != 2 && glm::length(mid(o) - L.pos) < 2.f) hurtThing(lv, o, L, 20, R_CHEM);
          for (int i = 0; i < 4; i++) { const float a = i * 1.571f + frnd(); L.bits.push_back(vec4(L.pos + vec3(std::cos(a) * 0.8f, (frnd() - 0.3f) * 0.6f, std::sin(a) * 0.8f), 5.f)); }
          break;
        }
        break;
      }
      case AK_MIRE:
        if (lv.isRemote()) break;
        for (Thing& t : things) {
          if (t.k == 0 || hyp(rel(t)) > L.r + 0.2f || t.p.y > L.c.y - 0.1f || t.p.y < L.c.y - 3.5f) continue;
          t.e->vel.x *= std::pow(0.15f, dt); t.e->vel.z *= std::pow(0.15f, dt); t.e->vel.y -= 4 * dt;
          if (t.k == 1 && dot) hurtThing(lv, t, L, 0.5f, R_CHEM);
        }
        break;
      case AK_WISPS: {
        if (!lv.isNight()) { L.bits.clear(); L.hidden = 1; break; }
        L.hidden = 0;
        const int n = 3 + (int)(L.id % 3);
        while ((int)L.bits.size() < n) L.bits.push_back(vec4(L.c + vec3(0, 1.2f, 0), (float)L.bits.size() * 2.1f));
        Thing* lure = nullptr;
        for (Thing& t : things) if (t.k == 0 && hyp(rel(t)) < L.r + 6) { lure = &t; break; }
        for (vec4& w : L.bits) {
          const float i = w.w;
          vec3 want = L.c + vec3(std::sin((float)now * 0.4f + i) * L.r * 0.7f, 1.2f + 0.5f * std::sin((float)now * 0.9f + i), std::cos((float)now * 0.33f + i * 0.8f) * L.r * 0.7f);
          if (lure) want = glm::mix(want, mid(*lure) + vec3(0, 0.6f, 0), 0.35f);
          vec3 p(w); const vec3 d = want - p; const float l = glm::length(d);
          if (l > 1e-3f) p += d * std::min(1.f, 1.2f * dt / l);
          if (lure && glm::length(mid(*lure) + vec3(0, 0.6f, 0) - p) < 1.f) {   // it touches you, and flits off
            hurtThing(lv, *lure, L, 0, R_PSY, vec3(0), false, 0, 0, 12);
            sound(lv, "zone_ano_wisp", p, 24); p = L.c + vec3((frnd() - 0.5f) * L.r * 2, 1.5f, (frnd() - 0.5f) * L.r * 2);
          }
          w = vec4(p, i);
        }
        break;
      }
      default: break;
    }
  }

  // ---- artifacts: one or two per field each emission, only for a held detector good enough to find them
  void artifacts(Level& lv);
};

AnoSys* sys(Level& lv) { return lv.system<AnoSys>(); }

// ---------------------------------------------------------------- artifacts and detectors
int artifactCount(const AnomalyField& f) { return f.tier >= 4 ? 2 : 1; }
vec3 artifactPos(u32 seed, const AnomalyField& f, int k, i64 cycle, int by) {
  const double a = hashFloat((i32)seed, f.x + (i32)cycle * 31, f.z, 70 + k) * 6.2832, d = (0.3 + hashFloat((i32)seed, f.x, f.z + (i32)cycle, 71 + k) * 0.6) * f.r;
  const int x = f.x + (int)std::floor(std::cos(a) * d), z = f.z + (int)std::floor(std::sin(a) * d);
  const Ground g = groundAt(seed, x, z);
  return vec3(x + 0.5f, by + (g.water ? WATER : (int)std::round(g.h)) + 1.2f, z + 0.5f);
}
// each family's artifacts, commonest first
std::string artifactFor(u32 seed, const AnomalyField& f, int k, i64 cycle) {
  static const std::vector<std::vector<const char*>> FAM = {
      {"ember_knot", "kiln_glass", "night_candle"}, {"static_pearl", "spindle", "storm_eye"}, {"moth_wing", "gravel_heart", "shard_lens", "hollow_star"},
      {"rust_shell", "blood_coral", "iron_moss", "bog_pearl"}, {"sleepers_seed", "glass_eye", "mothers_tear"}};
  const auto& L = FAM[std::min<int>(f.family, A_COUNT - 1)];
  const double r = hashFloat((i32)seed, f.x, f.z + (i32)cycle * 7, 80 + k) + (f.tier - 1) * 0.07;
  const size_t t = r < 0.55 ? 0 : r < 0.84 ? 1 : r < 0.97 ? 2 : 3;
  const char* id = L[std::min(t, L.size() - 1)];
  return zi(id) ? id : L[0];
}
// the tier of detector it takes: Echo the plain ones, Bear up to tier 3, Veles everything
int artifactNeeds(const std::string& id) {
  const ZItem* z = zi(id); const int t = z ? z->raw["artifact"].value("tier", 1) : 1;
  return t <= 1 ? 1 : t <= 3 ? 2 : 3;
}
bool artifactTaken(const Level& lv, int fi, int k, i64 cycle) {
  auto it = lv.info.data.find("zoneArt"); if (it == lv.info.data.end() || !it->is_object()) return false;
  auto v = it->find(std::to_string(fi) + ":" + std::to_string(k)); return v != it->end() && v->is_number() && v->get<i64>() >= cycle;
}
int detectorTier(const Player& p, float& range) {
  range = 0;
  // only the one in your hand works (you hold it up in your left hand; render/zone_fpgun.cpp)
  if (p.selected >= 0 && p.selected < (int)p.inv.size()) if (const ZItem* z = p.inv[p.selected].empty() ? nullptr : zi(p.inv[p.selected].id); z && z->cat == "detector") {
    range = z->raw["detector"].value("range", 12.f); return z->raw["detector"].value("tier", 1);
  }
  return 0;
}
template <class F> void eachArtifact(Level& lv, vec3 at, float range, F f) {
  const Plan& P = zonePlan((u32)lv.info.seed);
  const i64 cyc = std::max<i64>(0, emissionCycle(lv) + 1);
  const int by = dimBase(DIM_OVERWORLD), x = ifloor(at.x), z = ifloor(at.z), R = (int)range + 40;
  std::vector<int> seen;
  for (int cz = (z - R) >> 6; cz <= (z + R) >> 6; cz++) for (int cx = (x - R) >> 6; cx <= (x + R) >> 6; cx++) {
    auto it = P.fieldGrid.find(cellKey(cx, cz)); if (it == P.fieldGrid.end()) continue;
    for (int fi : it->second) {
      if (std::find(seen.begin(), seen.end(), fi) != seen.end()) continue;
      seen.push_back(fi);
      const AnomalyField& fl = P.fields[fi];
      for (int k = 0; k < artifactCount(fl); k++) {
        if (artifactTaken(lv, fi, k, cyc)) continue;
        const vec3 a = artifactPos((u32)lv.info.seed, fl, k, cyc, by);
        if (glm::length(a - at) > range) continue;
        f(fi, k, cyc, a, artifactFor((u32)lv.info.seed, fl, k, cyc));
      }
    }
  }
}

void AnoSys::artifacts(Level& lv) {
  const i64 cyc = std::max<i64>(0, emissionCycle(lv) + 1);
  // what lies out there now (also after a reload): artifacts are tagged with their field and emission
  artEnt.clear();
  for (auto& up : lv.entities()) {
    Entity* e = up.get();
    if (e->removed || e->type != "item" || !e->data.contains("zoneArt")) continue;
    if (e->data.value("zoneCyc", (i64)-1) != cyc) { e->removed = true; continue; }   // an emission came through: the old ones are gone
    artEnt[e->data.value("zoneArt", std::string())] = e->id;
  }
  if (lastCyc != cyc) { lastCyc = cyc; known.clear(); }
  // picked up (or faded): taken for this emission
  for (const std::string& k : known) if (!artEnt.count(k)) { json& d = lv.info.data["zoneArt"]; if (!d.is_object()) d = json::object(); d[k] = cyc; }
  known.clear(); for (auto& [k, id] : artEnt) known.insert(k);
  std::vector<Player*> ps;   // (gathered first: dropItem adds to lv.entities())
  for (auto& pe : lv.entities()) if (auto* p = dynamic_cast<Player*>(pe.get())) ps.push_back(p);
  for (Player* p : ps) {
    if (!p || p->removed || p->dead || p->realm != REALM_MAIN) continue;
    float range; const int tier = detectorTier(*p, range); if (!tier) continue;
    eachArtifact(lv, p->pos, range, [&](int fi, int k, i64 c, vec3 a, const std::string& id) {
      const std::string key = std::to_string(fi) + ":" + std::to_string(k);
      if (artEnt.count(key) || artifactNeeds(id) > tier) return;
      if (Entity* e = dropItem(lv, REALM_MAIN, ItemStack(id, 1), a, false, 0.f, nullptr, 0.f)) {
        e->vel = vec3(0); e->data["zoneArt"] = key; e->data["zoneCyc"] = c; artEnt[key] = e->id; known.insert(key);
        lv.fx->particles("wisp", a, 16); lv.fx->sound("zone_ano_chime", a, 16, 0.6f, 1.6f);
      }
    });
  }
}
} // namespace

DetectorHud detectorHud(Level& lv, const Player& p) {
  DetectorHud h;
  if (!zoneWorld(lv) || p.realm != REALM_MAIN) return h;
  h.tier = detectorTier(p, h.range); h.has = h.tier > 0;
  if (!h.has) return h;
  eachArtifact(lv, p.pos, h.range, [&](int, int, i64, vec3 a, const std::string& id) {
    if (artifactNeeds(id) > h.tier) return;   // too weak a detector: it never hears this one
    const vec3 d = a - p.pos; const float dist = glm::length(d);
    if (h.dist < 0 || dist < h.dist) { h.dist = dist; h.dir = d / std::max(0.01f, dist); }
    if (h.tier >= 3) h.dots.push_back(a);
  });
  return h;
}

// ---------------------------------------------------------------- what the client asks
const std::vector<AnoLive>& anomaliesLive(Level& lv) {
  static const std::vector<AnoLive> none;
  AnoSys* S = sys(lv); return S ? S->live : none;
}
AnoSense anomalySense(Level& lv, u32 playerId) {
  AnoSys* S = sys(lv); if (!S) return {};
  auto it = S->sense.find(playerId); return it == S->sense.end() ? AnoSense{} : it->second;
}

// ---------------------------------------------------------------- the body (the local player, every frame)
void anomalyBody(PlayerController& pc, float dt, PlayerInput& in, const std::function<void(float, const char*)>& hurt) {
  Level& lv = pc.lv; Player& p = pc.p;
  AnoSys* S = sys(lv); if (!S) return;
  AnoSense& se = S->sense[p.id];
  se.flash = std::max(0.f, se.flash - dt * 1.4f); se.gas = std::max(0.f, se.gas - dt * 0.4f); se.skip = std::max(0.f, se.skip - dt * 1.2f);
  se.spin = std::max(0.f, se.spin - dt * 2); se.wisps = std::max(0.f, se.wisps - dt * 0.8f);
  if (p.realm != REALM_MAIN) { se.drowse = se.asleep = 0; return; }
  const Resist R = resist(p);
  auto res = [&](int t) { switch (t) { case R_PHYS: return R.bullet * 0.6f; case R_BURN: return R.burn; case R_SHOCK: return R.shock; case R_CHEM: return R.chem; case R_RAD: return R.rad; default: return R.psy; } };
  auto dmg = [&](float cat, int type) { if (cat > 0) hurt(cat * DMG_SCALE * (1 - res(type)), "anomaly"); };
  // what the anomalies did to us this tick
  for (Hit& h : S->hits) {
    if (h.who != p.id || h.done) continue;
    h.done = true;
    dmg(h.dmg, h.res);
    if (h.tp) { p.pos = p.prevPos = h.to; p.vel = vec3(0); se.skip = 1; p.fallDist = 0; }
    if (h.knock != vec3(0)) p.vel = h.set ? h.knock : p.vel + h.knock;
    if (h.fire > 0) p.burn = std::max(p.burn, h.fire);
    if (h.bleed > 0) zadd(p, "bleed", h.bleed, 0, 10);
    if (h.psy > 0) zadd(p, "psy", -h.psy * (1 - R.psy));
    se.flash = std::max(se.flash, h.flash);
  }
  const vec3 feet = p.pos, mid = p.pos + vec3(0, 0.9f, 0);
  bool lull = false; float lullK = 1;
  static float cueT = 0; cueT -= dt;
  for (AnoLive& L : S->live) {
    const vec3 rel = feet - L.c; const float dh = hyp(rel);
    if (dh > L.r + 14) continue;
    const float tm = tierMul(L.tier);
    switch (L.kind) {
      case AK_UNDERTOW: {
        const float Rr = L.r + 2.5f;
        if (L.cd > 0 || dh > Rr || rel.y < -1.5f || rel.y > 4) break;
        const float k = L.state ? 80.f : 8.f + 32.f * (1 - dh / Rr) * (1 - dh / Rr);
        p.vel += glm::normalize(L.c + vec3(0, 0.3f, 0) - mid) * dt * k;
        break;
      }
      case AK_SPINNER:
        if (L.state == 1 && L.target == p.id) {
          const vec3 hold = L.c + vec3(0, 1.4f + (1.8f - std::max(0.f, L.st)) * 1.5f, 0);
          const float a = (float)lv.info.time * (5 + (1.8f - std::max(0.f, L.st)) * 6);
          p.vel = (hold + vec3(std::cos(a), 0, std::sin(a)) * 0.7f - feet) * 7.f;
          in.mx = in.mz = 0; in.jump = in.sprint = false; in.lookX += dt * (3 + (1.8f - std::max(0.f, L.st)) * 4);
          se.spin = 1; p.fallDist = 0;
        }
        break;
      case AK_SHARDS:
        if (dh < L.r && rel.y > -0.5f && rel.y < 2) {
          const float v = hyp(p.vel);
          if (p.sprinting || v > 4.9f || (!p.onGround && v > 3.5f)) {
            dmg(14 * tm * dt, R_PHYS); zadd(p, "bleed", 0.5f * dt, 0, 10);
            if (cueT <= 0) { cueT = 0.45f; lv.fx->sound("zone_ano_cut", feet + vec3(0, 0.5f, 0), 16, 0.8f, 0.9f + frnd() * 0.3f); }
          }
        }
        break;
      case AK_FLARE:
        if (L.fire > 0 && dh < L.r + 0.3f && rel.y > -0.5f && rel.y < 4.5f) { dmg(40 * tm * dt, R_BURN); p.burn = std::max(p.burn, 2.f); }
        break;
      case AK_WANDERFIRE:
        if (glm::length(mid - L.pos) < 1.5f) { dmg(35 * tm * dt, R_BURN); p.burn = std::max(p.burn, 2.5f); }
        break;
      case AK_STEAM:
        if (L.fire > 0 && dh < L.r + 0.3f && rel.y > -0.5f && rel.y < 5) { dmg(20 * tm * dt, R_BURN); p.vel.y = std::max(p.vel.y, 5.5f); }
        break;
      case AK_ASHMOSS:
        for (const vec4& s : L.bits) if (std::hypot(feet.x - s.x, feet.z - s.z) < 0.5f && feet.y < s.y && feet.y + 1.8f > s.y - s.w) {
          dmg(18 * tm * dt, R_BURN); p.burn = std::max(p.burn, 1.5f);
          if (cueT <= 0) { cueT = 0.5f; lv.fx->sound("zone_ano_sizzle", vec3(s.x, feet.y + 1.5f, s.z), 16); }
          break;
        }
        break;
      case AK_SPARKPOOL:
        if (dh < L.r + 0.3f && feet.y < L.c.y + 0.2f && feet.y > L.c.y - 3) {
          dmg(12 * tm * dt, R_SHOCK); p.vel += vec3(frnd() - 0.5f, 0, frnd() - 0.5f) * dt * 30.f;
          if (cueT <= 0) { cueT = 0.6f; lv.fx->sound("zone_ano_zap", feet, 20, 0.5f, 1.2f + frnd() * 0.3f); }
        }
        break;
      case AK_ACID:
        if (dh < L.r + 0.3f && feet.y < L.c.y + 0.3f && feet.y > L.c.y - 3) { dmg(10 * tm * dt, R_CHEM); zadd(p, "rad", 3.f * tm * dt * (1 - R.rad)); }
        break;
      case AK_GAS:
        if (L.fire > 0 && dh < L.r + 1.5f && rel.y > -1 && rel.y < 3.5f) {
          dmg(12 * tm * dt, R_CHEM); zadd(p, "stamina", -15 * dt * (1 - R.chem)); se.gas = std::min(1.f, se.gas + dt * 2);
          if (cueT <= 0) { cueT = 1.6f; lv.fx->sound("zone_ano_cough", mid, 12); }
        }
        break;
      case AK_MIRE:
        if (dh < L.r + 0.2f && feet.y < L.c.y - 0.1f && feet.y > L.c.y - 3.5f) {
          in.mx *= 0.35f; in.mz *= 0.35f; in.sprint = false;
          if (!in.jump) p.vel.y -= 6 * dt;
          dmg(1.5f * tm * dt, R_CHEM);
          if (cueT <= 0) { cueT = 1.2f + frnd(); lv.fx->sound("zone_ano_glop", feet, 16, 0.8f, 0.8f + frnd() * 0.4f); }
        }
        break;
      case AK_PSYFIELD:
        if (dh < L.r && std::abs(rel.y) < 4) zadd(p, "psy", -15 * tm * dt * (1 - R.psy));
        break;
      case AK_LULL:
        if (dh < L.r && std::abs(rel.y) < 3) { lull = true; lullK = tm; }
        break;
      case AK_WISPS:
        for (const vec4& w : L.bits) {
          const float d = glm::length(vec3(w) - (mid + vec3(0, 0.6f, 0)));
          if (d < 6) { zadd(p, "psy", -6 * tm * dt * (1 - R.psy)); se.wisps = std::max(se.wisps, 1 - d / 6); }
        }
        break;
      default: break;
    }
  }
  // the poppies: drowsy, then asleep where you stand
  if (se.asleep > 0) {
    se.asleep -= dt; in.mx = in.mz = 0; in.jump = in.sprint = in.attack = in.attackPressed = false;
    zadd(p, "psy", -2 * dt * (1 - R.psy));
    if (se.asleep <= 0) { se.drowse = 0.6f; pc.toastMsg("You come to among the poppies. Get out of here.", "warn"); }
  } else if (lull) {
    se.drowse += dt * 0.07f * lullK * (1 - R.psy * 0.7f);
    if (se.drowse >= 1) { se.drowse = 1; se.asleep = 8; pc.toastMsg("Your eyes close...", "warn"); }
  } else se.drowse = std::max(0.f, se.drowse - dt * 0.08f);
  if (se.drowse > 0.25f && se.asleep <= 0) {
    const float k = 1 - 0.6f * (se.drowse - 0.25f) / 0.75f;
    in.mx *= k; in.mz *= k; in.sprint = false;
    zadd(p, "sleep", -1.5f * dt);
  }
}

// ---------------------------------------------------------------- creatures step round what they know
static void steer(Level& lv, Creature& c, float& want, float& dir) {
  if (c.realm != REALM_MAIN || lv.isRemote() || want < 0.05f || !zoneWorld(lv)) return;
  AnoSys* S = sys(lv); if (!S || S->live.empty()) return;
  const vec3 f(-std::sin(dir), 0, -std::cos(dir));
  auto clearOf = [&](const AnoLive& L, float d, float reach) { const vec3 a = c.pos + vec3(-std::sin(d), 0, -std::cos(d)) * 2.4f; return std::hypot(a.x - L.c.x, a.z - L.c.z) > reach; };
  for (const AnoLive& L : S->live) {
    if (L.kind == AK_WANDERFIRE || L.kind == AK_BALL || L.kind == AK_BLISTER || L.kind == AK_WISPS) continue;   // the wanderers can't be stepped round
    if (std::abs(c.pos.y - L.c.y) > 4) continue;
    const float reach = (L.kind == AK_UNDERTOW ? L.r + 2.5f : L.kind == AK_ASHMOSS ? 3.f : L.r) + 0.9f;
    const vec3 ahead = c.pos + f * 2.4f;
    if (std::hypot(ahead.x - L.c.x, ahead.z - L.c.z) > reach) continue;
    if (std::hypot(c.pos.x - L.c.x, c.pos.z - L.c.z) < reach - 0.3f) { const vec3 a = c.pos - L.c; dir = std::atan2(-a.x, -a.z); return; }   // inside: straight out
    for (float turn : {0.7f, 1.2f, 1.7f}) {
      if (clearOf(L, dir + turn, reach)) { dir += turn; return; }
      if (clearOf(L, dir - turn, reach)) { dir -= turn; return; }
    }
    want *= 0.3f;
    return;
  }
}

void registerZoneAnomalies() {
  static bool done = false; if (done) return; done = true;
  registerLevelSystem([](Level& lv) -> std::shared_ptr<LevelSystem> { return zoneWorld(lv) ? std::make_shared<AnoSys>() : nullptr; });
  creatureSteerHook = steer;
  // /anomaly [kind]: to the nearest anomaly (of that kind: undertow, flare_vent, arc_nest...), a few steps south of it, facing it
  registerChatCommand("anomaly", "Go to the nearest anomaly (of a kind)", [](CommandSource& src, const std::vector<std::string>& a) -> std::string {
    if (!src.level || !src.player || !zoneWorld(*src.level)) return "Only in the Zone";
    Player& p = *src.player; Level& lv = *src.level;
    const u32 seed = (u32)lv.info.seed;
    std::vector<const ZAnomaly*> near; zoneAnomaliesNear(seed, ifloor(p.pos.x), ifloor(p.pos.z), 1600, near);
    const ZAnomaly* best = nullptr; float bd = 1e9f;
    for (const ZAnomaly* z : near) {
      if (!a.empty() && a[0] != anoDef(z->kind).key) continue;
      const float d = std::hypot(z->x - p.pos.x, z->z - p.pos.z);
      if (d > 3 && d < bd) { bd = d; best = z; }
    }
    if (!best) return a.empty() ? "No anomaly within 1600 blocks" : "No " + a[0] + " within 1600 blocks";
    const int off = (int)std::ceil((best->kind == AK_UNDERTOW ? best->r + 2.5f : best->r) + 3);
    const Ground g = groundAt(seed, best->x, best->z + off);
    p.pos = p.prevPos = vec3(best->x + 0.5f, (float)(dimBase(DIM_OVERWORLD) + (g.water ? WATER : (int)std::round(g.h)) + 1), best->z + off + 0.5f);
    p.vel = vec3(0); p.yaw = 0; p.pitch = -0.25f; p.fallDist = 0;
    return std::string(anoDef(best->kind).name) + " (tier " + std::to_string(best->tier) + ") " + std::to_string(off) + " blocks north";
  }, true, "[kind]");
  // /site <hut|house|barn|church|ruin|panel|...>: to the nearest building of that kind, standing off its south-east corner, facing it
  registerChatCommand("site", "Go to the nearest building of a kind", [](CommandSource& src, const std::vector<std::string>& a) -> std::string {
    if (!src.level || !src.player || !zoneWorld(*src.level)) return "Only in the Zone";
    static const char* names[] = {"hut", "house", "panel", "office", "public", "hangar", "factory", "chimney", "checkpoint", "tower", "watertower", "church"};
    int t = -1; for (int i = 0; i < 12; i++) if (!a.empty() && a[0] == names[i]) t = i;
    if (!a.empty() && a[0] == "barn") t = S_BARN; if (!a.empty() && a[0] == "ruin") t = S_RUIN;
    if (t < 0) return "Which? hut house barn church ruin panel office hangar factory ...";
    Player& p = *src.player; const u32 seed = (u32)src.level->info.seed;
    const Plan& P = zonePlan(seed); const Placed* best = nullptr; float bd = 1e9f;
    for (const Placed& q : P.s) if (q.type == t) { const float d = std::hypot(q.x0 - p.pos.x, q.z0 - p.pos.z); if (d < bd) { bd = d; best = &q; } }
    if (!best) return "None";
    const int x = best->x0 + best->W + 4, z = best->z0 + best->D + 5;
    const Ground g = groundAt(seed, x, z);
    p.pos = p.prevPos = vec3(x + 0.5f, (float)(dimBase(DIM_OVERWORLD) + (int)std::round(g.h) + 1), z + 0.5f);
    const vec3 c(best->x0 + best->W * 0.5f, dimBase(DIM_OVERWORLD) + best->y + 3.f, best->z0 + best->D * 0.5f), dv = glm::normalize(c - (p.pos + vec3(0, 1.62f, 0)));
    p.vel = vec3(0); p.yaw = std::atan2(-dv.x, -dv.z); p.pitch = std::asin(dv.y); p.fallDist = 0;
    return "at " + std::to_string(x) + " " + std::to_string(z);
  }, true, "<kind>");
}

} // namespace dq::zone
