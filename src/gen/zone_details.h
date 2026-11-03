// gen/zone_details.h — the Zone's set pieces (docs/zone_details.md): vehicles and big props (cars, trucks, buses, armour,
// helicopters, locomotives and wagons, tractors, cranes) as real-size models drawn by render/zone_prop_layer.cpp over
// invisible collision cells (zone_prop_solid / zone_prop_half) that this file lays chunk by chunk; the railway lines; and
// hand-placed block work (catwalks, scaffolds, lights) on the user's buildings. All of it from data/zone_details.json,
// plus the planner's random wrecks (zone_plan S_WRECK) turned into props. Pure and thread-safe; the same in every chunk.
#pragma once
#include "gen/zone_plan.h"

namespace dq::zone {

// one kind of vehicle / prop. Local frame in metres: x along the length (+x = its front), y up from its wheels / base,
// z across (+z = its right side), centred on x and z
struct PropBox { float x0, y0, z0, x1, y1, z1; };
struct PropKit {
  const char* id;
  float L, W, H;                 // its size (m)
  std::vector<PropBox> col;      // what you bump into (collision cells are laid where these are)
  bool rail = false;             // rides on rails (sits 0.3 m up, on the rail heads)
};
const std::vector<PropKit>& propKits();
int propKitIndex(const std::string& id);   // -1: unknown

enum PropLook : u8 { PL_OK, PL_BURNT, PL_STRIPPED };
struct Prop {
  u16 kit = 0;
  u8 look = PL_OK;               // paint state (PL_*)
  u8 var = 0;                    // the kit's variant (box van, cistern, no rotors...)
  u8 tint = 0;                   // paint colour (a seed: the model picks from the kit's palette)
  float x = 0, z = 0;            // world centre
  float yaw = 0, roll = 0, pitch = 0;   // degrees: yaw 0 = front to the east (+x), 90 north, 180 west, 270 south
  float sink = 0;                // metres pushed into the ground (negative: lifted, e.g. stacked on another wreck)
  float y = -1;                  // >= 0: the band-local y its base stands on (a floor, a deck); else the ground under it
  bool level = false;            // don't follow the ground's slope
};
const std::vector<Prop>& zoneProps(u32 seed);
// the props whose footprint may reach into the box (world x/z, inclusive)
void zonePropsIn(u32 seed, int x0, int z0, int x1, int z1, std::vector<int>& out);
// a prop's placement, local metres -> world (band-local y), resolved once (it reads the ground)
const mat4& zonePropMatrix(u32 seed, int i);
// is a prop's footprint within r blocks of (x, z)? (trees keep out of them)
bool zonePropNear(u32 seed, int x, int z, int r);

// lay this chunk's share of the set pieces: the props' collision cells, the rails (ballast, sleepers), the block work.
// Marks what it covers in `occupied` (no undergrowth there)
void zoneDetailsChunk(Ctx& c, const Ground* G, u8* occupied);

} // namespace dq::zone
