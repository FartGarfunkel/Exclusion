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
// kind: 0 in the hands (the crack and the action), 1 indoors (that, plus the room), 2 over a hill (only the
// thud and the roll — a different sound, not the near one turned down), 3 across a yard (the report: the
// crack has already broken up, the boom and one slap off a wall are what arrive).
void gunShot(json& t, const GunVoice& g, double p, int kind) {
  if (kind == 0) {
    t.push_back(nh(0, 0.007, g.crack * p, 1.4, g.crackG * 1.15, "highpass"));
    t.push_back(nh(0, 0.004, std::min(7800.0, g.crack * 1.8) * p, 0.55, g.crackG * 0.55, "highpass"));
    t.push_back(nh(0.001, g.boomDur * 0.35, g.boom * 1.6 * p, 0.9, g.boomG * 0.28, "bandpass"));
    t.push_back(tone(0, g.thump * p, 0.05, "sine", g.thumpG * 0.45, g.thump * 0.55 * p));
    if (g.mech > 0) {
      t.push_back(nh(g.mech, 0.008, 4200 * p, 4, 0.16, "bandpass"));
      t.push_back(nh(g.mech + 0.028, 0.01, 2400 * p, 3.5, 0.1, "bandpass"));
    }
    return;
  }
  if (kind == 1) {
    t.push_back(nh(0, 0.01, g.crack * 0.75 * p, 1.1, g.crackG * 0.7, "bandpass"));
    t.push_back(nh(0, g.boomDur * 1.4, g.boom * 0.8 * p, 0.7, g.boomG * 1.25, "lowpass"));
    t.push_back(tone(0, g.thump * 0.9 * p, g.boomDur * 1.6, "sine", g.thumpG * 1.4, g.thump * 0.4 * p));
    t.push_back(nh(0.016, 0.07, 1600 * p, 0.7, g.boomG * 0.55, "bandpass"));
    t.push_back(nh(0.042, 0.11, 620 * p, 0.65, g.boomG * 0.4, "lowpass"));
    t.push_back(nh(0.09, 0.18, 340 * p, 0.6, g.boomG * 0.22, "lowpass"));
    if (g.mech > 0) t.push_back(nh(g.mech, 0.012, 2800 * p, 3, 0.08, "bandpass"));
    return;
  }
  if (kind == 3) {
    t.push_back(nh(0.004, 0.02, g.crack * 0.28 * p, 0.8, g.crackG * 0.12, "bandpass"));
    t.push_back(nh(0.0, g.boomDur * 1.6, g.boom * p, 0.85, g.boomG * 1.05, "lowpass"));
    t.push_back(tone(0, g.thump * 0.85 * p, g.boomDur * 2.2, "sine", g.thumpG * 1.15, g.thump * 0.45 * p));
    t.push_back(nh(0.08, 0.16, g.boom * 0.55 * p, 0.7, g.boomG * 0.45, "lowpass"));
    t.push_back(nh(0.22, 0.28, g.boom * 0.35 * p, 0.65, g.tailG * 0.7, "lowpass"));
    return;
  }
  // over a hill: the transient is gone. A slow low arrival and a long roll, pitched by the class.
  t.push_back(nh(0.04, 0.55, std::min(g.boom, 280.0) * 0.55 * p, 0.55, g.boomG * 0.95, "lowpass"));
  t.push_back(tone(0.03, g.thump * 0.62 * p, g.tail * 0.55, "sine", g.thumpG * 1.35, g.thump * 0.28 * p));
  t.push_back(swell(g.tail * 0.9, g.boom * 0.4 * p, 48 * p, 0.55, g.tailG * 1.1, 0.35, 0.08));
  t.push_back(nh(0.35, g.tail, 90 * p, 0.5, g.tailG * 0.55, "lowpass"));
  t.push_back(nh(0.7, g.tail * 0.8, 60 * p, 0.45, g.tailG * 0.28, "lowpass"));
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
    json out = json::array(), in = json::array(), mid = json::array(), far = json::array();
    for (int k = 0; k < 3; k++) {
      json a = json::array(), b = json::array(), c = json::array(), d = json::array();
      gunShot(a, g, P[k], 0); gunShot(b, g, P[k], 1); gunShot(d, g, P[k], 3); gunShot(c, g, P[k], 2);
      out.push_back(a); in.push_back(b); mid.push_back(d); far.push_back(c);
    }
    add(std::string("zone_gun_") + cls, louder(out, 1.35), 0.03);
    add(std::string("zone_gun_") + cls + "_in", louder(in, 1.15), 0.03);
    add(std::string("zone_gun_") + cls + "_mid", louder(mid, 1.25), 0.03);
    add(std::string("zone_gun_") + cls + "_far", louder(far, 1.45), 0.03);
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
    // a suppressor: the crack is gone. A low cough, the gas still moving, and the action, which is now the loud part.
    supp.push_back(json::array({nh(0, 0.09, 420 * p, 0.7, 0.55, "lowpass"), nh(0, 0.045, 900 * p, 0.85, 0.22, "lowpass"),
                                tone(0, 95 * p, 0.07, "sine", 0.16, 60 * p),
                                nh(0.012, 0.016, 3100 * p, 3.5, 0.22, "bandpass"), nh(0.04, 0.014, 2200 * p, 3, 0.12, "bandpass")}));
  }
  add("zone_gun_dry", louder(dry, 2.2), 0.05); add("zone_gun_mag_out", louder(mout, 2.2), 0.2); add("zone_gun_mag_in", louder(min, 2.2), 0.2);
  add("zone_gun_rack", louder(rack, 2.2), 0.4); add("zone_gun_jam", louder(jam, 2.2), 0.2); add("zone_gun_supp", louder(supp, 2.5), 0.03);
  // underbarrel (a hollow thunk) and the RPG (a whoosh and a backblast). Not a firework.
  json glN = json::array(), glM = json::array(), glF = json::array(), glI = json::array();
  json rpN = json::array(), rpM = json::array(), rpF = json::array(), rpI = json::array();
  for (int k = 0; k < 3; k++) {
    const double p = P[k];
    glN.push_back(json::array({tone(0, 150 * p, 0.09, "sine", 0.55, 70 * p), nh(0, 0.08, 280 * p, 0.7, 0.5, "lowpass"),
                               nh(0.01, 0.12, 900 * p, 0.8, 0.18, "bandpass"), nh(0.02, 0.02, 1800 * p, 2, 0.08, "bandpass")}));
    glM.push_back(json::array({nh(0, 0.22, 220 * p, 0.65, 0.7, "lowpass"), tone(0, 90 * p, 0.2, "sine", 0.4, 50 * p),
                               nh(0.09, 0.2, 160 * p, 0.6, 0.35, "lowpass")}));
    glF.push_back(json::array({nh(0.05, 0.6, 80 * p, 0.5, 0.65, "lowpass"), tone(0.04, 48 * p, 0.5, "sine", 0.35, 32 * p),
                               swell(0.9, 140 * p, 40 * p, 0.5, 0.25, 0.3, 0.05)}));
    glI.push_back(json::array({tone(0, 140 * p, 0.1, "sine", 0.6, 60 * p), nh(0, 0.16, 240 * p, 0.65, 0.6, "lowpass"),
                               nh(0.02, 0.08, 700 * p, 0.7, 0.3, "bandpass"), nh(0.05, 0.14, 320 * p, 0.6, 0.25, "lowpass")}));
    rpN.push_back(json::array({swell(0.45, 180 * p, 740 * p, 0.7, 0.4, 0.15), nh(0, 0.35, 120 * p, 0.55, 0.7, "lowpass"),
                               tone(0, 55 * p, 0.28, "sine", 0.45, 40 * p), nh(0.02, 0.2, 600 * p, 0.6, 0.15, "bandpass")}));
    rpM.push_back(json::array({swell(0.7, 140 * p, 420 * p, 0.6, 0.28, 0.25), nh(0.02, 0.5, 100 * p, 0.55, 0.75, "lowpass"),
                               tone(0.02, 46 * p, 0.4, "sine", 0.4, 30 * p)}));
    rpF.push_back(json::array({swell(1.3, 90 * p, 40 * p, 0.5, 0.35, 0.4, 0.05), nh(0.08, 1.1, 55 * p, 0.45, 0.6, "lowpass"),
                               tone(0.06, 34 * p, 0.9, "sine", 0.32, 24 * p)}));
    rpI.push_back(json::array({nh(0, 0.4, 110 * p, 0.55, 0.85, "lowpass"), tone(0, 48 * p, 0.3, "sine", 0.5, 32 * p),
                               swell(0.35, 200 * p, 600 * p, 0.7, 0.2, 0.1), nh(0.03, 0.12, 500 * p, 0.7, 0.25, "bandpass")}));
  }
  add("zone_gun_gl", louder(glN, 1.4), 0.05); add("zone_gun_gl_mid", louder(glM, 1.3), 0.05);
  add("zone_gun_gl_far", louder(glF, 1.4), 0.05); add("zone_gun_gl_in", louder(glI, 1.2), 0.05);
  add("zone_gun_rpg", louder(rpN, 1.5), 0.08); add("zone_gun_rpg_mid", louder(rpM, 1.4), 0.08);
  add("zone_gun_rpg_far", louder(rpF, 1.5), 0.08); add("zone_gun_rpg_in", louder(rpI, 1.3), 0.08);
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
      const double d = e * 0.45, g = e ? 0.055 : 0.16;
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
    // gravity: a low shove of air. Undertow circles under 200 Hz; the draw and the burst stay in the bass.
    an[i++].push_back(json::array({swell(2.4, 42 * p, 110 * p, 0.6, 0.34, 0.45), swell(2.0, 90 * p, 48 * p, 0.55, 0.22, 0.35, 0.5),
                                   tone(0, 38 * p, 2.2, "sine", 0.16, 46 * p), nh(0.8, 0.05, 1800 * p, 2, 0.03, "bandpass")}));
    an[i++].push_back(json::array({swell(0.7, 36 * p, 90 * p, 0.55, 0.45, 0.85), tone(0, 32 * p, 0.7, "sine", 0.4, 58 * p), nh(0, 0.55, 80 * p, 0.5, 0.35, "lowpass")}));
    an[i++].push_back(json::array({tone(0, 48 * p, 0.55, "sine", 0.85, 24 * p), nh(0, 0.4, 90 * p, 0.5, 0.7, "lowpass"), nh(0.02, 0.08, 180 * p, 0.6, 0.2, "lowpass")}));
    // kickback: the same low whump, no bright tail. Spinner: a bass whirr, not a dentist's drill.
    an[i++].push_back(json::array({tone(0, 42 * p, 0.4, "sine", 0.75, 22 * p), nh(0, 0.32, 70 * p, 0.5, 0.55, "lowpass"), swell(0.45, 120 * p, 40 * p, 0.5, 0.18, 0.08)}));
    an[i++].push_back(json::array({tone(0, 55 * p, 1.7, "sine", 0.22, 140 * p), swell(1.8, 70 * p, 220 * p, 0.7, 0.28, 0.75), nh(0, 1.6, 90 * p, 0.55, 0.2, "lowpass")}));
    an[i++].push_back(json::array({tone(0, 1180 * p, 2.2, "sine", 0.035, 1176 * p), tone(0.05, 1770 * p, 1.8, "sine", 0.025, 1772 * p), tone(0.12, 2650 * p, 1.4, "sine", 0.018, 2645 * p), tone(0.3, 3520 * p, 0.9, "sine", 0.01, 3500 * p)}));
    an[i++].push_back(json::array({nh(0, 0.06, 5200 * p, 2, 0.3, "bandpass"), tone(0, 2600 * p, 0.18, "square", 0.04, 2300 * p), nh(0.05, 0.12, 3800 * p, 3, 0.12, "bandpass")}));
    // skip: the arrival is a low whoomp; the shimmer is only the aftertaste
    an[i++].push_back(json::array({tone(0, 180 * p, 0.45, "sine", 0.4, 40 * p), nh(0, 0.4, 70 * p, 0.5, 0.4, "lowpass"), tone(0.12, 900 * p, 0.35, "sine", 0.04, 1400 * p)}));
    // fire: the vent inhales, then the pillar pops. Crackling is the pops, not a hiss. Steam is a wet jet.
    an[i++].push_back(json::array({swell(0.55, 180 * p, 700 * p, 0.7, 0.28, 0.7), nh(0.5, 0.12, 1400 * p, 0.8, 0.55, "bandpass"),
                                   nh(0.52, 1.4, 320 * p, 0.6, 0.4, "lowpass"), tone(0.5, 70 * p, 1.2, "sine", 0.2, 48 * p)}));
    { json t = json::array(); t.push_back(nh(0, 1.5, 180 * p, 0.55, 0.16, "lowpass"));
      t.push_back(swell(0.4, 200 * p, 500 * p, 0.7, 0.08, 0.6));
      for (int j = 0; j < 7; j++) t.push_back(nh(0.35 + j * 0.16, 0.018, (900 + 180 * (j % 3)) * p, 2.5, 0.2, "bandpass"));
      an[i++].push_back(t); }
    an[i++].push_back(json::array({nh(0, 0.2, 400 * p, 0.7, 0.35, "lowpass"), nh(0.15, 1.3, 2800 * p, 0.8, 0.22, "highpass"), swell(1.2, 600 * p, 2200 * p, 0.8, 0.12, 0.2, 0.1)}));
    // electricity: dry ticks, then one crack. The hum is a mains buzz. The pop is a single snap.
    { json t = json::array(); for (int j = 0; j < 12; j++) t.push_back(nh(j * 0.12, 0.006, (3500 + 500 * (j % 4)) * p, 6, 0.22, "highpass"));
      t.push_back(nh(1.35, 0.04, 2400 * p, 0.55, 0.7, "bandpass")); an[i++].push_back(t); }
    an[i++].push_back(json::array({nh(0, 0.012, 4200 * p, 0.45, 0.9, "highpass"), nh(0.008, 0.05, 1800 * p, 0.6, 0.35, "bandpass"),
                                   tone(0, 90 * p, 0.12, "square", 0.06, 40 * p)}));
    an[i++].push_back(json::array({tone(0, 100 * p, 1.5, "sawtooth", 0.05, 100 * p), tone(0, 200 * p, 1.5, "square", 0.02, 200 * p), nh(0, 1.4, 3500 * p, 4, 0.04, "bandpass")}));
    an[i++].push_back(json::array({nh(0, 0.008, 5000 * p, 0.4, 0.95, "highpass"), nh(0.006, 0.04, 2200 * p, 0.7, 0.3, "bandpass")}));
    // chemistry: a hiss first. Bubbles and the glop sit under it.
    { json t = json::array(); t.push_back(nh(0, 1.8, 4600 * p, 0.7, 0.28, "highpass")); t.push_back(nh(0, 1.6, 1800 * p, 0.8, 0.08, "bandpass"));
      for (int j = 0; j < 6; j++) t.push_back(tone(j * 0.28, (280 + 40 * (j % 3)) * p, 0.05, "sine", 0.04, (500 + 80 * (j % 3)) * p));
      an[i++].push_back(t); }
    an[i++].push_back(json::array({nh(0, 1.1, 3800 * p, 0.75, 0.4, "highpass"), swell(1.0, 2200 * p, 900 * p, 0.8, 0.12, 0.1), nh(0.1, 0.4, 400 * p, 0.7, 0.12, "lowpass")}));
    an[i++].push_back(json::array({nh(0, 0.08, 3200 * p, 0.8, 0.25, "highpass"), tone(0, 240 * p, 0.1, "sine", 0.22, 70 * p), nh(0, 0.12, 600 * p, 1.2, 0.15, "bandpass")}));
    an[i++].push_back(json::array({nh(0, 0.15, 2400 * p, 0.8, 0.12, "highpass"), tone(0, 90 * p, 0.16, "sine", 0.28, 160 * p), nh(0, 0.1, 220 * p, 1.5, 0.2, "lowpass")}));
    // the lullfield's slow sleepy hum; the wisps' airy chime; ash moss burning; a cough in the gas
    an[i++].push_back(json::array({voice({{0, 220}, {1, 220}}, 1.2, 0.03, p, "oo", 0.2, 900, 4, 3, 0.4, 0.5), voice({{0, 196}, {1, 196}}, 1.2, 0.03, p, "oo", 0.2, 900, 4, 3, 0.4, 0.5, 1.1),
                                   voice({{0, 165}, {1, 160}}, 1.8, 0.03, p, "oo", 0.2, 900, 4, 3, 0.5, 0.8, 2.2)}));
    an[i++].push_back(json::array({tone(0, 880 * p, 1.2, "sine", 0.03, 990 * p), tone(0.2, 1320 * p, 1.0, "sine", 0.02, 1250 * p), voice({{0, 300}, {1, 340}}, 1.4, 0.02, p, "ee", 0.9, 1800, 3, 6, 0.4, 0.4)}));
    an[i++].push_back(json::array({nh(0, 0.5, 4500 * p, 0.9, 0.25, "highpass"), nh(0, 0.3, 1200 * p, 1.5, 0.08, "bandpass")}));
    an[i++].push_back(json::array({nh(0, 0.12, 700 * p, 1.2, 0.4, "bandpass"), voice({{0, 180}, {1, 140}}, 0.15, 0.08, p, "a", 0.8, 1500, 0, 0, 0.01, 0.05), nh(0.28, 0.1, 650 * p, 1.2, 0.3, "bandpass")}));
  }
  for (int i = 0; i < NA; i++) add((std::string("zone_ano_") + AN[i]).c_str(), an[i], GAP[i]);
  {   // the Generators' energy spheres: a mains buzz with its harmonics and a fizz on top (audio/ambience.cpp keeps it going near one)
    json hum = json::array();
    for (double q : {0.98, 1.0, 1.02})
      hum.push_back(json::array({tone(0, 50 * q, 3.2, "sawtooth", 0.05, 50 * q), tone(0, 100 * q, 3.2, "sine", 0.07, 101 * q), tone(0, 150 * q, 3.2, "triangle", 0.025, 149 * q),
                                 nh(0, 3.2, 2400 * q, 3, 0.015, "bandpass")}));
    add("zone_sphere_hum", hum, 0);
  }
  // ---- loot furniture opened up close (sim/controller.cpp plays the block's openSound), a crate smashed (sim/zone_crates.cpp) ----
  json wood = json::array(), drawer = json::array(), metal = json::array(), fridge = json::array(), box = json::array(), smash = json::array();
  for (int k = 0; k < 3; k++) {
    const double p = P[k];
    // a wardrobe / cupboard door: the hinge's dry creak, the door knocking open
    wood.push_back(json::array({tone(0, 310 * p, 0.45, "sawtooth", 0.035, 440 * p), nh(0, 0.42, 950 * p, 6, 0.12, "bandpass"), tone(0.1, 620 * p, 0.3, "triangle", 0.02, 700 * p),
                                nh(0.44, 0.05, 420 * p, 1.4, 0.22, "bandpass")}));
    // a drawer: the runners rumbling, the knock at the end
    drawer.push_back(json::array({nh(0, 0.3, 380 * p, 1.1, 0.4, "lowpass"), nh(0, 0.28, 2200 * p, 2.5, 0.05, "bandpass"), nh(0.3, 0.05, 620 * p, 1.4, 0.3, "bandpass"),
                                  tone(0.3, 160 * p, 0.12, "sine", 0.12, 110 * p)}));
    // a steel locker: the latch, the door's ring and the squeal of its hinge
    metal.push_back(json::array({nh(0, 0.05, 1900 * p, 3, 0.3, "bandpass"), tone(0.05, 410 * p, 0.6, "triangle", 0.06, 395 * p), tone(0.05, 1170 * p, 0.45, "sine", 0.025, 1150 * p),
                                 nh(0.08, 0.38, 3100 * p, 9, 0.05, "bandpass")}));
    // a fridge: the seal letting go, a bottle chinking inside
    fridge.push_back(json::array({nh(0, 0.1, 320 * p, 1, 0.35, "lowpass"), tone(0.02, 95 * p, 0.25, "sine", 0.18, 70 * p), tone(0.16, 2450 * p, 0.16, "sine", 0.03, 2420 * p),
                                  tone(0.23, 2950 * p, 0.12, "sine", 0.02, 2930 * p), nh(0.05, 0.3, 1400 * p, 4, 0.03, "bandpass")}));
    // a wooden case: two latches snapping, the lid knocking back
    box.push_back(json::array({nh(0, 0.03, 2400 * p, 3, 0.3, "bandpass"), nh(0.09, 0.03, 2200 * p, 3, 0.28, "bandpass"), nh(0.22, 0.07, 380 * p, 1.2, 0.35, "lowpass"),
                               tone(0.22, 140 * p, 0.12, "sine", 0.15, 90 * p)}));
    // a crate smashed: the crack, the boards' thump, splinters ticking down
    json s = json::array({nh(0, 0.07, 1500 * p, 1.2, 0.7, "bandpass"), nh(0, 0.35, 480 * p, 0.9, 0.55, "lowpass"), tone(0, 150 * p, 0.16, "sine", 0.3, 80 * p)});
    for (int i = 0; i < 6; i++) s.push_back(nh(0.06 + i * 0.045 + 0.01 * ((i * 7 + k) % 3), 0.03, (2300 + 300 * ((i + k) % 3)) * p, 2, 0.18 - i * 0.02, "bandpass"));
    smash.push_back(s);
  }
  add("zone_open_wood", wood, 0.2); add("zone_open_drawer", drawer, 0.2); add("zone_open_metal", metal, 0.2); add("zone_open_fridge", fridge, 0.2);
  add("zone_open_case", box, 0.2); add("zone_crate_break", smash, 0.05);

  // a Geiger-Müller tube: the discharge is a snap, then the tube rings for a few cycles. Not a UI beep.
  // The detector is a separate sine pip. The click rate is the dose (sim/zone_core.cpp clocks it; client_fx.cpp
  // plays this sample instead of "click" while the body is actually in a field).
  json geiger = json::array(), det = json::array();
  for (int k = 0; k < 3; k++) {
    const double p = P[k];
    geiger.push_back(json::array({nh(0, 0.0011, 4800 * p, 0.55, 0.9, "highpass"), nh(0, 0.0035, 3200 * p, 6, 0.42, "bandpass"),
                                  tone(0, 2700 * p, 0.003, "sine", 0.05, 1900 * p)}));
    det.push_back(json::array({tone(0, 1320 * p, 0.07, "sine", 0.28, 1480 * p), tone(0.012, 1980 * p, 0.045, "sine", 0.06, 1960 * p)}));
  }
  add("zone_geiger", geiger, 0.012); add("zone_det_beep", det, 0.04);

  // anomaly families, heard before the one in the grass is obvious (ambience.cpp, at the field)
  json gravL = json::array(), chemL = json::array(), elecL = json::array(), fireL = json::array();
  for (int k = 0; k < 3; k++) {
    const double p = P[k];
    gravL.push_back(json::array({swell(3.2, 36 * p, 78 * p, 0.5, 0.3, 0.5), swell(2.8, 70 * p, 40 * p, 0.5, 0.18, 0.4, 0.4), tone(0, 32 * p, 3.0, "sine", 0.1, 38 * p)}));
    chemL.push_back(json::array({nh(0, 3.2, 4200 * p, 0.65, 0.16, "highpass"), nh(0.4, 2.4, 2400 * p, 0.8, 0.06, "bandpass"),
                                 tone(0.6, 320 * p, 0.05, "sine", 0.04, 520 * p), tone(1.7, 280 * p, 0.05, "sine", 0.035, 480 * p)}));
    { json t = json::array({tone(0, 100 * p, 3.0, "sawtooth", 0.012, 100 * p)});
      for (int j = 0; j < 9; j++) t.push_back(nh(j * 0.34, 0.007, (2800 + 400 * (j % 5)) * p, 5, 0.14, "highpass"));
      elecL.push_back(t); }
    fireL.push_back(json::array({swell(0.7, 160 * p, 640 * p, 0.7, 0.16, 0.75), nh(0.62, 0.06, 1200 * p, 1.2, 0.28, "bandpass"),
                                 nh(0.7, 2.2, 220 * p, 0.55, 0.14, "lowpass"), swell(0.8, 200 * p, 520 * p, 0.7, 0.08, 0.6, 1.6)}));
  }
  add("zone_ano_loop_grav", gravL, 0); add("zone_ano_loop_chem", chemL, 0); add("zone_ano_loop_elec", elecL, 0); add("zone_ano_loop_fire", fireL, 0);

  // kickback idle (render/zone_anomaly_fx.cpp): one shove of air over the stain. Thinner and higher than
  // the undertow's gust, which is the long bass crush. Not that sample.
  // gas-bank idle: a hiss sitting at the ankles. No bubbles (that is the acid's fizz) and no cough (the puff).
  json shove = json::array(), hiss = json::array();
  for (int k = 0; k < 3; k++) {
    const double p = P[k];
    shove.push_back(json::array({swell(0.85, 180 * p, 640 * p, 0.65, 1.05, 0.28), nh(0.05, 0.55, 260 * p, 0.75, 1.05, "bandpass"),
                                 nh(0.0, 0.18, 90 * p, 0.6, 0.28, "lowpass"), tone(0.02, 150 * p, 0.28, "sine", 0.22, 78 * p)}));
    // overlapping leaks around 500–1400 Hz, so the bank holds instead of coughing out like a puff
    hiss.push_back(json::array({swell(1.35, 720 * p, 1180 * p, 2.2, 0.95, 0.42), swell(1.3, 1100 * p, 760 * p, 2.4, 0.82, 0.5, 0.75),
                                swell(1.25, 860 * p, 1400 * p, 1.8, 0.7, 0.4, 1.35), swell(1.4, 480 * p, 390 * p, 1.3, 0.32, 0.55, 0.15)}));
  }
  add("zone_ano_shove", shove, 1.2); add("zone_ano_hiss", hiss, 1.0);

  // mutants: idle, the hunt, a hit, and death. The cat's idle is a breath, not a tell.
  auto mv = [&](const char* nm, json a, json b, json c, json d, double gap) {
    add((std::string("zone_mut_") + nm + "_idle").c_str(), a, gap);
    add((std::string("zone_mut_") + nm + "_hunt").c_str(), b, 0.4);
    add((std::string("zone_mut_") + nm + "_hurt").c_str(), c, 0.15);
    add((std::string("zone_mut_") + nm + "_death").c_str(), d, 0.3);
  };
  json dogI = json::array(), dogH = json::array(), dogU = json::array(), dogD = json::array();
  json boarI = json::array(), boarH = json::array(), boarU = json::array(), boarD = json::array();
  json ratI = json::array(), ratH = json::array(), ratU = json::array(), ratD = json::array();
  json crI = json::array(), crH = json::array(), crU = json::array(), crD = json::array();
  json catI = json::array(), catH = json::array(), catU = json::array(), catD = json::array();
  json huskI = json::array(), huskH = json::array(), huskU = json::array(), huskD = json::array();
  json pupI = json::array(), pupH = json::array(), pupU = json::array(), pupD = json::array();
  json brI = json::array(), brH = json::array(), brU = json::array(), brD = json::array();
  for (int k = 0; k < 3; k++) {
    const double p = P[k];
    dogI.push_back(json::array({voice({{0, 520}, {1, 380}}, 0.18, 0.07, p, "a", 0.45, 1600, 0, 0, 0.01, 0.04)}));
    { json t = json::array(); for (int i = 0; i < 3; i++) t.push_back(voice({{0, 640}, {1, 420}}, 0.14, 0.1, p, "a", 0.55, 1800, 0, 0, 0.01, 0.02, i * 0.22)); dogH.push_back(t); }
    dogU.push_back(json::array({voice({{0, 900}, {1, 640}}, 0.22, 0.1, p, "ee", 0.4, 2200, 0, 0, 0.01, 0.05)}));
    dogD.push_back(json::array({voice({{0, 480}, {0.4, 260}, {1, 140}}, 0.9, 0.09, p, "ou", 0.35, 1200, 4, 6, 0.05, 0.3)}));
    boarI.push_back(json::array({voice({{0, 140}, {1, 110}}, 0.28, 0.08, p, "oo", 0.7, 700, 0, 0, 0.02, 0.08), nh(0, 0.25, 180 * p, 0.8, 0.08, "lowpass")}));
    boarH.push_back(json::array({voice({{0, 220}, {0.3, 380}, {1, 160}}, 0.55, 0.12, p, "ae", 0.65, 1400, 0, 0, 0.02, 0.1), nh(0, 0.4, 200 * p, 0.7, 0.12, "lowpass")}));
    boarU.push_back(json::array({voice({{0, 420}, {1, 700}}, 0.3, 0.12, p, "ee", 0.5, 2000, 0, 0, 0.01, 0.08)}));
    boarD.push_back(json::array({voice({{0, 180}, {1, 70}}, 0.8, 0.1, p, "oo", 0.6, 600, 3, 4, 0.05, 0.3), nh(0, 0.7, 90 * p, 0.6, 0.1, "lowpass")}));
    ratI.push_back(json::array({voice({{0, 2400}, {1, 1800}}, 0.08, 0.05, p, "ee", 0.4, 4000, 0, 0, 0.005, 0.02)}));
    { json t = json::array(); for (int i = 0; i < 5; i++) t.push_back(voice({{0, 2800}, {1, 2000}}, 0.05, 0.05, p, "i", 0.35, 4500, 0, 0, 0.004, 0.01, i * 0.07)); ratH.push_back(t); }
    ratU.push_back(json::array({voice({{0, 3200}, {1, 2200}}, 0.1, 0.07, p, "ee", 0.3, 5000, 0, 0, 0.004, 0.02)}));
    ratD.push_back(json::array({voice({{0, 2000}, {1, 600}}, 0.25, 0.05, p, "ee", 0.4, 3000, 0, 0, 0.01, 0.05)}));
    crI.push_back(json::array({nh(0, 0.4, 1400 * p, 2, 0.08, "bandpass"), voice({{0, 260}, {1, 200}}, 0.35, 0.05, p, "uu", 0.8, 900, 0, 0, 0.04, 0.1)}));
    crH.push_back(json::array({voice({{0, 500}, {0.2, 800}, {1, 300}}, 0.45, 0.1, p, "ae", 0.7, 1800, 0, 0, 0.02, 0.08), nh(0, 0.3, 900 * p, 1.2, 0.1, "bandpass")}));
    crU.push_back(json::array({voice({{0, 700}, {1, 400}}, 0.2, 0.09, p, "aa", 0.6, 1600, 0, 0, 0.01, 0.04), nh(0, 0.12, 600 * p, 1, 0.1, "lowpass")}));
    crD.push_back(json::array({voice({{0, 300}, {1, 90}}, 0.7, 0.08, p, "uu", 0.75, 700, 3, 3, 0.04, 0.25), nh(0, 0.6, 200 * p, 0.7, 0.1, "lowpass")}));
    catI.push_back(json::array({nh(0, 0.5, 400 * p, 0.8, 0.04, "lowpass"), voice({{0, 90}, {1, 80}}, 0.4, 0.02, p, "uu", 0.9, 500, 0, 0, 0.08, 0.15)}));
    catH.push_back(json::array({voice({{0, 160}, {0.4, 220}, {1, 100}}, 0.7, 0.07, p, "ao", 0.55, 800, 5, 4, 0.06, 0.25), nh(0, 0.6, 180 * p, 0.7, 0.08, "lowpass")}));
    catU.push_back(json::array({voice({{0, 500}, {1, 280}}, 0.25, 0.08, p, "aa", 0.5, 1500, 0, 0, 0.01, 0.06)}));
    catD.push_back(json::array({voice({{0, 200}, {1, 60}}, 0.8, 0.07, p, "oo", 0.6, 600, 3, 3, 0.05, 0.3)}));
    huskI.push_back(json::array({voice({{0, 150}, {1, 130}}, 0.7, 0.06, p, "uu", 0.8, 700, 3, 3, 0.1, 0.3)}));
    huskH.push_back(json::array({voice({{0, 180}, {0.5, 240}, {1, 120}}, 1.0, 0.09, p, "ao", 0.75, 800, 4, 4, 0.08, 0.35)}));
    huskU.push_back(json::array({voice({{0, 280}, {1, 160}}, 0.3, 0.08, p, "aa", 0.7, 1100, 0, 0, 0.02, 0.08)}));
    huskD.push_back(json::array({voice({{0, 140}, {1, 60}}, 1.1, 0.07, p, "uu", 0.85, 500, 2, 2, 0.1, 0.4)}));
    pupI.push_back(json::array({voice({{0, 210}, {1, 230}}, 1.6, 0.035, p, "ee", 0.9, 1400, 3, 5, 0.3, 0.5), tone(0, 330 * p, 1.4, "sine", 0.015, 340 * p)}));
    pupH.push_back(json::array({voice({{0, 280}, {0.4, 420}, {1, 200}}, 1.2, 0.06, p, "ee", 0.85, 1800, 6, 8, 0.1, 0.3), swell(1.2, 600 * p, 200 * p, 2, 0.04, 0.3)}));
    pupU.push_back(json::array({voice({{0, 500}, {1, 240}}, 0.35, 0.07, p, "aa", 0.6, 1600, 0, 0, 0.02, 0.08), tone(0, 880 * p, 0.2, "sine", 0.02, 700 * p)}));
    pupD.push_back(json::array({voice({{0, 240}, {1, 80}}, 1.3, 0.05, p, "uu", 0.9, 700, 3, 4, 0.15, 0.5), tone(0, 110 * p, 1.2, "sine", 0.04, 60 * p)}));
    brI.push_back(json::array({nh(0, 0.8, 60 * p, 0.5, 0.12, "lowpass"), voice({{0, 70}, {1, 55}}, 0.7, 0.06, p, "oo", 0.8, 400, 2, 2, 0.1, 0.3)}));
    brH.push_back(json::array({voice({{0, 90}, {0.3, 140}, {1, 60}}, 1.1, 0.12, p, "ao", 0.7, 500, 4, 3, 0.08, 0.4), nh(0, 0.9, 50 * p, 0.45, 0.18, "lowpass"), tone(0.1, 40 * p, 0.5, "sine", 0.15, 28 * p)}));
    brU.push_back(json::array({voice({{0, 160}, {1, 90}}, 0.4, 0.1, p, "aa", 0.6, 700, 0, 0, 0.02, 0.1), nh(0, 0.3, 80 * p, 0.6, 0.15, "lowpass")}));
    brD.push_back(json::array({voice({{0, 80}, {1, 36}}, 1.2, 0.1, p, "oo", 0.75, 350, 2, 2, 0.08, 0.45), tone(0, 32 * p, 1.0, "sine", 0.2, 22 * p)}));
  }
  mv("dog", dogI, dogH, dogU, dogD, 0.8); mv("boar", boarI, boarH, boarU, boarD, 1.2);
  mv("rat", ratI, ratH, ratU, ratD, 0.4); mv("crawler", crI, crH, crU, crD, 1.0);
  mv("cat", catI, catH, catU, catD, 1.5); mv("husk", huskI, huskH, huskU, huskD, 1.4);
  mv("puppet", pupI, pupH, pupU, pupD, 1.6); mv("brute", brI, brH, brU, brD, 1.8);

  // a real firefight, heard as yard reports rather than the thin hill shot (ambience.cpp)
  json fight = json::array();
  for (int k = 0; k < 3; k++) {
    json t = json::array();
    const double p = P[k];
    for (int i = 0; i < 5; i++) {
      const double at = i * (0.11 + 0.02 * (i % 2));
      t.push_back(nh(at, 0.05, (520 - 40 * (i % 3)) * p, 0.8, 0.35, "lowpass"));
      t.push_back(tone(at, 78 * p, 0.08, "sine", 0.12, 50 * p));
    }
    t.push_back(nh(0.9, 0.4, 160 * p, 0.6, 0.2, "lowpass"));
    fight.push_back(t);
  }
  add("zone_fight", louder(fight, 1.6), 0.4);

  // biome beds and the half-hour before the rain (ambience.cpp overlaps them)
  json marsh = json::array(), pine = json::array(), yard = json::array(), city = json::array(), lab = json::array(), pre = json::array();
  for (int k = 0; k < 3; k++) {
    const double p = P[k];
    { json t = json::array({nh(0, 4.2, 380 * p, 0.6, 0.07, "lowpass"), nh(1.6, 2.6, 260 * p, 0.55, 0.05, "lowpass")});
      for (int j = 0; j < 8; j++) t.push_back(tone(0.2 + j * 0.45, (3100 + 180 * (j % 4)) * p, 0.04, "sine", 0.012, (3400 + 120 * (j % 3)) * p));
      t.push_back(voice({{0, 210}, {0.4, 280}, {1, 180}}, 0.45, 0.03, p, "oo", 0.3, 900, 0, 0, 0.04, 0.15, 2.2));
      marsh.push_back(t); }
    pine.push_back(json::array({nh(0, 4.2, 1800 * p, 0.7, 0.045, "bandpass"), nh(0, 4.0, 240 * p, 0.55, 0.05, "lowpass"),
                                tone(1.2, 1900 * p, 0.15, "sine", 0.008, 2100 * p)}));
    yard.push_back(json::array({tone(0, 50 * p, 4.0, "sawtooth", 0.018, 50 * p), tone(0, 100 * p, 4.0, "sine", 0.012, 100 * p),
                                nh(0, 4.0, 320 * p, 0.6, 0.04, "lowpass"), nh(1.4, 0.08, 900 * p, 2, 0.06, "bandpass"), nh(3.1, 0.05, 1400 * p, 2, 0.04, "bandpass")}));
    city.push_back(json::array({nh(0, 4.2, 500 * p, 0.55, 0.06, "lowpass"), nh(0, 4.0, 1400 * p, 0.8, 0.02, "bandpass"),
                                tone(2.0, 620 * p, 0.6, "triangle", 0.008, 540 * p)}));
    lab.push_back(json::array({tone(0, 50 * p, 4.0, "sine", 0.04, 50 * p), tone(0, 100 * p, 4.0, "sine", 0.015, 100.4 * p),
                               nh(0, 4.0, 180 * p, 0.6, 0.03, "lowpass"), tone(1.1, 1400 * p, 0.08, "sine", 0.02, 900 * p),
                               tone(2.8, 1600 * p, 0.06, "sine", 0.015, 1100 * p)}));
    pre.push_back(json::array({nh(0, 4.5, 160 * p, 0.5, 0.45, "lowpass"), swell(4.2, 220 * p, 60 * p, 0.55, 0.28, 0.4),
                               nh(2.4, 1.6, 70 * p, 0.45, 0.4, "lowpass")}));
  }
  add("zone_bed_marsh", marsh, 0); add("zone_bed_pine", pine, 0); add("zone_bed_yard", yard, 0);
  add("zone_bed_city", city, 0); add("zone_bed_lab", lab, 0); add("zone_prestorm", pre, 0);

  // footsteps, one surface each (audio.cpp swaps them in for the block-game step in a Zone world)
  json stD = json::array(), stW = json::array(), stM = json::array(), stA = json::array(), stC = json::array();
  for (int k = 0; k < 3; k++) {
    const double p = P[k];
    stD.push_back(json::array({nh(0, 0.09, 280 * p, 0.7, 0.45, "lowpass"), nh(0.02, 0.06, 900 * p, 1.2, 0.12, "bandpass")}));
    stW.push_back(json::array({nh(0, 0.07, 700 * p, 1.1, 0.35, "bandpass"), tone(0, 180 * p, 0.05, "sine", 0.08, 120 * p)}));
    stM.push_back(json::array({nh(0, 0.04, 1800 * p, 2.2, 0.4, "bandpass"), tone(0, 240 * p, 0.08, "triangle", 0.06, 200 * p)}));
    stA.push_back(json::array({nh(0, 0.12, 500 * p, 0.6, 0.35, "lowpass"), nh(0, 0.08, 2000 * p, 0.8, 0.12, "highpass")}));
    stC.push_back(json::array({nh(0, 0.05, 900 * p, 1.4, 0.4, "bandpass"), tone(0, 140 * p, 0.04, "sine", 0.1, 90 * p)}));
  }
  add("zone_step_dirt", louder(stD, 4.8), 0.12); add("zone_step_wood", louder(stW, 3.6), 0.12);
  add("zone_step_metal", louder(stM, 2.4), 0.12); add("zone_step_water", louder(stA, 1.6), 0.12);
  add("zone_step_concrete", louder(stC, 3.2), 0.12);

  // the little speaker a camp radio sits in: hiss and a 1 kHz cone, under the tune (ambience.cpp)
  json spk = json::array();
  for (int k = 0; k < 3; k++) {
    const double p = P[k];
    spk.push_back(json::array({nh(0, 2.4, 1400 * p, 1.6, 0.04, "bandpass"), nh(0, 2.4, 3200 * p, 0.8, 0.015, "highpass"),
                               tone(0, 1000 * p, 2.2, "sine", 0.006, 1004 * p)}));
  }
  add("zone_radio_hiss", spk, 0);
}

} // namespace dq::snd
