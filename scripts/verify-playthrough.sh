#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
python3 "$root/scripts/verify-evidence-manifest.py" playthrough
evidence_paths="$(python3 - "$root" <<'PY'
import json
import pathlib
import sys
root = pathlib.Path(sys.argv[1])
data = json.loads((root / 'verification/playthrough/manifest.json').read_text())
for role in ('original_trace', 'rewrite_trace', 'comparison'):
    matches = [item['path'] for item in data['artifacts']
               if item.get('role') == role]
    if len(matches) != 1:
        raise SystemExit(f'playthrough manifest must contain exactly one {role}')
    print(root / matches[0])
PY
)"
# macOS still ships Bash 3.2, which has no readarray/mapfile. Evidence paths are
# repository-relative manifest values, so extracting the three validated lines
# keeps this wrapper portable across the native targets we support.
original_trace="$(printf '%s\n' "$evidence_paths" | sed -n '1p')"
rewrite_trace="$(printf '%s\n' "$evidence_paths" | sed -n '2p')"
comparison="$(printf '%s\n' "$evidence_paths" | sed -n '3p')"
python3 "$root/scripts/verify-replay-trace.py" "$rewrite_trace" --require-full-game
exec python3 "$root/scripts/compare-replay-traces.py" \
  "$original_trace" "$rewrite_trace" --verify-report "$comparison"
