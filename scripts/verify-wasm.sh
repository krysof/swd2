#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
site="${1:-$root/build-wasm/site}"

for name in index.html index.js index.wasm index.data .nojekyll; do
  if [[ ! -f "$site/$name" ]]; then
    echo "error: missing WebAssembly site artifact: $site/$name" >&2
    exit 1
  fi
done

for name in index.html index.js index.wasm index.data; do
  if [[ ! -s "$site/$name" ]]; then
    echo "error: empty WebAssembly site artifact: $site/$name" >&2
    exit 1
  fi
done

magic="$(od -An -tx1 -N8 "$site/index.wasm" | tr -d '[:space:]')"
if [[ "$magic" != "0061736d01000000" ]]; then
  echo "error: index.wasm has an invalid magic/version header: $magic" >&2
  exit 1
fi

grep -Fq 'index.js' "$site/index.html" || {
  echo "error: index.html does not load index.js" >&2
  exit 1
}
grep -Fq 'index.wasm' "$site/index.js" || {
  echo "error: index.js does not reference index.wasm" >&2
  exit 1
}
grep -Fq 'index.data' "$site/index.js" || {
  echo "error: index.js does not reference the preloaded game data" >&2
  exit 1
}

grep -Fq '轩辕剑2' "$site/index.html" || {
  echo "error: index.html does not identify the game as 轩辕剑2" >&2
  exit 1
}
if grep -Eq '水浒|水滸' "$site/index.html"; then
  echo "error: index.html contains an incorrect game title" >&2
  exit 1
fi

button_count="$(grep -o '<button' "$site/index.html" | wc -l | tr -d '[:space:]')"
if [[ "$button_count" != "6" ]]; then
  echo "error: index.html must contain four directions, ESC and Enter (found $button_count)" >&2
  exit 1
fi
for key in ArrowUp ArrowDown ArrowLeft ArrowRight Escape Enter; do
  grep -Eq "data-key=(\"$key\"|'$key'|$key)([[:space:]>])" "$site/index.html" || {
    echo "error: index.html is missing the $key control" >&2
    exit 1
  }
done
for label in ▲ ▼ ◀ ▶ ESC 回车; do
  grep -Fq ">$label</button>" "$site/index.html" || {
    echo "error: index.html is missing the $label button label" >&2
    exit 1
  }
done
if grep -Eq 'id=("fullscreen"|fullscreen)([[:space:]>])' "$site/index.html"; then
  echo "error: index.html unexpectedly exposes a fullscreen control" >&2
  exit 1
fi

for pattern in \
  "idbfs-self-test" \
  "FS.writeFile" \
  "location.reload()" \
  "FS.readFile" \
  "dataset.idbfsSelfTest"; do
  grep -Fq "$pattern" "$site/index.html" || {
    echo "error: index.html is missing the IDBFS restart self-test: $pattern" >&2
    exit 1
  }
done

# Parsing and rewriting with Binaryen provides an additional structural check
# when the Emscripten installation exposes wasm-opt. Emscripten emits bulk
# memory and other standardized features, so enable its complete feature set
# instead of validating against the historical MVP-only default.
if command -v wasm-opt >/dev/null 2>&1 &&
   wasm-opt --help 2>&1 | grep -Fq -- '--all-features'; then
  temporary="$(mktemp "${TMPDIR:-/tmp}/swd2-wasm-verify.XXXXXX")"
  trap 'rm -f "$temporary"' EXIT
  wasm-opt "$site/index.wasm" --all-features --vacuum -o "$temporary"
fi

printf 'WASM verification: OK (%s)\n' "$site"
