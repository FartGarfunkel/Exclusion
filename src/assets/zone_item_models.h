// assets/zone_item_models.h — every Zone item (data/zone_items.json) as a real 3D voxel model built from boxes, cylinders
// and ellipsoids (assets/zone_model_kit.h): ammo boxes and shells, grenades and rounds, medkits, food, detectors, tools,
// parts, mutant trophies, artifacts, suits and helmets; guns and attachments come from render/zone_gun_models.cpp.
// Hooked into buildItemModel (assets/item_models.cpp), so icons, dropped items and held items all use them.
#pragma once
#include "assets/item_models.h"

namespace dq {
// CPU only, any thread. false = not a Zone item (or one we have no model for): the normal sprite pipeline takes over.
bool buildZoneItemModel(const std::string& id, VoxelModel& out);
// a hand-held device's screen in its model's units (the item-model frame): x/y corners and the z of its face.
// The PDA and the three detectors (render/zone_fpgun.cpp draws them held, ui/zone_screens.cpp draws their displays).
bool zoneDeviceScreen(const std::string& id, vec2& lo, vec2& hi, float& z);
// Zone items lying in the world (item piles, shelves, dropped items) at real size, 1 block = 1 m: the item's real length
// (its longest side, metres; 0 = not a Zone item) from a table per item and per category
float zoneItemRealLength(const std::string& id);
// the transform (times m.matrix()) that lays a Zone item's model on y = 0 at its real size, centred on x/z = 0 and turned
// by `yaw`: lying flat on its side (picture face up), or standing on its foot (bottles). false: not a Zone item
bool zoneItemRest(const std::string& id, const VoxelModel& m, float yaw, mat4& out);
} // namespace dq
