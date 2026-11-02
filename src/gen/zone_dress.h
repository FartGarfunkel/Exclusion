// gen/zone_dress.h — the Zone's living (and dead) dressing, drawn chunk by chunk by gen/zone_gen.cpp: trees with slim
// trunks and open crowns, fallen trunks, and along the roads the power lines and the towns' street lamps.
#pragma once
#include "gen/zone_plan.h"

namespace dq::zone {

enum TreeKind : u8 { T_PINE, T_BIRCH, T_DEAD, T_RED_PINE, T_BURNT, T_SPRUCE };
// one tree standing on (tx, ty, tz) (ty = first air above the ground), drawn where it reaches into the chunk at (ox, oz)
void zoneTree(Chunk& ch, int ox, int oz, const Pal& B, u32 seed, int tx, int ty, int tz, TreeKind kind);
// a fallen trunk along x (alongZ: z) from (x, y, z), n blocks, the part inside the chunk
void zoneFallenTrunk(Chunk& ch, int ox, int oz, const Pal& B, int x, int y, int z, int n, bool alongZ);
// power lines (poles, crossarms, sagging wires) and street lamps along the roads: the parts inside the chunk
void zoneRoadside(Chunk& ch, int ox, int oz, const Pal& B, u32 seed, const Plan& plan);

} // namespace dq::zone
