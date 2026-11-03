// audio/zone_sfx.cpp — the Zone's far-off sounds, made on the fly with the live synthesis ops (audio/dsp.cpp: "tone",
// "nh" noise hits, "voice", "swell"), like audio/nether_sfx.cpp: gunfire over the hills (single shots, bursts), dogs
// howling and barking, crows, a mutant's roar, metal groaning in the wind, far thunder and the whispering of the psy
// fields. Everything is low-passed: it comes from far away. audio/ambience.cpp plays them in Zone worlds.
#include "audio/sfxlib.h"

namespace dq::snd {

namespace {
json tone(double t, double f, double dur, const char* type, double gain, double slide = 0) { return json::array({"tone", t, f, dur, type, gain, slide}); }
json nh(double t, double dur, double f, double q, double gain, const char* type) { return json::array({"nh", t, dur, f, q, gain, type}); }
json swell(double dur, double f0, double f1, double q, double gain, double rise, double delay = 0) {
  return json::array({"swell", {{"dur", dur}, {"fr", json::array({json::array({0, f0}), json::array({1, f1})})}, {"q", q}, {"gain", gain}, {"rise", rise}, {"delay", delay}}});
}
json voice(std::initializer_list<std::pair<double, double>> f, double dur, double gain, double p, const char* v, double noise, double lp, double vibR, double vibD,
           double att, double hold, double delay = 0) {
  json pts = json::array(); for (auto& q : f) pts.push_back(json::array({q.first, q.second * p}));
  return json::array({"voice", {{"f", pts}, {"dur", dur}, {"v", v}, {"k", 1.0}, {"gain", gain}, {"noise", noise}, {"vib", json::array({vibR, vibD * p})},
                                {"lp", lp}, {"att", att}, {"hold", hold}, {"delay", delay}}});
}
// one far shot: the crack (thinned by distance), its thump, and the roll of its echo off the hills
void shot(json& t, double at, double p, double g) {
  t.push_back(nh(at, 0.09, 420 * p, 0.9, 0.5 * g, "lowpass"));
  t.push_back(nh(at, 0.05, 1300 * p, 1.2, 0.12 * g, "bandpass"));
  t.push_back(tone(at, 70 * p, 0.25, "sine", 0.18 * g, 45 * p));
  t.push_back(nh(at + 0.25, 1.1, 260 * p, 0.7, 0.12 * g, "lowpass"));
}
// ---- guns (sim/zone_guns.cpp names the class: "zone_gun_" + cls; client/client_fx.cpp picks the indoor "_in" or far "_far"
// take by where the shot is). A shot: the supersonic crack (a hard, bright noise burst), the muzzle's boom (low noise and a
// falling thump), the action cycling; outdoors the echo rolls back off the treelines and the buildings for a second or
// two; indoors a hard slap off the walls instead (the engine's room reverb rings on); far off only the low boom and the
// roll remain.
// every op's gain times k (a take is a list of ops; a sound a list of takes)
json louder(json takes, double k) {
  for (auto& t : takes) for (auto& o : t) {
    const std::string op = o[0].get<std::string>();
    if (op == "tone" || op == "nh") o[5] = o[5].get<double>() * k; else o[1]["gain"] = o[1]["gain"].get<double>() * k;
  }
  return takes;
}
struct GunVoice { double crack, crackG, boom, boomG, boomDur, thump, thumpG, mech, tail, tailG; };
void gunShot(json& t, const GunVoice& g, double p, int kind /* 0 outdoors, 1 indoors, 2 far */) {
  if (kind != 2) {
    t.push_back(nh(0, 0.035, g.crack * p, 0.7, g.crackG, "bandpass"));
    t.push_back(nh(0, 0.012, 6000 * p, 0.6, g.crackG * 0.6, "highpass"));
  }
  const double bf = kind == 2 ? std::min(g.boom, 360.0) * 0.7 : kind == 1 ? g.boom * 0.85 : g.boom;
  t.push_back(nh(0.002, g.boomDur * (kind == 1 ? 1.25 : 1), bf * p, 0.75, g.boomG * (kind == 2 ? 0.8 : kind == 1 ? 1.15 : 1), "lowpass"));
  t.push_back(tone(0.0, g.thump * p, g.boomDur * 1.4, "sine", g.thumpG * (kind == 1 ? 1.3 : 1), g.thump * 0.5 * p));
  if (kind != 2 && g.mech > 0) { t.push_back(nh(g.mech, 0.02, 3300 * p, 3, 0.07, "bandpass")); t.push_back(nh(g.mech + 0.035, 0.025, 2100 * p, 2.5, 0.06, "bandpass")); }
  if (kind == 1) {   // the room: a hard slap and a short boomy flutter
    t.push_back(nh(0.018, 0.09, 1400 * p, 0.8, g.boomG * 0.35, "lowpass"));
    t.push_back(nh(0.045, 0.16, 700 * p, 0.8, g.boomG * 0.25, "lowpass"));
    return;
  }
  // outdoors (and far): the echoes, each later one duller, and the long roll
  const double k = kind == 2 ? 1.3 : 1, tg = g.tailG * (kind == 2 ? 3.0 : 2.2);
  t.push_back(nh(0.22 * k, 0.22, g.boom * 0.7 * p, 0.8, tg * 0.55, "lowpass"));
  t.push_back(nh(0.5 * k, 0.3, g.boom * 0.5 * p, 0.8, tg * 0.35, "lowpass"));
  t.push_back(nh(0.85 * k, 0.4, g.boom * 0.4 * p, 0.8, tg * 0.2, "lowpass"));
  t.push_back(nh(0.12, g.tail, g.boom * 0.45 * p, 0.7, tg * 0.5, "lowpass"));
  t.push_back(swell(g.tail * 1.1, 420 * p, 120 * p, 0.7, tg * 0.35, 0.18, 0.15));
}
void addGunSounds(json& table) {
  //                                crack  crackG boom  boomG  boomDur thump thumpG  mech   tail  tailG
  const std::pair<const char*, GunVoice> G[] = {
      {"pistol",   {3300, 0.55, 760, 0.85, 0.11, 115, 0.30, 0.03, 0.9, 0.30}},
      {"smg",      {3100, 0.5, 700, 0.8, 0.09, 105, 0.28, 0.025, 0.8, 0.28}},
      {"carbine",  {2800, 0.6, 640, 0.9, 0.15, 90, 0.34, 0.035, 1.2, 0.36}},
      {"ar",       {2600, 0.65, 600, 0.95, 0.17, 85, 0.36, 0.035, 1.3, 0.40}},
      {"lmg",      {2300, 0.68, 520, 1.0, 0.2, 70, 0.42, 0.03, 1.5, 0.45}},
      {"dmr",      {2200, 0.72, 500, 1.0, 0.26, 66, 0.45, 0.06, 1.8, 0.48}},
      {"sniper",   {1900, 0.75, 430, 1.05, 0.34, 55, 0.5, 0.09, 2.3, 0.52}},
      {"shotgun",  {1500, 0.5, 420, 1.1, 0.3, 62, 0.5, 0.0, 1.7, 0.5}},
      {"launcher", {900, 0.15, 320, 0.7, 0.14, 120, 0.45, 0.0, 0.8, 0.2}},
  };
  const double P[3] = {1.0, 0.95, 1.05};
  auto add = [&](const std::string& name, json takes, double gap) { table[name] = {{"live", takes}, {"gap", gap}}; };
  for (auto& [cls, g] : G) {
    json out = json::array(), in = json::array(), far = json::array();
    for (int k = 0; k < 3; k++) {
      json a = json::array(), b = json::array(), c = json::array();
      gunShot(a, g, P[k], 0); gunShot(b, g, P[k], 1); gunShot(c, g, P[k], 2);
      out.push_back(a); in.push_back(b); far.push_back(c);
    }
    add(std::string("zone_gun_") + cls, louder(out, 1.5), 0.03); add(std::string("zone_gun_") + cls + "_in", louder(in, 1.2), 0.03);
    add(std::string("zone_gun_") + cls + "_far", louder(far, 1.3), 0.03);
  }
  // the hands on the gun: a dry click on an empty chamber, the magazine out, in and the bolt let go, racking a jam clear,
  // the dull clack of a jam, and a suppressed shot's cough
  json dry = json::array(), mout = json::array(), min = json::array(), rack = json::array(), jam = json::array(), supp = json::array();
  for (int k = 0; k < 3; k++) {
    const double p = P[k];
    dry.push_back(json::array({nh(0, 0.014, 3900 * p, 3, 0.55, "bandpass"), nh(0.011, 0.02, 2300 * p, 2.5, 0.3, "bandpass"), tone(0, 1700 * p, 0.03, "square", 0.015, 1200 * p)}));
    mout.push_back(json::array({nh(0, 0.025, 2700 * p, 2.5, 0.4, "bandpass"), swell(0.16, 2200 * p, 1100 * p, 4, 0.07, 0.3, 0.03), nh(0.2, 0.05, 820 * p, 1.6, 0.3, "bandpass"),
                                tone(0.2, 260 * p, 0.07, "triangle", 0.04, 200 * p)}));
    min.push_back(json::array({nh(0, 0.06, 760 * p, 1.6, 0.45, "bandpass"), nh(0.035, 0.014, 3600 * p, 3, 0.45, "bandpass"),
                               nh(0.3, 0.035, 2000 * p, 2, 0.35, "bandpass"), swell(0.14, 1600 * p, 2600 * p, 4, 0.06, 0.4, 0.3),
                               nh(0.45, 0.05, 1400 * p, 1.6, 0.55, "bandpass"), tone(0.45, 430 * p, 0.09, "triangle", 0.05, 380 * p)}));
    { json t = json::array();
      for (int r = 0; r < 2; r++) { const double d = r * 0.55; t.push_back(nh(d, 0.045, 1800 * p, 2, 0.4, "bandpass")); t.push_back(swell(0.16, 1500 * p, 2400 * p, 4, 0.06, 0.4, d + 0.02));
        t.push_back(nh(d + 0.18, 0.06, 1250 * p, 1.6, 0.5, "bandpass")); t.push_back(tone(d + 0.18, 380 * p, 0.08, "triangle", 0.05, 330 * p)); }
      t.push_back(nh(0.62, 0.012, 3000 * p, 3, 0.25, "bandpass"));   // the bad round flicked out
      rack.push_back(t); }
    jam.push_back(json::array({nh(0, 0.05, 900 * p, 1.5, 0.5, "bandpass"), tone(0, 300 * p, 0.07, "triangle", 0.06, 210 * p), nh(0.01, 0.02, 2600 * p, 2, 0.15, "bandpass")}));
    supp.push_back(json::array({nh(0, 0.08, 950 * p, 0.8, 0.6, "lowpass"), nh(0, 0.025, 2600 * p, 1.4, 0.18, "bandpass"), tone(0, 140 * p, 0.08, "sine", 0.12, 80 * p),
                                nh(0.03, 0.02, 3300 * p, 3, 0.08, "bandpass"), nh(0.06, 0.02, 2100 * p, 2.5, 0.06, "bandpass")}));
  }
  add("zone_gun_dry", louder(dry, 2.2), 0.05); add("zone_gun_mag_out", louder(mout, 2.2), 0.2); add("zone_gun_mag_in", louder(min, 2.2), 0.2);
  add("zone_gun_rack", louder(rack, 2.2), 0.4); add("zone_gun_jam", louder(jam, 2.2), 0.2); add("zone_gun_supp", louder(supp, 2.5), 0.03);
}
// a camp's talk heard from a way off: two or three men in turn, a few words each, low and muffled, now and then a laugh
json murmur(double p, u32 seed) {
  json t = json::array();
  static const char* V[] = {"a", "o", "e", "u", "aa", "oo", "ou"};
  double at = 0;
  for (int line = 0; line < 3; line++) {
    const double pitch = (line % 2 ? 128 : 104 + (seed % 3) * 9) * p;
    const int n = 4 + (int)((seed >> (line * 3)) % 5);
    for (int i = 0; i < n; i++) {
      const double d = 0.1 + 0.05 * ((seed + i * 7 + line) % 4), f = pitch * (1 + 0.06 * ((i * 5 + line) % 3) - (i == n - 1 ? 0.1 : 0));
      t.push_back(voice({{0, f}, {1, f * 0.94}}, d, 0.05, 1.0, V[(seed + i * 3 + line) % 7], 0.35, 820, 0, 0, 0.02, 0.04, at));
      at += d + 0.035 + ((i * 13 + seed) % 5 == 0 ? 0.18 : 0.0);
    }
    if (line == 1 && seed % 2) for (int i = 0; i < 4; i++) t.push_back(voice({{0, 190 * p}, {1, 160 * p}}, 0.09, 0.04, 1.0, "a", 0.6, 1000, 0, 0, 0.01, 0.02, at + i * 0.15));   // a laugh
    at += 0.5 + 0.2 * (line % 2);
  }
  return t;
}
} // namespace

void addZoneSounds(json& table) {
  if (!table.is_object() || table.contains("zone_shots")) return;
  const double P[3] = {1.0, 0.9, 1.1};
  json single = json::array(), burst = json::array(), howl = json::array(), bark = json::array(), crow = json::array(), roar = json::array(),
       creak = json::array(), thunder = json::array(), whisper = json::array(), clank = json::array();
  for (int k = 0; k < 3; k++) {
    const double p = P[k];
    { json t = json::array(); shot(t, 0, p, 1); if (k == 1) shot(t, 1.6, p, 0.8); single.push_back(t); }
    { json t = json::array(); const int n = 4 + k * 2; for (int i = 0; i < n; i++) shot(t, i * (0.095 + 0.01 * k), p * (1 + 0.02 * (i % 2)), 0.75);
      if (k != 1) for (int i = 0; i < 3; i++) shot(t, 1.4 + i * 0.36, p * 0.92, 0.5);   // and an answer from further off
      burst.push_back(t); }
    // a dog (or something like one) howling far away, another joining in
    howl.push_back(json::array({voice({{0, 430}, {0.25, 600}, {0.7, 640}, {1, 410}}, 2.6, 0.07, p, "oou", 0.2, 1300, 5, 9, 0.5, 0.8),
                                voice({{0, 470}, {0.35, 680}, {1, 520}}, 2.0, 0.045, p * 1.06, "ou", 0.25, 1200, 6, 10, 0.4, 0.6, 1.3)}));
    // barking off in the trees
    { json t = json::array(); for (int i = 0; i < 3 + k; i++) t.push_back(voice({{0, 620}, {1, 470}}, 0.16, 0.08, p * (1 + 0.04 * (i % 2)), "a", 0.5, 1700, 0, 0, 0.01, 0.03, i * 0.32 + (i > 1 ? 0.5 : 0)));
      bark.push_back(t); }
    // crows: harsh, nasal caws
    { json t = json::array(); for (int i = 0; i < 2 + k % 2; i++) t.push_back(voice({{0, 760}, {0.3, 820}, {1, 610}}, 0.3, 0.05, p, "aa", 0.65, 2600, 0, 0, 0.02, 0.1, i * 0.45));
      crow.push_back(t); }
    // something big, roaring
    roar.push_back(json::array({voice({{0, 120}, {0.3, 165}, {1, 88}}, 1.7, 0.12, p, "aoo", 0.75, 650, 8, 6, 0.15, 0.6), nh(0, 1.5, 180 * p, 0.8, 0.12, "lowpass")}));
    // metal groaning in the wind: a slow scrape that bends in pitch, a tick at its end
    creak.push_back(json::array({tone(0, 96 * p, 1.5, "sawtooth", 0.025, 132 * p), nh(0, 1.4, 820 * p, 7, 0.07, "bandpass"), tone(0.4, 180 * p, 1.0, "triangle", 0.012, 150 * p),
                                 nh(1.45, 0.05, 2100 * p, 2, 0.05, "bandpass")}));
    // a door or a sheet of iron banging somewhere in the ruins
    clank.push_back(json::array({nh(0, 0.12, 700 * p, 1.5, 0.22, "bandpass"), tone(0, 210 * p, 0.6, "triangle", 0.04, 190 * p), nh(0.5, 0.08, 650 * p, 1.5, 0.08, "bandpass")}));
    // thunder rolling a long way off
    thunder.push_back(json::array({nh(0, 3.5, 85 * p, 0.7, 0.4, "lowpass"), swell(5.5, 170 * p, 55 * p, 0.7, 0.3, 0.12, 0.2), tone(0.1, 42 * p, 3.0, "sine", 0.15, 30 * p)}));
    // the psy fields: voices that are not there, close and far at once
    whisper.push_back(json::array({voice({{0, 210}, {1, 222}}, 4.0, 0.035, p, "uoo", 0.9, 900, 3, 4, 1.2, 1.2), voice({{0, 314}, {1, 296}}, 3.6, 0.028, p, "ee", 0.85, 1500, 4, 6, 1.0, 1.0, 0.7),
                                   swell(4.2, 1800 * p, 600 * p, 3, 0.03, 0.5, 0.2)}));
  }
  // ---- the emission ----
  json siren = json::array(), rumble = json::array(), crack = json::array(), wave = json::array(), heart = json::array();
  for (int k = 0; k < 3; k++) {
    const double p = P[k];
    // the air-raid siren over the loudspeakers: wind up, hold, wind down (two horns a little apart, and their echo)
    json t = json::array();
    for (int e = 0; e < 2; e++) {
      const double d = e * 0.45, g = e ? 0.035 : 0.09;
      t.push_back(tone(d, 150 * p, 2.6, "triangle", g, 590 * p)); t.push_back(tone(d + 2.6, 590 * p, 2.2, "triangle", g, 605 * p)); t.push_back(tone(d + 4.8, 605 * p, 3.4, "triangle", g, 160 * p));
      t.push_back(tone(d, 225 * p, 2.6, "sawtooth", g * 0.18, 885 * p)); t.push_back(tone(d + 2.6, 885 * p, 2.2, "sawtooth", g * 0.18, 905 * p)); t.push_back(tone(d + 4.8, 905 * p, 3.4, "sawtooth", g * 0.18, 240 * p));
    }
    siren.push_back(t);
    // the sky grumbling as it builds
    rumble.push_back(json::array({nh(0, 5.5, 70 * p, 0.7, 0.45, "lowpass"), swell(6.5, 130 * p, 48 * p, 0.7, 0.32, 0.35), tone(0.3, 38 * p, 5.5, "sine", 0.2, 30 * p)}));
    // a crack of it overhead
    crack.push_back(json::array({nh(0, 0.16, 2400 * p, 0.6, 0.5, "bandpass"), nh(0.02, 2.8, 190 * p, 0.7, 0.6, "lowpass"), swell(4.5, 260 * p, 60 * p, 0.7, 0.35, 0.05, 0.1),
                                 tone(0.05, 60 * p, 2.0, "sine", 0.3, 34 * p)}));
    // the wave: a roar rising over everything, a choir of moans inside it
    wave.push_back(json::array({swell(7.5, 70 * p, 520 * p, 0.7, 0.42, 0.75), nh(0.5, 6.5, 120 * p, 0.7, 0.35, "lowpass"), tone(0, 55 * p, 7.0, "sawtooth", 0.05, 110 * p),
                                voice({{0, 110}, {0.6, 150}, {1, 98}}, 6.5, 0.05, p, "oou", 0.7, 700, 2, 3, 2.0, 2.5, 0.5)}));
    heart.push_back(json::array({tone(0, 58 * p, 0.13, "sine", 0.55, 40 * p), tone(0.27, 52 * p, 0.14, "sine", 0.42, 36 * p)}));
  }
  auto add = [&](const char* name, json takes, double gap) { table[name] = {{"live", takes}, {"gap", gap}}; };
  // the wave going over (render/zone_emission_fx.h sweep): a vast boom with its crack, and the ringing in your ears after
  json boom = json::array(), ring = json::array(), talk = json::array();
  for (int k = 0; k < 3; k++) {
    const double p = P[k];
    boom.push_back(json::array({nh(0, 0.3, 1600 * p, 0.6, 0.35, "bandpass"), nh(0, 4.0, 95 * p, 0.7, 0.85, "lowpass"), tone(0, 50 * p, 3.8, "sine", 0.42, 27 * p),
                                swell(5.5, 420 * p, 55 * p, 0.7, 0.4, 0.04, 0.05), nh(0.6, 3.0, 300 * p, 0.7, 0.18, "lowpass")}));
    ring.push_back(json::array({tone(0.2, 3950 * p, 7.5, "sine", 0.045, 3900 * p), tone(0.25, 4140 * p, 6.0, "sine", 0.02, 4120 * p), tone(0.3, 2600 * p, 3.0, "sine", 0.01, 2590 * p)}));
    for (int j = 0; j < 2; j++) talk.push_back(murmur(p, (u32)(k * 2 + j) * 2654435761u >> 7));
  }
  add("zone_em_boom", boom, 6); add("zone_em_ring", louder(ring, 1.5), 8); add("zone_murmur", louder(talk, 2.5), 4);
  addGunSounds(table);
  add("zone_siren", siren, 7); add("zone_em_rumble", rumble, 3); add("zone_em_crack", crack, 1); add("zone_em_wave", wave, 4); add("zone_heartbeat", heart, 0.6);
  add("zone_shot", single, 2); add("zone_shots", burst, 3); add("zone_howl", howl, 6); add("zone_bark", bark, 3); add("zone_crow", crow, 2);
  add("zone_roar", roar, 8); add("zone_creak", creak, 3); add("zone_clank", clank, 3); add("zone_thunder", thunder, 8); add("zone_whisper", whisper, 5);
  // ---- the anomalies (sim/zone_anomaly.cpp sets them off; render/zone_anomaly_fx.cpp plays their tells where they are) ----
  const char* AN[] = {"gust", "suck", "burst", "thump", "spin", "chime", "cut", "skip", "flare", "fire", "steam", "crackle", "zap", "hum", "pop", "fizz",
                      "puff", "blister", "glop", "lull", "wisp", "sizzle", "cough"};
  const double GAP[] = {1.5, 0.5, 0.5, 0.3, 1, 1.2, 0.3, 0.5, 1, 1.2, 1, 1.2, 0.15, 1, 0.5, 1.5, 1, 0.2, 0.6, 3, 1.5, 0.4, 1.2};
  constexpr int NA = sizeof(AN) / sizeof(AN[0]);
  json an[NA];
  for (auto& a : an) a = json::array();
  for (int k = 0; k < 3; k++) {
    const double p = P[k];
    int i = 0;
    // undertow: a gust that circles (noise swept up and down), dry leaves ticking in it; the draw before the burst; the burst
    an[i++].push_back(json::array({swell(2.2, 260 * p, 900 * p, 1.2, 0.22, 0.4), swell(1.8, 1100 * p, 400 * p, 2, 0.1, 0.3, 0.4), nh(0.6, 0.04, 3200 * p, 3, 0.05, "bandpass"), nh(1.1, 0.04, 2900 * p, 3, 0.04, "bandpass")}));
    an[i++].push_back(json::array({swell(0.6, 200 * p, 1600 * p, 1.5, 0.35, 0.9), tone(0, 50 * p, 0.6, "sine", 0.25, 110 * p)}));
    an[i++].push_back(json::array({tone(0, 70 * p, 0.5, "sine", 0.7, 30 * p), nh(0, 0.35, 380 * p, 0.8, 0.8, "lowpass"), nh(0, 0.08, 1800 * p, 0.8, 0.35, "bandpass"), nh(0.25, 1.2, 2400 * p, 1.5, 0.08, "bandpass")}));
    // kickback: a heavy whump of air; spinner: a whirr winding up; shardfield: thin glass ringing, and its grating cut
    an[i++].push_back(json::array({tone(0, 64 * p, 0.35, "sine", 0.6, 34 * p), nh(0, 0.3, 300 * p, 0.8, 0.5, "lowpass"), swell(0.5, 900 * p, 300 * p, 1, 0.12, 0.1)}));
    an[i++].push_back(json::array({tone(0, 90 * p, 1.7, "sawtooth", 0.05, 420 * p), swell(1.8, 300 * p, 1800 * p, 3, 0.15, 0.8), tone(0, 180 * p, 1.7, "triangle", 0.04, 840 * p)}));
    an[i++].push_back(json::array({tone(0, 1180 * p, 2.2, "sine", 0.035, 1176 * p), tone(0.05, 1770 * p, 1.8, "sine", 0.025, 1772 * p), tone(0.12, 2650 * p, 1.4, "sine", 0.018, 2645 * p), tone(0.3, 3520 * p, 0.9, "sine", 0.01, 3500 * p)}));
    an[i++].push_back(json::array({nh(0, 0.06, 5200 * p, 2, 0.3, "bandpass"), tone(0, 2600 * p, 0.18, "square", 0.04, 2300 * p), nh(0.05, 0.12, 3800 * p, 3, 0.12, "bandpass")}));
    // skip: a hollow whoomp bending down, a shimmer after
    an[i++].push_back(json::array({tone(0, 420 * p, 0.5, "sine", 0.35, 70 * p), swell(0.6, 2400 * p, 300 * p, 2, 0.18, 0.1), tone(0.15, 1300 * p, 0.6, "triangle", 0.04, 1900 * p)}));
    // flare vent: the jet's roar; fire crackling; steam's hiss
    an[i++].push_back(json::array({swell(2.3, 400 * p, 900 * p, 0.8, 0.5, 0.08), nh(0, 2.2, 700 * p, 0.7, 0.35, "lowpass"), tone(0, 55 * p, 2.2, "sawtooth", 0.06, 70 * p)}));
    { json t = json::array(); t.push_back(nh(0, 1.6, 500 * p, 0.7, 0.12, "lowpass"));
      for (int j = 0; j < 9; j++) t.push_back(nh(j * 0.17 + 0.05 * (j % 3), 0.02, (2200 + 300 * (j % 4)) * p, 4, 0.12, "bandpass"));
      an[i++].push_back(t); }
    an[i++].push_back(json::array({nh(0, 1.6, 3400 * p, 0.9, 0.35, "highpass"), swell(1.7, 2600 * p, 1800 * p, 1, 0.18, 0.05)}));
    // arc nest: dry ticks as it charges, then the discharge; ball lightning's buzz; flashpop's crack
    { json t = json::array(); for (int j = 0; j < 14; j++) t.push_back(nh(j * 0.11 + 0.03 * ((j * 7) % 5), 0.012, (3000 + 400 * (j % 5)) * p, 5, 0.15 + 0.05 * (j % 3), "bandpass"));
      t.push_back(tone(0, 100 * p, 1.6, "sawtooth", 0.012, 100 * p)); an[i++].push_back(t); }
    an[i++].push_back(json::array({nh(0, 0.07, 2600 * p, 0.7, 0.6, "bandpass"), tone(0, 140 * p, 0.35, "sawtooth", 0.18, 90 * p), nh(0.03, 0.3, 900 * p, 0.8, 0.25, "lowpass"), tone(0, 1900 * p, 0.08, "square", 0.05, 600 * p)}));
    an[i++].push_back(json::array({tone(0, 98 * p, 1.4, "sawtooth", 0.06, 102 * p), tone(0, 196 * p, 1.4, "square", 0.02, 200 * p), nh(0, 1.4, 4200 * p, 3, 0.03, "bandpass")}));
    an[i++].push_back(json::array({nh(0, 0.05, 3000 * p, 0.6, 0.8, "bandpass"), tone(0, 2400 * p, 0.25, "sine", 0.12, 900 * p), nh(0.02, 0.4, 600 * p, 0.8, 0.3, "lowpass")}));
    // acid bubbling; a gas pocket's puff; a blister's wet pop; the mire's glop
    { json t = json::array(); t.push_back(nh(0, 1.8, 5200 * p, 1, 0.06, "highpass"));
      for (int j = 0; j < 8; j++) t.push_back(tone(j * 0.2 + 0.07 * (j % 3), (380 + 90 * ((j * 5) % 7)) * p, 0.06, "sine", 0.05, (700 + 120 * (j % 4)) * p));
      an[i++].push_back(t); }
    an[i++].push_back(json::array({nh(0, 0.9, 500 * p, 0.8, 0.45, "lowpass"), swell(1.2, 1500 * p, 500 * p, 1, 0.2, 0.15)}));
    an[i++].push_back(json::array({tone(0, 320 * p, 0.12, "sine", 0.3, 70 * p), nh(0, 0.15, 900 * p, 1.5, 0.25, "bandpass"), nh(0.08, 0.4, 3000 * p, 2, 0.05, "bandpass")}));
    an[i++].push_back(json::array({tone(0, 110 * p, 0.18, "sine", 0.3, 230 * p), nh(0, 0.12, 300 * p, 2, 0.15, "lowpass")}));
    // the lullfield's slow sleepy hum; the wisps' airy chime; ash moss burning; a cough in the gas
    an[i++].push_back(json::array({voice({{0, 220}, {1, 220}}, 1.2, 0.03, p, "oo", 0.2, 900, 4, 3, 0.4, 0.5), voice({{0, 196}, {1, 196}}, 1.2, 0.03, p, "oo", 0.2, 900, 4, 3, 0.4, 0.5, 1.1),
                                   voice({{0, 165}, {1, 160}}, 1.8, 0.03, p, "oo", 0.2, 900, 4, 3, 0.5, 0.8, 2.2)}));
    an[i++].push_back(json::array({tone(0, 880 * p, 1.2, "sine", 0.03, 990 * p), tone(0.2, 1320 * p, 1.0, "sine", 0.02, 1250 * p), voice({{0, 300}, {1, 340}}, 1.4, 0.02, p, "ee", 0.9, 1800, 3, 6, 0.4, 0.4)}));
    an[i++].push_back(json::array({nh(0, 0.5, 4500 * p, 0.9, 0.25, "highpass"), nh(0, 0.3, 1200 * p, 1.5, 0.08, "bandpass")}));
    an[i++].push_back(json::array({nh(0, 0.12, 700 * p, 1.2, 0.4, "bandpass"), voice({{0, 180}, {1, 140}}, 0.15, 0.08, p, "a", 0.8, 1500, 0, 0, 0.01, 0.05), nh(0.28, 0.1, 650 * p, 1.2, 0.3, "bandpass")}));
  }
  for (int i = 0; i < NA; i++) add((std::string("zone_ano_") + AN[i]).c_str(), an[i], GAP[i]);
}

} // namespace dq::snd
