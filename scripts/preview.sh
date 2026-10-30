#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
CMAKE="${CMAKE_BIN:-$(command -v cmake || true)}"
NINJA="${NINJA_BIN:-$(command -v ninja || true)}"
[ -n "$CMAKE" ] && [ -n "$NINJA" ] || { echo 'Install CMake and Ninja, or set CMAKE_BIN and NINJA_BIN.' >&2; exit 1; }
"$CMAKE" -S tests/preview -B .state/preview -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA"
"$CMAKE" --build .state/preview -j 6
.state/preview/preview
printf 'Snapshots: %s/.state/{five-hour,weekly,rotated,estimated,full,empty,wait-data}.ppm\n' "$ROOT"
