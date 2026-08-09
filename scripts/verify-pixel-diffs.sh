#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
python3 "$root/scripts/verify-evidence-manifest.py" pixel_diffs
evidence_paths="$(python3 - "$root" <<'PY'
import json
import pathlib
import sys
root = pathlib.Path(sys.argv[1])
data = json.loads((root / 'verification/pixel_diffs/manifest.json').read_text())
for role in ('input', 'baseline', 'rewrite_output', 'diff_report'):
    matches = [item['path'] for item in data['artifacts']
               if item.get('role') == role]
    if len(matches) != 1:
        raise SystemExit(f'pixel_diffs manifest must contain exactly one {role}')
    print(root / matches[0])
PY
)"
# Avoid readarray/mapfile so this remains compatible with macOS Bash 3.2.
input="$(printf '%s\n' "$evidence_paths" | sed -n '1p')"
baseline="$(printf '%s\n' "$evidence_paths" | sed -n '2p')"
rewrite_output="$(printf '%s\n' "$evidence_paths" | sed -n '3p')"
diff_report="$(printf '%s\n' "$evidence_paths" | sed -n '4p')"
exec python3 "$root/scripts/compare-frame-captures.py" \
  "$baseline" "$rewrite_output" --input "$input" \
  --verify-report "$diff_report"
