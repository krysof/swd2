#!/usr/bin/env python3
"""Lock every RPG Status row and both passes of the ME01 strip artwork."""

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
            "total": 39,
            "consumed": 39,
            "remaining": 0,
            "implicit_quit_calls": 0,
        }:
            raise ValueError("Status replay input accounting differs")
        if data.get("boundaries") != {
            "wait": 38,
            "poll": 1,
            "text": 0,
            "frontend": 105,
        }:
            raise ValueError("Status replay input boundaries differ")
        video = data.get("video", {})
        if video.get("frames") != 144 or \
                video.get("last_width") != 320 or \
                video.get("last_height") != 200 or \
                video.get("fnv1a64") != "4012b59f129b4058":
            raise ValueError("Status replay video summary differs")
        hashes = data.get("frame_fnv1a64", [])
        if len(hashes) != 144 or hashes[113:144] != [
                "1b0a18f362ed2cb2",  # System/Book-selected field diamond
                "a2bc9fa38fe088af",  # down/Status-selected field diamond
                "9926d3f4783f8138",  # actor-zero selector
                "edebf73491c53a2a",  # selected row zero
                "90cf2568ca25de08",
                "f3ae5a3cece2fb27",
                "d68cdcc4afd8bcbb",
                "b37107ea503106df",
                "ebb36141ed32213f",
                "69d1882db3f99d24",
                "9a5969f896f3a881",
                "2f8af66f01a0f3b6",
                "93597e411919ea7b",
                "1fa3d601dc1859cb",
                "e4636c688eae01c6",
                "91b102027a51e845",
                "4655d480ac9168cc",
                "d0dc1264a3b373cc",  # first_visible=6
                "f3fa21c2b23bb940",  # first_visible=7
                "25cc949bbe0f8a64",  # first_visible=8
                "d67714d5a4c9a433",
                "486807874c144802",
                "c35db70ba79014fe",  # second ME01 pass begins
                "4b83cd5166b18ea1",  # selected row 20
                "672fe548de5e9630",
                "8d8fda2ae4229489",
                "b21128eee76e6e77",
                "17d08b03cd86131f",
                "6f017030c7d6fc13",
                "d3334395a7280a27",
                "e611fd66563ebb62",  # selected row 27 / maximum first
        ]:
            raise ValueError("Status selection/row frames differ")
        if (data.get("state_fnv1a64"), data.get("mapz_fnv1a64"),
                data.get("name_fnv1a64")) != (
                    "54098cf0ca14338b", "827f0f1b725a0958",
                    "e3d2853e2676513b"):
            raise ValueError("Status replay changed the loaded save triple")

        frames = load_indexed_frames(args.frames)
        if len(frames) != 144:
            raise ValueError("Status frame capture count differs")
        expected = [
            "40163683a334d336", "957dca70bc6c8704",
            "8140892ef2e02773", "140840122f3e51fb",
            "48105bc941385147", "7a03090577be0247",
            "1f0bf36a2e386980", "ffab5e35aff067f5",
            "ada8baa3b2a54a6a", "8655f6b723fd93ab",
            "a1a90f230d0b1977", "b7b7631851193992",
            "2dd05d67893dd88d", "0a3bf04a76e4bdf8",
            "8d4198c0abf65594", "6ebfe428a6e061d0",
            "dfb031f965949688", "f9b273529dd9f637",
            "dc657042456c03c6", "c350981ee0d139c6",
            "a94aad1cb110f265", "4fc1d2ed1064d014",
            "4896c7fcb52bc8a1", "c96cec6c498f9db3",
            "5cad67579be483eb", "e2e8105e0c953c6f",
            "3b33c2a9545ffd4f", "2ccf920a18114ac2",
        ]
        crop = (96, 40, 224, 144)
        for offset, digest in enumerate(expected):
            frame = 116 + offset
            if crop_fnv(frames[frame], crop) != digest:
                raise ValueError(f"Status indexed row differs: {offset}")

        print(
            "RPG Status checkpoint: 144 frames, all 28 selected rows and "
            "both ME01 strip passes locked")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG Status checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
