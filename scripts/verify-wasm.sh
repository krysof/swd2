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
