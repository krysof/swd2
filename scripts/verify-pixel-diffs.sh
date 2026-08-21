#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# The completion boundary is the hash-locked aggregate of every registered
# original/rewrite FIG pair. It deliberately does not claim that every
# theoretical party/monster/status combination was enumerated before release.
python3 "$root/scripts/verify-fig-rgb-checkpoint.py" \
  "$root/verification/pixel_diffs/checkpoint-2026-08-11-fig-rgb.json"
python3 "$root/scripts/audit-fig-story-visual-branches.py" "$root/game"
python3 "$root/scripts/audit-fig-monster-special-domain.py" "$root/game"
exec python3 "$root/scripts/audit-fig-monster-generic-flash-domain.py" \
  "$root/game"
