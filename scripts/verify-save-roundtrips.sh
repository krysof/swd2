#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${1:-$root/build}"

if [[ ! -f "$build/CMakeCache.txt" ]]; then
  cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release
fi
cmake --build "$build" --target swd2_tests -j4
ctest --test-dir "$build" --output-on-failure -R '^swd2_tests$'

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

printf '%s\n' \
  'SAVE/MAPZ verification: native five-slot round trips passed; IDBFS restore/flush wiring is present.' \
  'Note: automated real-browser restart persistence remains an unfinished completion gate.'
