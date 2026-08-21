#!/usr/bin/env python3
"""Lock the continuous pre-Jianmu TREEA training victory checkpoint."""

from __future__ import annotations

import json
import sys
from pathlib import Path

FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3


def u16(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 2 > len(data):
        raise ValueError(f"word {offset:#x} is outside {len(data)} bytes")
    return data[offset] | data[offset + 1] << 8


def fnv1a64(data: bytes) -> str:
    value = FNV_OFFSET
    for byte in data:
        value ^= byte
        value = (value * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return f"{value:016x}"


def main() -> int:
    if len(sys.argv) != 5:
        raise SystemExit("usage: verify-rpg-mainline-training.py TRACE SAVE MAPZ NAME")
    trace_path, save_path, mapz_path, name_path = map(Path, sys.argv[1:])
    try:
        trace = json.loads(trace_path.read_text(encoding="utf-8"))
        if trace.get("input") != {
            "total": 12026,
            "consumed": 12026,
            "remaining": 0,
            "implicit_quit_calls": 0,
        }:
            raise ValueError("strict training input totals changed")
        expected_sections = {
            "boundaries": {
                "wait": 1680,
                "poll": 10383,
                "text": 783,
                "frontend": 90341,
            },
            "video": {
                "frames": 36344,
                "direct_updates": 5401,
                "last_width": 320,
                "last_height": 200,
                "fnv1a64": "d5e99a6d1e5a1193",
            },
            "audio": {
                "music_calls": 337,
                "voice_calls": 726,
                "stop_music_calls": 8,
                "stop_audio_calls": 150,
                "fnv1a64": "2d1f75960d006bc2",
            },
        }
        for section, expected in expected_sections.items():
            if trace.get(section) != expected:
                raise ValueError(f"training {section} differs")
        if trace.get("delay_milliseconds") != 1639853:
            raise ValueError("training fixed-tick total changed")
        digests = {
            "state_fnv1a64": "2b09e8817c924e53",
            "mapz_fnv1a64": "8853834e394c715a",
            "name_fnv1a64": "e3d2853e2676513b",
        }
        for key, expected in digests.items():
            if trace.get(key) != expected:
                raise ValueError(f"training {key} differs")
        frames = trace.get("frame_fnv1a64", [])
        if len(frames) != 36344 or frames[-1] != "bb5c135e8a258475":
            raise ValueError("training final world frame differs")

        expected_transitions = [
            ("MEO.EXE", "--", "MT", True),
            ("RPG.EXE", "MT", "ED", True),
            ("DEMO.EXE", "ED", "--", True),
        ]
        for index in range(73):
            expected_transitions.extend([
                ("RPG.EXE", "OM" if index == 0 else "OC", "IF", True),
                ("FIG.EXE", "IF", "OC", True),
            ])
        expected_transitions.append(("RPG.EXE", "OC", "--", True))
        actual_transitions = [
            (entry.get("module"), entry.get("input"), entry.get("output"),
             entry.get("launched"))
            for entry in trace.get("transitions", [])
        ]
        if actual_transitions != expected_transitions:
            raise ValueError("training module chain differs")

        checkpoints = trace.get("input_checkpoints", [])
        if len(checkpoints) != 12026:
            raise ValueError("training checkpoint count differs")
        final = checkpoints[-1]
        if (final.get("boundary"), final.get("action"),
                final.get("map_location"), final.get("world_x"),
                final.get("world_y"), final.get("actor_direction"),
                final.get("state_fnv1a64"), final.get("mapz_fnv1a64")) != (
                    "POLL", "QUIT", 228, 96, 115, 9,
                    digests["state_fnv1a64"], digests["mapz_fnv1a64"]):
            raise ValueError("training final input boundary differs")

        save = save_path.read_bytes()
        mapz = mapz_path.read_bytes()
        name = name_path.read_bytes()
        if len(save) != 1350 or fnv1a64(save) != digests["state_fnv1a64"]:
            raise ValueError("persisted training SAVE differs")
        if fnv1a64(mapz) != digests["mapz_fnv1a64"]:
            raise ValueError("persisted training MAPZ differs")
        if len(name) != 514 or fnv1a64(name) != digests["name_fnv1a64"]:
            raise ValueError("persisted training NAME differs")
        if u16(save, 0x424) != 228 or u16(save, 0x10) != 4 or \
                u16(save, 0x104) != 4794 or u16(save, 0x49c) != 0x1470 or \
                u16(save, 0x51a) != 0x0004 or u16(save, 0x51c) != 0:
            raise ValueError("training world/battle continuation fields differ")
        expected_party = [
            (0x0000, 156, 156, 17, 122, 122, 103, 843, 94, 94),
            (0x0000, 150, 150, 17, 46, 46, 103, 842, 99, 99),
            (0x0000, 56, 161, 17, 12, 140, 375, 847, 46, 46),
            (0x3000, 0, 140, 16, 46, 46, 627, 702, 0, 84),
        ]
        for actor, expected in enumerate(expected_party):
            base = 0x106 + actor * 0x9f
            actual = tuple(u16(save, base + offset) for offset in (
                8, 0x2d, 0x2f, 0x31, 0x35, 0x37, 0x39, 0x3b, 0x55, 0x57))
            if actual != expected:
                raise ValueError(
                    f"training actor {actor} is {actual!r}, expected {expected!r}")
        if trace.get("stop_reason") != "module requested exit" or \
                trace.get("final_marker") != "--":
            raise ValueError("training replay did not use its explicit world quit")
    except (OSError, json.JSONDecodeError, TypeError, ValueError) as error:
        print(f"RPG mainline training validation: FAIL: {error}", file=sys.stderr)
        return 1
    print("RPG mainline training validation: OK (continuous legal TREEA victory)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
