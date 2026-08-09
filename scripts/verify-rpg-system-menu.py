#!/usr/bin/env python3
"""Lock every RPG System row as indexed pixels and original-matched RGB."""

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
            raise ValueError("unexpected System frame header")
        frames.append((
            take(320 * 200, "indexed pixels"),
            take(768, "VGA palette"),
        ))


def crop(pixels: bytes, box: tuple[int, int, int, int]) -> bytes:
    x, y, width, height = box
    return b"".join(
        pixels[row * 320 + x:row * 320 + x + width]
        for row in range(y, y + height))


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
                video.get("fnv1a64") != "fc43902be360af49":
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

        frames = load_frames(args.frames)
        if len(frames) != 121:
            raise ValueError("System frame capture count differs")
        palette_sha256 = \
            "1b620870840c937d4f0739fc9706df59594a348a143139cf6934d30c962c0960"
        indexed_sha256 = [
            "45f2fd56985e54a2150b68634db58ad32f3045aa4010b5d6857cbb1f3e78c5dc",
            "e78c2de9c860ce7435048ad3c6488b6a58683bf09b252299b3652f3f9b74c7b6",
            "b6440b89dd659a71a5b86cbaef00e82465d3b73eb99c880dceca7d7506b2deab",
            "d72e67488a2f31d4483998e88ef38f2f76cb028bfd707d779e39ba2b6d84fe7c",
            "09e18cb0400a6f8c03956136bef703e04a294fefc908c489d33b683107f634e0",
            "3c372a99ce9c9b3cb5112e0c521aa28f6fffce38bc0a3e2cf9904041f535d4df",
            "fc92a86fe289883ddae47f0359c62e3fbd2565182fcbf4a1f563d503cabd534f",
        ]
        # These seven digests also equal the original RPGOC/DOSBox RGB
        # crops recorded in rpg-system-menu-rgb-reference.json.  Keeping
        # both the indexed bytes and palette prevents a compensating palette
        # or index change from passing merely because the visible RGB agrees.
        rgb_sha256 = [
            "fd9cd81af728198968f4997f061e356634c5f2f5cf63fe085aff719fb8d8fa01",
            "c7c8a01e03e03c0b29596a9a26ba7815280bb3f62a2bcf491a886a816dccb9fb",
            "be6198ac4641643ab497e0a611804b673888f0c11428e0d56b21f785f66bfb30",
            "23b124db2cdd80749ad3eba1aabe861ed526b66967953cdd6cb6208287dac336",
            "87936c8ca2e9bdc29925d5619c4058289a6b839a64e95fce4ee5cb6a7ecf070f",
            "07db28e974195ffcb8755dee05123efc499b40aaff4917af98c826e1a2dbb288",
            "c446b2a14c39fdeadc99a0d827e3785244ab042466edfef4dedbcf1e113aead4",
        ]
        box = (80, 10, 186, 122)
        for row, frame_number in enumerate(range(114, 121)):
            pixels, palette = frames[frame_number]
            indexed = crop(pixels, box)
            if hashlib.sha256(palette).hexdigest() != palette_sha256:
                raise ValueError(f"System row-{row} VGA palette differs")
            if hashlib.sha256(indexed).hexdigest() != indexed_sha256[row]:
                raise ValueError(f"System row-{row} indexed crop differs")
            if hashlib.sha256(rgb(indexed, palette)).hexdigest() != \
                    rgb_sha256[row]:
                raise ValueError(f"System row-{row} original RGB crop differs")
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
        print(
            "RPG System menu checkpoint: 121 frames, all 7 rows locked as "
            "indexed pixels, VGA palette and exact original RGB")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG System menu checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
