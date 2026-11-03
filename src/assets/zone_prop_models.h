// assets/zone_prop_models.h — the Zone's vehicles and big props as voxel models at 8 voxels a metre (STALCRAFT-blocky, real
// size): Soviet cars, the UAZs, lorries (ZIL, Ural, KrAZ, GAZ), buses, armour (BTR, BRDM, BMP, T-72), the Mi-8, tractors, a
// dozer, an excavator, a truck crane, the shunting locomotives and goods wagons. Built from the kit's frame in
// gen/zone_details.h (x = length, front at +x; y up; z across), weathered by look (rusted / burnt / stripped). CPU only.
#pragma once
#include "assets/voxel.h"

namespace dq {

constexpr float PROP_VOX = 8.f;   // voxels per metre
// the mesh of kit `kit` (gen/zone_details.h propKits()) in variant `var`, look (PL_*), paint `tint`; `origin` = where the
// grid's corner sits in the kit's frame (metres). Thread-safe
bool buildPropModel(int kit, int var, int look, int tint, VoxelMeshData& out, vec3& origin, ivec3& size);

} // namespace dq
