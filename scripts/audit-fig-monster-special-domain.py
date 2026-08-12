#!/usr/bin/env python3
"""Fail closed on the shipped reachable monster-special selector domain."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
from pathlib import Path


PROGRAM_SHA256 = (
    "c4e0f0d04d25b70f80ab3d3490a81ef78c375193ccac70dad5b09c186c758876"
)
EXPECTED_DOMAIN_SHA256 = (
    "5357efb2478a4a3c7f27f571ba5ac7b3861dc4f767e9ed56e33d2d30b41a5268"
)
REFERENCES = {
    "fig-monster-special-silent-rgb-reference.json":
        ("original_fig_monster_special_silent_return_sequence", 10, 0x4C, 6),
    "fig-monster-special-effect5e-rgb-reference.json":
        ("original_fig_monster_special_effect5e_sequence", 65, 0x5E, 50),
    "fig-monster-special-effect5f-rgb-reference.json":
        ("original_fig_monster_special_effect5f_sequence", 139, 0x5F, 52),
    "fig-monster-special-status-rgb-reference.json":
        ("original_fig_monster_special_status_sequence", 117, 0x64, 10),
    "fig-monster-special-resistance-rgb-reference.json":
        ("original_fig_monster_special_resistance_sequence", 117, 0x64, 5),
    "fig-monster-special-effect65-rgb-reference.json":
        ("original_fig_monster_special_effect65_sequence", 58, 0x65, 91),
    "fig-monster-special-buff-rgb-reference.json":
        ("original_fig_monster_special_buff_sequence", 38, 0x67, 5),
}


def word(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 2 > len(data):
        raise ValueError("word lies outside executable image")
    return int.from_bytes(data[offset:offset + 2], "little")


def mz_image(path: Path) -> bytes:
    data = path.read_bytes()
    if data[:2] != b"MZ" or len(data) < 0x1C:
        raise ValueError(f"{path.name} is not a DOS MZ executable")
    header = word(data, 8) * 16
    if header < 0x1C or header > len(data):
        raise ValueError(f"{path.name} has a malformed MZ header")
    return data[header:]


def archive_records(image: bytes) -> list[bytes]:
    directory_bytes = word(image, 2)
    if directory_bytes < 4 or directory_bytes % 2:
        raise ValueError("ITEM.EXE directory is malformed")
    offsets = [word(image, offset)
               for offset in range(0, directory_bytes, 2)]
    sentinel = word(image, 0)
    return [image[start:min((value for value in offsets if value > start),
                            default=sentinel)]
            for start in offsets]


def exact_pairs(value: object):
    if isinstance(value, dict):
        rewrite = value.get("rewrite_rgb_sha256")
        original = value.get("original_rgb_sha256")
        if isinstance(rewrite, str) and rewrite == original:
            yield rewrite
        for child in value.values():
            yield from exact_pairs(child)
    elif isinstance(value, list):
        for child in value:
            yield from exact_pairs(child)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parent.parent
    parser.add_argument("game", type=Path, nargs="?", default=root / "game")
    parser.add_argument("evidence", type=Path, nargs="?", default=root / "scripts")
    args = parser.parse_args()
    try:
        fig_path = args.game / "FIG.EXE"
        if hashlib.sha256(fig_path.read_bytes()).hexdigest() != PROGRAM_SHA256:
            raise ValueError("FIG.EXE differs from monster-special reference")
        orc = mz_image(args.game / "ORC.EXE")
        directory_bytes = 0x4E4
        directory = [word(orc, offset)
                     for offset in range(0, directory_bytes, 2)]
        growth_start = min(directory[:4])
        starts = sorted({value for value in directory
                         if directory_bytes <= value < growth_start})
        definitions: set[int] = set()
        for index, start in enumerate(starts):
            finish = starts[index + 1] if index + 1 < len(starts) \
                else growth_start
            if start + 30 > finish:
                raise ValueError("ORC encounter header is truncated")
            ids = [word(orc, start + 20 + slot * 2) for slot in range(4)]
            count = word(orc, start + 28)
            if count == 0 or count > 5 or start + 30 + count * 4 > finish:
                raise ValueError("ORC encounter combatant list is malformed")
            for slot in range(count):
                selected = word(orc, start + 30 + slot * 2)
                if selected >= 4 or ids[selected] == 0:
                    raise ValueError("ORC encounter selects an empty definition")
                definitions.add(ids[selected])
        if len(starts) != 550 or len(definitions) != 174:
            raise ValueError(
                f"ORC domain changed: {len(starts)} encounters, "
                f"{len(definitions)} definitions")

        records = archive_records(mz_image(args.game / "ITEM.EXE"))
        fig = mz_image(fig_path)
        if fig[:1] != b"\xB8" or fig[3:5] != b"\x8E\xD8":
            raise ValueError("FIG.EXE data prologue changed")
        table = word(fig, 1) * 16 + 0x1DCC
        domain: dict[tuple[int, int, int, int], tuple[set[int], bool]] = {}
        for definition in definitions:
            record = records[definition + 2]
            if len(record) < 0x50:
                raise ValueError("reachable monster definition is truncated")
            for offset in (0x42, 0x46):
                ability = word(record, offset)
                if ability == 0:
                    continue
                if ability >= 151:
                    raise ValueError("special ability exceeds FIG table")
                descriptor = fig[
                    table + ability * 20:table + (ability + 1) * 20]
                row = (ability, word(descriptor, 12),
                       word(descriptor, 14), word(descriptor, 16))
                definitions_for_row, affordable = domain.setdefault(
                    row, (set(), False))
                definitions_for_row.add(definition)
                domain[row] = (
                    definitions_for_row,
                    affordable or word(record, 0x44) >= row[3])
        serialized = ";".join(
            f"{ability}:{flags:04x}:{effect:02x}:{cost}:"
            f"{int(affordable)}:{','.join(map(str, sorted(row_definitions)))}"
            for (ability, flags, effect, cost),
                (row_definitions, affordable) in sorted(domain.items()))
        if hashlib.sha256(serialized.encode("ascii")).hexdigest() != \
                EXPECTED_DOMAIN_SHA256 or len(domain) != 19:
            raise ValueError("reachable monster-special domain changed")
        affordable = {
            row for row, (_, can_pay) in domain.items() if can_pay
        }
        unavailable = {
            row for row, (_, can_pay) in domain.items() if not can_pay
        }
        effects = collections.Counter(row[2] for row in affordable)
        if len(affordable) != 17 or effects != {
                0x3D: 1, 0x3E: 1, 0x49: 1, 0x4C: 1,
                0x5E: 3, 0x5F: 2, 0x64: 5, 0x65: 2, 0x67: 1} or \
                {(row[0], row[2]) for row in unavailable} != {
                    (37, 0x69), (134, 0x5E)}:
            raise ValueError("monster-special affordability/classes changed")

        total_pairs = 0
        for filename, (kind, ability, effect, count) in REFERENCES.items():
            payload = json.loads(
                (args.evidence / filename).read_text(encoding="utf-8"))
            if (payload.get("kind"), payload.get("ability_id"),
                    payload.get("effect_code")) != (kind, ability, effect):
                raise ValueError(f"monster-special reference changed: {filename}")
            pairs = list(exact_pairs(payload))
            if len(pairs) != count or any(len(value) != 64 for value in pairs):
                raise ValueError(
                    f"monster-special exact RGB pages changed: {filename}")
            total_pairs += len(pairs)
        print(
            "FIG monster-special domain audit: 550 encounters / 174 monster "
            "definitions expose 19 special abilities, 17 affordable entries "
            "and nine selector codes; all six reachable presentation classes "
            f"are backed by {total_pairs} exact original RGB pages")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError) as error:
        parser.exit(1, f"FIG monster-special domain audit: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
