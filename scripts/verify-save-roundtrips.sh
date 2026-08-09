#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${1:-$root/build}"

if [[ ! -f "$build/CMakeCache.txt" ]]; then
  cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release
fi
cmake --build "$build" --target swd2_tests swd2_rewrite -j4
ctest --test-dir "$build" --output-on-failure \
  -R '^(swd2_tests|new_game_exit_checkpoint_(replay|validation)|explicit_save_routing_(prepare|replay|validation)|no_save_mode_(prepare|replay|validation))$'

shell="$root/src/web/shell.html"
main="$root/src/main.cpp"
grep -Fq "FS.mount(IDBFS, {}, '/saves')" "$shell" || {
  echo 'error: browser shell does not mount /saves through IDBFS' >&2
  exit 1
}
grep -Fq 'FS.syncfs(true' "$shell" || {
  echo 'error: browser shell does not restore IDBFS before startup' >&2
  exit 1
}
grep -Fq 'FS.syncfs(false' "$main" || {
  echo 'error: WASM save path does not flush IDBFS after a write' >&2
  exit 1
}
grep -Fq "'--save-dir', '/saves'" "$shell" || {
  echo 'error: browser runtime is not pointed at the persistent save mount' >&2
  exit 1
}
for pattern in \
  "get('idbfs-self-test')" \
  "FS.writeFile(idbfsSelfTestPath" \
  "location.reload()" \
  "FS.readFile(idbfsSelfTestPath" \
  "dataset.idbfsSelfTest = passed ? 'pass' : 'fail'"; do
  grep -Fq "$pattern" "$shell" || {
    echo "error: browser IDBFS restart self-test is missing: $pattern" >&2
    exit 1
  }
done
if command -v node >/dev/null 2>&1; then
  node "$root/scripts/verify-web-shell.mjs" "$shell"
fi

printf '%s\n' \
  'SAVE/MAPZ verification: native five-slot, live new-game pair, explicit Record routing, --no-save isolation, and simulated IDBFS reload passed.' \
  'Run the WASM site with ?idbfs-self-test=TOKEN in a real browser and require data-idbfs-self-test="pass".'
