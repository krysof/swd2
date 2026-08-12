#!/usr/bin/env python3
"""Lock the released main story through Wild Bear Mountain and T2 entry."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3


def u16(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 2 > len(data):
        raise ValueError(f"word at {offset:#x} is outside {len(data)} bytes")
    return data[offset] | (data[offset + 1] << 8)


def fnv1a64(data: bytes) -> str:
    digest = FNV_OFFSET
    for value in data:
        digest ^= value
        digest = (digest * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return f"{digest:016x}"


def map_area(image: bytes, location_offset: int) -> tuple[int, int]:
    location = u16(image, location_offset)
    area = u16(image, location + 26)
    count = u16(image, area + 4)
    if count == 0:
        raise ValueError(f"MAPZ location {location_offset} has no entities")
    return area, count


def map_field(image: bytes, location_offset: int, field: int,
              entity: int = 0) -> int:
    area, count = map_area(image, location_offset)
    if field >= 11 or entity >= count:
        raise ValueError("MAPZ field/entity index is outside the released area")
    return u16(image, area + 6 + field * count * 2 + entity * 2)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("save", type=Path)
    parser.add_argument("mapz", type=Path)
    parser.add_argument("name", type=Path)
    args = parser.parse_args()
    try:
        trace = json.loads(args.trace.read_text(encoding="utf-8"))
        expected_transitions = [
            ("MEO.EXE", "--", "MT", True),
            ("RPG.EXE", "MT", "ED", True),
            ("DEMO.EXE", "ED", "--", True),
        ]
        for _ in range(6):
            expected_transitions.extend([
                ("RPG.EXE", "OM" if len(expected_transitions) == 3 else "OC",
                 "IF", True),
                ("FIG.EXE", "IF", "OC", True),
            ])
        expected_transitions.append(("RPG.EXE", "OC", "--", True))
        transitions = [
            (item.get("module"), item.get("input"), item.get("output"),
             item.get("launched"))
            for item in trace.get("transitions", [])
        ]
        if transitions != expected_transitions:
            raise ValueError(f"mainline transitions differ: {transitions!r}")

        expected_values = {
            ("input", "total"): 1924,
            ("input", "consumed"): 1924,
            ("input", "remaining"): 0,
            ("input", "implicit_quit_calls"): 0,
            ("boundaries", "wait"): 100,
            ("boundaries", "poll"): 1827,
            ("boundaries", "text"): 129,
            ("video", "frames"): 5591,
            ("video", "direct_updates"): 1908,
            ("video", "fnv1a64"): "00a40c0e3fe0d0dd",
            ("audio", "music_calls"): 38,
            ("audio", "voice_calls"): 58,
            ("audio", "stop_audio_calls"): 16,
            ("audio", "fnv1a64"): "e787479490195566",
        }
        for (section, key), expected in expected_values.items():
            actual = trace.get(section, {}).get(key)
            if actual != expected:
                raise ValueError(
                    f"mainline {section}.{key} is {actual!r}, expected {expected!r}")
        if trace.get("delay_milliseconds") != 205135:
            raise ValueError("mainline cumulative 70-Hz timing differs")

        digests = {
            "state_fnv1a64": "b7ff851ac0057323",
            "mapz_fnv1a64": "ae6c1bca924a54ac",
            "name_fnv1a64": "e3d2853e2676513b",
        }
        for key, expected in digests.items():
            if trace.get(key) != expected:
                raise ValueError(f"mainline {key} differs")
        frames = trace.get("frame_fnv1a64", [])
        if len(frames) != 5591 or frames[-1] != "e8875c4faa5ffc81":
            raise ValueError("mainline final TW-2A world frame differs")

        checkpoints = trace.get("input_checkpoints", [])
        expected_checkpoints = {
            123: ("POLL", "CONFIRM", "efe04c584b1a88b8", "827f0f1b725a0958"),
            293: ("POLL", "UP", "430dd5b172859efe", "c0ddff17acca830b"),
            345: ("POLL", "DOWN", "86f36d75184bf502", "2ff9efae32900757"),
            515: ("WAIT", "CONFIRM", "6b2035b69889b10f", "004e684bc1b4f164"),
            519: ("POLL", "UP", "ddaf90d252d244fe", "004e684bc1b4f164"),
            610: ("WAIT", "CONFIRM", "51b99d07ba290357", "004e684bc1b4f164"),
            613: ("WAIT", "CONFIRM", "9eb0493a3aff37b2", "004e684bc1b4f164"),
            740: ("WAIT", "CONFIRM", "60f34fa985dcec85", "004e684bc1b4f164"),
            743: ("WAIT", "CONFIRM", "0357174787b81959", "004e684bc1b4f164"),
            821: ("POLL", "CONFIRM", "92ca94492a32a65f", "004e684bc1b4f164"),
            827: ("WAIT", "LEFT", "49c1a0ed9d826c07", "0f51c5416ef6a73d"),
            901: ("WAIT", "CONFIRM", "5f9bf4514bc149bd", "0f51c5416ef6a73d"),
            902: ("POLL", "DOWN", "e776787f6ae6f5a5", "0f51c5416ef6a73d"),
            1346: ("POLL", "CONFIRM", "9ceec50582d9d537", "0f51c5416ef6a73d"),
            1352: ("POLL", "DOWN", "facdcec8072cdd17", "88435907c045bd63"),
            1496: ("POLL", "NONE", "75edd863c474dafc", "88435907c045bd63"),
            1569: ("POLL", "NONE", "6950c6eda9b97cad", "88435907c045bd63"),
            1796: ("POLL", "CONFIRM", "ed84620a2f236f46", "88435907c045bd63"),
            1799: ("POLL", "UP", "ed84620a2f236f46", "ae6c1bca924a54ac"),
            1851: ("POLL", "NONE", "05e3ae1a8a1cf49e", "ae6c1bca924a54ac"),
            1855: ("WAIT", "DOWN", "0bdb7bc7c2296b61", "ae6c1bca924a54ac"),
            1858: ("WAIT", "CONFIRM", "0bdb7bc7c2296b61", "ae6c1bca924a54ac"),
            1859: ("POLL", "DOWN", "e310b387046e727f", "ae6c1bca924a54ac"),
            1922: ("POLL", "NONE", "b7ff851ac0057323", "ae6c1bca924a54ac"),
            1923: ("POLL", "QUIT", "b7ff851ac0057323", "ae6c1bca924a54ac"),
        }
        if len(checkpoints) != 1924:
            raise ValueError("mainline input checkpoint count differs")
        for index, expected in expected_checkpoints.items():
            item = checkpoints[index]
            actual = (
                item.get("boundary"), item.get("action"),
                item.get("state_fnv1a64"), item.get("mapz_fnv1a64"),
            )
            if actual != expected:
                raise ValueError(
                    f"mainline checkpoint {index} is {actual!r}, expected {expected!r}")

        expected_positions = {
            902: (14, 112, 29, 3),
            1346: (10, 59, 78, 6),
            1496: (12, 71, 119, 3),
            1569: (14, 122, 165, 3),
            1796: (14, 84, 29, 6),
            1851: (66, 123, 56, 0),
            1855: (66, 124, 57, 0),
            1922: (74, 96, 153, 3),
            1923: (74, 96, 153, 3),
        }
        for index, expected in expected_positions.items():
            item = checkpoints[index]
            actual = (item.get("map_location"), item.get("world_x"),
                      item.get("world_y"), item.get("actor_direction"))
            if actual != expected:
                raise ValueError(
                    f"mainline world checkpoint {index} is {actual!r}, "
                    f"expected {expected!r}")

        save = args.save.read_bytes()
        mapz = args.mapz.read_bytes()
        name = args.name.read_bytes()
        if len(save) != 1350 or fnv1a64(save) != digests["state_fnv1a64"]:
            raise ValueError("persisted mainline SAVE differs from live state")
        if fnv1a64(mapz) != digests["mapz_fnv1a64"]:
            raise ValueError("persisted mainline MAPZ differs from live database")
        if len(name) != 514 or fnv1a64(name) != digests["name_fnv1a64"]:
            raise ValueError("persisted mainline NAME differs from live font")

        # Flags 0/2/4 come from the village and entrance; entry 200 adds flag 8
        # after the Fire-Eyed Suanni battle. The freed father then adds flag 18
        # in the next story word before the secret mechanism becomes usable.
        if u16(save, 0x4A2) != 0xA880:
            raise ValueError("village/Stronghold/boss story flags are not exact")
        if u16(save, 0x4A4) != 0x2000:
            raise ValueError("freed-father secret-door flag is not exact")
        if save[0x521] != 1:
            raise ValueError("far-side AREA1 travel flag three was not set")
        if u16(save, 0x424) != 74:
            raise ValueError("mainline did not finish in TW-2A directory 74")
        world_x = u16(save, 0x41B) + ((u16(save, 0x012) + 2) >> 1)
        world_y = u16(save, 0x41D) + ((u16(save, 0x02A) + 16) >> 3)
        if (world_x, world_y) != (96, 153):
            raise ValueError(
                f"mainline final T2 position is {(world_x, world_y)!r}")
        if u16(save, 0x10) != 3 or u16(save, 0x104) != 121:
            raise ValueError("boss victory party count or money reward differs")
        expected_party = [
            (0x0000, 28, 59, 1, 36),
            (0x1000, 5, 59, 2, 37),
            (0x3000, 0, 65, 18, 18),
        ]
        for actor, expected in enumerate(expected_party):
            base = 0x106 + actor * 0x9F
            actual = (u16(save, base + 8), u16(save, base + 0x2D),
                      u16(save, base + 0x2F), u16(save, base + 0x55),
                      u16(save, base + 0x57))
            if actual != expected:
                raise ValueError(
                    f"boss victory actor {actor} state is {actual!r}, expected {expected!r}")

        header_size = u16(mapz, 8) * 16
        image = mapz[header_size:]
        if map_field(image, 10, 2) != 28192 or \
                map_field(image, 10, 3) != 0 or \
                map_field(image, 10, 9) != 74:
            raise ValueError("blocking-villager MAPZ mutation differs")
        if map_field(image, 50, 9) != 66:
            raise ValueError("village-chief persistent event redirect differs")
        if map_field(image, 14, 3, 39) != 3 or \
                map_field(image, 14, 9, 39) != 200:
            raise ValueError("Fire-Eyed Suanni was not persistently hidden")
        if map_field(image, 14, 3, 25) != 3 or \
                map_field(image, 14, 3, 38) != 3 or \
                map_field(image, 14, 3, 41) != 3:
            raise ValueError("Stronghold secret mechanism/wall mutation differs")
        if any(map_field(image, 12, 3, entity) != 3 for entity in range(10)):
            raise ValueError("SBOUT one-shot residents/guards were not all hidden")
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--":
            raise ValueError("mainline prefix did not stop at explicit world quit")
    except (OSError, json.JSONDecodeError, TypeError, ValueError) as error:
        print(f"RPG mainline prefix validation: FAIL: {error}", file=sys.stderr)
        return 1
    print(
        "RPG mainline prefix validation: OK "
        "(village -> Fire-Eyed Suanni -> secret passage -> T2 entry)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
