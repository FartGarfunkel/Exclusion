// audio/ambience.cpp — see ambience.h. Ports of game/ambience.js (Ambience.update) and game/sound3.js
// (snd3Field, snd3Wall, snd3Reach, snd3Tick).
#include "audio/ambience.h"
#include "render/zone_emission_fx.h"
#include "world/world.h"
#include "core/registry.h"

namespace dq::snd {

// solid rock to sound: solid, a full cube, not leaves (snd3Blocks)
static bool blocksSound(BlockId id) {
  static std::vector<u8> t;
  const Registry& R = reg();
  if (t.size() != R.blocks.size()) {
    t.assign(R.blocks.size(), 0);
    for (size_t i = 0; i < R.blocks.size(); i++) t[i] = R.tSolid[i] && !R.tShape[i] && R.blocks[i].key.find("leaves") == std::string::npos;
  }
  return id < t.size() && t[id];
}

void Ambience::computeField(const AudioScene& s) {
  const World& w = *s.world;
  int hx = (int)std::floor(s.head.x), hy = (int)std::floor(s.head.y), hz = (int)std::floor(s.head.z);
  f.valid = true; f.h = vec3(hx + 0.5f, hy + 0.5f, hz + 0.5f);
  if ((w.getLight(hx, hy, hz) >> 4) >= 14) { f.open = true; f.cells = 0; return; }
  f.open = false;
  const ivec3 o(hx - R, hy - R, hz - R); f.o = o;
  // the chunks around, for both bands the box may touch
  int cx0 = (hx - R) >> 4, cz0 = (hz - R) >> 4, cw = ((hx + R) >> 4) - cx0 + 1, cd = ((hz + R) >> 4) - cz0 + 1;
  int d0 = dimOfY(std::max(0, hy - R)), d1 = dimOfY(std::min(DIM_COUNT * DIM_H - 1, hy + R));
  std::vector<const Chunk*> chs((size_t)cw * cd * 2, nullptr);
  for (int k = 0; k < (d1 > d0 ? 2 : 1); k++) for (int a = 0; a < cw; a++) for (int b = 0; b < cd; b++) chs[((size_t)k * cw + a) * cd + b] = w.chunk(d0 + k, cx0 + a, cz0 + b);
  auto chunkOf = [&](int x, int y, int z) -> const Chunk* {
    if (y < 0 || y >= DIM_COUNT * DIM_H) return nullptr;
    int k = dimOfY(y) - d0; if (k < 0 || k > 1) return nullptr;
    return chs[((size_t)k * cw + ((x >> 4) - cx0)) * cd + ((z >> 4) - cz0)];
  };
  dist.assign((size_t)N * N * N, 255); queue.resize((size_t)N * N * N);
  int qh = 0, qt = 0, cells = 0, open = 0; bool complete = true;
  int st = ((hy - o.y) * N + (hz - o.z)) * N + (hx - o.x);
  dist[st] = 0; queue[qt++] = st;
  while (qh < qt) {
    if (cells >= 24000) { complete = false; break; }
    int i = queue[qh++], lx = i % N, lz = (i / N) % N, ly = i / (N * N); u8 dv = dist[i];
    cells++;
    int x = lx + o.x, y = ly + o.y, z = lz + o.z;
    if (const Chunk* c = chunkOf(x, y, z)) if ((c->getLight(x & 15, y & 255, z & 15) >> 4) >= 14) open++;
    if (dv >= 254) continue;
    static const int DX[6] = {1, -1, 0, 0, 0, 0}, DY[6] = {0, 0, 1, -1, 0, 0}, DZ[6] = {0, 0, 0, 0, 1, -1};
    for (int k = 0; k < 6; k++) {
      int nx = lx + DX[k], ny = ly + DY[k], nz = lz + DZ[k];
      if (nx < 0 || ny < 0 || nz < 0 || nx >= N || ny >= N || nz >= N) continue;
      int j = (ny * N + nz) * N + nx;
      if (dist[j] != 255) continue;
      int wx = nx + o.x, wy = ny + o.y, wz = nz + o.z;
      const Chunk* c = chunkOf(wx, wy, wz);
      if (!c || blocksSound(c->get(wx & 15, wy & 255, wz & 15))) { dist[j] = 254; continue; }   // rock: not looked at again
      dist[j] = (u8)(dv + 1); queue[qt++] = j;
    }
  }
  f.complete = complete; f.cells = cells; f.openFrac = cells ? (float)open / cells : 0;
}

// straight-line count of solid blocks between two points (half for glass, doors and the like)
float Ambience::wallCount(const World& w, vec3 a, vec3 p) const {
  vec3 dv = p - a; float len = glm::length(dv); int n = (int)std::ceil(len * 2);
  float c = 0; i64 last = -1;
  const Registry& R = reg();
  for (int s = 1; s < n; s++) {
    float t = (float)s / n; int bx = (int)std::floor(a.x + dv.x * t), by = (int)std::floor(a.y + dv.y * t), bz = (int)std::floor(a.z + dv.z * t);
    i64 k = ((i64)bx * 73856093) ^ ((i64)by * 19349663) ^ ((i64)bz * 83492791);
    if (k == last) continue; last = k;
    BlockId id = w.getBlock(bx, by, bz);
    if (R.tSolid[id]) c += R.tOpaque[id] && !R.tShape[id] ? 1.f : 0.5f;
    if (c >= 8) break;
  }
  return c;
}

void Ambience::reach(vec3 L, vec3 p, float& d, float& wall) const {
  d = glm::distance(L, p); wall = 0;
  const World* w = e.scene.world;
  if (!w || d < 1.5f) return;
  if (f.valid && !f.open && std::abs(L.x - f.h.x) < 2.5f && std::abs(L.y - f.h.y) < 2.5f && std::abs(L.z - f.h.z) < 2.5f) {
    int lx = (int)std::floor(p.x) - f.o.x, ly = (int)std::floor(p.y) - f.o.y, lz = (int)std::floor(p.z) - f.o.z;
    if (lx >= 0 && ly >= 0 && lz >= 0 && lx < N && ly < N && lz < N) {
      u8 v = dist[(ly * N + lz) * N + lx];
      if (v < 254) { d = std::max(d, v * 0.8f); return; }          // heard the long way round
      if (f.complete) { wall = std::max(1.f, wallCount(*w, L, p)); return; }
    }
  }
  // out in the open, things out in the open are simply heard
  int here = w->getLight((int)std::floor(L.x), (int)std::floor(L.y), (int)std::floor(L.z)) >> 4;
  int there = w->getLight((int)std::floor(p.x), (int)std::floor(p.y + 0.5f), (int)std::floor(p.z)) >> 4;
  if (here >= 12 && there >= 10) return;
  wall = wallCount(*w, L, p + vec3(0, 0.5f, 0));
}

// Exclusion: the Zone has no birdsong. Crows by day and crickets at night, a constant low wind, and every so often
// something far away: gunfire over the hills, dogs howling, a roar, metal groaning, a door banging; thunder in the rain;
// whispers in the psy fields (audio/zone_sfx.cpp)
void Ambience::flat(const char* name) {
  SoundRef r = e.lib.resolve(name); if (!r || !e.gapOk(name, r.gap)) return;
  AudioCmd c; c.cat = CAT_AMB; e.start(r, c, 1);
}
void Ambience::atPos(vec3 p, const char* name, float range, float vol) {
  SoundRef r = e.lib.resolve(name); if (!r) return;
  AudioCmd c;
  if (!e.place(p, range, vol, c) || !e.gapOk(name, r.gap)) return;
  c.cat = CAT_AMB; e.start(r, c, 1);
}
void Ambience::zone(const AudioScene& s, float dt, float vol, float open, float day) {
  tFar -= dt; tPsy -= dt; tThunder -= dt; tSiren -= dt; tEm -= dt; tCrack -= dt; tHeart -= dt; tMurmur -= dt; tHum -= dt;
  const bool night = day < 0.45f;
  // the emission (Call of Pripyat): sirens wail through the warning while the sky grumbles; the wave roars and cracks
  // overhead, your heart pounds if you're caught outside; then it rumbles away. Nothing else makes a sound meanwhile.
  if (s.zoneEm == 1 || s.zoneEm == 2 || (s.zoneEm == 3 && s.zoneEmT < 25)) {
    const bool out = open > 0.5f;
    if (s.zoneEm == 1 && tSiren <= 0) { tSiren = 8.6; at(s, "zone_siren", 45, 160); }
    if (tEm <= 0) { tEm = s.zoneEm == 2 ? 6.5 : s.zoneEm == 1 ? 9 - std::min(5.f, s.zoneEmT / 18.f) : 9; flat("zone_em_rumble"); }
    // each bolt the sky shows (render/zone_emission_fx.h), its thunder after it from where it struck: farther, later and duller
    const emfx::Bolt b = emfx::bolt(s.zoneEm, s.zoneEmT);
    if (b.id >= 0 && b.id != emBolt && b.age >= b.delay) {
      emBolt = b.id;
      if (b.age < b.delay + 1.5f) {
        const float d = 30 + (1 - b.near) * 110;
        atPos(s.player + vec3(std::cos(b.az) * d, 25, std::sin(b.az) * d), b.near > 0.55f ? "zone_em_crack" : "zone_thunder", d + 90, 0.6f + 0.4f * b.near);
      }
    }
    // the wave's passes: its roar rising as the front comes up over the north, the boom as it goes over, then the ringing
    const emfx::Sweep w = emfx::sweep(s.zoneEm, s.zoneEmT);
    if (w.id >= 0 && w.id != emSweepIn && w.u < 0.3f) { emSweepIn = w.id; flat("zone_em_wave"); }
    if (w.id >= 0 && w.id != emSweepOver && w.u >= 0.5f && w.u < 0.6f) { emSweepOver = w.id; flat("zone_em_boom"); if (out) flat("zone_em_ring"); }
    if (s.zoneEm == 2 && out && tHeart <= 0) { tHeart = 0.85; flat("zone_heartbeat"); }
    if (s.zoneEm == 2 && tPsy <= 0) { tPsy = 4 + e.rnd() * 5; at(s, "zone_whisper", 5, 20); }
    if (s.zoneEm == 1 && tFar <= 0) { tFar = 6 + e.rnd() * 8; at(s, e.rnd() < 0.6f ? "zone_howl" : "zone_bark", 70, 90); }   // the animals know first
    return;
  }
  // the Generators' energy spheres: an electric hum, takes overlapped so it never drops out while you're near one
  if (s.zoneHum && tHum <= 0) { tHum = 2.6; atPos(s.zoneHumAt, "zone_sphere_hum", 60, 1.0f); }
  // a camp: stalkers standing about talking (and the guitar, client_fx.cpp's jukebox)
  if (s.zoneTalk && tMurmur <= 0) { tMurmur = 4 + e.rnd() * 9; if (e.rnd() < vol + 0.2f) atPos(s.zoneTalkAt + vec3(0, 1.5f, 0), "zone_murmur", 30, 0.9f); }
  if (open > 0.6f && !night && tBird <= 0) { tBird = 9 + e.rnd() * 22; if (e.rnd() < vol) at(s, "zone_crow", 26); }
  if (open > 0.6f && night && tCricket <= 0) { tCricket = 1.2 + e.rnd() * 3; if (e.rnd() < vol) at(s, "cricket", 10); }
  if (tFar <= 0) {
    tFar = 16 + e.rnd() * 34;
    if (e.rnd() < vol) {
      const float r = e.rnd();
      const char* n = night ? (r < 0.3f ? "zone_howl" : r < 0.45f ? "zone_shots" : r < 0.55f ? "zone_shot" : r < 0.7f ? "zone_roar" : r < 0.85f ? "zone_creak" : "zone_bark")
                            : (r < 0.25f ? "zone_shots" : r < 0.42f ? "zone_shot" : r < 0.55f ? "zone_howl" : r < 0.7f ? "zone_creak" : r < 0.8f ? "zone_clank" : r < 0.9f ? "zone_bark" : "zone_roar");
      at(s, n, 70, 90);
    }
  }
  if (s.rain > 0.25f && tThunder <= 0) { tThunder = 18 + e.rnd() * 40; if (e.rnd() < vol) at(s, "zone_thunder", 80, 120); }
  if (s.zonePsy > 0.15f && tPsy <= 0) { tPsy = 6 + e.rnd() * 14 * (1.2f - s.zonePsy); if (e.rnd() < vol) at(s, "zone_whisper", 6, 20); }
}

// a sound somewhere around the listener (ambience.js at())
void Ambience::at(const AudioScene& s, const char* name, float dist, float range) {
  float a = e.rnd() * TAU, d = dist * (0.5f + e.rnd() * 0.5f);
  vec3 p = s.player + vec3(std::cos(a) * d, 2, std::sin(a) * d);
  SoundRef r = e.lib.resolve(name);
  if (!r) return;
  AudioCmd c;
  if (!e.place(p, range, 1, c)) return;
  if (!e.gapOk(name, r.gap)) return;
  c.cat = CAT_AMB;
  e.start(r, c, 1);
}

void Ambience::update(double dtIn, const AudioScene& s) {
  float dt = (float)std::min(dtIn, 0.1);
  auto wind = [&](float g, float fr) { AudioCmd c; c.t = AudioCmd::Wind; c.a = g; c.b = fr; e.send(c); };
  if (!s.inWorld || !s.world || s.paused) {
    if (windG > 0) { windG = 0; wind(0, 420); }
    { AudioCmd c; c.t = AudioCmd::Rain; c.a = 0; e.send(c); }
    if (f.valid) { f.valid = false; AudioCmd c; c.t = AudioCmd::Verb; c.a = 0; c.b = 0; e.send(c); }
    return;
  }
  // ---- sound3.js: the air around you and the ring of the room ----
  if ((fieldT -= dt) <= 0) {
    fieldT = 0.35; computeField(s);
    float room = 0, cave = 0;
    if (!f.open) {
      float shut = std::clamp(1 - f.openFrac * 4, 0.f, 1.f), size = f.complete ? (float)f.cells : 24000.f;
      cave = shut * std::clamp((size - 1500) / 9000, 0.f, 1.f) * 0.55f;
      room = shut * std::clamp(size / 600, 0.f, 1.f) * (1 - cave / 0.55f) * 0.3f;
    }
    if (s.eyeWater) { room = 0.06f; cave = -1; }   // under water: muffled, not a cave's ring (b < 0 = under water)
    AudioCmd c; c.t = AudioCmd::Verb; c.a = room; c.b = cave; e.send(c);
  }
  float level = e.vAmb.load();
  // ---- ambience.js ----
  int sky = s.skyAtHead; float open = sky / 15.f, day = s.day;
  int localY = (int)std::floor(s.player.y) - dimBase(dimOfY((int)std::floor(s.player.y)));
  float high = std::clamp((localY - 96) / 40.f, 0.f, 1.f);       // the original's y 80 is 32 above its sea; ours is at 64
  if (level > 0) {
    float gust = std::clamp(s.gust / 0.35f, 0.f, 1.f);
    // the Nether's lands each blow their own way: the Old World's ash wind loud and airy, the volcanoes' low and hot
    static const float LAND_WIND[5] = {0.05f, 0.03f, 0.028f, 0.02f, 0.075f}, LAND_FR[5] = {300, 210, 260, 380, 470};
    const bool land = s.dim == DIM_NETHER && s.netherLand >= 0 && s.netherLand < 5;
    const bool zw = s.place.rfind("zone", 0) == 0;
    const float em = s.zoneEm == 2 ? 1.f : s.zoneEm == 1 ? std::min(1.f, s.zoneEmT / 90.f) * 0.6f : 0.f;
    float want = zw ? level * (0.03f + open * (0.045f + high * 0.06f) * (1 + gust * 1.8f) + em * 0.09f) : level * (s.dim == DIM_OVERWORLD || s.dim == DIM_AETHER ? 0.02f + open * (0.025f + high * 0.08f) * (1 + gust * 1.6f) : land ? LAND_WIND[s.netherLand] : s.dim == DIM_NETHER ? 0.035f : 0.01f);
    windG += (want - windG) * std::min(1.f, dt * 1.5f);
    wind(windG, (zw ? 210 + high * 260 + (float)std::sin(s.time * 0.07) * 60 : land ? LAND_FR[s.netherLand] : 300 + high * 400) + (float)std::sin(s.time * 0.21) * 80);
  } else if (windG > 0) { windG = 0; wind(0, 420); }
  { AudioCmd c; c.t = AudioCmd::Rain; c.a = level * s.rain * 1.6f; e.send(c); }   // weather.js: ambience volume × rain × (0.08 + 0.22 open)
  const bool zoneW = s.place.rfind("zone", 0) == 0;
  {   // the End's drone; in the Zone a low bed under everything, heavier at night, underground and in the psy fields
    float g = s.dim == DIM_END && !s.dead ? 0.55f : 0.f;
    if (zoneW && !s.dead) g = 0.07f + (day < 0.45f ? 0.1f : 0.f) + (sky < 4 ? 0.14f : 0.f) + std::clamp(s.zonePsy, 0.f, 1.f) * 0.45f + (s.zoneEm == 2 ? 0.5f : s.zoneEm == 1 ? 0.2f : 0.f);
    AudioCmd c; c.t = AudioCmd::Drone; c.a = level * g; e.send(c);
  }
  tBird -= dt; tCricket -= dt; tDrip -= dt; tRumble -= dt; tLand -= dt;
  float vol = level;
  if (vol > 0.05f) {
    if (zoneW) zone(s, dt, vol, open, day);
    else if (s.dim == DIM_OVERWORLD || s.dim == DIM_AETHER) {
      if (open > 0.7f && day > 0.6f && tBird <= 0) { tBird = 2 + e.rnd() * 6; if (e.rnd() < vol) at(s, "chirp", 14); }
      if (open > 0.7f && day < 0.4f && tCricket <= 0) { tCricket = 0.6 + e.rnd() * 1.8; if (e.rnd() < vol) at(s, "cricket", 10); }
    }
    if (sky < 4 && tDrip <= 0) { tDrip = 1.5 + e.rnd() * 5; if (e.rnd() < vol) at(s, "drip", 8); }
    if (sky < 2 && tRumble <= 0) { tRumble = 40 + e.rnd() * 80; if (e.rnd() < vol * 0.8f) { SoundRef r = e.lib.resolve("cave"); AudioCmd c; c.cat = CAT_AMB; if (r && e.gapOk("cave", r.gap)) e.start(r, c, 1); } }
    if (s.dim == DIM_NETHER && tDrip <= 0.1 && e.rnd() < dt * (s.netherLand == 1 ? 0.7f : 0.3f)) at(s, "burn", 6);
    // each of the Nether's lands its own sounds (audio/nether_sfx.cpp)
    if (s.dim == DIM_NETHER && s.netherLand >= 0 && tLand <= 0) {
      switch (s.netherLand) {
        case 0: tLand = 8 + e.rnd() * 14; if (e.rnd() < vol) at(s, "crag_crack", 22); break;           // the Crag: stones falling
        case 1: tLand = 14 + e.rnd() * 22; if (e.rnd() < vol) at(s, "nether_rumble", 30); break;       // Volcanic Islands: the ground grumbles
        case 2: tLand = 0.8 + e.rnd() * 2.6; if (e.rnd() < vol) at(s, "acid_bubble", 9); break;       // Sulfur Pools: bubbling acid
        case 3: tLand = 2.5 + e.rnd() * 5; if (e.rnd() < vol) at(s, "crystal_chime", 12); break;      // Crystal Forest: crystals ringing
        default: tLand = 18 + e.rnd() * 24; if (e.rnd() < vol) at(s, "ash_moan", 30); break;          // the Old World: wind in the ruins
      }
    }
    if (s.dim == DIM_END && e.rnd() < dt * 0.035f && e.rnd() < vol) at(s, "ender_tp", 34);   // somewhere far off, something blinks from place to place
  }
  // ---- sound3.js: sit in the dark long enough and the caves start making their noises ----
  if (s.dead || level <= 0) return;
  if ((moodT -= dt) > 0) return;
  moodT = 0.25;
  int x = (int)std::floor(s.player.x + (e.rnd() - 0.5f) * 16), y = (int)std::floor(s.player.y + 1 + (e.rnd() - 0.5f) * 16), z = (int)std::floor(s.player.z + (e.rnd() - 0.5f) * 16);
  BlockId id = s.world->getBlock(x, y, z); u8 l = s.world->getLight(x, y, z);
  if (id == AIR && l == 0 && s.dim != DIM_AETHER) mood += 1.0 / 220;
  else if ((l >> 4) > 8 || (l & 15) > 6) mood = std::max(0.0, mood - 1.0 / 400);
  if (mood >= 1) {
    mood = 0;
    // the pack's own cave sounds when it has them (Minecraft's cave ambience), else the original's six
    static const char* CAVE[6] = {"cave_a", "cave_b", "cave_c", "cave_d", "cave_e", "cave_f"};
    const char* pick = e.lib.packs().event("fx.cave") && e.lib.usePack ? "cave" : CAVE[(int)(e.rnd() * 6) % 6];
    SoundRef r = e.lib.resolve(pick);
    AudioCmd c;
    if (r && e.place(vec3(x + 0.5f, y + 0.5f, z + 0.5f), 48, 1, c)) { c.cat = CAT_AMB; e.start(r, c, 1); }
  }
}

} // namespace dq::snd
