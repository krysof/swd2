#!/usr/bin/env python3
"""Lock the RPG Exit-DOS confirmation and frontend-close boundary."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from swd2_frame_capture import crop_indexed, expand_rgb, load_indexed_frames


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("frames", type=Path)
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
        frames = load_indexed_frames(args.frames)
        if len(frames) != 124:
            raise ValueError("System Exit-DOS frame capture count differs")
        pixels, palette = frames[123]
        indexed = crop_indexed(pixels, (16, 116, 284, 80))
        if hashlib.sha256(palette).hexdigest() != \
                "1b620870840c937d4f0739fc9706df59594a348a143139cf6934d30c962c0960":
            raise ValueError("System Exit-DOS VGA palette differs")
        if hashlib.sha256(indexed).hexdigest() != \
                "8e7ee7216b945d22efa0e888baed3ec3d2691680182939e0a1b8f4977743f702":
            raise ValueError("System Exit-DOS indexed crop differs")
        if hashlib.sha256(expand_rgb(indexed, palette)).hexdigest() != \
                "c7aaff73e52530dbf6b43e0359356c46db856ac41434cbe4fc53def44282a074":
            raise ValueError("System Exit-DOS original RGB crop differs")
        if (data.get("state_fnv1a64"), data.get("mapz_fnv1a64"),
                data.get("name_fnv1a64")) != (
                    "54098cf0ca14338b", "827f0f1b725a0958",
                    "e3d2853e2676513b"):
            raise ValueError("frontend close changed the save triple")
        if data.get("stop_reason") != "module requested exit" or \
                data.get("final_marker") != "--":
            raise ValueError("System exit close was not propagated")
        print(
            "RPG System Exit DOS: indexed/RGB confirmation page and close locked")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG System Exit DOS: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
