#!/usr/bin/env python3
"""Lock the deterministic replay point used by the RPG System RGB check."""

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
            "total": 16,
            "consumed": 16,
            "remaining": 0,
            "implicit_quit_calls": 0,
        }:
            raise ValueError("System replay input accounting differs")
        if data.get("boundaries") != {
            "wait": 15,
            "poll": 1,
            "text": 0,
            "frontend": 105,
        }:
            raise ValueError("System replay input boundaries differ")
        video = data.get("video", {})
        if video.get("frames") != 121 or \
                video.get("last_width") != 320 or \
                video.get("last_height") != 200 or \
                video.get("fnv1a64") != "34eb753a9797c178":
            raise ValueError("System replay video summary differs")
        hashes = data.get("frame_fnv1a64", [])
        selected_pages = [
            "2f11d877c82ae183", "1ac0bc30df3f7f9f",
            "8b59c9244548ce1b", "0fd0d81b7ef7d37e",
            "97818ae38ba6ae38", "9b171870e29e9216",
            "c6a000c8447515af",
        ]
        if len(hashes) != 121 or hashes[114:121] != selected_pages:
            raise ValueError("RPG System selected-row pages differ")
        if (data.get("state_fnv1a64"), data.get("mapz_fnv1a64"),
                data.get("name_fnv1a64")) != (
                    "54098cf0ca14338b", "827f0f1b725a0958",
                    "e3d2853e2676513b"):
            raise ValueError("System replay changed the loaded save triple")
        expected_transitions = [
            {"module": "MEO.EXE", "input": "--", "output": "MT",
             "launched": True},
            {"module": "RPG.EXE", "input": "MT", "output": "--",
             "launched": True},
        ]
        if data.get("transitions") != expected_transitions:
            raise ValueError("System replay module transitions differ")
        if data.get("stop_reason") != "module requested exit" or \
                data.get("final_marker") != "--":
            raise ValueError("System replay did not stop at the explicit quit")
        print("RPG System menu checkpoint: 121 frames, all 7 rows locked")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG System menu checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
