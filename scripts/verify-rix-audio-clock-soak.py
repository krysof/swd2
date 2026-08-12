#!/usr/bin/env python3
"""Fail closed on the all-track virtual RIX loop-clock soak checkpoint."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


DIGEST = re.compile(r"^[0-9a-f]{64}$")


def asset_set_sha256(game: Path) -> tuple[int, str]:
    paths = sorted(path for path in game.rglob("*.RIX") if path.is_file())
    digest = hashlib.sha256()
    for path in paths:
        digest.update(path.relative_to(game).as_posix().encode("utf-8"))
        digest.update(b"\0")
        digest.update(path.read_bytes())
    return len(paths), digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("game", type=Path)
    args = parser.parse_args()
    try:
        data = json.loads(args.reference.read_text(encoding="utf-8"))
        expected = {
            "schema_version": 1,
            "kind": "swd2_rix_virtual_clock_soak",
            "status": "verified",
            "track_count": 43,
            "device_sample_rates": [8000, 44100, 48000],
            "minimum_virtual_hours_per_track_rate": 24,
            "minimum_virtual_track_hours": 3096,
            "loop_boundaries": 765546,
            "emitted_samples": 371932516654,
            "checkpoint_fnv1a64": "ae6ff348967c0217",
        }
        for key, value in expected.items():
            if data.get(key) != value:
                raise ValueError(f"checkpoint field differs: {key}")
        count, digest = asset_set_sha256(args.game)
        registered_digest = data.get("rix_asset_set_sha256")
        if count != data["track_count"] or \
                not isinstance(registered_digest, str) or \
                DIGEST.fullmatch(registered_digest) is None or \
                digest != registered_digest:
            raise ValueError("shipped RIX asset set differs")
        limitation = data.get("limitation")
        if not isinstance(limitation, str) or \
                "Accelerated" not in limitation or \
                "not a wall-clock" not in limitation:
            raise ValueError("checkpoint overstates accelerated soak scope")
        print(
            "RIX virtual clock soak: 43 tracks x 3 rates x >=24 hours, "
            "765546 exact loop boundaries")
        return 0
    except (OSError, ValueError, KeyError, TypeError,
            json.JSONDecodeError) as error:
        parser.exit(1, f"RIX virtual clock soak: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
