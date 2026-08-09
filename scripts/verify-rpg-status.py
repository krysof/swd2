#!/usr/bin/env python3
"""Lock the RPG Status page and all three ME01 strip-scroll positions."""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


MAGIC = b"SWD2FRM2"
FNV_OFFSET = 1469598103934665603
FNV_PRIME = 1099511628211
FNV_MASK = (1 << 64) - 1


def load_indexed_frames(path: Path) -> list[bytes]:
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
    frames: list[bytes] = []
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
            raise ValueError("unexpected Status frame header")
        frames.append(take(320 * 200, "indexed pixels"))
        take(768, "VGA palette")


def crop_fnv(pixels: bytes, crop: tuple[int, int, int, int]) -> str:
    x, y, width, height = crop
    value = FNV_OFFSET
    for row in range(y, y + height):
        for pixel in pixels[row * 320 + x:row * 320 + x + width]:
            value ^= pixel
            value = (value * FNV_PRIME) & FNV_MASK
    return f"{value:016x}"


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
            "total": 14,
            "consumed": 14,
            "remaining": 0,
            "implicit_quit_calls": 0,
        }:
            raise ValueError("Status replay input accounting differs")
        if data.get("boundaries") != {
            "wait": 13,
            "poll": 1,
            "text": 0,
            "frontend": 105,
        }:
            raise ValueError("Status replay input boundaries differ")
        video = data.get("video", {})
        if video.get("frames") != 119 or \
                video.get("last_width") != 320 or \
                video.get("last_height") != 200 or \
                video.get("fnv1a64") != "b923f32764b54353":
            raise ValueError("Status replay video summary differs")
        hashes = data.get("frame_fnv1a64", [])
        if len(hashes) != 119 or hashes[113:119] != [
                "1b0a18f362ed2cb2",  # System/Book-selected field diamond
                "a2bc9fa38fe088af",  # down/Status-selected field diamond
                "9926d3f4783f8138",  # actor-zero selector
                "edebf73491c53a2a",  # rows 0..7 / ME01 top strips
                "c354b5714d577d76",  # rows 8..15 / ME01 lower strips
                "0d41b4774d768489",  # rows 16..23 / equipment values
        ]:
            raise ValueError("Status selection/page frames differ")
        if (data.get("state_fnv1a64"), data.get("mapz_fnv1a64"),
                data.get("name_fnv1a64")) != (
                    "54098cf0ca14338b", "827f0f1b725a0958",
                    "e3d2853e2676513b"):
            raise ValueError("Status replay changed the loaded save triple")

        frames = load_indexed_frames(args.frames)
        if len(frames) != 119:
            raise ValueError("Status frame capture count differs")
        expected = [
            (116, "40163683a334d336"),
            (117, "01c6ea8a17af29ba"),
            (118, "af41fb6401b133dd"),
        ]
        crop = (96, 40, 224, 144)
        for frame, digest in expected:
            if crop_fnv(frames[frame], crop) != digest:
                raise ValueError(f"Status indexed page differs: {frame}")

        print(
            "RPG Status checkpoint: 119 frames, three list pages and "
            "ME01 strip positions locked")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG Status checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
