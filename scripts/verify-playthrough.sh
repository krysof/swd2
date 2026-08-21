#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${1:-$root/build}"
trace="$build/rpg-mainline-event334-trace.json"
saves="$build/rpg-mainline-event334-saves"

# Completion requires the modern C++ runtime's uninterrupted released route.
# The trace contains every input checkpoint, frame, audio/timeline event and
# RPG/Fight transition; a second full original trace is not a release gate.
python3 "$root/scripts/verify-replay-trace.py" "$trace" \
  --require-full-game --min-frames 110000
exec python3 "$root/scripts/verify-rpg-mainline-event334.py" \
  "$trace" "$saves/SAVE.DA3" "$saves/MAPZ.DA3" "$saves/NAME3.DSK"
