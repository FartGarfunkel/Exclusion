// audio/ambience.h — the world's background sound and how sound travels in it (game thread).
//  * game/ambience.js: wind on the heights (a live band of noise), birds by day, crickets at night,
//    drips and far rumbles in caves, the hiss of the Ember Depths;
//  * game/sound3.js: the air around the listener is flooded out a few times a second, so a sound round
//    the corner of a cave is heard the long way round and one behind rock comes muffled (reach); small
//    rooms ring a little and caverns a lot (reverb sends); sit in the dark long enough and the caves
//    make their noises.
#pragma once
#include "audio/engine.h"

namespace dq::snd {

class Ambience {
public:
  explicit Ambience(AudioEngine::Impl& e) : e(e) {}
  void update(double dt, const AudioScene& s);
  // snd3Reach: d = how far the sound travels to the listener, wall = how much is in the way
  void reach(vec3 listener, vec3 src, float& d, float& wall) const;
private:
  AudioEngine::Impl& e;
  static constexpr int R = 20, N = 2 * R + 1;
  struct Field { bool valid = false, open = true, complete = false; int cells = 0; float openFrac = 0; ivec3 o{0}; vec3 h{0}; };
  Field f;
  std::vector<u8> dist;
  std::vector<int> queue;
  double fieldT = 0, moodT = 0, mood = 0;
  double tBird = 3, tCricket = 2, tDrip = 4, tRumble = 30, tLand = 4, tFar = 12, tPsy = 6, tThunder = 20, tSiren = 0, tEm = 0, tCrack = 0, tHeart = 0;
  double tMurmur = 3; int emBolt = -1, emSweepIn = -1, emSweepOver = -1;   // Exclusion: camp talk; the emission's bolts and passes heard (render/zone_emission_fx.h)
  float windG = 0;
  void computeField(const AudioScene& s);
  float wallCount(const World& w, vec3 a, vec3 b) const;
  void at(const AudioScene& s, const char* name, float dist, float range = 40);
  void zone(const AudioScene& s, float dt, float vol, float open, float day);
  void atPos(vec3 p, const char* name, float range, float vol = 1);   // a sound at a place
  void flat(const char* name);   // a sound with no place (all around you)   // Exclusion: the Zone's far-off sounds
};

} // namespace dq::snd
