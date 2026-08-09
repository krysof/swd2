#!/usr/bin/env python3
"""Lock all five RPG System speed values and the original right wrap."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    args = parser.parse_args()
    try:
        data = json.loads(args.trace.read_text(encoding="utf-8"))
        if data.get("schema_version") != 1:
            raise ValueError("unsupported trace schema")
        if data.get("input") != {
            "total": 20, "consumed": 20, "remaining": 0,
            "implicit_quit_calls": 0,
        } or data.get("boundaries") != {
            "wait": 19, "poll": 1, "text": 0, "frontend": 105,
        }:
            raise ValueError("System speed input accounting differs")
        video = data.get("video", {})
        if video != {
            "frames": 125, "direct_updates": 0, "last_width": 320,
            "last_height": 200, "fnv1a64": "f76add407cedee25",
        }:
            raise ValueError("System speed video summary differs")
        expected_pages = [
            "1c34596f4dbbac5b", "4e869b29e122695b",
            "f16de67152c3a55b", "445c73f8d1b4945b",
            "310502c4797b465b", "1c34596f4dbbac5b",
        ]
        hashes = data.get("frame_fnv1a64", [])
        if len(hashes) != 125 or hashes[119:125] != expected_pages:
            raise ValueError("System speed pages or right wrap differ")
        if (data.get("state_fnv1a64"), data.get("mapz_fnv1a64"),
                data.get("name_fnv1a64")) != (
                    "54098cf0ca14338b", "827f0f1b725a0958",
                    "e3d2853e2676513b"):
            raise ValueError("unconfirmed speed selector changed the save triple")
        if data.get("stop_reason") != "module requested exit" or \
                data.get("final_marker") != "--":
            raise ValueError("System speed replay did not stop explicitly")
        print("RPG System speed: values 1..5 and Right wrap locked")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG System speed: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
