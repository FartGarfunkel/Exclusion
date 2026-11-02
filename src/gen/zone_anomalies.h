// gen/zone_anomalies.h — the Zone's anomaly roster: what each anomaly of a field (gen/zone_plan.h fieldAnomalies) is,
// where exactly it sits (its ground, its pool), and the ground dressing round it (scorch, acid stains, glass shards,
// poppies, a dead tree hung with ash moss, the pits of mires and sparkpools). Pure and thread-safe: the generator,
// gameplay (sim/zone_anomaly.cpp) and the client's tells (render/zone_anomaly_fx.cpp) all ask it.
#pragma once
#include "gen/zone_plan.h"

namespace dq::zone {

enum AnoKind : u8 {
  AK_UNDERTOW, AK_KICKBACK, AK_SPINNER, AK_SHARDS, AK_SKIP,     // gravity
  AK_FLARE, AK_WANDERFIRE, AK_STEAM, AK_ASHMOSS,                 // thermal
  AK_ARCNEST, AK_BALL, AK_FLASHPOP, AK_SPARKPOOL,                // electric
  AK_ACID, AK_GAS, AK_BLISTER, AK_MIRE,                          // chemical
  AK_PSYFIELD, AK_LULL, AK_WISPS,                                // psy
  AK_COUNT
};
struct AnoDef { const char* key; const char* name; u8 family; const char* about; };
const AnoDef& anoDef(int kind);

struct ZAnomaly {
  int x, y, z;          // centre column; y = band-local first air above the ground, or above a pool's surface
  float r;              // reach (blocks); the loop / path radius of a wandering one
  u8 family, tier, kind;
  bool pool;            // stands in water (a pool, a pit or the river)
  u32 id;               // stable (seed, place, kind)
  int field;            // index into zonePlan().fields
  int tx, ty, tz;       // a skip: where its partner is (band-local ty, like y)
};
// every anomaly of field fi (cached)
const std::vector<ZAnomaly>& zoneFieldAnomalies(u32 seed, int fi);
// every anomaly of the fields within about r of (x, z)
void zoneAnomaliesNear(u32 seed, int x, int z, int r, std::vector<const ZAnomaly*>& out);
// gen: the dressing of every anomaly reaching into the chunk at (ox, oz); G = the chunk's 256 grounds, occupied as
// gen/zone_gen.cpp keeps it (1 road/fence: left alone; set to 2 here: no plants)
void dressAnomalies(Chunk& ch, int ox, int oz, const Pal& B, u32 seed, const Ground* G, u8* occupied);

} // namespace dq::zone
