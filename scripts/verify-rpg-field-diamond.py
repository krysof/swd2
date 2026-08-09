#!/usr/bin/env python3
"""Lock the original spatial mapping of the RPG field-menu diamond."""

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
            "total": 13,
            "consumed": 13,
            "remaining": 0,
            "implicit_quit_calls": 0,
        }:
            raise ValueError("field-diamond input accounting differs")
        if data.get("boundaries") != {
            "wait": 12,
            "poll": 1,
            "text": 0,
            "frontend": 105,
        }:
            raise ValueError("field-diamond input boundaries differ")
        video = data.get("video", {})
        if video.get("frames") != 118 or \
                video.get("last_width") != 320 or \
                video.get("last_height") != 200 or \
                video.get("fnv1a64") != "a6313b86d5eac036":
            raise ValueError("field-diamond video summary differs")
        hashes = data.get("frame_fnv1a64", [])
        if len(hashes) != 118 or hashes[113:118] != [
                "1b0a18f362ed2cb2",  # initial/up: System/Book
                "ea4e949938110458",  # left: Magic
                "1b0a18f362ed2cb2",  # up: System/Book
                "a2bc9fa38fe088af",  # down: Status
                "ddb26857dd11b4f3",  # right: Item
        ]:
            raise ValueError("field-diamond directional pages differ")
        if (data.get("state_fnv1a64"), data.get("mapz_fnv1a64"),
                data.get("name_fnv1a64")) != (
                    "54098cf0ca14338b", "827f0f1b725a0958",
                    "e3d2853e2676513b"):
            raise ValueError("field-diamond replay changed the save triple")
        print("RPG field diamond: left Magic, right Item, up Book, down Status")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG field diamond: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
