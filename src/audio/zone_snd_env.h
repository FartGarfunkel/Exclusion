// audio/zone_snd_env.h — a sound environment on the volume the listener is standing in.
//
// OpenXRay stores an environment id on a geometry brush and, each frame,
// CSoundRender_Core::update_listener does e_current.lerp(e_current, e_target, dt)
// toward whatever get_environment hits under the listener (SoundRender_Scene casts
// a ray down onto that brush). Walking through a door is a change of volume, then
// about a second of crossfade. Exclusion has no .snd_env mesh. The volume is read
// off the voxel shell instead: a ceiling and walls make a shut room, a long or tall
// shell makes a hall, buried stone or the pocket bunker makes the underground tail.
//
// The preset names follow Anomaly's open environment list (house, room, long-corridor,
// plant, forest, open-space, yard): short and dull outside, a short room, a long hall,
// a dark bunker. The decay times are Exclusion's own. No level file and no recording
// is copied. Adapted from OpenXRay (MIT). See docs/CREDITS.md.
#pragma once
#include "common/common.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace dq {
class World;
namespace snd {

constexpr float kZoneEnvTau = 1.f;   // seconds. update_listener's lerp factor is the frame dt, so the blend settles in about a second

enum ZoneEnv : int {
  ENV_OPEN = 0,     // open ground: short, dull
  ENV_FOREST = 1,   // trees, marsh, a village
  ENV_STREET = 2,   // a built region, heard from the street
  ENV_ROOM = 3,     // a shut room (Anomaly's house / room)
  ENV_HALL = 4,     // a corridor, a hangar, a plant floor (long-corridor / plant)
  ENV_BUNKER = 5,   // buried, or the pocket bunker
  ENV_BLOCK = 6     // the block game's own room, not a Zone volume
};

// One step of e_current.lerp toward the volume you just entered.
inline float zoneEnvApproach(float cur, float target, float seconds) {
  if (seconds <= 0.f) return cur;
  const float k = 1.f - std::exp(-seconds / kZoneEnvTau);
  return cur + (target - cur) * k;
}

// What the rays found. Distances are in blocks. A span is set only when both
// directions of that axis hit a cover block; openReach is the farthest one-sided hit.
struct ZoneEnvGeom {
  int ceil = 0;        // air blocks above the head until cover; 0 = open sky in range
  int stacked = 0;     // more cover piled on that ceiling (earth over a cellar)
  int sky = 15;        // sky light at the head, 0..15
  int walls = 0;       // how many of the four cardinal rays hit cover
  int spanX = 0, spanZ = 0;
  int openReach = 0;
  bool pocket = false; // the player's bunker realm
  bool below = false;  // head under the plain
};

struct ZoneEnvTag {
  int preset = ENV_OPEN;
  float room = 0.035f, cave = 0.f;
  float enclose = 0.f;   // 0 out in the air, 1 shut in
  bool indoor = false;
};

struct ZoneEnvHold {
  int preset = ENV_OPEN;
  float room = 0.035f, cave = 0.f;
  bool inside = false;
  int pending = -1;    // a room/hall flip has to agree twice, so a grazing ray does not retune
  int pendingN = 0;
};

// Classify the shell. outdoorPreset / outdoorRoom are the region you would hear
// with no roof (open, forest, street).
inline ZoneEnvTag zoneEnvTag(const ZoneEnvGeom& g, int outdoorPreset, float outdoorRoom) {
  ZoneEnvTag t;
  t.preset = outdoorPreset;
  t.room = outdoorRoom;
  t.cave = 0.f;
  t.enclose = 0.f;
  t.indoor = false;
  if (g.pocket) {
    t.indoor = true; t.enclose = 1.f; t.preset = ENV_BUNKER; t.room = 0.07f; t.cave = 0.50f;
    return t;
  }
  const bool roof = g.ceil > 0 && g.ceil <= 12;
  float roofK = 0.f;
  if (g.ceil > 0 && g.ceil <= 4) roofK = 1.f;
  else if (g.ceil > 0 && g.ceil <= 8) roofK = 1.f - (g.ceil - 4) / 8.f;
  else if (g.ceil > 0 && g.ceil <= 12) roofK = 0.35f;
  const float wallK = std::clamp(g.walls / 4.f, 0.f, 1.f);
  float enclose = roof ? roofK * (0.42f + 0.58f * wallK) : 0.f;
  if (roof && g.sky <= 4) enclose = std::max(enclose, 0.74f);
  const bool buried = (g.stacked >= 3 && g.sky <= 4 && roof) || (g.below && roof && g.sky <= 2);
  if (buried) enclose = std::max(enclose, 0.9f);
  t.enclose = std::clamp(enclose, 0.f, 1.f);
  if (!(buried || t.enclose >= 0.68f)) return t;

  const bool hall = [&]() {
    if (g.spanX > 0 && g.spanZ > 0) {
      const int a = std::min(g.spanX, g.spanZ), b = std::max(g.spanX, g.spanZ);
      if (b >= 11 && b * 5 >= a * 11) return true;          // a corridor, long side about 2.2x
      if (a >= 12 && b >= 12) return true;                  // a plant floor, a hangar
      if (g.ceil >= 6 && a >= 8 && b >= 8) return true;     // tall enough to ring
      return false;
    }
    const int shut = g.spanX > 0 ? g.spanX : g.spanZ;
    // one axis shut and narrow, the other a passage: a corridor with a door at the end
    if (shut >= 2 && shut <= 4 && g.ceil > 0 && g.ceil <= 5 && g.walls >= 2 && (g.openReach >= 8 || g.openReach == 0))
      return true;
    if (g.ceil >= 6 && g.walls >= 2 && g.sky <= 5) return true;
    return false;
  }();

  t.indoor = true;
  if (buried) { t.preset = ENV_BUNKER; t.room = 0.07f; t.cave = 0.50f; }
  else if (hall) { t.preset = ENV_HALL; t.room = 0.20f; t.cave = 0.24f; }
  else {
    t.preset = ENV_ROOM;
    float span = (float)std::max(g.spanX, g.spanZ);
    if (span < 1.f) span = 6.f;
    t.room = std::clamp(span / 12.f, 0.15f, 1.f) * 0.34f;
    t.cave = std::clamp((span - 8.f) / 16.f, 0.f, 1.f) * 0.12f;
  }
  return t;
}

// Schmitt on the shell: step through a door and the volume changes; stand in the
// doorway and it holds the volume you came from. Returns true when the target changed.
inline bool zoneEnvFollow(ZoneEnvHold& h, const ZoneEnvTag& sensed, int outdoorPreset, float outdoorRoom) {
  int want = h.preset;
  float wantR = h.room, wantC = h.cave;
  bool wantIn = h.inside;
  if (sensed.indoor) {
    bool take = !h.inside || sensed.preset == h.preset;
    if (!take) {
      if (sensed.preset == ENV_BUNKER || h.preset == ENV_BUNKER) take = true;
      else if (sensed.preset == h.pending) { h.pendingN++; take = h.pendingN >= 2; }
      else { h.pending = sensed.preset; h.pendingN = 1; }
    }
    if (take) {
      wantIn = true;
      if (sensed.preset != h.preset) { want = sensed.preset; wantR = sensed.room; wantC = sensed.cave; }
      h.pending = sensed.preset; h.pendingN = 0;
    }
  } else if (!h.inside) {
    want = outdoorPreset; wantR = outdoorRoom; wantC = 0.f;
    h.pending = -1; h.pendingN = 0;
  } else if (sensed.enclose <= 0.22f) {
    wantIn = false; want = outdoorPreset; wantR = outdoorRoom; wantC = 0.f;
    h.pending = -1; h.pendingN = 0;
  }
  const bool changed = want != h.preset || wantIn != h.inside
      || std::fabs(wantR - h.room) > 0.001f || std::fabs(wantC - h.cave) > 0.001f;
  h.inside = wantIn; h.preset = want; h.room = wantR; h.cave = wantC;
  return changed;
}

// Rays from the head. Cover is a solid shell: cubes, slabs, stairs, shut doors,
// glass. Fences, bars, leaves and an open door are not a room.
ZoneEnvGeom zoneEnvMeasure(const World& w, vec3 head, int sky, int realm);

inline bool zoneEnvSelfCheck(std::string& why) {
  auto fail = [&](const char* m) { why += m; why += "\n"; };
  ZoneEnvGeom open;
  open.sky = 15;
  ZoneEnvTag o = zoneEnvTag(open, ENV_FOREST, 0.08f);
  if (o.indoor || o.preset != ENV_FOREST || o.enclose > 0.2f) fail("open ground stayed indoor");

  ZoneEnvGeom porch;
  porch.ceil = 2; porch.stacked = 1; porch.sky = 12; porch.walls = 1;
  ZoneEnvTag p = zoneEnvTag(porch, ENV_STREET, 0.12f);
  if (p.indoor || p.enclose < 0.30f || p.enclose > 0.65f) fail("a porch was not the doorway band");

  ZoneEnvGeom hut;
  hut.ceil = 2; hut.stacked = 1; hut.sky = 0; hut.walls = 4; hut.spanX = 5; hut.spanZ = 4;
  ZoneEnvTag room = zoneEnvTag(hut, ENV_OPEN, 0.035f);
  if (!room.indoor || room.preset != ENV_ROOM) fail("a shut hut was not a room");
  if (room.room < 0.10f || room.room > 0.16f || room.cave > 0.001f) fail("small room send");

  ZoneEnvGeom door;
  door.ceil = 2; door.stacked = 1; door.sky = 2; door.walls = 3; door.spanX = 5; door.openReach = 4;
  ZoneEnvTag edged = zoneEnvTag(door, ENV_OPEN, 0.035f);
  if (!edged.indoor || edged.preset != ENV_ROOM) fail("a room with the door open was a hall");

  ZoneEnvGeom corridor;
  corridor.ceil = 3; corridor.stacked = 1; corridor.sky = 0; corridor.walls = 2; corridor.spanX = 3; corridor.openReach = 0;
  ZoneEnvTag hall = zoneEnvTag(corridor, ENV_STREET, 0.12f);
  if (!hall.indoor || hall.preset != ENV_HALL) fail("a corridor was not a hall");

  ZoneEnvGeom plant;
  plant.ceil = 7; plant.stacked = 1; plant.sky = 0; plant.walls = 4; plant.spanX = 16; plant.spanZ = 14;
  ZoneEnvTag big = zoneEnvTag(plant, ENV_STREET, 0.12f);
  if (!big.indoor || big.preset != ENV_HALL || std::fabs(big.room - 0.20f) > 0.001f || std::fabs(big.cave - 0.24f) > 0.001f)
    fail("a plant floor was not a hall");

  ZoneEnvGeom cellar;
  cellar.ceil = 3; cellar.stacked = 5; cellar.sky = 0; cellar.walls = 4; cellar.spanX = 5; cellar.spanZ = 5;
  ZoneEnvTag bunk = zoneEnvTag(cellar, ENV_OPEN, 0.035f);
  if (!bunk.indoor || bunk.preset != ENV_BUNKER || std::fabs(bunk.cave - 0.50f) > 0.001f) fail("a buried room was not a bunker");

  ZoneEnvGeom pocket;
  pocket.pocket = true; pocket.sky = 8;
  ZoneEnvTag pk = zoneEnvTag(pocket, ENV_OPEN, 0.035f);
  if (!pk.indoor || pk.preset != ENV_BUNKER || pk.enclose < 0.99f) fail("the pocket bunker was not underground");

  ZoneEnvHold h;
  if (!zoneEnvFollow(h, room, ENV_OPEN, 0.035f) || !h.inside || h.preset != ENV_ROOM) fail("stepping inside did not take the room");
  if (zoneEnvFollow(h, p, ENV_STREET, 0.12f) || !h.inside || h.preset != ENV_ROOM) fail("the doorway dropped the room");
  if (!zoneEnvFollow(h, o, ENV_FOREST, 0.08f) || h.inside || h.preset != ENV_FOREST) fail("walking back outside did not crossfade target");

  h = ZoneEnvHold();
  h.preset = ENV_STREET; h.room = 0.12f;
  if (zoneEnvFollow(h, p, ENV_STREET, 0.12f) || h.inside || h.preset != ENV_STREET) fail("standing under a porch entered the building");

  h = ZoneEnvHold();
  zoneEnvFollow(h, room, ENV_OPEN, 0.035f);
  if (zoneEnvFollow(h, hall, ENV_OPEN, 0.035f) || h.preset != ENV_ROOM) fail("one grazing hall ray retuned the room");
  if (!zoneEnvFollow(h, hall, ENV_OPEN, 0.035f) || h.preset != ENV_HALL) fail("a real hall did not take over");

  float t60 = 0.45f;
  const float frame = 1.f / 60.f;
  const float snapped = zoneEnvApproach(t60, 0.85f, frame);
  if (!(snapped > 0.45f && snapped < 0.50f)) fail("the door crossfade snapped on the first frame");
  for (int i = 0; i < 60; i++) t60 = zoneEnvApproach(t60, 0.85f, frame);
  const float expect = 0.45f + (0.85f - 0.45f) * (1.f - std::exp(-1.f));
  if (std::fabs(t60 - expect) > 0.01f || t60 > 0.78f) fail("one second through the door was not a partial crossfade");
  for (int i = 0; i < 120; i++) t60 = zoneEnvApproach(t60, 0.85f, frame);
  if (std::fabs(t60 - 0.85f) > 0.02f) fail("the crossfade did not arrive");
  return why.empty();
}

} // namespace snd
} // namespace dq
