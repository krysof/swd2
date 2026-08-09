#!/usr/bin/env python3
"""Lock the RPG OP01/Continue opening pages as indexed VGA frames."""

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
            raise ValueError("unexpected RPG opening frame header")
        frames.append((
            take(320 * 200, "indexed pixels"),
            take(768, "VGA palette"),
        ))


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
            "total": 8,
            "consumed": 8,
            "remaining": 0,
            "implicit_quit_calls": 0,
        } or data.get("boundaries") != {
            "wait": 7,
            "poll": 1,
            "text": 0,
            "frontend": 105,
        }:
            raise ValueError("RPG opening input accounting differs")
        if data.get("video") != {
            "frames": 113,
            "direct_updates": 0,
            "last_width": 320,
            "last_height": 200,
            "fnv1a64": "014ecefd7f3b8a7b",
        }:
            raise ValueError("RPG opening video summary differs")
        expected_transitions = [
            {"module": "MEO.EXE", "input": "--", "output": "MT",
             "launched": True},
            {"module": "RPG.EXE", "input": "MT", "output": "--",
             "launched": True},
        ]
        if data.get("transitions") != expected_transitions:
            raise ValueError("RPG opening module transitions differ")
        if (data.get("state_fnv1a64"), data.get("mapz_fnv1a64"),
                data.get("name_fnv1a64")) != (
                    "54098cf0ca14338b", "827f0f1b725a0958",
                    "e3d2853e2676513b"):
            raise ValueError("RPG opening loaded save triple differs")
        if data.get("stop_reason") != "module requested exit" or \
                data.get("final_marker") != "--":
            raise ValueError("RPG opening did not stop at explicit quit")

        frame_numbers = [87, 88, 89, 90, 112]
        trace_hashes = [
            "8f366df5aba52b49", "b1c6e483e46d842d",
            "768af961b88865a4", "61bc786c5ac1e19e",
            "214eeea5ce30d467",
        ]
        hashes = data.get("frame_fnv1a64", [])
        if len(hashes) != 113 or \
                [hashes[number] for number in frame_numbers] != trace_hashes:
            raise ValueError("RPG opening selected frame summaries differ")

        frames = load_frames(args.frames)
        if len(frames) != 113:
            raise ValueError("RPG opening frame capture count differs")
        indexed_sha256 = [
            "bd235d957cd760bbebcec136d3289022201a5c00a28d780ae14ea75e5dc0c929",
            "d6a7f45ff68f4a88a36ba14a93e8e8d759d29ce6a0838efd41f7729a6d9ce6a4",
            "3725f91c5d5216e1637dee53cde97864d4098a888a832bda56a8aa64bf49f0c5",
            "177b40c88ae381d3f4d1ceeacff33664d28a3d9df1824f238078fa93c2dd061e",
            "37b0bdf857ed52af157432b07f43001be655accdef6bd2e24356cf80bdf11df0",
        ]
        palette_sha256 = [
            "3c5e200ac4b06c21a08908f7f1f1bf3d7aabe2bce810d8f4140d50961c61e282",
        ] * 4 + [
            "1b620870840c937d4f0739fc9706df59594a348a143139cf6934d30c962c0960",
        ]
        # These hashes are the decoded RGB bytes of the five original PNGs
        # registered in rpg-opening-rgb-reference.json.  All 64,000 pixels
        # match; the indexed and palette hashes make that match stricter.
        rgb_sha256 = [
            "aed8377f252b69f6f46ec4ba1e52fbbee0efcc6f2ef154cf0abb52d22e4156d7",
            "4f1381fbeb26cfeb558985af1d58a815dca93227e233801752af795b27236650",
            "a4131be1675ac1968f12cbc6910ddbc166ffcf78e5a774e707607f8dadad9b8d",
            "4163fecf88c1325cf43a1455cef29f63ea2ef84464d44839faa604f5a442f8b2",
            "d8230560337d6713e8bb90031497ff0752cbc50e160e0b9b348e0ec9dd815067",
        ]
        for position, frame_number in enumerate(frame_numbers):
            pixels, palette = frames[frame_number]
            if hashlib.sha256(pixels).hexdigest() != indexed_sha256[position]:
                raise ValueError(f"RPG opening frame {frame_number} indices differ")
            if hashlib.sha256(palette).hexdigest() != palette_sha256[position]:
                raise ValueError(f"RPG opening frame {frame_number} palette differs")
            if hashlib.sha256(rgb(pixels, palette)).hexdigest() != \
                    rgb_sha256[position]:
                raise ValueError(f"RPG opening frame {frame_number} RGB differs")

        print(
            "RPG opening checkpoint: OP01 title, Continue, slot selector, "
            "confirmation and loaded world locked as indexed VGA frames")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG opening checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
