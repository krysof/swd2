#!/usr/bin/env python3
"""Verify that RPG Record selects the pair used by the exit checkpoint."""

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
    parser.add_argument("game_root", type=Path)
    parser.add_argument("save_root", type=Path)
    parser.add_argument("trace", type=Path)
    args = parser.parse_args()

    trace = json.loads(args.trace.read_text(encoding="utf-8"))
    expected_state = trace.get("state_fnv1a64")
    expected_map = trace.get("mapz_fnv1a64")
    expected_name = trace.get("name_fnv1a64")
    slot_one_state = (args.save_root / "SAVE.DA1").read_bytes()
    slot_one_map = (args.save_root / "MAPZ.DA1").read_bytes()
    original_one_state = (args.game_root / "SAVE.DA1").read_bytes()
    original_one_map = (args.game_root / "MAPZ.DA1").read_bytes()
    slot_one_name = (args.save_root / "NAME1.DSK").read_bytes()
    original_one_name = (args.game_root / "NAME1.DSK").read_bytes()
    slot_four_state = (args.save_root / "SAVE.DA4").read_bytes()
    slot_four_map = (args.save_root / "MAPZ.DA4").read_bytes()
    slot_four_name = (args.save_root / "NAME4.DSK").read_bytes()

    if (slot_one_state != original_one_state or slot_one_map != original_one_map or
            slot_one_name != original_one_name):
        raise SystemExit("exit checkpoint overwrote seed slot one after Record chose slot four")
    if slot_four_state == (args.game_root / "SAVE.DA4").read_bytes():
        raise SystemExit("Record did not change selected slot four")
    if fnv1a64(slot_four_state) != expected_state:
        raise SystemExit("selected SAVE.DA4 differs from replay final state")
    if fnv1a64(slot_four_map) != expected_map:
        raise SystemExit("selected MAPZ.DA4 differs from replay final database")
    if fnv1a64(slot_four_name) != expected_name:
        raise SystemExit("selected NAME4.DSK differs from replay final name font")
    print(
        "explicit Record routing matches replay: active=4, "
        f"state={expected_state}, mapz={expected_map}, name={expected_name}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
