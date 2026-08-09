#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
python3 "$root/scripts/verify-evidence-manifest.py" playthrough
trace="$(python3 - "$root" <<'PY'
import json
import pathlib
import sys
root = pathlib.Path(sys.argv[1])
data = json.loads((root / 'verification/playthrough/manifest.json').read_text())
matches = [item['path'] for item in data['artifacts']
           if item.get('role') == 'rewrite_trace']
if len(matches) != 1:
    raise SystemExit('playthrough manifest must contain exactly one rewrite_trace')
print(root / matches[0])
PY
)"
exec python3 "$root/scripts/verify-replay-trace.py" "$trace" --require-full-game
