#!/usr/bin/env python3
"""Lock the deterministic RPG music/sound System toggle replay."""

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
            "total": 13, "consumed": 13, "remaining": 0,
            "implicit_quit_calls": 0,
        } or data.get("boundaries") != {
            "wait": 12, "poll": 1, "text": 0, "frontend": 105,
        }:
            raise ValueError("System toggle input accounting differs")
        video = data.get("video", {})
        if video != {
            "frames": 118, "direct_updates": 0, "last_width": 320,
            "last_height": 200, "fnv1a64": "d0e9dc3b826cd1ec",
        }:
            raise ValueError("System toggle video summary differs")
        expected_pages = [
            "2f11d877c82ae183", "5b42982322dcd8c3",
            "4d28192901f7a0e3", "05e47e89987f145f",
        ]
        hashes = data.get("frame_fnv1a64", [])
        if len(hashes) != 118 or hashes[114:118] != expected_pages:
            raise ValueError("System toggle pages differ")
        if data.get("audio") != {
            "music_calls": 2, "voice_calls": 0,
            "stop_music_calls": 1, "stop_audio_calls": 2,
            "fnv1a64": "9d6b5970656ebc0f",
        }:
            raise ValueError("System toggle audio call sequence differs")
        if (data.get("state_fnv1a64"), data.get("mapz_fnv1a64"),
                data.get("name_fnv1a64")) != (
                    "0311d19e2eb0fb4d", "827f0f1b725a0958",
                    "e3d2853e2676513b"):
            raise ValueError("System toggle final save triple differs")
        if data.get("stop_reason") != "module requested exit" or \
                data.get("final_marker") != "--":
            raise ValueError("System toggle replay did not stop explicitly")
        print("RPG System toggles: visible states, save flags and audio locked")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG System toggles: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
