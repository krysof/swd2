#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repository="${1:-krysof/swd2}"
site="$root/build-wasm/site"
deploy="$root/build/pages-deploy"

"$root/scripts/build-wasm.sh"

if ! gh repo view "$repository" >/dev/null 2>&1; then
  gh repo create "$repository" --public \
    --description "SWD2 portable rewrite — WebAssembly build" \
    --disable-issues --disable-wiki
fi

rm -rf "$deploy"
mkdir -p "$deploy"
cp -R "$site"/. "$deploy"/
git -C "$deploy" init -b gh-pages
git -C "$deploy" config user.name "${GIT_AUTHOR_NAME:-krysof}"
git -C "$deploy" config user.email "${GIT_AUTHOR_EMAIL:-krysof@users.noreply.github.com}"
git -C "$deploy" add --all
git -C "$deploy" commit -m "Deploy SWD2 WebAssembly"
git -C "$deploy" remote add origin "https://github.com/$repository.git"
git -C "$deploy" push --force origin HEAD:gh-pages

# Create Pages on first deployment; switch an existing Pages project to the
# generated gh-pages root on later deployments.
if gh api "repos/$repository/pages" >/dev/null 2>&1; then
  gh api --method PUT "repos/$repository/pages" \
    -f "source[branch]=gh-pages" -f "source[path]=/" >/dev/null
else
  # A newly pushed gh-pages branch may be auto-enabled between the GET and
  # POST. Treat that race's "already enabled" response as success.
  pages_error="$(mktemp)"
  if ! gh api --method POST "repos/$repository/pages" \
      -f "source[branch]=gh-pages" -f "source[path]=/" \
      >/dev/null 2>"$pages_error"; then
    if ! grep -q "already enabled" "$pages_error"; then
      cat "$pages_error" >&2
      rm -f "$pages_error"
      exit 1
    fi
  fi
  rm -f "$pages_error"
fi

echo "Published: https://${repository%%/*}.github.io/${repository#*/}/"
