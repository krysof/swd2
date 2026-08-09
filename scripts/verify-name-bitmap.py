#!/usr/bin/env python3
"""Lock the deterministic RPG:1880 name-bitmap presentation checkpoint."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    args = parser.parse_args()
    try:
        data = json.loads(args.trace.read_text(encoding="utf-8"))
        expected_transitions = [
            ("MEO.EXE", "--", "MT"),
            ("RPG.EXE", "MT", "ED"),
            ("DEMO.EXE", "ED", "--"),
            ("RPG.EXE", "OM", "--"),
        ]
        transitions = [
            (entry.get("module"), entry.get("input"), entry.get("output"))
            for entry in data.get("transitions", [])
        ]
        if transitions != expected_transitions:
            raise ValueError("name-bitmap module transitions differ")
        if data.get("input") != {
            "total": 17,
            "consumed": 17,
            "remaining": 0,
            "implicit_quit_calls": 0,
        }:
            raise ValueError("name-bitmap input accounting differs")
        if data.get("boundaries", {}).get("wait") != 15 or \
                data.get("boundaries", {}).get("poll") != 2:
            raise ValueError("name-bitmap input boundaries differ")
        video = data.get("video", {})
        if video.get("frames") != 262 or video.get("direct_updates") != 37:
            raise ValueError("name-bitmap page/timeline counts differ")
        frame_hashes = data.get("frame_fnv1a64", [])
        expected_frames = {
            109: "86d24d180442b751",  # first character page
            110: "752a7015a76b4413",  # second character page
            111: "d9e2eb6b2db2ee87",  # third character page
            113: "86d24d180442b751",  # PageUp returned to first
            118: "7af8c6a490382b9d",  # 16x15 bitmap editor
        }
        if len(frame_hashes) <= max(expected_frames) or any(
                frame_hashes[index] != expected
                for index, expected in expected_frames.items()):
            raise ValueError("RPG:1880 default bitmap frame differs")
        if data.get("name_fnv1a64") != "e3d2853e2676513b":
            raise ValueError("cancelled name bitmap unexpectedly changed NAME.DSK")
        if data.get("state_fnv1a64") != "1693cf52a3bbdad7" or \
                data.get("mapz_fnv1a64") != "827f0f1b725a0958":
            raise ValueError("name-bitmap replay changed startup world state")
        if data.get("stop_reason") != "module requested exit" or \
                data.get("final_marker") != "--":
            raise ValueError("name-bitmap replay did not reach its explicit quit")
    except (OSError, json.JSONDecodeError, TypeError, ValueError) as error:
        print(f"RPG name-bitmap validation: FAIL: {error}", file=sys.stderr)
        return 1
    print("RPG name-bitmap validation: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
