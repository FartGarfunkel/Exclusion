// audio/zone_snd_env.cpp — the voxel shell around the listener (see zone_snd_env.h).
#include "audio/zone_snd_env.h"
#include "world/world.h"
#include "world/dims.h"
#include "core/registry.h"
#include "gen/zone_map.h"

namespace dq::snd {

namespace {

// A block that closes a volume. Fences, bars, leaves and an open door do not:
// sound still has a way through, and the doorway is how you leave the volume.
bool covers(const Registry& R, BlockId id, u8 meta) {
  if (!id || !R.tSolid[id] || R.tLeaves[id]) return false;
  const std::string& key = R.block(id).key;
  if (key.find("leaves") != std::string::npos) return false;
  const u8 sh = R.tShape[id];
  if (sh == ShapeKind::Fence || sh == ShapeKind::Wall || sh == ShapeKind::Ladder || sh == ShapeKind::Carpet
      || sh == ShapeKind::Rod || sh == ShapeKind::Layer || sh == ShapeKind::Candle || sh == ShapeKind::Cake
      || sh == ShapeKind::Bed || sh == ShapeKind::Scaffold || sh == ShapeKind::Path)
    return false;
  if (sh == ShapeKind::Pane) {
    if (key.find("fence") != std::string::npos || key.find("chain") != std::string::npos || key.find("bars") != std::string::npos)
      return false;
    return key.find("window") != std::string::npos || key.find("glass") != std::string::npos;
  }
  if (sh == ShapeKind::Door || sh == ShapeKind::Trapdoor || sh == ShapeKind::Gate) return (meta & 4) == 0;
  return true;
}

} // namespace

ZoneEnvGeom zoneEnvMeasure(const World& w, vec3 head, int sky, int realm) {
  ZoneEnvGeom g;
  g.sky = sky;
  g.pocket = realm != REALM_MAIN;
  const int hx = (int)std::floor(head.x), hy = (int)std::floor(head.y), hz = (int)std::floor(head.z);
  const int local = hy - dimBase(dimOfY(hy));
  g.below = local < zone::BASE - 6;
  const Registry& R = reg();
  auto coverAt = [&](int x, int y, int z) {
    const BlockId id = w.getBlock(x, y, z);
    if (!id || !R.tSolid[id]) return false;
    const u8 sh = R.tShape[id];
    const u8 meta = (sh == ShapeKind::Door || sh == ShapeKind::Trapdoor || sh == ShapeKind::Gate) ? w.getMeta(x, y, z) : 0;
    return covers(R, id, meta);
  };
  for (int i = 1; i <= 14; i++) {
    if (!coverAt(hx, hy + i, hz)) continue;
    g.ceil = i;
    for (int k = 1; k <= 8; k++) {
      if (!coverAt(hx, hy + i + k, hz)) break;
      g.stacked++;
    }
    break;
  }
  auto ray = [&](int dx, int dz) {
    for (int i = 1; i <= 16; i++) if (coverAt(hx + dx * i, hy, hz + dz * i)) return i;
    return 0;
  };
  const int east = ray(1, 0), west = ray(-1, 0), south = ray(0, 1), north = ray(0, -1);
  g.walls = (east > 0) + (west > 0) + (south > 0) + (north > 0);
  if (east && west) g.spanX = east + west;
  if (north && south) g.spanZ = north + south;
  if (!(east && west)) g.openReach = std::max(g.openReach, std::max(east, west));
  if (!(north && south)) g.openReach = std::max(g.openReach, std::max(north, south));
  return g;
}

} // namespace dq::snd
