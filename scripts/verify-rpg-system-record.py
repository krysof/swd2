#!/usr/bin/env python3
"""Lock all RPG Record slot pages, wrap, and confirmation."""

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
            raise ValueError("System Record input accounting differs")
        video = data.get("video", {})
        if video != {
            "frames": 125, "direct_updates": 0, "last_width": 320,
            "last_height": 200, "fnv1a64": "b93bc9d7cd9b834a",
        }:
            raise ValueError("System Record video summary differs")
        expected_pages = [
            "bb6c1c1eb08afee5", "0be5a2169759e7e5",
            "6d28b980162ecde5", "d3fd437fc1f404e5",
            "ef30dc699230e4e5", "bb6c1c1eb08afee5",
            "030c86d1e924d13e",
        ]
        hashes = data.get("frame_fnv1a64", [])
        if len(hashes) != 125 or hashes[118:125] != expected_pages:
            raise ValueError("Record slots, wrap, or confirmation differs")
        if (data.get("state_fnv1a64"), data.get("mapz_fnv1a64"),
                data.get("name_fnv1a64")) != (
                    "54098cf0ca14338b", "827f0f1b725a0958",
                    "e3d2853e2676513b"):
            raise ValueError("unconfirmed Record changed the save triple")
        if data.get("stop_reason") != "module requested exit" or \
                data.get("final_marker") != "--":
            raise ValueError("System Record replay did not stop explicitly")
        print("RPG System Record: 5 slots, wrap and default Yes locked")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG System Record: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
