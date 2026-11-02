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
  add("zone_siren", siren, 7); add("zone_em_rumble", rumble, 3); add("zone_em_crack", crack, 1); add("zone_em_wave", wave, 4); add("zone_heartbeat", heart, 0.6);
  add("zone_shot", single, 2); add("zone_shots", burst, 3); add("zone_howl", howl, 6); add("zone_bark", bark, 3); add("zone_crow", crow, 2);
  add("zone_roar", roar, 8); add("zone_creak", creak, 3); add("zone_clank", clank, 3); add("zone_thunder", thunder, 8); add("zone_whisper", whisper, 5);
}

} // namespace dq::snd
