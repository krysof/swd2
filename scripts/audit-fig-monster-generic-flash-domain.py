#!/usr/bin/env python3
"""Fail closed on every shipped reachable generic-monster 25ee flash class."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


PROGRAM_SHA256 = (
    "c4e0f0d04d25b70f80ab3d3490a81ef78c375193ccac70dad5b09c186c758876"
)
EXPECTED_DOMAIN_SHA256 = (
    "8e721fb4592c06c42b9c904c1c9905ad1c98a071635212c99b73a2ee85a3f06b"
)
REFERENCES = {
    0x8D: ("fig-monster-generic-rgb-reference.json", 10, 0xA200, 0x4C),
    0x81: ("fig-monster-generic-color81-rgb-reference.json", 4, 0xA205, 0x5F),
    0x5C: ("fig-monster-generic-color5c-rgb-reference.json", 70, 0xA401, 0x41),
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


def flash_color(flags: int) -> int:
    return {1: 0x5C, 2: 0x81, 3: 0xAA, 4: 0x7C, 5: 0x81}.get(
        flags & 7, 0x8D)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parent.parent
    parser.add_argument("game", type=Path, nargs="?", default=root / "game")
    parser.add_argument("evidence", type=Path, nargs="?", default=root / "scripts")
    args = parser.parse_args()
    try:
        fig_path = args.game / "FIG.EXE"
        if hashlib.sha256(fig_path.read_bytes()).hexdigest() != PROGRAM_SHA256:
            raise ValueError("FIG.EXE differs from generic-flash reference")
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
        affordable: set[tuple[int, int, int, int]] = set()
        unavailable: set[tuple[int, int, int, int]] = set()
        for definition in definitions:
            record = records[definition + 2]
            if len(record) < 0x50:
                raise ValueError("reachable monster definition is truncated")
            ability = word(record, 0x32)
            if ability == 0:
                continue
            if ability >= 151:
                raise ValueError("generic ability exceeds FIG table")
            descriptor = fig[table + ability * 20:table + (ability + 1) * 20]
            row = (ability, word(descriptor, 12), word(descriptor, 14),
                   word(descriptor, 16))
            (affordable if word(record, 0x44) >= row[3] else unavailable).add(row)
        serialized = ";".join(
            f"{ability}:{flags:04x}:{effect:02x}:{cost}"
            for ability, flags, effect, cost in sorted(affordable))
        if hashlib.sha256(serialized.encode("ascii")).hexdigest() != \
                EXPECTED_DOMAIN_SHA256 or len(affordable) != 41 or \
                len(unavailable) != 4:
            raise ValueError("reachable generic-ability domain changed")
        by_color: dict[int, set[int]] = {}
        for ability, flags, _, _ in affordable:
            by_color.setdefault(flash_color(flags), set()).add(ability)
        if {color: len(abilities) for color, abilities in by_color.items()} != {
                0x5C: 4, 0x81: 6, 0x8D: 31}:
            raise ValueError(f"generic 25ee flash classes changed: {by_color}")

        total_pairs = 0
        for color, (filename, ability, flags, effect) in REFERENCES.items():
            payload = json.loads(
                (args.evidence / filename).read_text(encoding="utf-8"))
            if (payload.get("ability_id"), payload.get("effect_code")) != \
                    (ability, effect):
                raise ValueError(f"generic flash reference changed: {filename}")
            if filename != "fig-monster-generic-rgb-reference.json" and \
                    (payload.get("target_flags"),
                     payload.get("solid_palette_index")) != (flags, color):
                raise ValueError(f"generic flash class changed: {filename}")
            pairs = list(exact_pairs(payload))
            if len(pairs) != 6 or any(len(value) != 64 for value in pairs):
                raise ValueError(
                    f"generic flash exact RGB pages changed: {filename}")
            total_pairs += len(pairs)
        all_target = json.loads((
            args.evidence / "fig-monster-generic-all-target-rgb-reference.json"
        ).read_text(encoding="utf-8"))
        all_target_pairs = list(exact_pairs(all_target))
        if tuple(all_target.get(name) for name in (
                "monster_definition_id", "ability_id", "effect_code",
                "target_flags", "solid_palette_index")) != (
                    334, 74, 0x42, 0x8401, 0x5C) or \
                len(all_target_pairs) != 57:
            raise ValueError("generic four-party all-target evidence changed")
        total_pairs += len(all_target_pairs)
        dead_slots = json.loads((
            args.evidence /
            "fig-monster-generic-dead-slots-rgb-reference.json"
        ).read_text(encoding="utf-8"))
        dead_slot_pairs = list(exact_pairs(dead_slots))
        if tuple(dead_slots.get(name) for name in (
                "monster_definition_id", "ability_id", "effect_code",
                "target_flags")) != (334, 74, 0x42, 0x8401) or \
                dead_slots.get("initial_living_party_slots") != [0, 2] or \
                len(dead_slot_pairs) != 37:
            raise ValueError("generic dead-slot evidence changed")
        total_pairs += len(dead_slot_pairs)
        print(
            "FIG generic-monster flash audit: 550 encounters / 174 monster "
            "definitions expose 41 affordable generic abilities in exactly "
            f"three 25ee colour classes; all {total_pairs} archetype, "
            "four-party and dead-slot all-target RGB pages match original")
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            json.JSONDecodeError) as error:
        parser.exit(1, f"FIG generic-monster flash audit: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
