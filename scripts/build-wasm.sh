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

web_version="${SWD2_WEB_VERSION:-$(TZ=Asia/Tokyo date +%Y.%m.%d).dev}"
cp "$root/src/web/service-worker.js" "$build_dir/site/service-worker.js"
python3 - "$build_dir/site/index.html" "$build_dir/site/service-worker.js" \
    "$web_version" <<'PY'
import pathlib
import re
import sys

path = pathlib.Path(sys.argv[1])
worker_path = pathlib.Path(sys.argv[2])
version = sys.argv[3]
if not re.fullmatch(r"\d{4}\.\d{2}\.\d{2}\.(?:\d+|dev)", version):
    raise SystemExit(f"error: invalid Web release version: {version}")
html = path.read_text(encoding="utf-8")
pattern = re.compile(
    r'(<p\b[^>]*\bid=(?:"build-version"|\'build-version\'|build-version)'
    r'[^>]*>\s*版本\s*)[^<]*(</p>)')
html, count = pattern.subn(lambda match: match.group(1) + version + match.group(2), html)
if count != 1:
    raise SystemExit(
        f"error: expected one visible Web version element, replaced {count}")
asset_version = re.compile(
    r'index\.js(?:\?v=\d{4}\.\d{2}\.\d{2}\.(?:\d+|dev))?')
html, script_count = asset_version.subn(f"index.js?v={version}", html)
if script_count == 0:
    raise SystemExit("error: index.html does not reference the Web loader")
path.write_text(html, encoding="utf-8")

javascript_path = path.with_name("index.js")
javascript = javascript_path.read_text(encoding="utf-8")
for asset in ("index.wasm", "index.data"):
    asset_pattern = re.compile(
        re.escape(asset) +
        r'(?:\?v=\d{4}\.\d{2}\.\d{2}\.(?:\d+|dev))?')
    javascript, asset_count = asset_pattern.subn(
        f"{asset}?v={version}", javascript)
    if asset_count == 0:
        raise SystemExit(f"error: index.js does not reference {asset}")
javascript_path.write_text(javascript, encoding="utf-8")

worker = worker_path.read_text(encoding="utf-8")
worker, worker_count = re.subn(
    r"__SWD2_RELEASE_VERSION__", version, worker)
if worker_count != 1:
    raise SystemExit(
        f"error: expected one service-worker version token, replaced {worker_count}")
worker_path.write_text(worker, encoding="utf-8")
PY
touch "$build_dir/site/.nojekyll"
"$root/scripts/verify-wasm.sh" "$build_dir/site"

printf 'Web version: %s\n' "$web_version"
printf 'Web build: %s\n' "$build_dir/site/index.html"
du -sh "$build_dir/site"
