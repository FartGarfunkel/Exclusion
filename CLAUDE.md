# Exclusion — map for AI sessions

Exclusion is an original survival shooter set in a voxel Chernobyl Exclusion Zone (S.T.A.L.K.E.R. / Misery /
STALCRAFT-like), built on the user's DokuQuest C++ engine (seamless stacked voxel world, LOD, launcher, mods,
split screen + LAN + online co-op, full controller support). Every world is the "zone" world type.
Weapons and attachments come from the user's Battle of Spades (game/weapons.js numbers and rules).

The Zone (all new code is in `zone_*` files):
| File | What |
|---|---|
| `src/gen/zone_map.*` | regions (21, ~13 x 28 km, south fence z=+13800 to the plant z=-12500), rivers, roads, rail, radiation and psy maps |
| `src/gen/zone_plan.*` | terrain height (groundAt), the site plan per seed: buildings, smart terrains (A-Life), anomaly fields, trader + hatch spots |
| `src/gen/zone_build.*` | the building builders (panel blocks, factories, cooling towers, the reactor, the Duga, wrecks...) and loot tables |
| `src/gen/zone_gen.cpp` | the "zone" DimGenerator (other bands void); `zone_bunker_gen.cpp` the players' bunkers (pocket realm, "bunker" type) |
| `src/sim/zone.h` | the Zone's gameplay API |
| `src/sim/zone_core.cpp` | catalogue (data/zone_items.json), body (rad, bleed, hunger, thirst, sleep, stamina, psy), weight, outfits, consumables, anomalies, artifacts + detectors, emissions (from the world clock), bunker travel, traders' stock |
| `src/sim/zone_guns.cpp` | guns: hit-scan, ADS zoom, recoil, falloff, attachments (stack tag.att), rounds (tag.am/at), jams by condition, underbarrel GL |
| `src/sim/zone_alife.cpp` | A-Life: squads/packs simulated map-wide offline, online near players (ONLINE_R 150), fights, respawns, emissions, PDA news |
| `src/sim/zone_npc.cpp` | online NPC brains (factions, grudges, firing), traders |
| `src/sim/zone_story.cpp` | the story steps (FIND THE FERRYMAN), starting kit |
| `src/ui/zone_screens.cpp` | HUD (bars, status marks, compass, gun, detector, emission, scope), PDA, weapon bench, trader, ending |
| `src/ui/zone_inventory.cpp` | the Zone's inventory (STALKER / Misery style: gear + quick + belt slots, packed w x h backpack grid, item card, weight bar; opened by `openInventory` in Zone worlds) + the loot screen (`ZoneLoot`: backpack / card / container grid, Take / Take all; every block container in a Zone world opens it, see `zoneLootFits`; writes the block entity through `World::blockEntityChanged`; `--screen zoneloot`) |
| `src/render/zone_gun_models.*` | guns + attachments as real 3D voxel models (port of Battle of Spades render/guns.js + gunGrid): `zoneGunModel(stack)` / `zoneGunInfo` (sight line, grip, fore hand), cached per id + `tag.att`; `zoneGunItemKey` for third person; drawn by `zone_fpgun.cpp` |
| `src/assets/zone_item_models.*`, `zone_model_kit.h` | every other Zone item (ammo, grenades, meds, food, detectors, tools, parts, trophies, artifacts, suits, helmets) as 3D models built from boxes / cylinders / ellipsoids; hooked at the top of `buildItemModel`, so icons, drops and held items use them (bump `VERSION` in assets/icons.cpp when changing a model) |
| `src/render/zone_fpgun.cpp` | first person (blocky, STALCRAFT-like: the player's own skin arms, `localPlayerArm` / `localPlayerArmLeft`; the body's arms hide in Zone first person) holding guns (grip / handguard), the knife, a detector up in the left hand with a bolt in the right, the PDA in both hands; exports the PDA's and detector's on-screen rects (`zonePdaRect`, `zoneDetRect`) that ui/zone_screens.cpp draws their displays into (`zoneDeviceScreen` in assets/zone_item_models.cpp says where each device's screen is) |
| `src/sim/zone_mutants.cpp`, `src/render/zone_creature_draw.*`, `tools/zone/mutant_models.py` | mutant behaviours (charge, kick, pack hunting, cloaking grab, leap, psy master, stomp, telekinesis), their rigs / models, NPCs holding real 3D guns; vanilla creatures are refused in Zone worlds by `Level::spawn` |
| `src/audio/zone_sfx.cpp`, `audio/ambience.cpp` (`Ambience::zone`) | the Zone's soundscape (no music outdoors; music.cpp only plays radios / guitars / the menu): wind + drone bed, crows, crickets, far gunfire, howls, roars, creaks, thunder, psy whispers; emission sirens, rumble, wave, cracks, heartbeat |
| `tools/zone/` | catalog.py (items: run after editing), paint_items.py / paint_skins.py (sprites, NPC skins, the player's looks z_player / z_suit_*), paint_looks.py (every look with each helmet: z_look_*), mobs.py, make_icon.py |
| `tools/sync_engine.sh`, `ENGINE_BASE`, `.github/workflows/engine-sync.yml` | keeping the engine in step with DokuQuest-Rewrite: 3-way applies the engine's changes since ENGINE_BASE (not its game content); the workflow runs daily and opens a pull request |
Story beats, characters and places are original (no Strelok, Wish Granter, ...). Factions use the standard STALKER names
(the user's choice): Loners, Bandits, Military, Duty, Freedom, Mercenaries, Monolith, Ecologists (code ids stay drifter,
bandit, army, bastion, opensky, merc, choir, scientist/institute). Mutants keep their own names.
User's rules carried over: full controller support in every menu; patch notes never mention Herobrine and always end
with "Removed Herobrine."; no phantoms, ever. Hunger/thirst/sleep ARE in Exclusion (Misery), unlike DokuQuest.

## Where the original lives (in this session)
- JS source: `/home/claude/src_in/DokuQuest-0.9.0.90/src/js/**` (+ `src/styles.css`, `src/index.html`,
  `launcher/desktop/launcher.html`, `launcher/news.html`, `notes/roadmap.md`)
- Resource pack: `/home/claude/pack_in/assets/minecraft/**` (GeruDoku 1.19.3, 32px). NEVER commit pack files
  (third-party art) — the game loads packs from `userDir()/resourcepacks/` at run time.
- Extract any JS data: `python3 tools/port/jsdump.py /home/claude/src_in/DokuQuest-0.9.0.90 out.json "<JS expr>"`
  (runs the whole original game headless; the expression can use any top-level const/function).

## Build & test
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build -j2
# headless run (Xvfb + Mesa): SDL here only has EGL, so force it
Xvfb :99 -screen 0 1280x720x24 & export DISPLAY=:99 SDL_VIDEO_X11_FORCE_EGL=1
./build/exclusion --userdir /tmp/ud --create Test --seed hello --smoke-frames 300 --smoke-shot /tmp/shot.png
./build/exclusion --renderer legacy ...   # the GL 3.3 path
```
Only 2 CPU cores: compile single files while iterating (`ninja -C build CMakeFiles/dqcore.dir/src/gen/worldgen.cpp.o`).
Third-party code is in `third_party/` (SDL2, imgui, glad, miniaudio, enet, quickjs-ng, stb, glm, miniz,
nlohmann json) — `tools/fetch_deps.sh` fetches it on a clean checkout.

## Layout (one feature per file; keep files < ~1500 lines)
| Dir | What | Links |
|---|---|---|
| `src/common` | types, logging, fs/json/zlib, settings, thread pool, noise (exact util.js port) | — |
| `src/core` | registry (blocks/items/recipes/tags/mobs from `data/*.json`), ItemStack, `initCore()` | common |
| `src/world` | Chunk (16×256×16 band, 16 sections), World (streaming, blocks in global y, saving), light, regions | core |
| `src/gen` | world generators (DimGenerator per band), structures | world |
| `src/sim` | Level (realms, entities, time, systems), Entity, Player, physics, events, gameplay systems | world |
| `src/net` | ENet protocol, host/guest | sim |
| `src/mod` | mod loader, QuickJS DQ API, native plugin API | sim |
| `src/server` | DedicatedServer (used by launcher + `exclusion-server`) | sim, net |
| `src/render` | GL backend, mesher, world/LOD/entity/portal/sky/water/particle renderers | client |
| `src/assets` | tiles (+ resource packs), item voxel models, skin → voxel character, character creator data | client |
| `src/audio` | miniaudio output + procedural synth | client |
| `src/ui`, `src/input` | ImGui screens styled like styles.css, HUD, inventory; keyboard/mouse/controllers | client |
| `src/client` | App (window, loop, screens), GameSession (open world, local player, render layers) | client |
| `src/launcher` | launcher exe: news, play, profile/skin, options (renderer), mods, server, Mod Studio, updater | client |
`dqcore` = common/core/world/gen/sim/net/mod/server (no GL; the server links only this).
`dqclient` = render/audio/ui/input/assets/client. CMake globs `*.cpp` per directory — new files need no CMake edit.

## Key design facts
- Coordinates: x/z unbounded ints, y global 0..1023. `dimOfY(y)`, `dimBase(d)`, band-local `y & 255`.
  World::getBlock/setBlock take GLOBAL y. Chunk accessors and generators use BAND-LOCAL y.
- Chunk = one dimension band (16×256×16) like the JS chunk (`colIdx` = `(y<<8)|(z<<4)|x`), stored as 16
  sections (nullptr = all air). meta u8 per block (same meaning as the JS meta), light = sky<<4|block.
- Overworld sea level is band-local 64 (the original was 48 in a 128-tall world; generators scale the
  original terrain to the taller band). See gen/ for the scaling rule.
- Side realms (pocket dimension, his realm) are separate `World`s (`Level::world(realm)`).
- Block ids are runtime-only; saves store keys (palette per chunk), so adding blocks never breaks saves.
- Global registration: NEVER rely on static constructors in other .cpp files (static-lib linking drops
  them). Expose `registerXxx()` and call it from `initCore()` (core/init.cpp) or the client equivalent.
- Threads: generation and meshing run on `ThreadPool` workers. Workers never touch `World` maps; they
  get copies (MeshInput) or produce new Chunks that the main thread inserts.
- Render passes per frame (GameSession::render): sky → opaque sections → cutout → RenderLayers opaque →
  translucent sections → RenderLayers translucent → overlay (hand). Add features as `RenderLayer`s
  (`registerRenderLayer`) instead of editing session.cpp.
- Shaders: `assets/shaders/*.glsl`, one file holds both stages (`#ifdef VERTEX` / `#ifdef FRAGMENT`),
  GLSL 330 subset, `#include "common.glsl"`. Version line + `DQ_MODERN` are injected. Shader packs override files.
- Gameplay events go through `EventBus` (sim/events.h) with the original DQ mod API names.
- Settings: `common/settings.h` (original option names + engine additions), saved in userDir()/settings.json.
- Smoke test before every push: create a world, run frames, no crash, screenshot looks right.

## Rules for AI sessions (token budget — the user pays per use)
- Read only what your task needs, but DO read the original JS for the system you port.
- Targeted edits; never reprint whole files you didn't write.
- If a build error survives 2 fix attempts, stop and report it.
- Don't take screenshots for the user; use them only to check your own work.
- Update `docs/PORT_STATUS.md` when you port a JS module.
