#!/usr/bin/env python3
"""Lock the original RPG Magic page, including the selected actor identity."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

from swd2_frame_capture import crop_indexed, expand_rgb


MAGIC = b"SWD2FRM2"
FNV_OFFSET = 1469598103934665603
FNV_PRIME = 1099511628211
FNV_MASK = (1 << 64) - 1


def load_indexed_frames(path: Path) -> list[tuple[bytes, bytes]]:
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
            raise ValueError("unexpected Magic frame header")
        frames.append((
            take(320 * 200, "indexed pixels"),
            take(768, "VGA palette"),
        ))


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
            "total": 62,
            "consumed": 62,
            "remaining": 0,
            "implicit_quit_calls": 0,
        }:
            raise ValueError("Magic replay input accounting differs")
        if data.get("boundaries") != {
            "wait": 61,
            "poll": 1,
            "text": 0,
            "frontend": 105,
        }:
            raise ValueError("Magic replay input boundaries differ")
        video = data.get("video", {})
        if video.get("frames") != 167 or \
                video.get("last_width") != 320 or \
                video.get("last_height") != 200 or \
                video.get("fnv1a64") != "50dc64e9e31dee40":
            raise ValueError("Magic replay video summary differs")
        hashes = data.get("frame_fnv1a64", [])
        if len(hashes) != 167 or hashes[113:117] != [
                "1b0a18f362ed2cb2",  # System/Book-selected field diamond
                "ea4e949938110458",  # left/Magic-selected field diamond
                "277b3517e4fd40a2",  # actor-zero selector
                "3852ea686be2fbf5",  # complete actor-zero Magic list
        ]:
            raise ValueError("Magic selection pages differ")
        # Frames 116..165 are cursor positions 0..49. The 50th Down redraws
        # the clamped final row and must therefore reproduce frame 165.
        if len(set(hashes[116:166])) != 50 or \
                hashes[165] != "cbd0c738dd911d8d" or \
                hashes[166] != hashes[165]:
            raise ValueError("Magic 50-row cursor/scroll sequence differs")
        if (data.get("state_fnv1a64"), data.get("mapz_fnv1a64"),
                data.get("name_fnv1a64")) != (
                    "54098cf0ca14338b", "827f0f1b725a0958",
                    "e3d2853e2676513b"):
            raise ValueError("Magic replay changed the loaded save triple")

        frames = load_indexed_frames(args.frames)
        if len(frames) != 167:
            raise ValueError("Magic frame capture count differs")
        page, palette = frames[116]
        expected_crops = {
            (32, 21, 48, 48): "dde88ca16f452e5d",  # actor portrait
            (16, 72, 80, 32): "d2a6fdb5b34d4687",  # editable name
            (16, 104, 80, 32): "fab010557bcab25b",  # resource label/value
            (96, 40, 224, 144): "10bcec2c58b7aaba",  # ability list
        }
        for crop, expected in expected_crops.items():
            if crop_fnv(page, crop) != expected:
                raise ValueError(f"Magic indexed crop differs: {crop}")
        original_sha256 = [
            ((32, 21, 48, 48),
             "58bf57cd0e83a831d46e1728c18dee26088eaac80794f3f59526855fc8ab7b7b",
             "e2bb3ef8be503be6e044c6b6f5f7d1cacf212da50738a15e24ac43630a5a6741"),
            ((16, 72, 80, 32),
             "e2b0a765e7cd02455001ed5e56ee84a59dc934dcf4097024d65e07ee2244cd2d",
             "9cad1f83ef11dfb60c584a6dbc0cb2091042479c4a1659a3fac662cad02f535e"),
            ((16, 104, 80, 32),
             "b86e08eb137d336853dfc2fb5cf46f4f6c95dab63fcb63747560215f80e5ebcf",
             "3ba69c191edc455c266dff9cafd7b9520bfaa308aaa5dea21bac1a781b8e589d"),
            ((96, 40, 224, 144),
             "d92e1082ac5ad43128079655a00f1f887f7da3746b3910f6d61d1888fed12409",
             "280849e5811508a017c99a6c8406f309f8cea78d94c31f46dffbc28fb6b684b3"),
        ]
        if hashlib.sha256(palette).hexdigest() != \
                "1b620870840c937d4f0739fc9706df59594a348a143139cf6934d30c962c0960":
            raise ValueError("Magic VGA palette differs")
        for box, expected_indices, expected_rgb in original_sha256:
            indexed = crop_indexed(page, box)
            if hashlib.sha256(indexed).hexdigest() != expected_indices:
                raise ValueError(f"Magic indexed SHA-256 crop differs: {box}")
            if hashlib.sha256(expand_rgb(indexed, palette)).hexdigest() != \
                    expected_rgb:
                raise ValueError(f"Magic original RGB crop differs: {box}")
        final_page = frames[165][0]
        if crop_fnv(final_page, (96, 40, 224, 144)) != \
                "d9b6314010665d59" or \
                crop_fnv(final_page, (16, 104, 80, 32)) != \
                "a372580af0ce94e7":
            raise ValueError("Magic final list/resource crops differ")

        print(
            "RPG Magic checkpoint: 167 frames, actor identity and all 50 "
            "ability cursor/scroll positions locked; initial UI body also "
            "locked as indexed VGA and exact original RGB")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG Magic checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
