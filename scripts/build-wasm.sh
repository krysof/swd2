#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-$root/build-wasm}"

if ! command -v emcmake >/dev/null 2>&1; then
  echo "error: emcmake was not found; install and activate Emscripten first" >&2
  exit 1
fi

# Homebrew's Emscripten requires its own modern Python. Some macOS command
# runners put Xcode's older python3 before Homebrew, so keep the tool directory
# first for the entire configure/build.
tool_dir="$(dirname "$(command -v emcmake)")"
export PATH="$tool_dir:$PATH"

emcmake cmake -S "$root" -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DSWD2_WEB_PRELOAD_GAME_DATA=ON \
  -DSWD2_GAME_DIR="$root/game"
cmake --build "$build_dir" --parallel
touch "$build_dir/site/.nojekyll"
"$root/scripts/verify-wasm.sh" "$build_dir/site"

printf 'Web build: %s\n' "$build_dir/site/index.html"
du -sh "$build_dir/site"
