// The Zone's room volumes: a shut hut, a corridor, a plant floor, a buried room,
// and the crossfade as the listener steps through a door. No world and no audio device.
#include "audio/zone_snd_env.h"
#include <cstdio>
#include <string>

static int fails = 0;
static void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) fails++;
}

int main() {
  std::string why;
  const bool ok = dq::snd::zoneEnvSelfCheck(why);
  if (!why.empty()) std::fputs(why.c_str(), stderr);
  check(ok, "room, hall, bunker and the door crossfade");
  return fails ? 1 : 0;
}
