// render/zone_fp_hands.h — first-person hands for the Zone: a gloved fist (right, and its mirror for the left) and a
// sleeved forearm, real size (metres), coloured by the suit you wear (fingerless leather gloves for most, rubber for the
// hazmat suit, armoured for the exoskeleton). Built from code on first use (GL thread), cached per look.
//
// A fist's frame (origin = the middle of what it grips):  X = the way the fingers wrap round to (the front of a pistol
// grip), Y = along what it grips (up a pistol grip), Z = the back of the hand (right hand: to the right; the left hand's
// mirror model has its back toward -Z). The thumb and the fingertips end on the far side from the back of the hand.
#pragma once
#include "assets/item_models.h"
#include <string>

namespace dq {

struct ZoneHands {
  VoxelModel fist[2];      // 0 right, 1 left
  VoxelModel arm;          // the forearm: along +Y from the wrist (y 0) for ARM_LEN, centred on x/z
  vec3 wrist[2];           // where each fist's forearm starts, in the fist's frame (metres)
  bool ok = false;
};
constexpr float ARM_LEN = 0.62f;

// the hands for what this player wears (armor[1]'s id, "" for none)
const ZoneHands& zoneHands(const std::string& suitId);

// draw one hand: F = the fist's frame (columns X, Y, Z as above, may carry a uniform scale) in render space,
// elbow = where the forearm goes toward (render space); the sleeve keeps the fist's scale across and is `armScale` long
void drawZoneHand(const ZoneHands& H, int side, const mat4& F, vec3 elbow, VoxelLight L, float armScale = 1.f);

// a frame from two axes (made orthonormal: y kept, x fixed against it), an origin and a scale
mat4 zoneFrame(vec3 origin, vec3 x, vec3 y, float scale = 1.f);

} // namespace dq
