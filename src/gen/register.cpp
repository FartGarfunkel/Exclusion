// gen/register.cpp — world types known to the game. The full port of the original world generator
// registers "default" here (see gen/worldgen.h); "debug" (simple noise) lives in generator.cpp.
#include "core/init.h"
#include "gen/generator.h"
#include "gen/dimgens.h"
#include "gen/structures_api.h"

namespace dq {
// (declared out here: MSVC gives a function declared inside a block the global namespace, so these never linked on Windows)
void registerSkyblockWorldGen();
void registerZoneWorldGen();
void registerBunkerWorldGen();
void registerWorldTypes() {
  registerStructures();        // every structure type (gen/structures_register.cpp), before any generator exists
  registerDefaultWorldGen();   // "default", "pocket", "realm" (gen/realms.cpp)
  registerSkyblockWorldGen();   // "skyblock" (gen/skyblock.cpp)
  registerZoneWorldGen();       // "zone" (gen/zone_gen.cpp): Exclusion's world
  registerBunkerWorldGen();     // "bunker" (gen/zone_bunker_gen.cpp): the players' bunkers
}
} // namespace dq
