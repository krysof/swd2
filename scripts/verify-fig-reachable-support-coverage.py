#!/usr/bin/env python3
"""Prove every shipped reachable FIG support ability has original RGB evidence or the fault guard."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def mz_image(path: Path) -> bytes:
    data = path.read_bytes()
    if data[:2] != b"MZ" or len(data) < 0x1C:
        raise ValueError(f"not a DOS MZ executable: {path}")
    header_bytes = int.from_bytes(data[8:10], "little") * 16
    if header_bytes < 0x1C or header_bytes > len(data):
        raise ValueError(f"invalid DOS MZ header: {path}")
    return data[header_bytes:]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parent.parent
    parser.add_argument("game", type=Path)
    parser.add_argument("evidence", type=Path, default=root / "scripts", nargs="?")
    args = parser.parse_args()
    try:
        orc = mz_image(args.game / "ORC.EXE")
        growth_offsets = [
            int.from_bytes(orc[offset:offset + 2], "little")
            for offset in range(0, 8, 2)
        ]
        reachable: set[int] = set()
        for start in growth_offsets:
            for row in range(60):
                ability = int.from_bytes(
                    orc[start + row * 18 + 16:start + row * 18 + 18], "little")
                if ability:
                    reachable.add(ability)
        save = (args.game / "SAVE.DA1").read_bytes()
        for actor in range(4):
            for slot in range(50):
                ability = save[0x106 + actor * 0x9F + 0x6D + slot]
                if ability:
                    reachable.add(ability)
        if len(reachable) != 75:
            raise ValueError(f"reachable ability domain changed: {len(reachable)}")

        fig = mz_image(args.game / "FIG.EXE")
        if fig[:1] != b"\xB8" or fig[3:5] != b"\x8E\xD8":
            raise ValueError("FIG.EXE data prologue changed")
        data_base = int.from_bytes(fig[1:3], "little") * 16
        table = data_base + 0x1DCC
        records: dict[int, tuple[int, int]] = {}
        for ability in reachable:
            offset = table + ability * 20
            flags = int.from_bytes(fig[offset + 12:offset + 14], "little")
            effect = int.from_bytes(fig[offset + 14:offset + 16], "little")
            if effect and effect <= 0x30 and not flags & 0x4000:
                records[ability] = (effect, flags)
        expected_ids = {
            9, 41, 42, 44, 45, 46, 47, 49, 50, 68, 94, 101, 103,
            104, 108, 109, 112, 113,
        }
        if set(records) != expected_ids:
            raise ValueError(f"reachable support records changed: {sorted(records)}")
        if records[41] != (0x11, 0x0500):
            raise ValueError("ability 41 fault record changed")

        references: dict[int, tuple[int, Path]] = {}
        for path in sorted(args.evidence.glob("fig-player-support*-rgb-reference.json")):
            payload = json.loads(path.read_text(encoding="utf-8"))
            if payload.get("kind") != "original_fig_player_support":
                continue
            ability = payload.get("ability_id")
            effect = payload.get("effect_code")
            if not isinstance(ability, int) or not isinstance(effect, int):
                raise ValueError(f"malformed support reference: {path}")
            if ability in references:
                raise ValueError(f"duplicate support reference for ability {ability}")
            references[ability] = (effect, path)

        normal_ids = expected_ids - {41}
        missing = normal_ids - references.keys()
        if missing:
            raise ValueError(f"reachable support RGB evidence missing: {sorted(missing)}")
        for ability in normal_ids:
            effect, _ = references[ability]
            if effect != records[ability][0]:
                raise ValueError(f"ability {ability} reference selector changed")
        # Abilities 81 and 111 are historical non-growth selector fixtures.
        # They remain useful but cannot count in place of reachable records.
        extras = set(references) - normal_ids
        if extras != {81, 111}:
            raise ValueError(f"unexpected non-reachable support references: {sorted(extras)}")

        print(
            "FIG reachable support coverage: 75 reachable abilities, "
            "17 normal support records with original RGB evidence, "
            "ability 41 isolated by the machine-code fault guard"
        )
        return 0
    except (OSError, ValueError, KeyError, IndexError, json.JSONDecodeError) as error:
        parser.exit(1, f"FIG reachable support coverage: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
