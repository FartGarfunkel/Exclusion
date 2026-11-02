// render/zone_creature_draw.h — how the Zone's people and mutants look beyond their rigs (used by render/creature_layer.cpp):
// stalkers hold their real 3D guns (render/zone_gun_models) two-handed — shouldered and aimed in a fight, at the low ready
// otherwise — and wear one of their faction's several suits; armed husks point theirs; the shroud fades to a shimmer.
#pragma once
#include "render/char_anim.h"
#include "assets/char_model.h"
#include "assets/voxel.h"

namespace dq {

class Creature;
struct RigModel;

// the skin a Zone stalker wears: its type's skin, or one of the faction's other suits (z_npc_<f>_<n>.png) picked per stalker
std::string zoneNpcSkin(const Creature& m);
// a Zone stalker holding a gun: the arms posed on it (R's arm parts rebuilt) and the gun's frame (blocks, grip at the origin,
// barrel along +X) in render space. false = not a gun (draw as before).
bool zoneNpcGunPose(const Creature& m, const CharModel& M, CharRig& R, float headPitch, mat4& gunFrame, float& flash);
// draw that gun (and its muzzle flash) with the voxel renderer already begun
void drawZoneNpcGun(const Creature& m, const mat4& gunFrame, float flash, VoxelLight light, vec4 tint);
// a voxel-rigged creature holding a Zone gun (an armed husk): the gun's frame from its posed body. false = not a gun.
bool zoneRigGunFrame(const Creature& m, const RigModel& RM, const mat4& T0, mat4& gunFrame);
// how much of the shroud shows (1 = all of it, ~0.1 = a shimmer in the air); 1 for everything else
float zoneCloakAlpha(const Creature& m);

} // namespace dq
