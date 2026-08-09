#!/usr/bin/env python3
"""Lock all RPG Read slot pages, wrap, and confirmation."""

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
            "total": 19, "consumed": 19, "remaining": 0,
            "implicit_quit_calls": 0,
        } or data.get("boundaries") != {
            "wait": 18, "poll": 1, "text": 0, "frontend": 105,
        }:
            raise ValueError("System Read input accounting differs")
        video = data.get("video", {})
        if video != {
            "frames": 124, "direct_updates": 0, "last_width": 320,
            "last_height": 200, "fnv1a64": "827bb14c48a5b180",
        }:
            raise ValueError("System Read video summary differs")
        expected_pages = [
            "3ea28e630ccb7b10", "0c0561e7c985a810",
            "9c55a63181e9c510", "b4b763cb9ab13c10",
            "7bd32231d3932310", "3ea28e630ccb7b10",
            "b8a18bfdc880f0d3",
        ]
        hashes = data.get("frame_fnv1a64", [])
        if len(hashes) != 124 or hashes[117:124] != expected_pages:
            raise ValueError("Read slots, wrap, or confirmation differs")
        if (data.get("state_fnv1a64"), data.get("mapz_fnv1a64"),
                data.get("name_fnv1a64")) != (
                    "54098cf0ca14338b", "827f0f1b725a0958",
                    "e3d2853e2676513b"):
            raise ValueError("unconfirmed Read changed the save triple")
        if data.get("stop_reason") != "module requested exit" or \
                data.get("final_marker") != "--":
            raise ValueError("System Read replay did not stop explicitly")
        print("RPG System Read: 5 slots, wrap and default Yes locked")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG System Read: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
