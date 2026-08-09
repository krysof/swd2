#!/usr/bin/env python3
"""Lock the original spatial mapping of the RPG field-menu diamond."""

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
        frames = load_indexed_frames(args.frames)
        if len(frames) != 118:
            raise ValueError("field-diamond frame capture count differs")
        checks = [
            (113, (64, 8, 64, 32),
             "a6b4365b34aaddae36745b25e9d3d579533960e585b9da7d3dd74fdcc5b7f690",
             "47f383adb80ae2008635626450fcf6e0e879fd4fa09f09152d907c5b38b3b92c"),
            (114, (24, 40, 64, 32),
             "b60f03bc42de2c947ed309278afb8d5bf02478ee0f1aeac06968364ab9de50f3",
             "98be752de1f5c94c9c414e70ca22c64c08fd59c57b54f5be182da4a2a8a95f18"),
            (115, (64, 8, 64, 32),
             "a6b4365b34aaddae36745b25e9d3d579533960e585b9da7d3dd74fdcc5b7f690",
             "47f383adb80ae2008635626450fcf6e0e879fd4fa09f09152d907c5b38b3b92c"),
            (116, (64, 72, 64, 32),
             "def7fede73164b09e8ee25c9d20ed7d1ab114e9aeca5cf4137dd26eb34afd771",
             "d550bc4d0d5f37fd91d349b02e2a2556bf2266832226a812f32ce41478225ecc"),
            (117, (104, 40, 64, 32),
             "5c905d9d571643eb5172fca917e826cc1ac40167e6c49049b664cc9c45e4ff09",
             "15dbaed072322fb3c405f10c9f635279eba51155d6affeeafe1ee3e0d8cbe66d"),
        ]
        palette_sha256 = \
            "1b620870840c937d4f0739fc9706df59594a348a143139cf6934d30c962c0960"
        for frame_number, box, expected_indices, expected_rgb in checks:
            pixels, palette = frames[frame_number]
            indexed = crop_indexed(pixels, box)
            if hashlib.sha256(palette).hexdigest() != palette_sha256:
                raise ValueError(f"field-diamond frame {frame_number} palette differs")
            if hashlib.sha256(indexed).hexdigest() != expected_indices:
                raise ValueError(f"field-diamond frame {frame_number} indices differ")
            if hashlib.sha256(expand_rgb(indexed, palette)).hexdigest() != expected_rgb:
                raise ValueError(f"field-diamond frame {frame_number} RGB differs")
        if (data.get("state_fnv1a64"), data.get("mapz_fnv1a64"),
                data.get("name_fnv1a64")) != (
                    "54098cf0ca14338b", "827f0f1b725a0958",
                    "e3d2853e2676513b"):
            raise ValueError("field-diamond replay changed the save triple")
        print(
            "RPG field diamond: left Magic, right Item, up Book, down Status; "
            "all selected cards locked as indexed VGA and exact original RGB")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG field diamond: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
