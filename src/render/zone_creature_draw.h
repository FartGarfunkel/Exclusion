// render/zone_creature_draw.h — how the Zone's people and mutants look beyond their rigs (used by render/creature_layer.cpp,
// render/char_layer.cpp and render/zone_fpgun.cpp). Stalkers hold their real 3D guns two-handed at REAL scale (1 block = 1 m):
// at the low ready while they walk (bobbing with the stride, never swinging free), across the chest at a run, shouldered and
// aimed in a fight (blended in), kicked by each shot, a mag swapped from the belt when it runs dry; they take a knee in cover,
// flinch when hit and fall away from the shot (onto the back or the face), then lie there as a body by their pack (the
// zone_body_pack block, sim/zone_jobs.cpp dropBody); at a camp they sit on the ground round the fire warming their hands,
// eating, resting, one playing the guitar, and get up smoothly when alarmed. Co-op players in third person hold their real gun
// the same way. Armed husks point theirs; the shroud fades to a shimmer.
#pragma once
#include "render/char_anim.h"
#include "assets/char_model.h"
#include "assets/voxel.h"

namespace dq {

class Creature;
class Player;
struct RigModel;
struct CharWear;
struct CharDrawOpts;

// the skin a Zone stalker wears: its type's skin, or one of the faction's other suits (z_npc_<f>_<n>.png) picked per stalker
std::string zoneNpcSkin(const Creature& m);

// a Zone stalker's pose state for one frame (filled by zoneNpcCharPose, read back by zoneNpcHands / drawZoneNpcHands)
struct ZoneNpcPose {
  bool zone = false;                   // a Zone stalker (else everything else is unused)
  float sit = 0, kneel = 0, aim = 0, reload = -1, flinch = 0, hitDir = 1, time = 0, fallT = 0;
  int act = 0;                         // sitting: 0 rests, 1 warms his hands, 2 eats, 3 plays the guitar
  float actK = 0;                      // eating: the hand at the mouth (0..1)
  bool magInHand = false; mat4 magFrame{1}; int magKind = 0;
};
// before charRig: sitting on the ground / a knee / the fall from the hit (base is moved, ps gets the angle hook), the tint
// (the hurt flash only while it's fresh). Z must live until charRig is done. false = not a Zone stalker.
bool zoneNpcCharPose(const Creature& m, CharPose& ps, mat4& base, vec4& tint, ZoneNpcPose& Z);
// after charRig: the arms posed on the gun (gunFrame: gun blocks -> render, grip at the origin, barrel along +X) or on what
// he holds sitting (the guitar, his food). true = a gun to draw.
bool zoneNpcHands(const Creature& m, const CharModel& M, CharRig& R, ZoneNpcPose& Z, mat4& gunFrame, float& flash);
// draw that gun (and its muzzle flash, the mag in the left hand while reloading) / the guitar or food; voxel renderer begun
void drawZoneNpcHands(const Creature& m, const CharRig& R, const ZoneNpcPose& Z, bool gun, const mat4& gunFrame, float flash, VoxelLight light, vec4 tint);

// a co-op player in third person (render/char_layer.cpp drawPlayer): a Zone gun held for real (arms on it), any other Zone item
// in the fist at its real size. Takes wear.held over (clears it) and sets o.rigHook.
void zonePlayerHands(const Player& p, const CharPose& ps, CharWear& wear, CharDrawOpts& o);

// a voxel-rigged creature holding a Zone gun (an armed husk): the gun's frame from its posed body. false = not a gun.
bool zoneRigGunFrame(const Creature& m, const RigModel& RM, const mat4& T0, mat4& gunFrame);
// how much of the shroud shows (1 = all of it, ~0.1 = a shimmer in the air); 1 for everything else
float zoneCloakAlpha(const Creature& m);

// real-world size (metres = blocks, the item model's biggest x/y side) of a Zone item held in the hand
float zoneHeldSize(const std::string& id);
// small props: 0 a rifle mag, 1 a pistol mag, 2 a shotgun shell, 3 the guitar. local() maps the mesh to blocks: a mag / shell
// with the hand's hold (its top) at the origin, y up its length; the guitar with the sound hole at the origin, the neck
// along +x, its top facing +z.
const VoxelMeshGL& zonePropMesh(int kind);
mat4 zonePropLocal(int kind);

// the dead stalkers' bodies lying by their packs (a render layer); called from ui/zone_screens.cpp registration
void registerZoneBodyLayer();

} // namespace dq
