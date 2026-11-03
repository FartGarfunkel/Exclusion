// sim/zone_mutants.cpp — the Zone's mutants when they are online (A-Life, sim/zone_alife.cpp, puts them in the world near
// players). One brain per archetype, all original creatures on the S.T.A.L.K.E.R. / Misery / STALCRAFT roles:
//   scab dogs and lurchers   hunt in packs: run in, circle the prey, dart in to bite by turns; flee once the pack is thinned
//   rad rats                 swarm straight in, jinking
//   tuskers (boars)          paw the ground, then charge in a line and throw what they hit
//   hogspawn (pigs)          lumber up, turn and kick with both hind legs
//   crawlers                 lope on all fours and spring from ten metres
//   shrouds                  stalk unseen (a shimmer, render/zone_creature_draw.cpp), show themselves to grab and drain
//   husks                    shamble and claw; an armed one still shoots, badly
//   puppeteers               hang back out of reach, crush minds (the psy stat, Player::stats.zone.psy) and drive husks
//   stonebacks               plod in and stomp: a shockwave that throws everything near
//   hurlers                  keep their distance, fling debris with their minds, shove, and wrap themselves in a shield
//   ridgecats                circle, leap, maul, and slink off when badly hurt
// What a creature is doing rides its snapshot (Creature::state, attack, data.hw) so the rigs show it everywhere
// (sim/creature_rig.cpp zonePost). Mutants fight anyone: players and stalkers alike (the stalkers shoot them back).
#include "sim/zone.h"
#include "sim/creature.h"
#include "sim/combat.h"
#include "sim/physics.h"
#include "core/registry.h"
#include <cmath>

namespace dq::zone {

namespace {
constexpr float SCAN = 0.5f;
float rnd() { return dq::frand(); }
json& hw(Creature& c) { json& h = c.data["hw"]; if (!h.is_object()) h = json::object(); return h; }
void setHw(Creature& c, const char* k, float v) { hw(c)[k] = std::round(v * 100) / 100; }
float getHw(Creature& c, const char* k) { const json& h = c.data.value("hw", json()); return h.is_object() ? h.value(k, 0.f) : 0.f; }
float dget(Creature& c, const char* k, float d = 0) { return c.data.value(k, d); }
void dset(Creature& c, const char* k, float v) { c.data[k] = v; }
bool tick(Creature& c, const char* k, float dt) { float v = dget(c, k) - dt; dset(c, k, v); return v <= 0; }   // a countdown ran out

bool isStalker(const Entity& e) { return e.type.rfind("z_npc_", 0) == 0 && e.type != "z_npc_trader"; }
bool fair(const Entity* e) {   // something a mutant goes for
  if (!e || e->removed) return false;
  if (auto* p = dynamic_cast<const Player*>(e)) return !p->dead && !p->creative() && p->mode != "spectator";
  if (auto* c = dynamic_cast<const Creature*>(e)) return c->alive() && isStalker(*c);
  return false;
}
Entity* target(Level& lv, Creature& c, float range, float dt) {
  u32 id = c.data.value("tgt", 0u);
  Entity* T = id ? lv.entity(id) : nullptr;
  if (T && (!fair(T) || T->realm != c.realm || glm::length(T->pos - c.pos) > range * 1.6f)) { T = nullptr; c.data["tgt"] = 0; }
  if (!T && tick(c, "scanT", dt)) {
    dset(c, "scanT", SCAN + rnd() * 0.3f);
    float bd = range;
    for (auto& e : lv.entities()) {
      Entity* E = e.get();
      if (E == &c || E->realm != c.realm || !fair(E)) continue;
      const float d = glm::length(E->pos - c.pos);
      if (d >= bd) continue;
      if (auto* p = dynamic_cast<Player*>(E); p && p->sneaking && d > range * 0.45f) continue;   // a crouching stalker is missed further off
      if (!c.lineOfSight(lv, *E) && d > 6) continue;
      bd = d; T = E;
    }
    if (T) { c.data["tgt"] = T->id; c.say(lv, "angry"); }
  }
  return T;
}
float hdg(const vec3& from, const vec3& to) { return heading(from.x, from.z, to.x, to.z); }
float flat(const vec3& a, const vec3& b) { return std::hypot(a.x - b.x, a.z - b.z); }
float hit(Level& lv, Creature& c, Entity& T, float dmg, float knock, float up = 0) {
  DamageSource s; s.type = "mob"; s.attacker = c.id; s.knock = knock;
  s.hasRay = true; s.rayO = c.pos + vec3(0, c.h * 0.6f, 0); s.rayD = glm::normalize(T.pos - c.pos + vec3(0, 1e-3f, 0));
  const float d = damageEntity(lv, T, dmg, s);
  if (up > 0) T.vel.y = std::max(T.vel.y, up);
  return d;
}
bool meleeReady(Creature& c, Entity& T, float reach) {
  return c.attackCd <= 0 && flat(c.pos, T.pos) < reach + c.w + T.w && std::fabs(T.pos.y - c.pos.y) < 1.8f;
}
bool claw(Level& lv, Creature& c, Entity& T, float dmg, float knock = 4, float reach = 1.15f, float cd = 1.1f) {
  if (!meleeReady(c, T, reach)) return false;
  c.attackCd = cd * (0.85f + rnd() * 0.3f); c.swingT = 0.3f;
  hit(lv, c, T, dmg, knock);
  return true;
}
void psyHit(Level& lv, Creature& c, Entity& T, float amount) {   // a blow to the mind: the psy stat, then the body once it is gone
  if (auto* p = dynamic_cast<Player*>(&T)) {
    const float k = 1 - resist(*p).psy;
    zadd(*p, "psy", -amount * k);
    lv.fx->shake(p->id, 0.35f * k);
    if (zget(*p, "psy", 100) <= 5) { DamageSource s; s.type = "psy"; s.attacker = c.id; s.knock = 0; s.bypassArmor = true; damagePlayer(lv, *p, 1.5f * k, s); }
  } else { DamageSource s; s.type = "psy"; s.attacker = c.id; s.knock = 0; damageEntity(lv, T, amount * 0.12f, s); }
  lv.fx->particles("burst", T.pos + vec3(0, T.h * 0.8f, 0), 8, json{{"tex", "purple"}, {"spread", 0.5f}});
}
// where its squad lives (A-Life): idle creatures keep near it
Move idle(Level& lv, Creature& c, float dt, float speedK = 0.6f) {
  c.state = "idle"; c.aiming = false;
  const int sq = c.data.value("alife", -1);
  if (sq >= 0) {
    const SquadGoal g = alifeGoal(lv, sq);
    if (g.ok && flat(c.pos, g.pos) > g.radius + 6) return Move{c.T->speed * speedK, c.goToward(lv, g.pos, dt)};
  }
  c.wander(dt); Move m = c.wanderMove();
  if (m.want > 0 && c.hitWall) c.wanderDir += PI * 0.8f;
  return m;
}
Move toward(Level& lv, Creature& c, Entity& T, float speed, float dt) { return Move{speed, c.goToward(lv, T.pos, dt)}; }
Move away(Creature& c, Entity& T, float speed) { return Move{speed, c.fleeDir(&T)}; }

// ---------------------------------------------------------------- packs: dogs, lurchers, rats
int packAlive(Level& lv, Creature& c, float r) {
  int n = 0;
  const int sq = c.data.value("alife", -1);
  for (auto& e : lv.entities()) if (auto* o = dynamic_cast<Creature*>(e.get()); o && o->alive() && !o->removed && o->T->family == c.T->family) {
    if (sq >= 0 ? o->data.value("alife", -2) == sq : flat(o->pos, c.pos) < r) n++;
  }
  return n;
}
OptMove dogBrain(Level& lv, Creature& c, Entity*, float, float, float, float dt) {
  if (!zoneWorld(lv) || lv.isRemote() || !c.alive()) return std::nullopt;
  const bool big = c.type == "z_lurcher";
  // the pack: how many it set out with, how many are left
  const int n = packAlive(lv, c, 24);
  if (n > dget(c, "pk0")) dset(c, "pk0", (float)n);
  if (dget(c, "fleeT") > 0) {
    tick(c, "fleeT", dt);
    Entity* F = lv.entity(c.data.value("tgt", 0u));
    c.state = "flee";
    if (F) return away(c, *F, c.T->run);
    return idle(lv, c, dt, 1.2f);
  }
  Entity* T = target(lv, c, big ? 36 : 30, dt);
  if (!T) return idle(lv, c, dt);
  const float d = flat(c.pos, T->pos), toT = hdg(c.pos, T->pos);
  // thinned out and hurt: the rest break and run
  if (n <= std::max(1.f, dget(c, "pk0") * 0.4f) && dget(c, "pk0") >= 3 && c.hp < c.maxHp * 0.7f && !big) { dset(c, "fleeT", 10 + rnd() * 6); c.say(lv, "hurt"); return away(c, *T, c.T->run); }
  if (c.hp < c.maxHp * 0.25f && rnd() < dt * 0.5f) { dset(c, "fleeT", 6 + rnd() * 4); return away(c, *T, c.T->run); }
  c.soundT -= dt;
  if (c.soundT < 0) { c.soundT = 2.5f + rnd() * 4; c.say(lv, big ? "angry" : "say"); }
  // a dart in: straight at it until it bites (or gives up), then back out to the ring
  if (dget(c, "lungeT") > 0) {
    tick(c, "lungeT", dt); c.state = "charge";
    if (claw(lv, c, *T, c.T->damage, big ? 6 : 3.5f, 1.1f, 0.6f)) dset(c, "lungeT", 0);
    if (dget(c, "lungeT") <= 0) dset(c, "ringT", 1.2f + rnd() * 2.2f);
    return Move{c.T->run, d > 2.5f ? c.goToward(lv, T->pos, dt) : toT};
  }
  if (d > 7.5f) { c.state = "hunt"; return Move{c.T->run, c.goToward(lv, T->pos, dt)}; }
  // the ring: each dog its own way round, closing slowly; whoever's turn it is goes in
  c.state = "circle";
  if (tick(c, "ringT", dt)) { dset(c, "lungeT", 1.6f); dset(c, "ringT", 0); return Move{c.T->run, toT}; }
  const float side = (c.id & 1) ? 1.f : -1.f, R = big ? 3.8f : 4.5f;
  const float dir = toT + side * (PI / 2) - side * clampv((d - R) * 0.35f, -0.9f, 0.9f);
  if (d < 1.8f) claw(lv, c, *T, c.T->damage, 3, 1.1f, 1.0f);
  return Move{c.T->speed * 1.15f, dir};
}
OptMove ratBrain(Level& lv, Creature& c, Entity*, float, float, float, float dt) {
  if (!zoneWorld(lv) || lv.isRemote() || !c.alive()) return std::nullopt;
  Entity* T = target(lv, c, 20, dt);
  if (!T) return idle(lv, c, dt);
  c.state = "hunt";
  const float d = flat(c.pos, T->pos);
  claw(lv, c, *T, c.T->damage, 0.8f, 0.8f, 0.7f);
  const float jink = std::sin(c.age * 6 + c.id) * (d > 2 ? 0.7f : 0.2f);
  return Move{d < 0.5f ? 0 : c.T->run, c.goToward(lv, T->pos, dt) + jink};
}

// ---------------------------------------------------------------- tusker: paw, charge, throw
OptMove boarBrain(Level& lv, Creature& c, Entity*, float, float, float, float dt) {
  if (!zoneWorld(lv) || lv.isRemote() || !c.alive()) return std::nullopt;
  Entity* T = target(lv, c, 32, dt);
  const std::string st = c.state;
  if (st == "charge") {   // a straight line at a gallop; whatever is in the way goes flying
    tick(c, "chargeT", dt);
    const float dir = dget(c, "chargeDir");
    std::vector<u32> struck;   // (gathered first: a death spawns drops into lv.entities() while it's walked)
    for (auto& e : lv.entities()) {
      Entity* E = e.get();
      if (E == &c || E->realm != c.realm || !fair(E) || flat(E->pos, c.pos) > c.w + E->w + 0.9f || std::fabs(E->pos.y - c.pos.y) > 1.6f) continue;
      struck.push_back(E->id);
    }
    for (u32 id : struck) {
      Entity* E = lv.entity(id); if (!E || E->removed) continue;
      hit(lv, c, *E, c.T->damage * 1.6f, 14, 6.5f);
      lv.fx->sound("thud", E->pos, 24, 1.0f, 0.7f);
      dset(c, "chargeT", 0);
    }
    if (dget(c, "chargeT") <= 0 || c.hitWall) { c.state = "recover"; dset(c, "recT", 1.0f + rnd() * 0.6f); if (c.hitWall) lv.fx->shake(0, 0.05f); }
    return Move{c.T->run, dir};
  }
  if (st == "recover") { if (tick(c, "recT", dt)) c.state = "idle"; return Move{0.6f, T ? hdg(c.pos, T->pos) : c.yaw}; }
  if (!T) return idle(lv, c, dt);
  const float d = flat(c.pos, T->pos), toT = hdg(c.pos, T->pos);
  if (st == "paw") {   // scraping the ground, head down, then it goes
    if (tick(c, "pawT", dt)) { c.state = "charge"; dset(c, "chargeT", std::min(2.4f, d / c.T->run + 0.9f)); dset(c, "chargeDir", toT); c.say(lv, "angry"); }
    if (rnd() < dt * 6) lv.fx->particles("smoke", c.pos + vec3(-std::sin(c.yaw) * 0.4f, 0.1f, -std::cos(c.yaw) * 0.4f), 1);
    c.yaw = toT; return Move{0, toT};
  }
  c.state = "hunt";
  if (d < 2.4f) { claw(lv, c, *T, c.T->damage, 7, 1.1f, 1.2f); return Move{c.T->speed * 0.6f, toT}; }   // close in: gore
  if (d < 20 && tick(c, "chargeCd", dt) && c.lineOfSight(lv, *T)) { c.state = "paw"; dset(c, "pawT", 0.7f + rnd() * 0.4f); dset(c, "chargeCd", 3.5f + rnd() * 2); return Move{0, toT}; }
  return toward(lv, c, *T, c.T->speed, dt);
}

// ---------------------------------------------------------------- hogspawn: lumber in, turn, kick
OptMove hogBrain(Level& lv, Creature& c, Entity*, float, float, float, float dt) {
  if (!zoneWorld(lv) || lv.isRemote() || !c.alive()) return std::nullopt;
  Entity* T = target(lv, c, 24, dt);
  if (!T) return idle(lv, c, dt, 0.5f);
  const float d = flat(c.pos, T->pos), toT = hdg(c.pos, T->pos);
  if (c.state == "kick") {   // its rump to you, both hind legs
    c.yaw = toT + PI;
    if (tick(c, "kickT", dt)) {
      if (flat(c.pos, T->pos) < 2.4f + T->w) { hit(lv, c, *T, c.T->damage * 1.4f, 11, 4.5f); lv.fx->sound("hurt", T->pos, 20, 1, 0.6f); }
      c.state = "hunt"; c.attackCd = 1.6f + rnd() * 0.6f;
    }
    return Move{0, toT + PI};
  }
  c.state = "hunt";
  c.soundT -= dt; if (c.soundT < 0) { c.soundT = 3 + rnd() * 4; c.say(lv, "say"); }
  if (d < 2.3f && c.attackCd <= 0) {
    if (rnd() < 0.6f) { c.state = "kick"; c.swingT = 0.45f; dset(c, "kickT", 0.42f); return Move{0, toT + PI}; }
    claw(lv, c, *T, c.T->damage, 5, 1.2f, 1.4f);
  }
  if (c.hp < c.maxHp * 0.3f) return away(c, *T, c.T->run);
  return Move{d < 1.4f ? 0 : (d > 8 ? c.T->run : c.T->speed), c.goToward(lv, T->pos, dt)};
}

// ---------------------------------------------------------------- crawler: lope in, spring
OptMove crawlerBrain(Level& lv, Creature& c, Entity*, float, float, float, float dt) {
  if (!zoneWorld(lv) || lv.isRemote() || !c.alive()) return std::nullopt;
  Entity* T = target(lv, c, 30, dt);
  if (c.state == "leap") {   // in the air: land on it
    if (T && flat(c.pos, T->pos) < 1.4f + T->w && dget(c, "leapHit") == 0) { hit(lv, c, *T, c.T->damage * 1.7f, 7, 3); dset(c, "leapHit", 1); c.swingT = 0.3f; }
    if ((c.onGround && dget(c, "airT") > 0.15f) || dget(c, "airT") > 2.5f) c.state = "hunt";
    dset(c, "airT", dget(c, "airT") + dt);
    return Move{dget(c, "leapV"), dget(c, "leapDir")};
  }
  if (!T) return idle(lv, c, dt);
  const float d = flat(c.pos, T->pos), toT = hdg(c.pos, T->pos);
  c.state = "hunt";
  c.soundT -= dt; if (c.soundT < 0) { c.soundT = 2 + rnd() * 3; c.say(lv, "say"); }
  tick(c, "leapCd", dt);
  if (c.onGround && d > 3.5f && d < 11 && dget(c, "leapCd") <= 0 && c.lineOfSight(lv, *T)) {
    const float tAir = 0.55f + d * 0.035f, v = std::min(14.f, d / tAir), dy = clampv(T->pos.y - c.pos.y, -2.f, 3.f);
    c.vel = vec3(-std::sin(toT) * v, 6.5f + dy * 1.2f, -std::cos(toT) * v); c.onGround = false; c.yaw = toT;
    c.state = "leap"; dset(c, "leapV", v); dset(c, "leapDir", toT); dset(c, "leapHit", 0); dset(c, "airT", 0); dset(c, "leapCd", 3.2f + rnd() * 2);
    c.say(lv, "angry");
    return Move{v, toT};
  }
  claw(lv, c, *T, c.T->damage, 3.5f);
  return Move{d < 1.2f ? 0 : c.T->run, c.goToward(lv, T->pos, dt)};
}

// ---------------------------------------------------------------- shroud: unseen, then the grab
OptMove shroudBrain(Level& lv, Creature& c, Entity*, float, float, float, float dt) {
  if (!zoneWorld(lv) || lv.isRemote() || !c.alive()) return std::nullopt;
  Entity* T = target(lv, c, 36, dt);
  // the cloak: on while it stalks, off to feed, torn off for a while by a hard hit
  const bool shown = c.state == "grab" || dget(c, "exposedT") > 0 || c.swingT > 0;
  tick(c, "exposedT", dt);
  const float cl = getHw(c, "cl"), want = shown ? 0.f : 1.f;
  const float nc = cl + clampv(want - cl, -dt * 3.f, dt * 0.8f);
  if (std::fabs(nc - cl) > 0.01f || (want == 0) != (cl == 0)) setHw(c, "cl", nc);
  if (c.state == "grab") {   // holding on, feeding: the victim's life into it
    if (!T || flat(c.pos, T->pos) > 2.2f + T->w || tick(c, "grabT", dt) || dget(c, "shaken") > 7) {
      c.state = "retreat"; dset(c, "retT", 2.5f + rnd() * 2); dset(c, "grabCd", 5 + rnd() * 3); dset(c, "shaken", 0); return Move{c.T->run, c.yaw + PI};
    }
    c.yaw = hdg(c.pos, T->pos);
    if (tick(c, "drainT", dt)) {
      dset(c, "drainT", 0.55f);
      const float got = hit(lv, c, *T, 1.3f, 0);
      c.hp = std::min(c.maxHp, c.hp + std::max(0.6f, got));
      T->vel.x *= 0.2f; T->vel.z *= 0.2f;   // held fast
      lv.fx->particles("burst", T->pos + vec3(0, T->h * 0.6f, 0), 4, json{{"tex", "red"}, {"spread", 0.3f}});
      if (auto* p = dynamic_cast<Player*>(T)) lv.fx->shake(p->id, 0.25f);
    }
    return Move{0, c.yaw};
  }
  if (c.state == "retreat") {   // fades back out and circles off before it comes again
    if (tick(c, "retT", dt) || !T) c.state = "stalk";
    return T ? Move{c.T->run, c.fleeDir(T) + 0.6f} : idle(lv, c, dt);
  }
  if (!T) { Move m = idle(lv, c, dt, 0.5f); c.state = "stalk"; return m; }
  const float d = flat(c.pos, T->pos), toT = hdg(c.pos, T->pos);
  c.state = d < 12 ? "hunt" : "stalk";
  tick(c, "grabCd", dt);
  if (d < 1.7f + T->w && dget(c, "grabCd") <= 0 && std::fabs(T->pos.y - c.pos.y) < 1.6f) {
    c.state = "grab"; dset(c, "grabT", 2.6f + rnd()); dset(c, "drainT", 0.2f); c.say(lv, "angry");
    lv.fx->sound("hurt", c.pos, 24, 1, 0.5f);
    return Move{0, toT};
  }
  if (d < 2.5f) claw(lv, c, *T, c.T->damage, 4, 1.3f, 1.2f);
  // it comes in a zigzag, faster than a man runs
  const float zig = d > 4 ? std::sin(c.age * 1.7f + c.id) * 0.5f : 0.f;
  return Move{d > 3 ? c.T->run : c.T->speed, c.goToward(lv, T->pos, dt) + zig};
}

// ---------------------------------------------------------------- husk: shamble, claw, shoot badly
OptMove huskBrain(Level& lv, Creature& c, Entity*, float, float, float, float dt) {
  if (!zoneWorld(lv) || lv.isRemote() || !c.alive()) return std::nullopt;
  Entity* T = target(lv, c, 28, dt);
  c.soundT -= dt; if (c.soundT < 0) { c.soundT = 5 + rnd() * 7; c.say(lv, "say"); }
  if (!T) { c.aiming = false; Move m = idle(lv, c, dt, 0.4f); return m; }
  const float d = flat(c.pos, T->pos), toT = hdg(c.pos, T->pos);
  const ZItem* g = c.heldItem.empty() ? nullptr : zi(c.heldItem);
  if (g && g->isGun && d > 2.5f && d < 32) {
    c.state = "aim"; c.aiming = true;
    if (tick(c, "fireT", dt) && c.lineOfSight(lv, *T)) {
      dset(c, "fireT", (g->gun.pellets > 1 ? 2.6f : 1.4f) + rnd() * 1.4f);
      const vec3 muzzle = c.pos + vec3(-std::sin(toT) * 0.5f, c.h * 0.72f, -std::cos(toT) * 0.5f);
      lv.fx->sound("firework_blast", muzzle, 80, 0.7f, 1.0f + rnd() * 0.2f);
      c.swingT = 0.12f;
      const float chance = clampv(0.42f - d / 70.f, 0.06f, 0.42f) * (glm::length(vec3(T->vel.x, 0, T->vel.z)) > 3 ? 0.6f : 1.f);
      if (rnd() < chance) {
        DamageSource s; s.type = "bullet"; s.attacker = c.id; s.knock = 0.6f; s.hasRay = true; s.rayO = muzzle; s.rayD = glm::normalize(T->pos - muzzle + vec3(0, 1, 0));
        float dmg = g->gun.dmg * DMG_SCALE * 0.7f * (g->gun.pellets > 1 ? std::min(4, g->gun.pellets) * 0.6f : 1.f);
        if (auto* p = dynamic_cast<Player*>(T)) damagePlayer(lv, *p, dmg * (1 - resist(*p).bullet * 0.6f), s);
        else damageEntity(lv, *T, dmg, s);
      }
    }
    return Move{d > 14 ? c.T->speed * 0.6f : 0.f, d > 14 ? c.goToward(lv, T->pos, dt) : toT};
  }
  c.state = "hunt"; c.aiming = false;
  claw(lv, c, *T, c.T->damage, 3, 1.1f, 1.4f);
  const float sway = std::sin(c.age * 1.3f + c.id) * 0.35f;
  return Move{d < 1.0f ? 0 : c.T->speed, c.goToward(lv, T->pos, dt) + sway};
}

// ---------------------------------------------------------------- puppeteer: out of reach, crushing minds, driving husks
OptMove puppetBrain(Level& lv, Creature& c, Entity*, float, float, float, float dt) {
  if (!zoneWorld(lv) || lv.isRemote() || !c.alive()) return std::nullopt;
  Entity* T = target(lv, c, 40, dt);
  if (!T) return idle(lv, c, dt, 0.4f);
  const float d = flat(c.pos, T->pos), toT = hdg(c.pos, T->pos);
  // its puppets: every husk near it goes for whatever it looks at
  if (tick(c, "driveT", dt)) {
    dset(c, "driveT", 2.5f);
    for (auto& e : lv.entities()) if (auto* h = dynamic_cast<Creature*>(e.get()); h && h->type == "z_husk" && h->alive() && flat(h->pos, c.pos) < 36) { h->data["tgt"] = T->id; h->data["scanT"] = 3.f; }
  }
  if (c.state == "psy") {   // the reach of its mind: a moment's gathering, then the blow
    c.yaw = toT;
    if (tick(c, "castT", dt)) {
      if (c.lineOfSight(lv, *T) && d < 42) { psyHit(lv, c, *T, 34 + rnd() * 10); c.say(lv, "angry"); }
      c.state = "hunt"; dset(c, "psyCd", 4.5f + rnd() * 2.5f);
    }
    if (rnd() < dt * 10) lv.fx->particles("burst", c.pos + vec3(0, c.h * 0.9f, 0), 2, json{{"tex", "purple"}, {"spread", 0.6f}});
    return Move{0, toT};
  }
  c.state = "hunt";
  tick(c, "psyCd", dt);
  if (d < 34 && dget(c, "psyCd") <= 0 && c.lineOfSight(lv, *T)) { c.state = "psy"; dset(c, "castT", 1.4f); return Move{0, toT}; }
  if (d < 2.4f) claw(lv, c, *T, c.T->damage, 3);
  // its distance: back off from anyone closing, come no nearer than it needs to see you
  if (d < 11) return Move{c.T->speed * 1.2f, c.fleeDir(T)};
  if (d > 26 || !c.lineOfSight(lv, *T)) return toward(lv, c, *T, c.T->speed, dt);
  return Move{0.4f, toT + PI / 2 * ((c.id & 1) ? 1 : -1)};
}

// ---------------------------------------------------------------- stoneback: plod in, stomp
void stomp(Level& lv, Creature& c) {
  const vec3 at = c.pos;
  lv.fx->sound("explode", at, 64, 0.8f, 0.5f);
  for (int i = 0; i < 16; i++) { const float a = i / 16.f * TAU; lv.fx->particles("smoke", at + vec3(std::cos(a) * 2.5f, 0.2f, std::sin(a) * 2.5f), 2); }
  std::vector<std::pair<u32, float>> thrown;   // (gathered first: a death spawns drops into lv.entities() while it's walked)
  for (auto& e : lv.entities()) {
    Entity* E = e.get();
    if (E == &c || E->realm != c.realm || E->removed) continue;
    const float d = flat(E->pos, at);
    if (auto* p = dynamic_cast<Player*>(E); p && d < 16) lv.fx->shake(p->id, 0.6f * (1 - d / 16));
    if (d > 6.5f || std::fabs(E->pos.y - at.y) > 2.5f) continue;
    auto* o = dynamic_cast<Creature*>(E);
    if (!fair(E) && !(o && o->alive() && o->T->family != c.T->family)) continue;   // the ground throws dogs as well as men
    thrown.push_back({E->id, d});
  }
  for (auto& [id, d] : thrown) if (Entity* E = lv.entity(id); E && !E->removed) hit(lv, c, *E, 6.5f * (1 - d / 8), 10, 7);
}
OptMove bruteBrain(Level& lv, Creature& c, Entity*, float, float, float, float dt) {
  if (!zoneWorld(lv) || lv.isRemote() || !c.alive()) return std::nullopt;
  Entity* T = target(lv, c, 30, dt);
  if (c.attack.on && c.attack.kind == "stomp") {
    c.attack.t += dt; const float before = c.attack.progress;
    c.attack.progress = std::min(1.f, c.attack.t / 1.15f);
    if (before < 0.78f && c.attack.progress >= 0.78f) stomp(lv, c);
    if (c.attack.progress >= 1) { c.attack.on = false; c.state = "hunt"; dset(c, "stompCd", 3.5f + rnd() * 2); }
    return Move{0, c.yaw};
  }
  if (!T) return idle(lv, c, dt, 0.4f);
  const float d = flat(c.pos, T->pos), toT = hdg(c.pos, T->pos);
  c.state = "hunt";
  c.soundT -= dt; if (c.soundT < 0) { c.soundT = 3 + rnd() * 4; c.say(lv, "say"); }
  tick(c, "stompCd", dt);
  if (d < 5.0f && dget(c, "stompCd") <= 0) {
    c.attack = Creature::Attack{}; c.attack.on = true; c.attack.kind = "stomp"; c.attack.yaw = toT; c.state = "stomp"; c.yaw = toT;
    c.say(lv, "angry");
    return Move{0, toT};
  }
  claw(lv, c, *T, c.T->damage, 12, 1.6f, 1.6f);
  return Move{d < 1.8f ? 0 : (d > 10 ? c.T->run : c.T->speed), c.goToward(lv, T->pos, dt)};
}

// ---------------------------------------------------------------- hurler: debris, shoves, the shield
class Debris : public Entity {   // a lump of the ground thrown by a mind: flies, falls, breaks on what it meets
public:
  Debris() { type = "z_debris"; w = 0.3f; h = 0.6f; hp = maxHp = 1; }
  void tick(Level& lv, double dtd) override {
    const float dt = (float)dtd;
    age += dt;
    if (age > 6) { removed = true; return; }
    if (lv.isRemote()) { pos += vel * dt; return; }
    vel.y -= 16 * dt;
    const vec3 next = pos + vel * dt;
    World& w = lv.world(realm);
    auto smash = [&](vec3 at) { lv.fx->particles("burst", at, 10, json{{"tex", data.value("blk", std::string("stone"))}, {"spread", 0.5f}}); lv.fx->sound("break_stone", at, 24); removed = true; };
    const u32 from = data.value("from", 0u);
    for (auto& e : lv.entities()) {
      Entity* E = e.get();
      if (E == this || E->id == from || E->realm != realm || !fair(E)) continue;
      const vec3 c = E->pos + vec3(0, E->h * 0.5f, 0);
      if (glm::length(c - next) < 0.55f + E->w + E->h * 0.35f) {
        DamageSource s; s.type = "mob"; s.attacker = from; s.knock = 7; s.hasRay = true; s.rayO = pos; s.rayD = glm::normalize(vel + vec3(0, 1e-3f, 0));
        damageEntity(lv, *E, data.value("dmg", 3.f), s);
        smash(next); return;
      }
    }
    if (reg().tSolid[w.getBlock(ifloor(next.x), ifloor(next.y), ifloor(next.z))]) { smash(pos); return; }
    pos = next;
  }
};
void fling(Level& lv, Creature& c, Entity& T) {
  World& w = lv.world(c.realm);
  // a chunk of whatever lies about: the ground at its feet, a wall near it
  std::string blk = "cobblestone";
  for (int k = 0; k < 6; k++) {
    const int x = ifloor(c.pos.x + (rnd() - 0.5f) * 6), z = ifloor(c.pos.z + (rnd() - 0.5f) * 6), y = ifloor(c.pos.y) - 1;
    const BlockId b = w.getBlock(x, y, z);
    if (reg().tSolid[b] && reg().item(reg().block(b).key)) { blk = reg().block(b).key; break; }
  }
  auto e = std::make_unique<Debris>();
  const vec3 from = c.pos + vec3(-std::sin(c.yaw) * 0.6f, c.h + 0.6f, -std::cos(c.yaw) * 0.6f);
  const vec3 to = T.pos + vec3(0, T.h * 0.6f, 0);
  const float hd = flat(from, to), sp = 15, tt = std::max(0.2f, hd / sp);
  e->pos = from; e->realm = c.realm;
  e->vel = vec3((to.x - from.x) / tt + (rnd() - 0.5f) * 1.2f, (to.y - from.y) / tt + 0.5f * 16 * tt, (to.z - from.z) / tt + (rnd() - 0.5f) * 1.2f);
  e->data["blk"] = blk; e->data["from"] = c.id; e->data["dmg"] = 3.0f + rnd() * 1.5f;
  lv.spawn(std::move(e));
  lv.fx->sound("flap", from, 24, 1, 0.5f);
  lv.fx->particles("burst", from, 6, json{{"tex", blk}, {"spread", 0.4f}});
}
OptMove hurlerBrain(Level& lv, Creature& c, Entity*, float, float, float, float dt) {
  if (!zoneWorld(lv) || lv.isRemote() || !c.alive()) return std::nullopt;
  Entity* T = target(lv, c, 34, dt);
  if (c.state == "shield") {
    if (tick(c, "shieldT", dt)) { c.state = "hunt"; setHw(c, "shield", 0); dset(c, "shieldCd", 9 + rnd() * 3); }
    return Move{0, T ? hdg(c.pos, T->pos) : c.yaw};
  }
  if (c.state == "throw") {
    if (tick(c, "throwT", dt)) { if (T) fling(lv, c, *T); c.state = "hunt"; c.swingT = 0.3f; }
    return Move{0, T ? hdg(c.pos, T->pos) : c.yaw};
  }
  if (!T) return idle(lv, c, dt, 0.4f);
  const float d = flat(c.pos, T->pos), toT = hdg(c.pos, T->pos);
  c.state = "hunt";
  tick(c, "throwCd", dt); tick(c, "shieldCd", dt); tick(c, "shoveCd", dt);
  if (d < 3.2f && dget(c, "shoveCd") <= 0) {   // too close: thrown off with a thought
    hit(lv, c, *T, 2, 16, 6); dset(c, "shoveCd", 3.5f); c.swingT = 0.3f;
    lv.fx->particles("burst", T->pos + vec3(0, 1, 0), 10, json{{"tex", "white"}, {"spread", 0.8f}});
    lv.fx->sound("flap", c.pos, 24, 1, 0.4f);
  }
  if (d < 30 && dget(c, "throwCd") <= 0 && c.lineOfSight(lv, *T)) { c.state = "throw"; dset(c, "throwT", 0.65f); dset(c, "throwCd", 2.2f + rnd() * 1.4f); return Move{0, toT}; }
  if (d < 9) return Move{c.T->speed, c.fleeDir(T)};
  if (d > 20 || !c.lineOfSight(lv, *T)) return toward(lv, c, *T, c.T->speed, dt);
  return Move{0, toT};
}
bool hurlerGuard(Level& lv, Creature& c, float& amount, const DamageSrc& src) {
  if (!zoneWorld(lv)) return true;
  if (c.state == "shield") { amount *= 0.1f; lv.fx->particles("burst", c.pos + vec3(0, 0.8f, 0), 4, json{{"tex", "white"}, {"spread", 0.9f}}); return true; }
  if (dget(c, "shieldCd") <= 0 && c.alive() && amount > 0) {   // the first hit after a while: up goes the shield
    c.state = "shield"; setHw(c, "shield", 1); dset(c, "shieldT", 3.2f); amount *= 0.4f;
    lv.fx->sound("flap", c.pos, 30, 1, 0.3f);
  }
  (void)src;
  return true;
}

// ---------------------------------------------------------------- ridgecat: circle, leap, maul
OptMove catBrain(Level& lv, Creature& c, Entity*, float, float, float, float dt) {
  if (!zoneWorld(lv) || lv.isRemote() || !c.alive()) return std::nullopt;
  Entity* T = target(lv, c, 40, dt);
  if (c.state == "leap") {
    if (T && flat(c.pos, T->pos) < 1.8f + T->w && dget(c, "leapHit") == 0) { hit(lv, c, *T, c.T->damage * 1.3f, 9, 3); dset(c, "leapHit", 1); c.swingT = 0.3f; }
    dset(c, "airT", dget(c, "airT") + dt);
    if ((c.onGround && dget(c, "airT") > 0.15f) || dget(c, "airT") > 2.5f) c.state = "hunt";
    return Move{dget(c, "leapV"), dget(c, "leapDir")};
  }
  if (dget(c, "fleeT") > 0) { tick(c, "fleeT", dt); c.state = "flee"; return T ? away(c, *T, c.T->run) : idle(lv, c, dt); }
  if (!T) return idle(lv, c, dt, 0.5f);
  const float d = flat(c.pos, T->pos), toT = hdg(c.pos, T->pos);
  if (c.hp < c.maxHp * 0.3f && dget(c, "fled") == 0) { dset(c, "fled", 1); dset(c, "fleeT", 7); c.say(lv, "hurt"); return away(c, *T, c.T->run); }
  c.state = "hunt";
  c.soundT -= dt; if (c.soundT < 0) { c.soundT = 2.5f + rnd() * 3; c.say(lv, "angry"); }
  tick(c, "leapCd", dt);
  if (c.onGround && d > 4.5f && d < 12 && dget(c, "leapCd") <= 0 && c.lineOfSight(lv, *T)) {
    const float tAir = 0.6f + d * 0.03f, v = std::min(16.f, d / tAir);
    c.vel = vec3(-std::sin(toT) * v, 7.f, -std::cos(toT) * v); c.onGround = false; c.yaw = toT;
    c.state = "leap"; dset(c, "leapV", v); dset(c, "leapDir", toT); dset(c, "leapHit", 0); dset(c, "airT", 0); dset(c, "leapCd", 4 + rnd() * 2);
    return Move{v, toT};
  }
  claw(lv, c, *T, c.T->damage, 7, 1.4f, 0.9f);
  if (d > 12) return Move{c.T->run, c.goToward(lv, T->pos, dt)};
  const float side = (c.id & 1) ? 1.f : -1.f;   // working round to the flank
  return Move{c.T->speed, d < 3 ? toT : toT + side * 0.7f};
}

// ---------------------------------------------------------------- everyone: who hurt it is who it goes for (and its pack too)
void mutantHurt(Level& lv, Creature& c, float dealt, const DamageSrc& src) {
  if (!zoneWorld(lv) || lv.isRemote() || !src.attacker) return;
  Entity* a = lv.entity(src.attacker);
  if (!fair(a)) return;
  c.data["tgt"] = src.attacker;
  if (c.type == "z_shroud") { dset(c, "exposedT", 2.5f); dset(c, "shaken", dget(c, "shaken") + dealt); }
  const int sq = c.data.value("alife", -1);
  for (auto& e : lv.entities()) if (auto* o = dynamic_cast<Creature*>(e.get()); o && o != &c && o->alive() && o->T->family == c.T->family &&
                                   (sq >= 0 ? o->data.value("alife", -2) == sq : flat(o->pos, c.pos) < 20) && !o->data.value("tgt", 0u))
    o->data["tgt"] = src.attacker;
}
} // namespace

void registerZoneMutants() {
  registerEntityFactory("z_debris", [](const std::string&) { return std::make_unique<Debris>(); });
  auto reg1 = [](const char* k, decltype(CreatureHooks::brain) b) { CreatureHooks h; h.brain = std::move(b); h.afterHurt = mutantHurt; setCreatureHooks(k, h); };
  reg1("z_scab_dog", dogBrain); reg1("z_lurcher", dogBrain); reg1("z_rad_rat", ratBrain);
  reg1("z_tusker", boarBrain); reg1("z_hogspawn", hogBrain); reg1("z_crawler", crawlerBrain);
  reg1("z_shroud", shroudBrain); reg1("z_husk", huskBrain); reg1("z_puppeteer", puppetBrain);
  reg1("z_stoneback", bruteBrain); reg1("z_ridgecat", catBrain);
  CreatureHooks hh; hh.brain = hurlerBrain; hh.afterHurt = mutantHurt; hh.beforeHurt = hurlerGuard; setCreatureHooks("z_hurler", hh);
}

} // namespace dq::zone
