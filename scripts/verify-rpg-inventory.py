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
            "total": 60,
            "consumed": 60,
            "remaining": 0,
            "implicit_quit_calls": 0,
        }:
            raise ValueError("inventory replay input accounting differs")
        if data.get("boundaries") != {
            "wait": 59,
            "poll": 1,
            "text": 0,
            "frontend": 105,
        }:
            raise ValueError("inventory replay input boundaries differ")
        video = data.get("video", {})
        if video.get("frames") != 165 or \
                video.get("last_width") != 320 or \
                video.get("last_height") != 200 or \
                video.get("fnv1a64") != "e3ae95999f65f1b0":
            raise ValueError("inventory replay video summary differs")
        hashes = data.get("frame_fnv1a64", [])
        inventory_pages = [
            "b39e6c5808648ed7", "ee0161595a5de622",
            "baad37ae3f821bd6", "7f62ae2b4a2e3672",
            "d8db0ba139c62eb2", "c17e39e2890267ca",
            "0ac20eb1d3a2a066", "c7a03895f43673ed",
            "5e0555c7c34f3689", "534ca6205e514f4d",
            "a097243b8440e9b5", "d28c70cd5b3c6031",
            "ebc9f67164d7feb5", "59c23761e1c18ea9",
            "8fc580446601ed41", "bc194b8b30a96275",
            "5c81ce1b4c82ff51", "c65a0044f1967b7d",
            "1b162316f47a7771", "9e48426ac9c83e21",
            "37d9a3b9f6e14879", "2a3aac105e164acd",
            "ff0e46080195d665", "8a1abc5fd58b42e1",
            "f82bd600c92da6f5", "10d459a55bb49385",
            "32d6a87ac525612d", "0fc82d014ab717e5",
            "fddf3d2c3181dfb9", "035761b6d8f68b41",
            "0e47318347496cd5", "e1b527da254e3991",
            "5008b03aae4b4ab1", "8c1ad4ab8793f741",
            "ad06dea4e1d17cb9", "3790cc331a0d44b5",
            "44ec2ba09d3339c9", "1a163fae2a9e0ec5",
            "603fc37c677a6fa5", "f46be5b5611e2219",
            "40a3553dc402f675", "11a0e1149694305d",
            "2c6427ccc22019e1", "6cab157dfe8a5f35",
            "1fdc091343b93149", "64e5d8c8dc8c1739",
            "21a588e4839efc41", "51234b904453ebed",
            "d7d9ddb409b75e55", "fbe2327bc1058915",
        ]
        if len(hashes) != 165 or hashes[113:115] != [
                "1b0a18f362ed2cb2",  # System/Book-selected field diamond
                "ddb26857dd11b4f3",  # Item-selected field diamond
        ] or hashes[115:165] != inventory_pages:
            raise ValueError("inventory selection pages differ")
        if (data.get("state_fnv1a64"), data.get("mapz_fnv1a64"),
                data.get("name_fnv1a64")) != (
                    "54098cf0ca14338b", "827f0f1b725a0958",
                    "e3d2853e2676513b"):
            raise ValueError("inventory replay changed the loaded save triple")

        frames = load_frames(args.frames)
        if len(frames) != 165:
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
            "RPG inventory checkpoint: 165 frames, all 50 rows locked, "
            f"{len(checked)} grayscale world pixels exact")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, f"RPG inventory checkpoint: FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
