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
                video.get("fnv1a64") != "2ab5fb8ab5eac3d0":
            raise ValueError("inventory replay video summary differs")
        hashes = data.get("frame_fnv1a64", [])
        inventory_pages = [
            "b39e6c5808648ed7", "ee0161595a5de622",
            "baad37ae3f821bd6", "7f62ae2b4a2e3672",
            "d8db0ba139c62eb2", "c17e39e2890267ca",
            "0ac20eb1d3a2a066", "c7a03895f43673ed",
            "bf211faeb8f93899", "652afd15db4bd6ad",
            "2efe3a042901e915", "220486a992ac2a21",
            "7a310c3a0998fe15", "7526a66ff19aedb9",
            "2233023ccdfe4511", "86dd80381b339cd5",
            "5a63a06abab9f641", "7e237a70f6b88b7d",
            "205fd8733edcfd61", "b94e25016fa60871",
            "d95a832b992e0c29", "fca7870230116a2d",
            "aaf40db0f8c14165", "f933d936efc1f131",
            "3e582eeaca129755", "bc36512644e54185",
            "54bd3cfa3076578d", "3b31d75e28967ae5",
            "e2e09ebf0a08c669", "95c4e3af40f2e311",
            "41f29786f40d90b5", "31a40c86aee73781",
            "47097801eedd26a1", "1e8856a3ef904f11",
            "92084037ba586369", "c5f7e1fbbece4415",
            "0ef3fb5674768ed9", "9da3954fe394d6c5",
            "05063d85dee05ea5", "8198a284d2643bc9",
            "0b6789eaae8d30d5", "1827c02f726be6dd",
            "9b7d44a3dc56c831", "ec6e8d42908c1895",
            "ac2f5ba206f68e59", "3ded9bfb24d435e9",
            "b4130adceb9b5411", "1a46e3945cf2f74d",
            "f4602a4eb4105235", "c737ae2763a734f5",
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
