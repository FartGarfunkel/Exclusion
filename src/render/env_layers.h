// render/env_layers.h — the world's surroundings as render layers: distant terrain, clouds, weather
// (rain, snow, lightning, sand, ash), ambient effects (torch flames, smoke, drips, leaves, fireflies,
// wind) and the particle engine. registerEnvironmentLayers() is called once from App::run (client start).
#pragma once

namespace dq {
void registerEnvironmentLayers();
void registerDynLights();
void dynLightExtra(double x, double y, double z, float level);   // (alpha.9) a point light for this frame from another layer (flickering lamps)   // dyn_light.cpp: carried torches, glowing drops, lit TNT, fireballs light the world (called by registerEnvironmentLayers)
} // namespace dq
