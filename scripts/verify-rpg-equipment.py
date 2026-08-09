#!/usr/bin/env python3
"""Lock the reconstructed RPG equipment page and its original RGB crop."""

from __future__ import annotations

import argparse
import hashlib
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
            raise ValueError("unexpected equipment frame header")
        frames.append((
            take(320 * 200, "indexed pixels"),
            take(768, "VGA palette"),
        ))


def crop(pixels: bytes, top: int, height: int) -> bytes:
    return b"".join(
        pixels[y * 320:(y + 1) * 320]
        for y in range(top, top + height))


def rgb(indexed: bytes, palette: bytes) -> bytes:
    output = bytearray(len(indexed) * 3)
    for index, color in enumerate(indexed):
        for component in range(3):
            value = palette[color * 3 + component]
            output[index * 3 + component] = (value << 2) | (value >> 4)
    return bytes(output)


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
            raise ValueError("equipment replay input accounting differs")
        if data.get("boundaries") != {
            "wait": 12,
            "poll": 1,
            "text": 0,
            "frontend": 105,
        }:
            raise ValueError("equipment replay input boundaries differ")
        video = data.get("video", {})
        if video.get("frames") != 118 or \
                video.get("last_width") != 320 or \
                video.get("last_height") != 200 or \
                video.get("fnv1a64") != "5a6abbcb02b0cb29":
            raise ValueError("equipment replay video summary differs")
        hashes = data.get("frame_fnv1a64", [])
        if len(hashes) != 118 or hashes[113:118] != [
                "1b0a18f362ed2cb2",  # System/Book-selected field diamond
                "ddb26857dd11b4f3",  # Item-selected field diamond
                "6fef253f12fc36a0",  # inventory row zero
                "009cb8b1749a4f5a",  # Equip action card / actor zero
                "272dc4c3740a8688",  # stable equipment page
        ]:
            raise ValueError("equipment entry frames differ")
        if (data.get("state_fnv1a64"), data.get("mapz_fnv1a64"),
                data.get("name_fnv1a64")) != (
                    "54098cf0ca14338b", "827f0f1b725a0958",
                    "e3d2853e2676513b"):
            raise ValueError("equipment replay changed the loaded save triple")

        frames = load_frames(args.frames)
        if len(frames) != 118:
            raise ValueError("equipment frame capture count differs")
        pixels, palette = frames[117]
        if hashlib.sha256(palette).hexdigest() != \
                "0aed89315649f14c1f64a4d2f2a701afc996fc08d9399875c159763af408ac03":
            raise ValueError("equipment CD000/map palette splice differs")
        stable = crop(pixels, 4, 192)
        if hashlib.sha256(stable).hexdigest() != \
                "0865ea6b0faa5e177fd247811e57f94a4c4626f912e70e8798952775929bab24":
            raise ValueError("equipment indexed UI crop differs")
        # This digest equals the original RPGOC/DOSBox frame-220 crop named
        # in rpg-equipment-rgb-reference.json (0/61,440 RGB pixels differ).
        if hashlib.sha256(rgb(stable, palette)).hexdigest() != \
                "679c0d0bec258e4f234c84e2adbc40180121efc6b0621eb7133bd7f0aba1f3fd":
            raise ValueError("equipment RGB UI crop differs")

        print(
            "RPG equipment checkpoint: 118 frames, CD000 palette and "
            "61,440-pixel stable UI crop locked")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG equipment checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
