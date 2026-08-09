#!/usr/bin/env python3
"""Prove a frontend exit checkpoint persisted the exact live SAVE/MAPZ pair."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3


def fnv1a64(data: bytes) -> str:
    digest = FNV_OFFSET
    for value in data:
        digest ^= value
        digest = (digest * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return f"{digest:016x}"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("trace", type=Path)
    parser.add_argument("save", type=Path)
    parser.add_argument("mapz", type=Path)
    args = parser.parse_args()

    trace = json.loads(args.trace.read_text(encoding="utf-8"))
    expected_state = trace.get("state_fnv1a64")
    expected_map = trace.get("mapz_fnv1a64")
    if not isinstance(expected_state, str) or not isinstance(expected_map, str):
        raise SystemExit("trace lacks final SAVE/MAPZ FNV-1a digests")

    save_bytes = args.save.read_bytes()
    map_bytes = args.mapz.read_bytes()
    actual_state = fnv1a64(save_bytes)
    actual_map = fnv1a64(map_bytes)
    if len(save_bytes) != 1350:
        raise SystemExit(f"SAVE checkpoint has {len(save_bytes)} bytes, expected 1350")
    if actual_state != expected_state:
        raise SystemExit(
            f"SAVE checkpoint differs from live state: {actual_state} != {expected_state}"
        )
    if actual_map != expected_map:
        raise SystemExit(
            f"MAPZ checkpoint differs from live database: {actual_map} != {expected_map}"
        )
    print(
        "live SAVE/MAPZ checkpoint matches replay: "
        f"state={actual_state}, mapz={actual_map}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
