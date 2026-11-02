#!/usr/bin/env bash
# tools/sync_engine.sh — bring the DokuQuest engine's newest changes into Exclusion.
# Exclusion is built on FartGarfunkel/DokuQuest-Rewrite; ENGINE_BASE holds the engine commit we're in step with.
# This fetches the engine, takes everything it changed since ENGINE_BASE in the engine's own code (src/, shaders,
# third_party, build files, tools) and applies it here as a 3-way merge, so our Zone changes to shared files are kept.
# DokuQuest's game content (data/, news, docs, README, CLAUDE.md) is left out: Exclusion has its own.
# On success ENGINE_BASE moves on; files that clash are left with conflict markers to resolve (the script says which).
#   bash tools/sync_engine.sh [engine-branch]      (default main)
set -euo pipefail
cd "$(dirname "$0")/.."
branch="${1:-main}"
base="$(cat ENGINE_BASE)"
if ! git diff --quiet HEAD --; then echo "engine: commit or stash your changes first (the sync merges into a clean checkout)"; exit 3; fi
git fetch --quiet https://github.com/FartGarfunkel/DokuQuest-Rewrite "$branch:refs/remotes/engine/$branch"
git cat-file -e "$base^{commit}" 2>/dev/null || git fetch --quiet --depth=2000 https://github.com/FartGarfunkel/DokuQuest-Rewrite "$branch:refs/remotes/engine/$branch"
head="$(git rev-parse "refs/remotes/engine/$branch")"
if [ "$head" = "$base" ]; then echo "engine: already up to date ($base)"; exit 0; fi
echo "engine: $base..$head"
git log --oneline "$base..$head" | head -40
# (the top CMakeLists.txt is left out: it carries DokuQuest's name and version; merge build changes there by hand)
paths=(src assets/shaders third_party/CMakeLists.txt third_party/glad third_party/VERSIONS.md tools .github/workflows/windows.yml)
excl=(':!src/**/zone_*' ':!tools/zone' ':!tools/port' ':!tools/sync_engine.sh')
git diff --binary "$base" "$head" -- "${paths[@]}" "${excl[@]}" > /tmp/engine.patch
if [ ! -s /tmp/engine.patch ]; then echo "engine: nothing in the shared code changed"; echo "$head" > ENGINE_BASE; exit 0; fi
if git apply -3 --whitespace=nowarn /tmp/engine.patch; then
  echo "$head" > ENGINE_BASE
  echo "engine: applied cleanly"
elif [ -n "$(git diff --name-only --diff-filter=U)" ]; then
  echo "$head" > ENGINE_BASE
  echo "engine: applied with conflicts to resolve in:"; git diff --name-only --diff-filter=U
  exit 2
else
  echo "engine: the patch did not apply (nothing changed; ENGINE_BASE stays at $base)"
  exit 1
fi
