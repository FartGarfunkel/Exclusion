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
