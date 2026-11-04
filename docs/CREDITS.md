# Credits

Third-party code and ideas used in the DokuQuest C++ rewrite, besides the libraries in `third_party/` (each with its own
licence file there).

## Horizon (MIT)

<https://github.com/horizon-rust/horizon>, Copyright (c) 2026 Ludovic Marie and the Horizon contributors, MIT License.

The dreamlike lighting (`assets/shaders/dreamlight.glsl`, `render/dreamlight_layer.cpp`, and the warm low-sun halo in
`assets/shaders/sky.glsl`) follows Horizon's Golden Hour shader pack (`crates/horizon-shader-api/packs/golden_hour`): the
golden light when the sun is low, the soft shoulder for the brightest colours, the saturation and vignette, the twelve-tap
halo round the brightest pixels, and the sky's warm halo and deeper blue overhead. Rewritten in GLSL for this engine; the
soft-focus glow and lifted shadows are our own additions.

MIT License text (as in Horizon's LICENSE): Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions: The above
copyright notice and this permission notice shall be included in all copies or substantial portions of the Software. THE
SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.

## OpenXRay (MIT) — adapted engine behaviour

<https://github.com/OpenXRay/xray-16>, Copyright © 2014-2025 OpenXRay and contributors, MIT License.
The original X-Ray engine is GSC Game World's. OpenXRay's own changes are MIT. No X-Ray source is vendored
here, and no GSC or Anomaly assets (recordings, textures, meshes, level binaries, dialogue) are included.

What was adapted, in this engine's own code:

- distance attenuation `min_distance / (0.75 * distance)`, culled under 0.01, from `CSoundRender_Emitter::update_culling`
  (`psSoundRolloff` 0.75, `psSoundCull` 0.01);
- occlusion as one ray whose first solid hit multiplies by 0.5 (`psSoundOcclusionScale`), and world ambience
  skipping that test (`SOUND_TYPE_WORLD_AMBIENT`);
- sound-environment presets lerped onto the listener reverb, in place of EAX volumes
  (`CSoundRender_Environment::lerp` from `CSoundRender_Core::update_listener`). The volume
  is tagged from the voxel shell (a shut room, a hall, a bunker) instead of a `.snd_env` brush;
- A-Life online and offline radii `switch_distance * (1 ± switch_factor)`, with Anomaly's published
  `switch_distance` 750 and `switch_factor` 0.1;
- detail slots of 2 m, seeded so neighbouring slots do not stripe (`DETAIL_SLOT_SIZE`, `cache_Decompress`).

MIT License text: Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction, including without
limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all copies or substantial portions of
the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

## Cubyz (GPL-3.0) — ideas only

<https://github.com/PixelGuys/Cubyz>. No code is taken from Cubyz (its licence is GPL). Ideas only:

- tools and armour made of several materials, each part of the crafting grid colouring its part of the item and adding to
  its stats (`core/tool_parts.*`, the mixed tools and armour);
- the distant terrain (`render/distant*.cpp`, `assets/shaders/distant.glsl`), after Cubyz's LOD chunks (`chunk.zig`
  `updateFromLowerResolution`, `renderer/mesh_storage.zig`): the far land is voxels at every distance, coarser further
  off; a coarse voxel is solid if any finer voxel under it is, and takes the block that shows the most (air round it
  beats buried); each level is made from the finer one; the voxels are drawn with the blocks' own textures, one per
  voxel face; a coarse piece gives way to the finer ones only once all of them are ready, and faces toward a coarser
  neighbour are drawn so no crack shows; and the far land is made by the world generator itself (the chunks nobody has
  been to are generated in the background for it) and kept on disk. Written for our engine (quadtree tiles over 256-high bands, voxels one block
  high so heights stay exact, runs of voxels per column instead of 32-cubed chunks).

## Better than Adventure! — ideas only

<https://www.betterthanadventure.net>. No code, art or music is taken from BTA. Its features (the Nether's five lands,
the blocks of the BTA list) are our own designs and pictures made after its changelogs.
