#!/usr/bin/env python3
"""Lock the RPG inventory replay and its original grayscale-page boundary."""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


MAGIC = b"SWD2FRM2"


def load_frames(path: Path) -> list[tuple[bytes, bytes]]:
    data = path.read_bytes()
    cursor = 0

    def take(count: int, label: str) -> bytes:
        nonlocal cursor
        result = data[cursor:cursor + count]
        if len(result) != count:
            raise ValueError(f"truncated {label}")
        cursor += count
        return result

    if take(8, "capture magic") != MAGIC:
        raise ValueError("unsupported frame capture")
    input_count = struct.unpack("<Q", take(8, "input count"))[0]
    take(input_count * 2, "input sequence")
    frames: list[tuple[bytes, bytes]] = []
    while True:
        tag = take(4, "record tag")
        if tag == b"DONE":
            recorded = struct.unpack("<Q", take(8, "frame count"))[0]
            if recorded != len(frames) or cursor != len(data):
                raise ValueError("invalid frame capture trailer")
            return frames
        if tag != b"FRAM":
            raise ValueError(f"unknown frame record {tag!r}")
        width, height, direct, reserved = struct.unpack(
            "<IIB3s", take(12, "frame header"))
        if (width, height, direct, reserved) != (320, 200, 0, b"\0\0\0"):
            raise ValueError("unexpected inventory frame header")
        pixels = take(320 * 200, "indexed pixels")
        palette = take(768, "VGA palette")
        frames.append((pixels, palette))


def translated(color: int, palette: bytes) -> int:
    # RPG:0dbf:0314 preserves its 16-entry grayscale target ramp and
    # translates every other source index through the saved DAC palette.
    if 0x10 <= color < 0x20:
        return color
    red, green, blue = palette[color * 3:color * 3 + 3]
    return 0x1f - ((red + green * 2 + blue) >> 4)


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
            "total": 12,
            "consumed": 12,
            "remaining": 0,
            "implicit_quit_calls": 0,
        }:
            raise ValueError("inventory replay input accounting differs")
        if data.get("boundaries") != {
            "wait": 11,
            "poll": 1,
            "text": 0,
            "frontend": 105,
        }:
            raise ValueError("inventory replay input boundaries differ")
        video = data.get("video", {})
        if video.get("frames") != 117 or \
                video.get("last_width") != 320 or \
                video.get("last_height") != 200 or \
                video.get("fnv1a64") != "e1151f6084b2e4f6":
            raise ValueError("inventory replay video summary differs")
        hashes = data.get("frame_fnv1a64", [])
        if len(hashes) != 117 or hashes[113:117] != [
                "a2bc9fa38fe088af",  # System-selected field diamond
                "ddb26857dd11b4f3",  # Item-selected field diamond
                "b39e6c5808648ed7",  # inventory row zero
                "ee0161595a5de622",  # inventory row one
        ]:
            raise ValueError("inventory selection pages differ")
        if (data.get("state_fnv1a64"), data.get("mapz_fnv1a64"),
                data.get("name_fnv1a64")) != (
                    "54098cf0ca14338b", "827f0f1b725a0958",
                    "e3d2853e2676513b"):
            raise ValueError("inventory replay changed the loaded save triple")

        frames = load_frames(args.frames)
        if len(frames) != 117:
            raise ValueError("inventory frame capture count differs")
        source_pixels, source_palette = frames[114]
        inventory_pixels, inventory_palette = frames[115]
        if source_palette != inventory_palette:
            raise ValueError("inventory unexpectedly changed the VGA palette")
        checked: set[int] = set()
        # These unobscured top and left strips remain the same world page
        # between the Item-selected diamond and the first inventory page.
        for y in range(8):
            checked.update(range(y * 320, (y + 1) * 320))
        for y in range(8, 200):
            checked.update(range(y * 320, y * 320 + 8))
        mismatched = sum(
            translated(source_pixels[index], source_palette) !=
            inventory_pixels[index]
            for index in checked)
        if mismatched:
            raise ValueError(
                f"inventory grayscale world boundary differs at "
                f"{mismatched}/{len(checked)} unobscured pixels")

        print(
            "RPG inventory checkpoint: 117 frames, two rows locked, "
            f"{len(checked)} grayscale world pixels exact")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG inventory checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
