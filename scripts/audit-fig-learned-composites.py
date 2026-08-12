#!/usr/bin/env python3
"""Prove every shipped learnable FIG 6Bh composite has learned-route evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


EXPECTED_ALL_COMPOSITES = {
    55: (0x8100, 34, 195),
    61: (0x8100, 36, 201),
    62: (0x8400, 0, 202),
    69: (0x8100, 35, 209),
    75: (0x8400, 0, 215),
    79: (0xa400, 0, 219),
    80: (0xa100, 60, 220),
    85: (0x84c0, 100, 225),
    90: (0xa400, 40, 230),
    92: (0x8400, 0, 232),
    96: (0xa400, 0, 236),
}

EXPECTED_REACHABLE = {
    80: (0x60, 0x46,
         "fig-player-buff-expiry-ability-composite-status-damage"),
    90: (0x38, 0x3a,
         "fig-player-buff-expiry-ability-composite-damage"),
}

# Ability 85 has a useful dispatcher regression capture, but it is deliberately
# not counted as shipped-learnable evidence: neither the four ORC growth tables
# nor the released SAVE slots can teach it to a player.
EXPECTED_SYNTHETIC = {
    85: (0x43, 0x36,
         "fig-player-buff-expiry-ability-composite-missing-media"),
}


def word(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 2 > len(data):
        raise ValueError("word lies outside executable image")
    return int.from_bytes(data[offset:offset + 2], "little")


def mz_image(path: Path) -> bytes:
    executable = path.read_bytes()
    if executable[:2] != b"MZ" or len(executable) < 0x1c:
        raise ValueError(f"{path.name} is not an MZ executable")
    header = word(executable, 8) * 16
    if header < 0x1c or header > len(executable):
        raise ValueError(f"{path.name} has a malformed MZ header")
    return executable[header:]


def item_records(executable: bytes) -> list[bytes]:
    directory_bytes = word(executable, 2)
    sentinel = word(executable, 0)
    if directory_bytes < 4 or directory_bytes > len(executable) or \
            directory_bytes % 2:
        raise ValueError("ITEM.EXE directory is malformed")
    offsets = [word(executable, pos)
               for pos in range(0, directory_bytes, 2)]
    records = []
    for start in offsets:
        finish = min((value for value in offsets if value > start),
                     default=sentinel)
        if start > finish or finish > len(executable):
            raise ValueError("ITEM.EXE record bounds are malformed")
        records.append(executable[start:finish] if start < finish else b"")
    return records


def fig_composites(fig: bytes) -> dict[int, tuple[int, int]]:
    if len(fig) < 5 or fig[0] != 0xb8 or fig[3:5] != b"\x8e\xd8":
        raise ValueError("FIG.EXE data-segment prologue differs")
    table = word(fig, 1) * 16 + 0x1dcc
    if table + 151 * 20 > len(fig):
        raise ValueError("FIG.EXE ability table is truncated")
    result = {}
    for ability_id in range(151):
        record = fig[table + ability_id * 20:table + (ability_id + 1) * 20]
        if word(record, 0x0e) == 0x6b:
            result[ability_id] = (word(record, 0x0c), word(record, 0x10))
    return result


def reachable_abilities(orc: bytes, save: bytes) -> tuple[set[int], dict[int, list[str]]]:
    if len(orc) < 8:
        raise ValueError("ORC.EXE directory is truncated")
    reachable: set[int] = set()
    origins: dict[int, list[str]] = {}
    for table in range(4):
        start = word(orc, table * 2)
        if start + 60 * 18 > len(orc):
            raise ValueError("ORC.EXE growth table is truncated")
        for row in range(60):
            packed = word(orc, start + row * 18 + 16)
            if packed:
                if packed > 150:
                    raise ValueError("ORC.EXE growth ability exceeds FIG table")
                reachable.add(packed)
                origins.setdefault(packed, []).append(
                    f"ORC growth table {table} row {row}")
    actor_zero = 0x106
    if len(save) < actor_zero + 4 * 0x9f:
        raise ValueError("SAVE.DA1 actor records are truncated")
    for actor in range(4):
        start = actor_zero + actor * 0x9f + 0x6d
        for slot, ability_id in enumerate(save[start:start + 50]):
            if ability_id:
                reachable.add(ability_id)
                origins.setdefault(ability_id, []).append(
                    f"SAVE actor {actor} slot {slot}")
    return reachable, origins


def valid_digest(value: object) -> bool:
    return isinstance(value, str) and len(value) == 64 and all(
        char in "0123456789abcdef" for char in value)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def check_reference(root: Path, ability_id: int, first: int, second: int,
                    stem: str) -> int:
    path = root / "scripts" / f"{stem}-rgb-reference.json"
    verifier = root / "scripts" / f"verify-{stem}.py"
    if not path.is_file() or not verifier.is_file():
        raise ValueError(f"ability {ability_id} learned-route evidence is absent")
    reference = json.loads(path.read_text(encoding="utf-8"))
    if reference.get("schema_version") != 1 or \
            reference.get("status") != "partial_exact_rgb_checkpoint" or \
            reference.get("finishing_ability_id") != ability_id or \
            reference.get("finishing_effect_code") != 0x6b or \
            reference.get("nested_effects") != [first, second]:
        raise ValueError(f"ability {ability_id} learned reference metadata differs")
    pages = reference.get("matched_frames")
    if not isinstance(pages, list) or not pages:
        raise ValueError(f"ability {ability_id} has no exact learned RGB page")
    for page in pages:
        rewrite = page.get("rewrite_rgb_sha256")
        if not valid_digest(rewrite) or rewrite != page.get("original_rgb_sha256"):
            raise ValueError(f"ability {ability_id} has a non-exact learned page")
    for path_key, digest_key in (
            ("capture_autotype", "capture_autotype_sha256"),
            ("replay_input", "replay_input_sha256")):
        evidence = root / "scripts" / reference[path_key]
        if not evidence.is_file() or \
                sha256(evidence.read_bytes()) != reference[digest_key]:
            raise ValueError(f"ability {ability_id} {path_key} differs")
    if not valid_digest(reference.get("capture_video_sha256")) or \
            not valid_digest(reference.get("capture_manifest_sha256")):
        raise ValueError(f"ability {ability_id} capture digest is malformed")
    return len(pages)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parent.parent
    parser.add_argument("game", type=Path, nargs="?", default=root / "game")
    args = parser.parse_args()
    try:
        fig = mz_image(args.game / "FIG.EXE")
        orc = mz_image(args.game / "ORC.EXE")
        items = item_records(mz_image(args.game / "ITEM.EXE"))
        save = (args.game / "SAVE.DA1").read_bytes()

        composites = fig_composites(fig)
        expected_descriptors = {
            ability: (flags, cost)
            for ability, (flags, cost, _) in EXPECTED_ALL_COMPOSITES.items()
        }
        if composites != expected_descriptors:
            raise ValueError(f"FIG 6Bh ability domain differs: {composites!r}")

        for ability_id, (_, _, item_id) in EXPECTED_ALL_COMPOSITES.items():
            record_index = item_id + 2
            if record_index >= len(items):
                raise ValueError(f"ability {ability_id} ITEM wrapper is absent")
            record = items[record_index]
            if len(record) < 11 or word(record, 0) != 0x10 or \
                    word(record, 7) != 0x6b or item_id != ability_id + 140:
                raise ValueError(f"ability {ability_id} ITEM wrapper differs")

        reachable, origins = reachable_abilities(orc, save)
        reachable_composites = set(composites).intersection(reachable)
        if reachable_composites != set(EXPECTED_REACHABLE):
            raise ValueError(
                f"shipped learned 6Bh domain differs: {sorted(reachable_composites)}")
        if origins.get(80) != ["ORC growth table 0 row 36"] or \
                origins.get(90) != ["ORC growth table 0 row 29"]:
            raise ValueError("learned composite growth origins differ")
        if set(EXPECTED_SYNTHETIC).intersection(reachable):
            raise ValueError("synthetic ability 85 became shipped-learnable")

        pages = sum(check_reference(root, ability, *entry)
                    for ability, entry in EXPECTED_REACHABLE.items())
        synthetic_pages = sum(check_reference(root, ability, *entry)
                              for ability, entry in EXPECTED_SYNTHETIC.items())
        print(
            "FIG learned composite coverage: "
            f"{len(EXPECTED_REACHABLE)}/{len(EXPECTED_REACHABLE)} shipped "
            f"learnable abilities, {pages} exact RGB pages; "
            f"1 synthetic dispatcher route, {synthetic_pages} exact RGB pages")
        return 0
    except (OSError, ValueError, KeyError, TypeError,
            json.JSONDecodeError) as error:
        parser.exit(1, f"FIG learned composite coverage: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
