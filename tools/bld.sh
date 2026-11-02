#!/usr/bin/env bash
# tools/bld.sh — build with a lock, so sessions sharing this checkout never run two ninjas on build/ at once
# (they'd compile the same files side by side). Usage: tools/bld.sh [ninja targets / args]   (default: exclusion)
cd "$(dirname "$0")/.."
exec flock build/.ninja.lock ninja -C build -j2 "${@:-exclusion}"
