#!/usr/bin/env python3
"""Lock the released first-village main-story prefix and persisted state."""

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
            ("RPG.EXE", "OM", "--", True),
        ]
        transitions = [
            (item.get("module"), item.get("input"), item.get("output"),
             item.get("launched"))
            for item in trace.get("transitions", [])
        ]
        if transitions != expected_transitions:
            raise ValueError(f"mainline transitions differ: {transitions!r}")

        expected_values = {
            ("input", "total"): 347,
            ("input", "consumed"): 347,
            ("input", "remaining"): 0,
            ("input", "implicit_quit_calls"): 0,
            ("boundaries", "wait"): 5,
            ("boundaries", "poll"): 342,
            ("boundaries", "text"): 38,
            ("video", "frames"): 2134,
            ("video", "direct_updates"): 1401,
            ("video", "fnv1a64"): "d2ba1f560eb01bd6",
            ("audio", "music_calls"): 7,
            ("audio", "voice_calls"): 0,
            ("audio", "stop_audio_calls"): 4,
            ("audio", "fnv1a64"): "185d19f46686aa62",
        }
        for (section, key), expected in expected_values.items():
            actual = trace.get(section, {}).get(key)
            if actual != expected:
                raise ValueError(
                    f"mainline {section}.{key} is {actual!r}, expected {expected!r}")
        if trace.get("delay_milliseconds") != 59743:
            raise ValueError("mainline cumulative 70-Hz timing differs")

        digests = {
            "state_fnv1a64": "86f36d75184bf502",
            "mapz_fnv1a64": "2ff9efae32900757",
            "name_fnv1a64": "e3d2853e2676513b",
        }
        for key, expected in digests.items():
            if trace.get(key) != expected:
                raise ValueError(f"mainline {key} differs")
        frames = trace.get("frame_fnv1a64", [])
        if len(frames) != 2134 or frames[-1] != "1566f5112e47c999":
            raise ValueError("mainline final SWRO4 world frame differs")

        checkpoints = trace.get("input_checkpoints", [])
        expected_checkpoints = {
            123: ("POLL", "CONFIRM", "efe04c584b1a88b8",
                  "827f0f1b725a0958"),
            127: ("POLL", "DOWN", "6a3c1ccedd6db258",
                  "827f0f1b725a0958"),
            285: ("POLL", "CONFIRM", "430dd5b172859efe",
                  "827f0f1b725a0958"),
            293: ("POLL", "UP", "430dd5b172859efe",
                  "c0ddff17acca830b"),
            323: ("POLL", "CONFIRM", "d2adef9285728fd4",
                  "c0ddff17acca830b"),
            345: ("POLL", "NONE", "86f36d75184bf502",
                  "2ff9efae32900757"),
            346: ("POLL", "QUIT", "86f36d75184bf502",
                  "2ff9efae32900757"),
        }
        if len(checkpoints) != 347:
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

        save = args.save.read_bytes()
        mapz = args.mapz.read_bytes()
        name = args.name.read_bytes()
        if len(save) != 1350 or fnv1a64(save) != digests["state_fnv1a64"]:
            raise ValueError("persisted mainline SAVE differs from live state")
        if fnv1a64(mapz) != digests["mapz_fnv1a64"]:
            raise ValueError("persisted mainline MAPZ differs from live database")
        if len(name) != 514 or fnv1a64(name) != digests["name_fnv1a64"]:
            raise ValueError("persisted mainline NAME differs from live font")

        # Story flags use the original high-bit-first layout. The innkeeper
        # sets flag 2 (2000h); the chief sets flag 4 (0800h).
        if u16(save, 0x4A2) != 0x2800:
            raise ValueError("innkeeper/chief story flags 2 and 4 are not exact")
        if u16(save, 0x424) != 50:
            raise ValueError("mainline did not finish in SWRO4 directory 50")
        world_x = u16(save, 0x41B) + ((u16(save, 0x012) + 2) >> 1)
        world_y = u16(save, 0x41D) + ((u16(save, 0x02A) + 16) >> 3)
        if (world_x, world_y) != (56, 16):
            raise ValueError(
                f"mainline final chief-house position is {(world_x, world_y)!r}")

        header_size = u16(mapz, 8) * 16
        image = mapz[header_size:]
        # Entry 72 moves the blocking actor two cells east and persists its
        # new event/behavior in MA-DE. Entry 64 redirects the chief to 66.
        if map_field(image, 10, 2) != 28192 or \
                map_field(image, 10, 3) != 0 or \
                map_field(image, 10, 9) != 74:
            raise ValueError("blocking-villager MAPZ mutation differs")
        if map_field(image, 50, 9) != 66:
            raise ValueError("village-chief persistent event redirect differs")
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--":
            raise ValueError("mainline prefix did not stop at explicit world quit")
    except (OSError, json.JSONDecodeError, TypeError, ValueError) as error:
        print(f"RPG mainline prefix validation: FAIL: {error}", file=sys.stderr)
        return 1
    print("RPG mainline prefix validation: OK (innkeeper -> blocker -> chief)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
