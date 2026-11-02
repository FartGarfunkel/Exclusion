// render/zone_gun_models.h — the Zone's guns as real 3D voxel models, ported from Battle of Spades' render/guns.js
// (procedural boxes in "fine" voxels of 19 mm; the attachments from the same game's gunGrid: optics, muzzle devices,
// grips / lasers / lights / bipods, the underslung launcher, extended mags and drums).
//
// Model frame (the one every GunModelInfo number is in): units are blocks (1 = one metre); the barrel runs along +X
// (muzzle at +X), +Y is up, +Z is to the gun's right; the origin is the right hand's grip (trigger point). A model is
// cached per gun id + fitted attachments (ItemStack::tag["att"]), so fitting or stripping one rebuilds it.
#pragma once
#include "assets/item_models.h"
#include "core/itemstack.h"

namespace dq {

struct GunModelInfo {
  float sightY = 0.15f;     // height of the sight line above the origin (aim: this line goes through the eye)
  float sightX = -0.05f;    // x of the rearmost sight (rear iron sight / the optic's eyepiece)
  float length = 0.8f;      // butt to muzzle
  float back = -0.3f, front = 0.5f;   // x of the butt end and of the muzzle end
  vec3 grip{-0.057f, -0.095f, 0};     // where the right hand holds it (the pistol grip)
  vec3 fore{0.2f, -0.06f, 0};         // where the left hand goes (under the handguard, the front grip or the launcher)
  bool pistol = false;      // one-handed: the left hand cups the grip instead
  bool optic = false;       // an optic (red dot, holo, scope) is fitted: sightX is its eyepiece
  bool scope = false;       // a magnifying optic is fitted (pso1, acog, scope8)
};

// GL thread. nullptr until built (a miss builds it right here, a few ms; unknown guns stay nullptr: draw the item model).
const VoxelModel* zoneGunModel(const ItemStack& gun);
const GunModelInfo* zoneGunInfo(const ItemStack& gun);

// CPU only (any thread): the gun in its model frame with these attachments (mesh in out.data, nothing uploaded).
bool buildZoneGunModel(const ItemStack& gun, VoxelModel& out, GunModelInfo& info);

// CPU only (any thread): the gun / attachment as an item model (icons, dropped, third person): biggest of x/y = 1, centred.
// A gun is drawn bare (with its default optic); a "zg:..." key (zoneGunItemKey) with its attachments. false = not ours.
bool buildZoneGunItem(const std::string& id, VoxelModel& out);

// An item-model key for a gun WITH attachments ("zg:<id>|optic|muzzle|under|mag"), so third-person bodies hold the gun as it is
// fitted (CharWear::heldModel); "" for a bare gun / not a gun (use the plain item id).
std::string zoneGunItemKey(const ItemStack& gun);

} // namespace dq
