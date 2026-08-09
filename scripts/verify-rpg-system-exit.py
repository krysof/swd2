#!/usr/bin/env python3
"""Lock the RPG Exit-DOS confirmation and frontend-close boundary."""

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
            "total": 18, "consumed": 18, "remaining": 0,
            "implicit_quit_calls": 0,
        } or data.get("boundaries") != {
            "wait": 16, "poll": 1, "text": 1, "frontend": 105,
        }:
            raise ValueError("System exit input accounting differs")
        video = data.get("video", {})
        if video != {
            "frames": 124, "direct_updates": 2, "last_width": 320,
            "last_height": 200, "fnv1a64": "0d5873d95376d16c",
        }:
            raise ValueError("System exit video summary differs")
        hashes = data.get("frame_fnv1a64", [])
        if len(hashes) != 124 or hashes[123] != "74ce605967ae400a":
            raise ValueError("System Exit-DOS confirmation page differs")
        if (data.get("state_fnv1a64"), data.get("mapz_fnv1a64"),
                data.get("name_fnv1a64")) != (
                    "54098cf0ca14338b", "827f0f1b725a0958",
                    "e3d2853e2676513b"):
            raise ValueError("frontend close changed the save triple")
        if data.get("stop_reason") != "module requested exit" or \
                data.get("final_marker") != "--":
            raise ValueError("System exit close was not propagated")
        print("RPG System Exit DOS: confirmation page and close locked")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG System Exit DOS: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
