# Exclusion

An original survival shooter set in the Chernobyl Exclusion Zone, in the spirit of STALKER and the Misery
mod: a huge open Zone (about 13 by 28 km, from the south fence to the plant), A-Life squads that live their
own lives, guns with attachments you fit and strip, weight and condition, anomalies, artifacts and
emissions, a bunker you dig out and furnish, split-screen, LAN and online co-op, and full controller support.

Built on the DokuQuest engine; weapons and attachments from Battle of Spades.

## Play (Linux)
Download `Exclusion-<version>-x86_64.AppImage` from the Releases page, make it executable
(`chmod +x`, or Properties → Allow executing) and run it. It opens the launcher: news, Play, options
(renderer: Modern OpenGL 4.6 or Legacy OpenGL 3.3 for older PCs; graphics presets from potato to ultra),
the dedicated server and the Mod Studio.
Your worlds and settings live in `~/.local/share/Exclusion/`; resource packs go in its `resourcepacks/` folder.

## Controls
See `docs/EXCLUSION.md` for the full table. Fire: left click / RT. Aim: right click / LT. Reload: R / X.
PDA: Tab / Back. Weapon bench (attachments): U. Head torch: L / d-pad up. Quick heal: H / d-pad down.

## Build
```
tools/fetch_deps.sh                 # third-party sources (SDL2, Dear ImGui, miniaudio, ENet, QuickJS-ng, ...)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build                      # exclusion, exclusion-launcher, exclusion-server
```
Packages: `tools/package.sh` makes `Exclusion-<version>-x86_64.AppImage`, `-linux-x64.tar.gz` and `-windows-x64.zip`.
See `CLAUDE.md` for the code map and `docs/` for the notes per area.

## Credits
Built on the DokuQuest engine; weapons and attachments from Battle of Spades.
